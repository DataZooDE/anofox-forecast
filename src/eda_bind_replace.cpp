#include "eda_bind_replace.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include <sstream>

namespace duckdb {

static unique_ptr<SubqueryRef> ParseSubquery(const string &query, const ParserOptions &options, const string &err_msg) {
	Parser parser(options);
	parser.ParseQuery(query);
	if (parser.statements.size() != 1 || parser.statements[0]->type != StatementType::SELECT_STATEMENT) {
		throw ParserException(err_msg);
	}
	auto select_stmt = unique_ptr_cast<SQLStatement, SelectStatement>(std::move(parser.statements[0]));
	return duckdb::make_uniq<SubqueryRef>(std::move(select_stmt));
}

// Helper function to extract column name from original expression or fallback to value
static string ExtractColumnName(const TableFunctionBindInput &input, idx_t param_idx, const Value &fallback_value) {
	// Try to extract from original expression in ref before it was evaluated
	if (input.ref.function && input.ref.function->GetExpressionType() == ExpressionType::FUNCTION) {
		auto &fexpr = input.ref.function->Cast<FunctionExpression>();
		if (param_idx < fexpr.children.size()) {
			auto &expr = fexpr.children[param_idx];
			if (expr->GetExpressionType() == ExpressionType::COLUMN_REF) {
				auto &colref = expr->Cast<ColumnRefExpression>();
				return colref.GetColumnName();
			} else if (expr->GetExpressionType() == ExpressionType::VALUE_CONSTANT) {
				return expr->Cast<ConstantExpression>().value.ToString();
			}
		}
	}
	// Fallback to evaluated value
	return fallback_value.ToString();
}

// Helper function to extract table name from original expression or fallback to value
// Handles both string literals and table identifiers
static string ExtractTableName(const TableFunctionBindInput &input, idx_t param_idx, const Value &fallback_value) {
	// Try to extract from original expression in ref before it was evaluated
	if (input.ref.function && input.ref.function->GetExpressionType() == ExpressionType::FUNCTION) {
		auto &fexpr = input.ref.function->Cast<FunctionExpression>();
		if (param_idx < fexpr.children.size()) {
			auto &expr = fexpr.children[param_idx];
			if (expr->GetExpressionType() == ExpressionType::COLUMN_REF) {
				// Table name passed as identifier
				auto &colref = expr->Cast<ColumnRefExpression>();
				return colref.GetColumnName();
			} else if (expr->GetExpressionType() == ExpressionType::VALUE_CONSTANT) {
				// Table name passed as string literal
				return expr->Cast<ConstantExpression>().value.ToString();
			}
		}
	}
	// Fallback to evaluated value
	return fallback_value.ToString();
}

// TS_STATS: VARCHAR frequency (date-based)
unique_ptr<TableRef> TSStatsVarcharBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	// Validate parameters
	if (input.inputs.size() < 5) {
		throw InvalidInputException(
		    "anofox_fcst_ts_stats requires 5 arguments: table_name, group_col, date_col, value_col, frequency");
	}

	// Extract parameters as strings
	// For table_name, extract from expression to handle both string literals and table identifiers
	string table_name = ExtractTableName(input, 0, input.inputs[0]);
	// For column references, try to extract the original column name from the expression
	// This handles both column references and constants
	string group_col = ExtractColumnName(input, 1, input.inputs[1]);
	string date_col = ExtractColumnName(input, 2, input.inputs[2]);
	string value_col = ExtractColumnName(input, 3, input.inputs[3]);
	// Frequency is required - reject NULL
	if (input.inputs[4].IsNull()) {
		throw InvalidInputException("frequency parameter is required and cannot be NULL");
	}
	string frequency = input.inputs[4].ToString();
	if (frequency.empty()) {
		throw InvalidInputException("frequency parameter cannot be empty");
	}

	// Escape identifiers
	string escaped_table = KeywordHelper::WriteQuoted(table_name);
	string escaped_group_col = KeywordHelper::WriteOptionallyQuoted(group_col);
	string escaped_date_col = KeywordHelper::WriteOptionallyQuoted(date_col);
	string escaped_value_col = KeywordHelper::WriteOptionallyQuoted(value_col);
	string escaped_frequency = KeywordHelper::WriteQuoted(frequency);

	// Generate SQL dynamically with DATE type detection and appropriate calculation
	std::ostringstream sql;
	sql << R"(WITH frequency_parsed AS (
    SELECT 
        CASE 
            WHEN UPPER(TRIM()"
	    << escaped_frequency << R"())) IN ('1D', '1DAY') THEN INTERVAL '1 day'
            WHEN UPPER(TRIM()"
	    << escaped_frequency << R"())) IN ('30M', '30MIN', '30MINUTE', '30MINUTES') THEN INTERVAL '30 minutes'
            WHEN UPPER(TRIM()"
	    << escaped_frequency << R"())) IN ('1H', '1HOUR', '1HOURS') THEN INTERVAL '1 hour'
            WHEN UPPER(TRIM()"
	    << escaped_frequency << R"())) IN ('1W', '1WEEK', '1WEEKS') THEN INTERVAL '1 week'
            WHEN UPPER(TRIM()"
	    << escaped_frequency << R"())) IN ('1MO', '1MONTH', '1MONTHS') THEN INTERVAL '1 month'
            WHEN UPPER(TRIM()"
	    << escaped_frequency << R"())) IN ('1Q', '1QUARTER', '1QUARTERS') THEN INTERVAL '3 months'
            WHEN UPPER(TRIM()"
	    << escaped_frequency << R"())) IN ('1Y', '1YEAR', '1YEARS') THEN INTERVAL '1 year'
            ELSE INTERVAL '1 day'
        END AS __interval,
        UPPER(TRIM()"
	    << escaped_frequency << R"())) IN ('1D', '1DAY') AS __is_daily_interval
    FROM (SELECT 1) t
),
temporal_metadata AS (
    SELECT 
        group_col AS series_id,
        COUNT(*) AS length,
        MIN(date_col) AS start_date,
        MAX(date_col) AS end_date,
        MIN(date_col) = CAST(MIN(date_col) AS DATE) AS __is_date_type
    FROM QUERY_TABLE()"
	    << escaped_table << R"()
    GROUP BY group_col
),
expected_length_calc AS (
    SELECT 
        tm.series_id,
        tm.length,
        tm.start_date,
        tm.end_date,
        fp.__interval,
        CASE 
            WHEN tm.end_date >= tm.start_date THEN
                CASE
                    -- For DATE columns with daily intervals, use DATEDIFF to avoid TIMESTAMP casts
                    WHEN tm.__is_date_type AND fp.__is_daily_interval THEN
                        CAST(DATEDIFF('day', tm.start_date, tm.end_date) AS INTEGER) + 1
                    -- For other cases, use EPOCH calculation
                    ELSE
                        CAST(EXTRACT(EPOCH FROM (CAST(tm.end_date AS TIMESTAMP) - CAST(tm.start_date AS TIMESTAMP))) / EXTRACT(EPOCH FROM fp.__interval) AS INTEGER) + 1
                END
            ELSE 1
        END AS expected_length
    FROM temporal_metadata tm
    CROSS JOIN frequency_parsed fp
),
features_agg AS (
    SELECT 
        group_col AS series_id,
        anofox_fcst_ts_features(date_col, value_col, [
            'mean', 'standard_deviation', 'minimum', 'maximum', 'median',
            'n_zeros', 'n_unique_values', 'is_constant',
            'plateau_size', 'plateau_size_non_zero', 'n_zeros_start', 'n_zeros_end'
        ]) AS feats
    FROM QUERY_TABLE()"
	    << escaped_table << R"()
    GROUP BY group_col
),
duplicate_timestamps AS (
    SELECT 
        group_col AS series_id,
        SUM(CASE WHEN key_count > 1 THEN key_count - 1 ELSE 0::BIGINT END) AS n_duplicate_timestamps
    FROM (
        SELECT 
            group_col,
            date_col,
            COUNT(*) AS key_count
        FROM QUERY_TABLE()"
	    << escaped_table << R"()
        GROUP BY group_col, date_col
    ) key_counts
    GROUP BY group_col
),
null_counts AS (
    SELECT 
        group_col AS series_id,
        COUNT(CASE WHEN value_col IS NULL THEN 1 END) AS n_null
    FROM QUERY_TABLE()"
	    << escaped_table << R"()
    GROUP BY group_col
)
SELECT 
    f.series_id,
    elc.length,
    elc.start_date,
    elc.end_date,
    elc.expected_length,
    ROUND(f.feats.mean, 2) AS mean,
    ROUND(f.feats.standard_deviation, 2) AS std,
    ROUND(f.feats.minimum, 2) AS min,
    ROUND(f.feats.maximum, 2) AS max,
    ROUND(f.feats.median, 2) AS median,
    n.n_null,
    CAST(f.feats.n_zeros AS BIGINT) AS n_zeros,
    CAST(f.feats.n_unique_values AS BIGINT) AS n_unique_values,
    CAST(f.feats.is_constant AS BOOLEAN) AS is_constant,
    CAST(f.feats.plateau_size AS BIGINT) AS plateau_size,
    CAST(f.feats.plateau_size_non_zero AS BIGINT) AS plateau_size_non_zero,
    CAST(f.feats.n_zeros_start AS BIGINT) AS n_zeros_start,
    CAST(f.feats.n_zeros_end AS BIGINT) AS n_zeros_end,
    COALESCE(dt.n_duplicate_timestamps, 0::BIGINT) AS n_duplicate_timestamps
FROM features_agg f
INNER JOIN expected_length_calc elc ON f.series_id = elc.series_id
INNER JOIN null_counts n ON f.series_id = n.series_id
LEFT JOIN duplicate_timestamps dt ON f.series_id = dt.series_id
ORDER BY f.series_id)";

	return ParseSubquery(sql.str(), context.GetParserOptions(),
	                     "Failed to parse generated SQL for anofox_fcst_ts_stats");
}

// TS_STATS: INTEGER frequency (integer-based)
unique_ptr<TableRef> TSStatsIntegerBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	// Validate parameters
	if (input.inputs.size() < 5) {
		throw InvalidInputException(
		    "anofox_fcst_ts_stats requires 5 arguments: table_name, group_col, date_col, value_col, frequency");
	}

	// Extract parameters
	// For table_name, extract from expression to handle both string literals and table identifiers
	string table_name = ExtractTableName(input, 0, input.inputs[0]);
	// For column references, try to extract the original column name from the expression
	// This handles both column references and constants
	string group_col = ExtractColumnName(input, 1, input.inputs[1]);
	string date_col = ExtractColumnName(input, 2, input.inputs[2]);
	string value_col = ExtractColumnName(input, 3, input.inputs[3]);
	// Frequency is required - reject NULL
	if (input.inputs[4].IsNull()) {
		throw InvalidInputException("frequency parameter is required and cannot be NULL");
	}
	// For INTEGER frequency, use the value directly (it's already an integer)
	string frequency_str = input.inputs[4].ToString();

	// Escape identifiers
	string escaped_table = KeywordHelper::WriteQuoted(table_name);
	string escaped_group_col = KeywordHelper::WriteOptionallyQuoted(group_col);
	string escaped_date_col = KeywordHelper::WriteOptionallyQuoted(date_col);
	string escaped_value_col = KeywordHelper::WriteOptionallyQuoted(value_col);

	// Generate SQL dynamically
	std::ostringstream sql;
	sql << R"(WITH frequency_parsed AS (
    SELECT 
        COALESCE(frequency, 1) AS __int_step
    FROM (SELECT 1) t
),
temporal_metadata AS (
    SELECT 
        group_col AS series_id,
        COUNT(*) AS length,
        MIN(date_col) AS start_date,
        MAX(date_col) AS end_date
    FROM QUERY_TABLE()"
	    << escaped_table << R"()
    GROUP BY group_col
),
expected_length_calc AS (
    SELECT 
        tm.series_id,
        tm.length,
        tm.start_date,
        tm.end_date,
        fp.__int_step,
        CAST(CASE 
            WHEN tm.end_date >= tm.start_date
            THEN CAST((tm.end_date - tm.start_date) / fp.__int_step AS INTEGER) + CAST(1 AS INTEGER)
            ELSE CAST(1 AS INTEGER)
        END AS INTEGER) AS expected_length
    FROM temporal_metadata tm
    CROSS JOIN frequency_parsed fp
),
features_agg AS (
    SELECT 
        group_col AS series_id,
        anofox_fcst_ts_features(date_col, value_col, [
            'mean', 'standard_deviation', 'minimum', 'maximum', 'median',
            'n_zeros', 'n_unique_values', 'is_constant',
            'plateau_size', 'plateau_size_non_zero', 'n_zeros_start', 'n_zeros_end'
        ]) AS feats
    FROM QUERY_TABLE()"
	    << escaped_table << R"()
    GROUP BY group_col
),
duplicate_timestamps AS (
    SELECT 
        group_col AS series_id,
        SUM(CASE WHEN key_count > 1 THEN key_count - 1 ELSE 0::BIGINT END) AS n_duplicate_timestamps
    FROM (
        SELECT 
            group_col,
            date_col,
            COUNT(*) AS key_count
        FROM QUERY_TABLE()"
	    << escaped_table << R"()
        GROUP BY group_col, date_col
    ) key_counts
    GROUP BY group_col
),
null_counts AS (
    SELECT 
        group_col AS series_id,
        COUNT(CASE WHEN value_col IS NULL THEN 1 END) AS n_null
    FROM QUERY_TABLE()"
	    << escaped_table << R"()
    GROUP BY group_col
)
SELECT 
    f.series_id,
    elc.length,
    elc.start_date,
    elc.end_date,
    elc.expected_length,
    ROUND(f.feats.mean, 2) AS mean,
    ROUND(f.feats.standard_deviation, 2) AS std,
    ROUND(f.feats.minimum, 2) AS min,
    ROUND(f.feats.maximum, 2) AS max,
    ROUND(f.feats.median, 2) AS median,
    n.n_null,
    CAST(f.feats.n_zeros AS BIGINT) AS n_zeros,
    CAST(f.feats.n_unique_values AS BIGINT) AS n_unique_values,
    CAST(f.feats.is_constant AS BOOLEAN) AS is_constant,
    CAST(f.feats.plateau_size AS BIGINT) AS plateau_size,
    CAST(f.feats.plateau_size_non_zero AS BIGINT) AS plateau_size_non_zero,
    CAST(f.feats.n_zeros_start AS BIGINT) AS n_zeros_start,
    CAST(f.feats.n_zeros_end AS BIGINT) AS n_zeros_end,
    COALESCE(dt.n_duplicate_timestamps, 0::BIGINT) AS n_duplicate_timestamps
FROM features_agg f
INNER JOIN expected_length_calc elc ON f.series_id = elc.series_id
INNER JOIN null_counts n ON f.series_id = n.series_id
LEFT JOIN duplicate_timestamps dt ON f.series_id = dt.series_id
ORDER BY f.series_id)";

	return ParseSubquery(sql.str(), context.GetParserOptions(),
	                     "Failed to parse generated SQL for anofox_fcst_ts_stats (INTEGER frequency)");
}

// TS_QUALITY_REPORT: Quality assessment report from TS_STATS output
unique_ptr<TableRef> TSQualityReportBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	// Validate parameters
	if (input.inputs.size() < 2) {
		throw InvalidInputException("anofox_fcst_ts_quality_report requires 2 arguments: stats_table, min_length");
	}

	// Extract parameters
	string stats_table = input.inputs[0].ToString();
	string min_length_sql = input.inputs[1].IsNull() ? "30" : input.inputs[1].ToString();

	// Escape identifiers
	string escaped_stats_table = KeywordHelper::WriteQuoted(stats_table);

	// Generate SQL dynamically
	std::ostringstream sql;
	sql << R"(WITH stats AS (
    SELECT * FROM QUERY_TABLE()"
	    << escaped_stats_table << R"()
),
params AS (
    SELECT COALESCE(CAST()"
	    << min_length_sql << R"( AS INTEGER), 30) AS min_length_threshold
),
gap_analysis AS (
    SELECT 
        COUNT(DISTINCT series_id) AS total_series,
        COUNT(DISTINCT CASE WHEN expected_length > length THEN series_id END) AS series_with_gaps,
        CASE 
            WHEN COUNT(DISTINCT series_id) > 0
            THEN 100.0 * COUNT(DISTINCT CASE WHEN expected_length > length THEN series_id END) / COUNT(DISTINCT series_id)
            ELSE 0.0
        END AS pct_with_gaps
    FROM stats
    CROSS JOIN params
),
missing_analysis AS (
    SELECT 
        COUNT(DISTINCT series_id) AS total_series,
        COUNT(DISTINCT CASE WHEN n_null > 0 THEN series_id END) AS series_with_missing,
        CASE 
            WHEN COUNT(DISTINCT series_id) > 0
            THEN 100.0 * COUNT(DISTINCT CASE WHEN n_null > 0 THEN series_id END) / COUNT(DISTINCT series_id)
            ELSE 0.0
        END AS pct_with_missing
    FROM stats
),
constant_analysis AS (
    SELECT 
        COUNT(DISTINCT series_id) AS total_series,
        COUNT(DISTINCT CASE WHEN is_constant = true THEN series_id END) AS series_constant,
        CASE 
            WHEN COUNT(DISTINCT series_id) > 0
            THEN 100.0 * COUNT(DISTINCT CASE WHEN is_constant = true THEN series_id END) / COUNT(DISTINCT series_id)
            ELSE 0.0
        END AS pct_constant
    FROM stats
),
short_analysis AS (
    SELECT 
        COUNT(DISTINCT series_id) AS total_series,
        COUNT(DISTINCT CASE WHEN length < min_length_threshold THEN series_id END) AS series_short,
        CASE 
            WHEN COUNT(DISTINCT series_id) > 0
            THEN 100.0 * COUNT(DISTINCT CASE WHEN length < min_length_threshold THEN series_id END) / COUNT(DISTINCT series_id)
            ELSE 0.0
        END AS pct_short
    FROM stats
    CROSS JOIN params
),
alignment_analysis AS (
    SELECT 
        COUNT(DISTINCT series_id) AS total_series,
        COUNT(DISTINCT start_date) AS n_start_dates,
        COUNT(DISTINCT end_date) AS n_end_dates,
        CASE 
            WHEN COUNT(DISTINCT start_date) > 1 OR COUNT(DISTINCT end_date) > 1
            THEN COUNT(DISTINCT series_id) - 1
            ELSE 0
        END AS series_misaligned
    FROM stats
)
SELECT 
    'Gap Analysis' AS check_type,
    ga.total_series,
    ga.series_with_gaps,
    ROUND(ga.pct_with_gaps, 1) AS pct_with_gaps
FROM gap_analysis ga
UNION ALL
SELECT 
    'Missing Values' AS check_type,
    ma.total_series,
    ma.series_with_missing,
    ROUND(ma.pct_with_missing, 1) AS pct_with_missing
FROM missing_analysis ma
UNION ALL
SELECT 
    'Constant Series' AS check_type,
    ca.total_series,
    ca.series_constant,
    ROUND(ca.pct_constant, 1) AS pct_constant
FROM constant_analysis ca
UNION ALL
SELECT 
    'Short Series (< ' || CAST(p.min_length_threshold AS VARCHAR) || ')' AS check_type,
    sa.total_series,
    sa.series_short,
    ROUND(sa.pct_short, 1) AS pct_short
FROM short_analysis sa
CROSS JOIN params p
UNION ALL
SELECT 
    'End Date Alignment' AS check_type,
    aa.total_series,
    aa.series_misaligned,
    CASE 
        WHEN aa.total_series > 0
        THEN ROUND(100.0 * aa.series_misaligned / aa.total_series, 1)
        ELSE 0.0
    END AS pct_misaligned
FROM alignment_analysis aa
ORDER BY check_type)";

	return ParseSubquery(sql.str(), context.GetParserOptions(),
	                     "Failed to parse generated SQL for anofox_fcst_ts_quality_report");
}

} // namespace duckdb
