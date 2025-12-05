#include "duckdb.hpp"
#include "duckdb/catalog/default/default_table_functions.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/parser/parsed_data/create_macro_info.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/function/table_macro_function.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include "eda_bind_replace.hpp"
#include "eda_macros.hpp"
#include <map>
#include <vector>

namespace duckdb {

// Array of EDA macros - all follow consistent signature:
// (table_name, group_col, date_col, value_col)
static const DefaultTableMacro eda_macros[] = {

    // Note: anofox_fcst_ts_stats and anofox_fcst_ts_quality_report are now table functions
    // (registered in RegisterEDATableFunctions, not as macros)

    // TS_STATS_SUMMARY: Aggregate statistics from TS_STATS output
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_stats_summary",
     {"stats_table", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH stats AS (
                SELECT * FROM QUERY_TABLE(stats_table)
            ),
            aggregated AS (
                SELECT 
                    COUNT(DISTINCT s.series_id) AS total_series,
                    SUM(s.length) AS total_observations,
                    ROUND(AVG(CAST(s.length AS DOUBLE)), 2) AS avg_series_length,
                    MIN(s.start_date) AS overall_start_date,
                    MAX(s.end_date) AS overall_end_date
                FROM stats s
            )
            SELECT 
                total_series,
                total_observations,
                avg_series_length,
                CASE 
                    WHEN overall_end_date >= overall_start_date
                    THEN CAST(DATEDIFF('day', overall_start_date, overall_end_date) AS INTEGER)
                    ELSE 0
                END AS date_span
            FROM aggregated
        )"},

    // End marker
    // Note: anofox_fcst_ts_quality_report is now a table function
    // (registered in RegisterEDATableFunctions, not as a macro)
    {nullptr, nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr}};

// Helper function to register table functions with IGNORE_ON_CONFLICT
static void RegisterTableFunctionIgnore(ExtensionLoader &loader, TableFunction function) {
	TableFunctionSet set(function.name);
	set.AddFunction(std::move(function));
	CreateTableFunctionInfo info(std::move(set));
	info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
	loader.RegisterFunction(std::move(info));
}

// Register EDA (Exploratory Data Analysis) table functions (bind_replace)
void RegisterEDATableFunctions(ExtensionLoader &loader) {
	// TS_STATS_INTERNAL: The "engine" function - strict VARCHAR signature (matching Commit 53a3dd6 pattern)
	// This is the internal implementation that does the actual work
	// Strategy: Use strict VARCHAR for all parameters, handle polymorphism in bind_replace
	// This matches the working pattern from fill_nulls_forward and other data prep functions
	TableFunction ts_stats_internal("anofox_fcst_ts_stats_internal",
	                                {LogicalType::VARCHAR,  // table_name
	                                 LogicalType::VARCHAR,  // group_col (Accepts "1" or "col_name" as string)
	                                 LogicalType::VARCHAR,  // date_col
	                                 LogicalType::VARCHAR,  // value_col
	                                 LogicalType::VARCHAR}, // frequency (Accepts "1" or "1d" as string)
	                                nullptr, nullptr);

	// Wrap TSStatsBindReplace in try-catch to catch any early exceptions
	static auto SafeTSStatsBindReplace = [](ClientContext &context,
	                                        TableFunctionBindInput &input) -> unique_ptr<TableRef> {
		try {
			return TSStatsBindReplace(context, input);
		} catch (const std::exception &e) {
			// Re-throw with context to help debugging
			throw BinderException("TSStatsBindReplace failed: %s", e.what());
		}
	};

	ts_stats_internal.bind_replace = SafeTSStatsBindReplace;
	// Named parameters for documentation (matching fill_nulls_forward pattern)
	ts_stats_internal.named_parameters["table_name"] = LogicalType::VARCHAR;
	ts_stats_internal.named_parameters["group_col"] = LogicalType::VARCHAR;
	ts_stats_internal.named_parameters["date_col"] = LogicalType::VARCHAR;
	ts_stats_internal.named_parameters["value_col"] = LogicalType::VARCHAR;
	ts_stats_internal.named_parameters["frequency"] = LogicalType::VARCHAR;
	loader.RegisterFunction(ts_stats_internal);

	// TS_QUALITY_REPORT
	TableFunction ts_quality_report("anofox_fcst_ts_quality_report", {LogicalType::VARCHAR, LogicalType::INTEGER},
	                                nullptr, nullptr);
	ts_quality_report.bind_replace = TSQualityReportBindReplace;
	ts_quality_report.named_parameters["stats_table"] = LogicalType::VARCHAR;
	ts_quality_report.named_parameters["min_length"] = LogicalType::INTEGER;

	// Create a copy for alias registration before registering the main function
	TableFunction ts_quality_report_for_alias = ts_quality_report;
	loader.RegisterFunction(ts_quality_report);

	// Register the public-facing SQL macro that wraps the internal function
	// This macro normalizes inputs: converts integers to VARCHAR, handles table identifiers
	// Pattern: Strict VARCHAR signature + Macro normalization (matching Commit 53a3dd6)
	Parser parser;
	// Macro SQL: Use format() for all parameters - it handles both literals and column references
	// format() converts integers to strings, and column references are passed as identifiers
	string macro_sql = R"(SELECT * FROM anofox_fcst_ts_stats_internal(
        format('{}', t),        -- table identifier -> string
        format('{}', g),        -- integer 1 -> '1', column 'col' -> 'col'
        format('{}', d),        -- date_col
        format('{}', v),        -- value_col
        format('{}', f)         -- integer 1 -> '1', string '1d' -> '1d'
    ))";
	parser.ParseQuery(macro_sql);
	if (parser.statements.size() != 1 || parser.statements[0]->type != StatementType::SELECT_STATEMENT) {
		throw InternalException("Failed to parse macro SQL for anofox_fcst_ts_stats");
	}
	auto select_stmt = unique_ptr_cast<SQLStatement, SelectStatement>(std::move(parser.statements[0]));
	auto macro_function = make_uniq<TableMacroFunction>(std::move(select_stmt->node));

	// Set macro parameters
	macro_function->parameters.push_back(make_uniq<ColumnRefExpression>("t")); // table_in
	macro_function->parameters.push_back(make_uniq<ColumnRefExpression>("g")); // group_col
	macro_function->parameters.push_back(make_uniq<ColumnRefExpression>("d")); // date_col
	macro_function->parameters.push_back(make_uniq<ColumnRefExpression>("v")); // value_col
	macro_function->parameters.push_back(make_uniq<ColumnRefExpression>("f")); // frequency

	// Create macro info
	auto macro_info = make_uniq<CreateMacroInfo>(CatalogType::TABLE_MACRO_ENTRY);
	macro_info->schema = DEFAULT_SCHEMA;
	macro_info->name = "anofox_fcst_ts_stats";
	macro_info->temporary = true;
	macro_info->internal = true;
	macro_info->macros.push_back(std::move(macro_function));
	loader.RegisterFunction(*macro_info);

	// Register alias for the macro (ts_stats)
	// Create a new parser for the alias (can't reuse the same parser)
	Parser alias_parser;
	alias_parser.ParseQuery(macro_sql);
	if (alias_parser.statements.size() != 1 || alias_parser.statements[0]->type != StatementType::SELECT_STATEMENT) {
		throw InternalException("Failed to parse macro SQL for ts_stats alias");
	}
	auto alias_select_stmt = unique_ptr_cast<SQLStatement, SelectStatement>(std::move(alias_parser.statements[0]));
	auto alias_macro_function = make_uniq<TableMacroFunction>(std::move(alias_select_stmt->node));
	alias_macro_function->parameters.push_back(make_uniq<ColumnRefExpression>("t"));
	alias_macro_function->parameters.push_back(make_uniq<ColumnRefExpression>("g"));
	alias_macro_function->parameters.push_back(make_uniq<ColumnRefExpression>("d"));
	alias_macro_function->parameters.push_back(make_uniq<ColumnRefExpression>("v"));
	alias_macro_function->parameters.push_back(make_uniq<ColumnRefExpression>("f"));

	auto alias_macro_info = make_uniq<CreateMacroInfo>(CatalogType::TABLE_MACRO_ENTRY);
	alias_macro_info->schema = DEFAULT_SCHEMA;
	alias_macro_info->name = "ts_stats";
	alias_macro_info->temporary = true;
	alias_macro_info->internal = true;
	alias_macro_info->macros.push_back(std::move(alias_macro_function));
	alias_macro_info->alias_of = "anofox_fcst_ts_stats";
	alias_macro_info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
	loader.RegisterFunction(*alias_macro_info);

	// Register alias for TS_QUALITY_REPORT
	auto register_table_alias = [&loader](TableFunction func) {
		string name = func.name;
		if (name.find("anofox_fcst_") == 0) {
			string alias_name = name.substr(12); // Remove "anofox_fcst_" prefix
			TableFunction alias_func = func;
			alias_func.name = alias_name;
			TableFunctionSet alias_set(alias_name);
			alias_set.AddFunction(std::move(alias_func));
			CreateTableFunctionInfo alias_info(std::move(alias_set));
			alias_info.alias_of = name;
			alias_info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
			loader.RegisterFunction(std::move(alias_info));
		}
	};
	register_table_alias(ts_quality_report_for_alias);
}

// Register EDA (Exploratory Data Analysis) table macros
void RegisterEDAMacros(ExtensionLoader &loader) {
	// Group macros by name to handle overloads
	std::map<string, vector<idx_t>> macro_groups;
	for (idx_t index = 0; eda_macros[index].name != nullptr; index++) {
		string name = string(eda_macros[index].name);
		macro_groups[name].push_back(index);
	}

	// Register each group (handles overloads)
	for (const auto &group : macro_groups) {
		if (group.second.size() == 1) {
			// Single macro, register normally
			idx_t index = group.second[0];
			auto table_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(eda_macros[index]);
			table_info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
			loader.RegisterFunction(*table_info);

			// Register alias
			if (table_info->name.find("anofox_fcst_") == 0) {
				string alias_name = table_info->name.substr(12); // Remove "anofox_fcst_" prefix
				DefaultTableMacro alias_macro = eda_macros[index];
				alias_macro.name = alias_name.c_str();
				auto alias_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(alias_macro);
				alias_info->alias_of = table_info->name;
				alias_info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
				loader.RegisterFunction(*alias_info);
			}
		} else {
			// Multiple macros with same name - create overloaded macro with typed parameters
			// For ts_stats, we have VARCHAR and INTEGER overloads
			if (group.second.size() == 2 && group.first == "anofox_fcst_ts_stats") {
				// Create a single CreateMacroInfo with both overloads
				auto first_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(eda_macros[group.second[0]]);
				auto second_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(eda_macros[group.second[1]]);

				// Set parameter types: VARCHAR for first (date-based), INTEGER for second (integer-based)
				// The frequency parameter is the 5th parameter (index 4) for ts_stats
				idx_t freq_param_idx = 4;

				// First overload: VARCHAR frequency (date-based)
				if (first_info->macros[0]->types.size() <= freq_param_idx) {
					first_info->macros[0]->types.resize(freq_param_idx + 1, LogicalType::UNKNOWN);
				}
				first_info->macros[0]->types[freq_param_idx] = LogicalType::VARCHAR;

				// Second overload: INTEGER frequency (integer-based)
				if (second_info->macros[0]->types.size() <= freq_param_idx) {
					second_info->macros[0]->types.resize(freq_param_idx + 1, LogicalType::UNKNOWN);
				}
				second_info->macros[0]->types[freq_param_idx] = LogicalType::INTEGER;

				// Add second overload to first info
				first_info->macros.push_back(std::move(second_info->macros[0]));

				// Register the combined macro
				first_info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
				loader.RegisterFunction(*first_info);

				// Register alias
				if (first_info->name.find("anofox_fcst_") == 0) {
					string alias_name = first_info->name.substr(12); // Remove "anofox_fcst_" prefix
					auto alias_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(eda_macros[group.second[0]]);
					alias_info->name = alias_name;
					// Copy the overloads
					for (size_t i = 1; i < first_info->macros.size(); i++) {
						auto alias_macro =
						    DefaultTableFunctionGenerator::CreateTableMacroInfo(eda_macros[group.second[i]]);
						alias_info->macros.push_back(std::move(alias_macro->macros[0]));
					}
					alias_info->alias_of = first_info->name;
					alias_info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
					loader.RegisterFunction(*alias_info);
				}
			} else {
				// For other cases, register normally
				for (idx_t i = 0; i < group.second.size(); i++) {
					idx_t index = group.second[i];
					auto table_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(eda_macros[index]);
					table_info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
					loader.RegisterFunction(*table_info);

					// Register alias
					if (table_info->name.find("anofox_fcst_") == 0) {
						string alias_name = table_info->name.substr(12); // Remove "anofox_fcst_" prefix
						DefaultTableMacro alias_macro = eda_macros[index];
						alias_macro.name = alias_name.c_str();
						auto alias_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(alias_macro);
						alias_info->alias_of = table_info->name;
						alias_info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
						loader.RegisterFunction(*alias_info);
					}
				}
			}
		}
	}
}

// Array of data quality macros
static const DefaultTableMacro data_quality_macros[] = {

    // TS_DATA_QUALITY: Comprehensive data quality assessment (VARCHAR frequency - date-based)
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_data_quality",
     {"table_name", "unique_id_col", "date_col", "value_col", "n_short", "frequency", nullptr},
     {{nullptr, nullptr}},
     R"(
        WITH frequency_parsed AS (
            SELECT 
                frequency,
                CASE 
                    WHEN frequency IS NULL THEN INTERVAL '1 day'
                    WHEN UPPER(TRIM(CAST(frequency AS VARCHAR))) IN ('1D', '1DAY') THEN INTERVAL '1 day'
                    WHEN UPPER(TRIM(CAST(frequency AS VARCHAR))) IN ('30M', '30MIN', '30MINUTE', '30MINUTES') THEN INTERVAL '30 minutes'
                    WHEN UPPER(TRIM(CAST(frequency AS VARCHAR))) IN ('1H', '1HOUR', '1HOURS') THEN INTERVAL '1 hour'
                    WHEN UPPER(TRIM(CAST(frequency AS VARCHAR))) IN ('1W', '1WEEK', '1WEEKS') THEN INTERVAL '1 week'
                    WHEN UPPER(TRIM(CAST(frequency AS VARCHAR))) IN ('1MO', '1MONTH', '1MONTHS') THEN INTERVAL '1 month'
                    WHEN UPPER(TRIM(CAST(frequency AS VARCHAR))) IN ('1Q', '1QUARTER', '1QUARTERS') THEN INTERVAL '3 months'
                    WHEN UPPER(TRIM(CAST(frequency AS VARCHAR))) IN ('1Y', '1YEAR', '1YEARS') THEN INTERVAL '1 year'
                    ELSE INTERVAL '1 day'
                END AS __interval
            FROM (SELECT 1) t
        ),
        base_data AS (
            SELECT 
                unique_id_col AS __uid,
                date_col AS __date,
                value_col AS __value
            FROM QUERY_TABLE(table_name)
        ),
        params AS (
            SELECT COALESCE(CAST(n_short AS INTEGER), 30) AS n_short_threshold
        ),
        -- Helper CTEs for structural checks
        key_counts AS (
            SELECT 
                __uid,
                __date,
                COUNT(*) AS key_count
            FROM base_data
            GROUP BY __uid, __date
        ),
        duplicate_stats AS (
            SELECT 
                __uid,
                SUM(CASE WHEN key_count > 1 THEN key_count - 1 ELSE 0 END) AS n_duplicates
            FROM key_counts
            GROUP BY __uid
        ),
        -- Helper CTEs for temporal checks
        series_stats AS (
            SELECT 
                __uid,
                COUNT(*) AS length,
                MIN(__date) AS start_date,
                MAX(__date) AS end_date
            FROM base_data
            GROUP BY __uid
        ),
        series_ranges AS (
            SELECT 
                __uid,
                MIN(__date) AS start_date,
                MAX(__date) AS end_date,
                COUNT(*) AS actual_count
            FROM base_data
            GROUP BY __uid
        ),
        expected_counts AS (
            SELECT 
                sr.__uid,
                sr.start_date,
                sr.end_date,
                sr.actual_count,
                fp.__interval,
                CASE 
                    WHEN sr.end_date >= sr.start_date 
                    THEN CAST(EXTRACT(EPOCH FROM (CAST(sr.end_date AS TIMESTAMP) - CAST(sr.start_date AS TIMESTAMP))) / EXTRACT(EPOCH FROM fp.__interval) AS INTEGER) + 1
                    ELSE 1
                END AS expected_count
            FROM series_ranges sr
            CROSS JOIN frequency_parsed fp
        ),
        gap_stats AS (
            SELECT 
                __uid,
                actual_count,
                expected_count,
                expected_count - actual_count AS n_gaps,
                CASE 
                    WHEN expected_count > 0 
                    THEN 100.0 * (expected_count - actual_count) / expected_count
                    ELSE 0.0
                END AS gap_pct
            FROM expected_counts
        ),
        series_bounds AS (
            SELECT 
                __uid,
                MIN(__date) AS start_date,
                MAX(__date) AS end_date
            FROM base_data
            GROUP BY __uid
        ),
        alignment_stats AS (
            SELECT 
                COUNT(DISTINCT start_date) AS n_start_dates,
                COUNT(DISTINCT end_date) AS n_end_dates,
                COUNT(DISTINCT __uid) AS n_series
            FROM series_bounds
        ),
        frequency_stats AS (
            SELECT 
                __uid,
                COUNT(*) AS n_points,
                MIN(__date) AS start_date,
                MAX(__date) AS end_date,
                CASE 
                    WHEN MAX(__date) > MIN(__date)
                    THEN CAST(DATEDIFF('day', MIN(__date), MAX(__date)) AS DOUBLE) / GREATEST(COUNT(*) - 1, 1)
                    ELSE NULL
                END AS avg_interval_days
            FROM base_data
            GROUP BY __uid
        ),
        frequency_classification AS (
            SELECT 
                __uid,
                avg_interval_days,
                CASE 
                    WHEN avg_interval_days IS NULL THEN 'Unknown'
                    WHEN avg_interval_days < 0.5 THEN 'Sub-hourly'
                    WHEN avg_interval_days < 1.0 THEN 'Hourly'
                    WHEN avg_interval_days < 7.0 THEN 'Daily'
                    WHEN avg_interval_days < 30.0 THEN 'Weekly'
                    WHEN avg_interval_days < 90.0 THEN 'Monthly'
                    ELSE 'Quarterly+'
                END AS inferred_frequency
            FROM frequency_stats
        ),
        frequency_diversity AS (
            SELECT 
                COUNT(DISTINCT inferred_frequency) AS n_frequencies,
                COUNT(DISTINCT __uid) AS n_series
            FROM frequency_classification
            WHERE inferred_frequency != 'Unknown'
        ),
        -- Helper CTEs for magnitude checks
        missing_stats AS (
            SELECT 
                __uid,
                COUNT(*) AS total_count,
                COUNT(CASE WHEN __value IS NULL THEN 1 END) AS null_count,
                CASE 
                    WHEN COUNT(*) > 0 
                    THEN 100.0 * COUNT(CASE WHEN __value IS NULL THEN 1 END) / COUNT(*)
                    ELSE 0.0
                END AS null_pct
            FROM base_data
            GROUP BY __uid
        ),
        negative_stats AS (
            SELECT 
                __uid,
                COUNT(CASE WHEN __value < 0 THEN 1 END) AS negative_count,
                COUNT(*) AS total_count
            FROM base_data
            WHERE __value IS NOT NULL
            GROUP BY __uid
        ),
        variance_stats AS (
            SELECT 
                __uid,
                COUNT(*) AS count,
                COUNT(DISTINCT __value) AS distinct_count,
                STDDEV(__value) AS stddev
            FROM base_data
            WHERE __value IS NOT NULL
            GROUP BY __uid
        ),
        -- Helper CTEs for behavioural checks
        zero_stats AS (
            SELECT 
                __uid,
                COUNT(*) AS total_count,
                COUNT(CASE WHEN __value = 0 OR __value IS NULL THEN 1 END) AS zero_count,
                CASE 
                    WHEN COUNT(*) > 0 
                    THEN 100.0 * COUNT(CASE WHEN __value = 0 OR __value IS NULL THEN 1 END) / COUNT(*)
                    ELSE 0.0
                END AS zero_pct
            FROM base_data
            GROUP BY __uid
        ),
        series_agg AS (
            SELECT 
                __uid,
                LIST(__value ORDER BY __date) AS values
            FROM base_data
            WHERE __value IS NOT NULL
            GROUP BY __uid
            HAVING COUNT(*) >= 7
        ),
        seasonality_results AS (
            SELECT 
                __uid,
                values,
                TS_DETECT_SEASONALITY(values) AS detected_periods
            FROM series_agg
        ),
        ordered_data AS (
            SELECT 
                __uid,
                __date,
                __value,
                ROW_NUMBER() OVER (PARTITION BY __uid ORDER BY __date) AS row_num
            FROM base_data
            WHERE __value IS NOT NULL
        ),
        trend_stats AS (
            SELECT 
                __uid,
                COUNT(*) AS n_points,
                CORR(row_num, __value) AS trend_correlation
            FROM ordered_data
            GROUP BY __uid
            HAVING COUNT(*) >= 3
        )"
     R"(
        ),
        -- Dimension 1: Structural Integrity
        structural_checks AS (
            SELECT 
                __uid AS unique_id,
                'Structural' AS dimension,
                'key_uniqueness' AS metric,
                CAST(n_duplicates AS INTEGER) AS value,
                NULL AS value_pct
            FROM duplicate_stats
            
            UNION ALL
            
            SELECT 
                'ALL_SERIES' AS unique_id,
                'Structural' AS dimension,
                'id_cardinality' AS metric,
                CAST(COUNT(DISTINCT __uid) AS INTEGER) AS value,
                NULL AS value_pct
            FROM base_data
        ),
        -- Dimension 2: Temporal Integrity
        temporal_checks AS (
            SELECT 
                ss.__uid AS unique_id,
                'Temporal' AS dimension,
                'series_length' AS metric,
                CAST(ss.length AS INTEGER) AS value,
                NULL AS value_pct
            FROM series_stats ss
            CROSS JOIN params
            
            UNION ALL
            
            SELECT 
                __uid AS unique_id,
                'Temporal' AS dimension,
                'timestamp_gaps' AS metric,
                CAST(n_gaps AS INTEGER) AS value,
                gap_pct / 100.0 AS value_pct
            FROM gap_stats
            
            UNION ALL
            
            SELECT 
                'ALL_SERIES' AS unique_id,
                'Temporal' AS dimension,
                'series_alignment' AS metric,
                CAST(CASE 
                    WHEN n_start_dates > 1 OR n_end_dates > 1 
                    THEN GREATEST(n_start_dates, n_end_dates)
                    ELSE 1
                END AS INTEGER) AS value,
                NULL AS value_pct
            FROM alignment_stats
            
            UNION ALL
            
            SELECT 
                'ALL_SERIES' AS unique_id,
                'Temporal' AS dimension,
                'frequency_inference' AS metric,
                CAST(n_frequencies AS INTEGER) AS value,
                NULL AS value_pct
            FROM frequency_diversity
        ),
        -- Dimension 3: Magnitude & Value Validity
        magnitude_checks AS (
            SELECT 
                __uid AS unique_id,
                'Magnitude' AS dimension,
                'missing_values' AS metric,
                CAST(null_count AS INTEGER) AS value,
                null_pct / 100.0 AS value_pct
            FROM missing_stats
            
            UNION ALL
            
            SELECT 
                __uid AS unique_id,
                'Magnitude' AS dimension,
                'value_bounds' AS metric,
                CAST(negative_count AS INTEGER) AS value,
                CASE 
                    WHEN total_count > 0 
                    THEN CAST(negative_count AS DOUBLE) / total_count
                    ELSE NULL
                END AS value_pct
            FROM negative_stats
            
            UNION ALL
            
            SELECT 
                __uid AS unique_id,
                'Magnitude' AS dimension,
                'static_values' AS metric,
                CAST(CASE 
                    WHEN distinct_count = 1 OR (stddev IS NOT NULL AND stddev = 0)
                    THEN 1
                    ELSE 0
                END AS INTEGER) AS value,
                NULL AS value_pct
            FROM variance_stats
        ),
        -- Dimension 4: Behavioural/Statistical (Advanced)
        behavioural_checks AS (
            SELECT 
                __uid AS unique_id,
                'Behavioural' AS dimension,
                'intermittency' AS metric,
                CAST(zero_count AS INTEGER) AS value,
                zero_pct / 100.0 AS value_pct
            FROM zero_stats
            
            UNION ALL
            
            SELECT 
                __uid AS unique_id,
                'Behavioural' AS dimension,
                'seasonality_check' AS metric,
                CAST(CASE 
                    WHEN LEN(detected_periods) = 0 
                    THEN 0
                    ELSE 1
                END AS INTEGER) AS value,
                NULL AS value_pct
            FROM seasonality_results
            
            UNION ALL
            
            SELECT 
                __uid AS unique_id,
                'Behavioural' AS dimension,
                'trend_detection' AS metric,
                NULL AS value,
                ABS(trend_correlation) AS value_pct
            FROM trend_stats
        ),
        -- Combine all checks
        all_checks AS (
            SELECT * FROM structural_checks
            UNION ALL
            SELECT * FROM temporal_checks
            UNION ALL
            SELECT * FROM magnitude_checks
            UNION ALL
            SELECT * FROM behavioural_checks
        )
        SELECT 
            unique_id,
            dimension,
            metric,
            value,
            value_pct
        FROM all_checks
        ORDER BY 
            dimension,
            metric,
            unique_id
    )"},

    // TS_DATA_QUALITY: Comprehensive data quality assessment (INTEGER frequency - integer-based)
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_data_quality",
     {"table_name", "unique_id_col", "date_col", "value_col", "n_short", "frequency", nullptr},
     {{nullptr, nullptr}},
     R"(
        WITH frequency_parsed AS (
            SELECT 
                COALESCE(frequency, 1) AS __int_step
            FROM (SELECT 1) t
        ),
        base_data AS (
            SELECT 
                unique_id_col AS __uid,
                date_col AS __date,
                value_col AS __value
            FROM QUERY_TABLE(table_name)
        ),
        params AS (
            SELECT COALESCE(CAST(n_short AS INTEGER), 30) AS n_short_threshold
        ),
        -- Helper CTEs for structural checks
        key_counts AS (
            SELECT 
                __uid,
                __date,
                COUNT(*) AS key_count
            FROM base_data
            GROUP BY __uid, __date
        ),
        duplicate_stats AS (
            SELECT 
                __uid,
                SUM(CASE WHEN key_count > 1 THEN key_count - 1 ELSE 0 END) AS n_duplicates
            FROM key_counts
            GROUP BY __uid
        ),
        -- Helper CTEs for temporal checks
        series_stats AS (
            SELECT 
                __uid,
                COUNT(*) AS length,
                MIN(__date) AS start_date,
                MAX(__date) AS end_date
            FROM base_data
            GROUP BY __uid
        ),
        series_ranges AS (
            SELECT 
                __uid,
                MIN(__date) AS start_date,
                MAX(__date) AS end_date,
                COUNT(*) AS actual_count
            FROM base_data
            GROUP BY __uid
        ),
        expected_counts AS (
            SELECT 
                sr.__uid,
                sr.start_date,
                sr.end_date,
                sr.actual_count,
                fp.__int_step,
                CASE 
                    WHEN sr.end_date >= sr.start_date 
                    THEN CAST((sr.end_date - sr.start_date) / fp.__int_step AS INTEGER) + 1
                    ELSE 1
                END AS expected_count
            FROM series_ranges sr
            CROSS JOIN frequency_parsed fp
        ),
        gap_stats AS (
            SELECT 
                __uid,
                actual_count,
                expected_count,
                expected_count - actual_count AS n_gaps,
                CASE 
                    WHEN expected_count > 0 
                    THEN 100.0 * (expected_count - actual_count) / expected_count
                    ELSE 0.0
                END AS gap_pct
            FROM expected_counts
        ),
        series_bounds AS (
            SELECT 
                __uid,
                MIN(__date) AS start_date,
                MAX(__date) AS end_date
            FROM base_data
            GROUP BY __uid
        ),
        alignment_stats AS (
            SELECT 
                COUNT(DISTINCT start_date) AS n_start_dates,
                COUNT(DISTINCT end_date) AS n_end_dates,
                COUNT(DISTINCT __uid) AS n_series
            FROM series_bounds
        ),
        frequency_stats AS (
            SELECT 
                __uid,
                COUNT(*) AS n_points,
                MIN(__date) AS start_date,
                MAX(__date) AS end_date,
                CASE 
                    WHEN MAX(__date) > MIN(__date)
                    THEN CAST(DATEDIFF('day', MIN(__date), MAX(__date)) AS DOUBLE) / GREATEST(COUNT(*) - 1, 1)
                    ELSE NULL
                END AS avg_interval_days
            FROM base_data
            GROUP BY __uid
        ),
        frequency_classification AS (
            SELECT 
                __uid,
                avg_interval_days,
                CASE 
                    WHEN avg_interval_days IS NULL THEN 'Unknown'
                    WHEN avg_interval_days < 0.5 THEN 'Sub-hourly'
                    WHEN avg_interval_days < 1.0 THEN 'Hourly'
                    WHEN avg_interval_days < 7.0 THEN 'Daily'
                    WHEN avg_interval_days < 30.0 THEN 'Weekly'
                    WHEN avg_interval_days < 90.0 THEN 'Monthly'
                    ELSE 'Quarterly+'
                END AS inferred_frequency
            FROM frequency_stats
        ),
        frequency_diversity AS (
            SELECT 
                COUNT(DISTINCT inferred_frequency) AS n_frequencies,
                COUNT(DISTINCT __uid) AS n_series
            FROM frequency_classification
            WHERE inferred_frequency != 'Unknown'
        ),
        -- Helper CTEs for magnitude checks
        missing_stats AS (
            SELECT 
                __uid,
                COUNT(*) AS total_count,
                COUNT(CASE WHEN __value IS NULL THEN 1 END) AS null_count,
                CASE 
                    WHEN COUNT(*) > 0 
                    THEN 100.0 * COUNT(CASE WHEN __value IS NULL THEN 1 END) / COUNT(*)
                    ELSE 0.0
                END AS null_pct
            FROM base_data
            GROUP BY __uid
        ),
        negative_stats AS (
            SELECT 
                __uid,
                COUNT(CASE WHEN __value < 0 THEN 1 END) AS negative_count,
                COUNT(*) AS total_count
            FROM base_data
            WHERE __value IS NOT NULL
            GROUP BY __uid
        ),
        variance_stats AS (
            SELECT 
                __uid,
                COUNT(*) AS count,
                COUNT(DISTINCT __value) AS distinct_count,
                STDDEV(__value) AS stddev
            FROM base_data
            WHERE __value IS NOT NULL
            GROUP BY __uid
        ),
        -- Helper CTEs for behavioural checks
        zero_stats AS (
            SELECT 
                __uid,
                COUNT(*) AS total_count,
                COUNT(CASE WHEN __value = 0 OR __value IS NULL THEN 1 END) AS zero_count,
                CASE 
                    WHEN COUNT(*) > 0 
                    THEN 100.0 * COUNT(CASE WHEN __value = 0 OR __value IS NULL THEN 1 END) / COUNT(*)
                    ELSE 0.0
                END AS zero_pct
            FROM base_data
            GROUP BY __uid
        ),
        series_agg AS (
            SELECT 
                __uid,
                LIST(__value ORDER BY __date) AS values
            FROM base_data
            WHERE __value IS NOT NULL
            GROUP BY __uid
            HAVING COUNT(*) >= 7
        ),
        seasonality_results AS (
            SELECT 
                __uid,
                values,
                TS_DETECT_SEASONALITY(values) AS detected_periods
            FROM series_agg
        ),
        ordered_data AS (
            SELECT 
                __uid,
                __date,
                __value,
                ROW_NUMBER() OVER (PARTITION BY __uid ORDER BY __date) AS row_num
            FROM base_data
            WHERE __value IS NOT NULL
        ),
        trend_stats AS (
            SELECT 
                __uid,
                COUNT(*) AS n_points,
                CORR(row_num, __value) AS trend_correlation
            FROM ordered_data
            GROUP BY __uid
            HAVING COUNT(*) >= 3
        ),
        -- Dimension 1: Structural Integrity
        structural_checks AS (
            SELECT 
                __uid AS unique_id,
                'Structural' AS dimension,
                'key_uniqueness' AS metric,
                CAST(n_duplicates AS INTEGER) AS value,
                NULL AS value_pct
            FROM duplicate_stats
            
            UNION ALL
            
            SELECT 
                'ALL_SERIES' AS unique_id,
                'Structural' AS dimension,
                'id_cardinality' AS metric,
                CAST(COUNT(DISTINCT __uid) AS INTEGER) AS value,
                NULL AS value_pct
            FROM base_data
        ),
        -- Dimension 2: Temporal Integrity
        temporal_checks AS (
            SELECT 
                ss.__uid AS unique_id,
                'Temporal' AS dimension,
                'series_length' AS metric,
                CAST(ss.length AS INTEGER) AS value,
                NULL AS value_pct
            FROM series_stats ss
            CROSS JOIN params
            
            UNION ALL
            
            SELECT 
                __uid AS unique_id,
                'Temporal' AS dimension,
                'timestamp_gaps' AS metric,
                CAST(n_gaps AS INTEGER) AS value,
                gap_pct / 100.0 AS value_pct
            FROM gap_stats
            
            UNION ALL
            
            SELECT 
                'ALL_SERIES' AS unique_id,
                'Temporal' AS dimension,
                'series_alignment' AS metric,
                CAST(CASE 
                    WHEN n_start_dates > 1 OR n_end_dates > 1 
                    THEN GREATEST(n_start_dates, n_end_dates)
                    ELSE 1
                END AS INTEGER) AS value,
                NULL AS value_pct
            FROM alignment_stats
            
            UNION ALL
            
            SELECT 
                'ALL_SERIES' AS unique_id,
                'Temporal' AS dimension,
                'frequency_inference' AS metric,
                CAST(n_frequencies AS INTEGER) AS value,
                NULL AS value_pct
            FROM frequency_diversity
        ),
        -- Dimension 3: Magnitude & Value Validity
        magnitude_checks AS (
            SELECT 
                __uid AS unique_id,
                'Magnitude' AS dimension,
                'missing_values' AS metric,
                CAST(null_count AS INTEGER) AS value,
                null_pct / 100.0 AS value_pct
            FROM missing_stats
            
            UNION ALL
            
            SELECT 
                __uid AS unique_id,
                'Magnitude' AS dimension,
                'value_bounds' AS metric,
                CAST(negative_count AS INTEGER) AS value,
                CASE 
                    WHEN total_count > 0 
                    THEN CAST(negative_count AS DOUBLE) / total_count
                    ELSE NULL
                END AS value_pct
            FROM negative_stats
            
            UNION ALL
            
            SELECT 
                __uid AS unique_id,
                'Magnitude' AS dimension,
                'static_values' AS metric,
                CAST(CASE 
                    WHEN distinct_count = 1 OR (stddev IS NOT NULL AND stddev = 0)
                    THEN 1
                    ELSE 0
                END AS INTEGER) AS value,
                NULL AS value_pct
            FROM variance_stats
        ),
        -- Dimension 4: Behavioural/Statistical (Advanced)
        behavioural_checks AS (
            SELECT 
                __uid AS unique_id,
                'Behavioural' AS dimension,
                'intermittency' AS metric,
                CAST(zero_count AS INTEGER) AS value,
                zero_pct / 100.0 AS value_pct
            FROM zero_stats
            
            UNION ALL
            
            SELECT 
                __uid AS unique_id,
                'Behavioural' AS dimension,
                'seasonality_check' AS metric,
                CAST(CASE 
                    WHEN LEN(detected_periods) = 0 
                    THEN 0
                    ELSE 1
                END AS INTEGER) AS value,
                NULL AS value_pct
            FROM seasonality_results
            
            UNION ALL
            
            SELECT 
                __uid AS unique_id,
                'Behavioural' AS dimension,
                'trend_detection' AS metric,
                NULL AS value,
                ABS(trend_correlation) AS value_pct
            FROM trend_stats
        ),
        -- Combine all checks
        all_checks AS (
            SELECT * FROM structural_checks
            UNION ALL
            SELECT * FROM temporal_checks
            UNION ALL
            SELECT * FROM magnitude_checks
            UNION ALL
            SELECT * FROM behavioural_checks
        )
        SELECT 
            unique_id,
            dimension,
            metric,
            value,
            value_pct
        FROM all_checks
        ORDER BY 
            dimension,
            metric,
            unique_id
    )"},

    // TS_DATA_QUALITY_SUMMARY: Aggregated summary by dimension
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_data_quality_summary",
     {"table_name", "unique_id_col", "date_col", "value_col", "n_short", nullptr},
     {{nullptr, nullptr}},
     R"(
        WITH health_card AS (
            SELECT * FROM ts_data_quality(table_name, unique_id_col, date_col, value_col, n_short, NULL::VARCHAR)
        )
        SELECT 
            dimension,
            metric,
            COUNT(*) AS total_series,
            COUNT(DISTINCT unique_id) AS unique_series_count
        FROM health_card
        WHERE unique_id != 'ALL_SERIES'
        GROUP BY dimension, metric
        ORDER BY 
            dimension,
            metric
    )"},

    // End marker
    {nullptr, nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr}};

// Register Data Quality table macros
void RegisterDataQualityMacros(ExtensionLoader &loader) {
	// Group macros by name to handle overloads
	std::map<string, vector<idx_t>> macro_groups;
	for (idx_t index = 0; data_quality_macros[index].name != nullptr; index++) {
		string name = string(data_quality_macros[index].name);
		macro_groups[name].push_back(index);
	}

	// Register each group (handles overloads)
	for (const auto &group : macro_groups) {
		if (group.second.size() == 1) {
			// Single macro, register normally
			idx_t index = group.second[0];
			auto table_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(data_quality_macros[index]);
			table_info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
			loader.RegisterFunction(*table_info);

			// Register alias
			if (table_info->name.find("anofox_fcst_") == 0) {
				string alias_name = table_info->name.substr(12); // Remove "anofox_fcst_" prefix
				DefaultTableMacro alias_macro = data_quality_macros[index];
				alias_macro.name = alias_name.c_str();
				auto alias_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(alias_macro);
				alias_info->alias_of = table_info->name;
				alias_info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
				loader.RegisterFunction(*alias_info);
			}
		} else {
			// Multiple macros with same name - create overloaded macro with typed parameters
			// For ts_data_quality, we have VARCHAR and INTEGER overloads
			if (group.second.size() == 2 && group.first == "anofox_fcst_ts_data_quality") {
				// Create a single CreateMacroInfo with both overloads
				auto first_info =
				    DefaultTableFunctionGenerator::CreateTableMacroInfo(data_quality_macros[group.second[0]]);
				auto second_info =
				    DefaultTableFunctionGenerator::CreateTableMacroInfo(data_quality_macros[group.second[1]]);

				// Set parameter types: VARCHAR for first (date-based), INTEGER for second (integer-based)
				// The frequency parameter is the 6th parameter (index 5) for ts_data_quality
				idx_t freq_param_idx = 5;

				// First overload: VARCHAR frequency (date-based)
				if (first_info->macros[0]->types.size() <= freq_param_idx) {
					first_info->macros[0]->types.resize(freq_param_idx + 1, LogicalType::UNKNOWN);
				}
				first_info->macros[0]->types[freq_param_idx] = LogicalType::VARCHAR;

				// Second overload: INTEGER frequency (integer-based)
				if (second_info->macros[0]->types.size() <= freq_param_idx) {
					second_info->macros[0]->types.resize(freq_param_idx + 1, LogicalType::UNKNOWN);
				}
				second_info->macros[0]->types[freq_param_idx] = LogicalType::INTEGER;

				// Add second overload to first info
				first_info->macros.push_back(std::move(second_info->macros[0]));

				// Register the combined macro
				first_info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
				loader.RegisterFunction(*first_info);

				// Register alias
				if (first_info->name.find("anofox_fcst_") == 0) {
					string alias_name = first_info->name.substr(12); // Remove "anofox_fcst_" prefix
					auto alias_info =
					    DefaultTableFunctionGenerator::CreateTableMacroInfo(data_quality_macros[group.second[0]]);
					alias_info->name = alias_name;
					// Copy the overloads
					for (size_t i = 1; i < first_info->macros.size(); i++) {
						auto alias_macro =
						    DefaultTableFunctionGenerator::CreateTableMacroInfo(data_quality_macros[group.second[i]]);
						alias_info->macros.push_back(std::move(alias_macro->macros[0]));
					}
					alias_info->alias_of = first_info->name;
					alias_info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
					loader.RegisterFunction(*alias_info);
				}
			} else {
				// For other cases, register normally
				for (idx_t i = 0; i < group.second.size(); i++) {
					idx_t index = group.second[i];
					auto table_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(data_quality_macros[index]);
					table_info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
					loader.RegisterFunction(*table_info);

					// Register alias
					if (table_info->name.find("anofox_fcst_") == 0) {
						string alias_name = table_info->name.substr(12); // Remove "anofox_fcst_" prefix
						DefaultTableMacro alias_macro = data_quality_macros[index];
						alias_macro.name = alias_name.c_str();
						auto alias_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(alias_macro);
						alias_info->alias_of = table_info->name;
						alias_info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
						loader.RegisterFunction(*alias_info);
					}
				}
			}
		}
	}
}

} // namespace duckdb
