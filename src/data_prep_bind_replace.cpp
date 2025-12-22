#include "data_prep_bind_replace.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"
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

unique_ptr<TableRef> TSFillGapsBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	// Validate we have the right number of parameters
	if (input.inputs.size() < 5) {
		throw InvalidInputException(
		    "anofox_fcst_ts_fill_gaps requires 5 arguments: table_name, group_col, date_col, value_col, frequency");
	}

	// Extract parameters as strings
	string table_name = input.inputs[0].ToString();
	string group_col = input.inputs[1].ToString();
	string date_col = input.inputs[2].ToString();
	string value_col = input.inputs[3].ToString();
	// Frequency is required - reject NULL
	if (input.inputs[4].IsNull()) {
		throw InvalidInputException("frequency parameter is required and cannot be NULL");
	}

	// Escape identifiers to prevent SQL injection
	string escaped_group_col = KeywordHelper::WriteOptionallyQuoted(group_col);
	string escaped_date_col = KeywordHelper::WriteOptionallyQuoted(date_col);
	string escaped_value_col = KeywordHelper::WriteOptionallyQuoted(value_col);
	string escaped_table = KeywordHelper::WriteQuoted(table_name);

	// Determine if frequency is INTEGER or VARCHAR
	bool is_integer_frequency =
	    (input.inputs[4].type().id() == LogicalTypeId::INTEGER || input.inputs[4].type().id() == LogicalTypeId::BIGINT);

	string frequency_str;
	if (is_integer_frequency) {
		frequency_str = input.inputs[4].ToString();
	} else {
		string frequency = input.inputs[4].ToString();
		if (frequency.empty()) {
			throw InvalidInputException("frequency parameter cannot be empty");
		}
		frequency_str = KeywordHelper::WriteQuoted(frequency);
	}

	// Generate SQL dynamically - route based on frequency type
	std::ostringstream sql;

	if (is_integer_frequency) {
		// INTEGER frequency: Use integer-based logic (for INTEGER/BIGINT date columns)
		sql << R"(WITH orig_aliased AS (
    SELECT 
        )" << escaped_group_col
		    << R"( AS __gid,
        )" << escaped_date_col
		    << R"( AS __did,
        )" << escaped_value_col
		    << R"( AS __vid,
        *
    FROM QUERY_TABLE()"
		    << escaped_table << R"()
),
frequency_parsed AS (
    SELECT 
        )" << frequency_str
		    << R"( AS __int_step
    FROM (SELECT 1) t
),
series_ranges AS (
    SELECT 
        __gid,
        MIN(__did) AS __min,
        MAX(__did) AS __max
    FROM orig_aliased
    GROUP BY __gid
),
grid AS (
    SELECT 
        sr.__gid,
        UNNEST(GENERATE_SERIES(sr.__min, sr.__max, fp.__int_step)) AS __did
    FROM series_ranges sr
    CROSS JOIN frequency_parsed fp
),
with_original_data AS (
    SELECT 
        g.__gid,
        g.__did,
        oa.__vid,
        oa.* EXCLUDE (__gid, __did, __vid)
    FROM grid g
    LEFT JOIN orig_aliased oa ON g.__gid = oa.__gid AND g.__did = oa.__did
)
SELECT 
    with_original_data.* EXCLUDE (__gid, __did, __vid, )"
		    << escaped_group_col << R"(, )" << escaped_date_col << R"(, )" << escaped_value_col << R"(),
    with_original_data.__gid AS )"
		    << escaped_group_col << R"(,
    with_original_data.__did AS )"
		    << escaped_date_col << R"(,
    with_original_data.__vid AS )"
		    << escaped_value_col << R"(
FROM with_original_data
ORDER BY )" << escaped_group_col
		    << R"(, )" << escaped_date_col << R"()";
	} else {
		// VARCHAR frequency: Use interval-based logic (for DATE/TIMESTAMP columns)
		// Preserve original date type - don't cast to DATE (loses TIMESTAMP time component)
		sql << R"(WITH orig_aliased AS (
    SELECT 
        )" << escaped_group_col
		    << R"( AS __gid,
        )" << escaped_date_col
		    << R"( AS __did,
        )" << escaped_value_col
		    << R"( AS __vid,
        *
    FROM QUERY_TABLE()"
		    << escaped_table << R"()
),
frequency_parsed AS (
    SELECT 
        CASE 
            WHEN UPPER(TRIM()"
		    << frequency_str << R"()) IN ('1D', '1DAY') THEN INTERVAL '1 day'
            WHEN UPPER(TRIM()"
		    << frequency_str << R"()) IN ('30M', '30MIN', '30MINUTE', '30MINUTES') THEN INTERVAL '30 minutes'
            WHEN UPPER(TRIM()"
		    << frequency_str << R"()) IN ('1H', '1HOUR', '1HOURS') THEN INTERVAL '1 hour'
            WHEN UPPER(TRIM()"
		    << frequency_str << R"()) IN ('1W', '1WEEK', '1WEEKS') THEN INTERVAL '1 week'
            WHEN UPPER(TRIM()"
		    << frequency_str << R"()) IN ('1MO', '1MONTH', '1MONTHS') THEN INTERVAL '1 month'
            WHEN UPPER(TRIM()"
		    << frequency_str << R"()) IN ('1Q', '1QUARTER', '1QUARTERS') THEN INTERVAL '3 months'
            WHEN UPPER(TRIM()"
		    << frequency_str << R"()) IN ('1Y', '1YEAR', '1YEAR') THEN INTERVAL '1 year'
            ELSE INTERVAL '1 day'
        END AS __interval
    FROM (SELECT 1) t
),
series_ranges AS (
    SELECT 
        __gid,
        MIN(__did) AS __min,
        MAX(__did) AS __max
    FROM orig_aliased
    GROUP BY __gid
),
grid AS (
    SELECT 
        sr.__gid,
        UNNEST(GENERATE_SERIES(sr.__min, sr.__max, fp.__interval)) AS __did
    FROM series_ranges sr
    CROSS JOIN frequency_parsed fp
),
with_original_data AS (
    SELECT 
        g.__gid,
        g.__did,
        oa.__vid,
        oa.* EXCLUDE (__gid, __did, __vid)
    FROM grid g
    LEFT JOIN orig_aliased oa ON g.__gid = oa.__gid AND g.__did = oa.__did
)
SELECT 
    with_original_data.* EXCLUDE (__gid, __did, __vid, )"
		    << escaped_group_col << R"(, )" << escaped_date_col << R"(, )" << escaped_value_col << R"(),
    with_original_data.__gid AS )"
		    << escaped_group_col << R"(,
    with_original_data.__did AS )"
		    << escaped_date_col << R"(,
    with_original_data.__vid AS )"
		    << escaped_value_col << R"(
FROM with_original_data
ORDER BY )" << escaped_group_col
		    << R"(, )" << escaped_date_col << R"()";
	}

	// Parse the generated SQL and return as SubqueryRef
	return ParseSubquery(sql.str(), context.GetParserOptions(), "Failed to parse generated SQL for ts_fill_gaps");
}

// Helper function to generate final SELECT with column preservation
static string GenerateFinalSelect(const string &cte_name, const string &escaped_group_col,
                                  const string &escaped_date_col, const string &escaped_value_col,
                                  const string &group_expr, const string &date_expr, const string &value_expr) {
	std::ostringstream sql;
	sql << "SELECT \n"
	    << "    " << cte_name << ".* EXCLUDE (__gid, __did, __vid, " << escaped_group_col << ", " << escaped_date_col
	    << ", " << escaped_value_col << "),\n"
	    << "    " << group_expr << " AS " << escaped_group_col << ",\n"
	    << "    " << date_expr << " AS " << escaped_date_col << ",\n"
	    << "    " << value_expr << " AS " << escaped_value_col << "\n"
	    << "FROM " << cte_name << "\n"
	    << "ORDER BY " << escaped_group_col << ", " << escaped_date_col;
	return sql.str();
}

// TS_FILL_NULLS_FORWARD
unique_ptr<TableRef> TSFillNullsForwardBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	if (input.inputs.size() < 4) {
		throw InvalidInputException(
		    "anofox_fcst_ts_fill_nulls_forward requires 4 arguments: table_name, group_col, date_col, value_col");
	}

	string table_name = input.inputs[0].ToString();
	string group_col = input.inputs[1].ToString();
	string date_col = input.inputs[2].ToString();
	string value_col = input.inputs[3].ToString();

	string escaped_table = KeywordHelper::WriteQuoted(table_name);
	string escaped_group_col = KeywordHelper::WriteOptionallyQuoted(group_col);
	string escaped_date_col = KeywordHelper::WriteOptionallyQuoted(date_col);
	string escaped_value_col = KeywordHelper::WriteOptionallyQuoted(value_col);

	std::ostringstream sql;
	sql << R"(WITH orig_aliased AS (
    SELECT 
        )"
	    << escaped_group_col << R"( AS __gid,
        )"
	    << escaped_date_col << R"( AS __did,
        )"
	    << escaped_value_col << R"( AS __vid,
        *
    FROM QUERY_TABLE()"
	    << escaped_table << R"()
),
with_filled AS (
    SELECT 
        __gid,
        __did,
        __vid,
        COALESCE(__vid, 
                LAST_VALUE(__vid IGNORE NULLS) 
                    OVER (PARTITION BY __gid ORDER BY __did 
                          ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW)
        ) AS __filled_vid,
        orig_aliased.* EXCLUDE (__gid, __did, __vid)
    FROM orig_aliased
)
)"
	    << GenerateFinalSelect("with_filled", escaped_group_col, escaped_date_col, escaped_value_col,
	                           "with_filled.__gid", "with_filled.__did", "with_filled.__filled_vid");

	return ParseSubquery(sql.str(), context.GetParserOptions(),
	                     "Failed to parse generated SQL for ts_fill_nulls_forward");
}

// TS_FILL_NULLS_BACKWARD
unique_ptr<TableRef> TSFillNullsBackwardBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	if (input.inputs.size() < 4) {
		throw InvalidInputException(
		    "anofox_fcst_ts_fill_nulls_backward requires 4 arguments: table_name, group_col, date_col, value_col");
	}

	string table_name = input.inputs[0].ToString();
	string group_col = input.inputs[1].ToString();
	string date_col = input.inputs[2].ToString();
	string value_col = input.inputs[3].ToString();

	string escaped_table = KeywordHelper::WriteQuoted(table_name);
	string escaped_group_col = KeywordHelper::WriteOptionallyQuoted(group_col);
	string escaped_date_col = KeywordHelper::WriteOptionallyQuoted(date_col);
	string escaped_value_col = KeywordHelper::WriteOptionallyQuoted(value_col);

	std::ostringstream sql;
	sql << R"(WITH orig_aliased AS (
    SELECT 
        )"
	    << escaped_group_col << R"( AS __gid,
        )"
	    << escaped_date_col << R"( AS __did,
        )"
	    << escaped_value_col << R"( AS __vid,
        *
    FROM QUERY_TABLE()"
	    << escaped_table << R"()
),
with_filled AS (
    SELECT 
        __gid,
        __did,
        __vid,
        COALESCE(__vid, 
                FIRST_VALUE(__vid IGNORE NULLS) 
                    OVER (PARTITION BY __gid ORDER BY __did 
                          ROWS BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING)
        ) AS __filled_vid,
        orig_aliased.* EXCLUDE (__gid, __did, __vid)
    FROM orig_aliased
)
)"
	    << GenerateFinalSelect("with_filled", escaped_group_col, escaped_date_col, escaped_value_col,
	                           "with_filled.__gid", "with_filled.__did", "with_filled.__filled_vid");

	return ParseSubquery(sql.str(), context.GetParserOptions(),
	                     "Failed to parse generated SQL for ts_fill_nulls_backward");
}

// TS_FILL_NULLS_MEAN
unique_ptr<TableRef> TSFillNullsMeanBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	if (input.inputs.size() < 4) {
		throw InvalidInputException(
		    "anofox_fcst_ts_fill_nulls_mean requires 4 arguments: table_name, group_col, date_col, value_col");
	}

	string table_name = input.inputs[0].ToString();
	string group_col = input.inputs[1].ToString();
	string date_col = input.inputs[2].ToString();
	string value_col = input.inputs[3].ToString();

	string escaped_table = KeywordHelper::WriteQuoted(table_name);
	string escaped_group_col = KeywordHelper::WriteOptionallyQuoted(group_col);
	string escaped_date_col = KeywordHelper::WriteOptionallyQuoted(date_col);
	string escaped_value_col = KeywordHelper::WriteOptionallyQuoted(value_col);

	std::ostringstream sql;
	sql << R"(WITH orig_aliased AS (
    SELECT 
        )"
	    << escaped_group_col << R"( AS __gid,
        )"
	    << escaped_date_col << R"( AS __did,
        )"
	    << escaped_value_col << R"( AS __vid,
        *
    FROM QUERY_TABLE()"
	    << escaped_table << R"()
),
series_means AS (
    SELECT 
        __gid,
        AVG(__vid) AS __mean
    FROM orig_aliased
    WHERE __vid IS NOT NULL
    GROUP BY __gid
),
with_means AS (
    SELECT 
        oa.__gid,
        oa.__did,
        oa.__vid,
        sm.__mean,
        oa.* EXCLUDE (__gid, __did, __vid)
    FROM orig_aliased oa
    LEFT JOIN series_means sm ON oa.__gid = sm.__gid
)
)"
	    << GenerateFinalSelect("with_means", escaped_group_col, escaped_date_col, escaped_value_col, "with_means.__gid",
	                           "with_means.__did", "COALESCE(with_means.__vid, with_means.__mean)");

	return ParseSubquery(sql.str(), context.GetParserOptions(), "Failed to parse generated SQL for ts_fill_nulls_mean");
}

// TS_FILL_NULLS_CONST
unique_ptr<TableRef> TSFillNullsConstBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	if (input.inputs.size() < 5) {
		throw InvalidInputException("anofox_fcst_ts_fill_nulls_const requires 5 arguments: table_name, group_col, "
		                            "date_col, value_col, fill_value");
	}

	string table_name = input.inputs[0].ToString();
	string group_col = input.inputs[1].ToString();
	string date_col = input.inputs[2].ToString();
	string value_col = input.inputs[3].ToString();
	// fill_value is a constant value - convert to SQL literal
	string fill_value_sql = input.inputs[4].ToSQLString();

	string escaped_table = KeywordHelper::WriteQuoted(table_name);
	string escaped_group_col = KeywordHelper::WriteOptionallyQuoted(group_col);
	string escaped_date_col = KeywordHelper::WriteOptionallyQuoted(date_col);
	string escaped_value_col = KeywordHelper::WriteOptionallyQuoted(value_col);

	std::ostringstream sql;
	sql << R"(WITH orig_aliased AS (
    SELECT 
        )"
	    << escaped_group_col << R"( AS __gid,
        )"
	    << escaped_date_col << R"( AS __did,
        )"
	    << escaped_value_col << R"( AS __vid,
        *
    FROM QUERY_TABLE()"
	    << escaped_table << R"()
)
SELECT 
    orig_aliased.* EXCLUDE (__gid, __did, __vid, )"
	    << escaped_group_col << R"(, )" << escaped_date_col << R"(, )" << escaped_value_col << R"(),
    orig_aliased.__gid AS )"
	    << escaped_group_col << R"(,
    orig_aliased.__did AS )"
	    << escaped_date_col << R"(,
    COALESCE(orig_aliased.__vid, )"
	    << fill_value_sql << R"() AS )" << escaped_value_col << R"(
FROM orig_aliased
ORDER BY )"
	    << escaped_group_col << R"(, )" << escaped_date_col << R"()";

	return ParseSubquery(sql.str(), context.GetParserOptions(),
	                     "Failed to parse generated SQL for ts_fill_nulls_const");
}

// TS_FILL_GAPS (INTEGER frequency)
unique_ptr<TableRef> TSFillGapsIntegerBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	// Same as VARCHAR version - use the table-in-out operator
	// The operator handles both VARCHAR and INTEGER frequency
	return TSFillGapsBindReplace(context, input);
}

// TS_FILL_FORWARD (VARCHAR frequency)
unique_ptr<TableRef> TSFillForwardVarcharBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	if (input.inputs.size() < 6) {
		throw InvalidInputException("anofox_fcst_ts_fill_forward requires 6 arguments: table_name, group_col, "
		                            "date_col, value_col, target_date, frequency");
	}

	string table_name = input.inputs[0].ToString();
	string group_col = input.inputs[1].ToString();
	string date_col = input.inputs[2].ToString();
	string value_col = input.inputs[3].ToString();
	// target_date can be a column name (VARCHAR) or a literal value (DATE/TIMESTAMP/etc)
	string target_date_sql = input.inputs[4].ToSQLString();
	// Frequency is required - reject NULL
	if (input.inputs[5].IsNull()) {
		throw InvalidInputException("frequency parameter is required and cannot be NULL");
	}
	string frequency = input.inputs[5].ToString();
	if (frequency.empty()) {
		throw InvalidInputException("frequency parameter cannot be empty");
	}

	string escaped_table = KeywordHelper::WriteQuoted(table_name);
	string escaped_group_col = KeywordHelper::WriteOptionallyQuoted(group_col);
	string escaped_date_col = KeywordHelper::WriteOptionallyQuoted(date_col);
	string escaped_value_col = KeywordHelper::WriteOptionallyQuoted(value_col);
	string escaped_frequency = KeywordHelper::WriteQuoted(frequency);

	// Generate SQL with CTEs similar to ts_fill_gaps but using target_date as upper bound
	std::ostringstream sql;
	sql << R"(WITH orig_aliased AS (
    SELECT
        )"
	    << escaped_group_col << R"( AS __gid,
        )"
	    << escaped_date_col << R"( AS __did,
        )"
	    << escaped_value_col << R"( AS __vid,
        *
    FROM QUERY_TABLE()"
	    << escaped_table << R"()
),
frequency_parsed AS (
    SELECT
        CASE
            WHEN UPPER(TRIM()"
	    << escaped_frequency << R"()) IN ('1D', '1DAY') THEN INTERVAL '1 day'
            WHEN UPPER(TRIM()"
	    << escaped_frequency << R"()) IN ('30M', '30MIN', '30MINUTE', '30MINUTES') THEN INTERVAL '30 minutes'
            WHEN UPPER(TRIM()"
	    << escaped_frequency << R"()) IN ('1H', '1HOUR', '1HOURS') THEN INTERVAL '1 hour'
            WHEN UPPER(TRIM()"
	    << escaped_frequency << R"()) IN ('1W', '1WEEK', '1WEEKS') THEN INTERVAL '1 week'
            WHEN UPPER(TRIM()"
	    << escaped_frequency << R"()) IN ('1MO', '1MONTH', '1MONTHS') THEN INTERVAL '1 month'
            WHEN UPPER(TRIM()"
	    << escaped_frequency << R"()) IN ('1Q', '1QUARTER', '1QUARTERS') THEN INTERVAL '3 months'
            WHEN UPPER(TRIM()"
	    << escaped_frequency << R"()) IN ('1Y', '1YEAR', '1YEAR') THEN INTERVAL '1 year'
            ELSE INTERVAL '1 day'
        END AS __interval
    FROM (SELECT 1) t
),
series_ranges AS (
    SELECT
        __gid,
        MIN(__did) AS __min
    FROM orig_aliased
    GROUP BY __gid
),
grid_raw AS (
    SELECT
        sr.__gid,
        UNNEST(GENERATE_SERIES(sr.__min, )"
	    << target_date_sql << R"( + fp.__interval, fp.__interval)) AS __did
    FROM series_ranges sr
    CROSS JOIN frequency_parsed fp
),
grid AS (
    SELECT __gid, __did FROM grid_raw
    WHERE __did <= )"
	    << target_date_sql << R"( OR DATE_TRUNC('day', __did) = DATE_TRUNC('day', )" << target_date_sql << R"()
),
with_original_data AS (
    SELECT
        g.__gid,
        g.__did,
        oa.__vid,
        oa.* EXCLUDE (__gid, __did, __vid)
    FROM grid g
    LEFT JOIN orig_aliased oa ON g.__gid = oa.__gid AND g.__did = oa.__did
)
SELECT
    with_original_data.* EXCLUDE (__gid, __did, __vid, )"
	    << escaped_group_col << R"(, )" << escaped_date_col << R"(, )" << escaped_value_col << R"(),
    with_original_data.__gid AS )"
	    << escaped_group_col << R"(,
    with_original_data.__did AS )"
	    << escaped_date_col << R"(,
    with_original_data.__vid AS )"
	    << escaped_value_col << R"(
FROM with_original_data
ORDER BY )"
	    << escaped_group_col << R"(, )" << escaped_date_col << R"()";

	return ParseSubquery(sql.str(), context.GetParserOptions(),
	                     "Failed to parse generated SQL for ts_fill_forward (VARCHAR frequency)");
}

// TS_FILL_FORWARD (INTEGER frequency)
unique_ptr<TableRef> TSFillForwardIntegerBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	if (input.inputs.size() < 6) {
		throw InvalidInputException("anofox_fcst_ts_fill_forward requires 6 arguments: table_name, group_col, "
		                            "date_col, value_col, target_date, frequency");
	}

	string table_name = input.inputs[0].ToString();
	string group_col = input.inputs[1].ToString();
	string date_col = input.inputs[2].ToString();
	string value_col = input.inputs[3].ToString();
	// target_date is INTEGER - convert to SQL string for use in generated SQL
	string target_date_sql = input.inputs[4].ToSQLString();
	// Frequency is required - reject NULL
	if (input.inputs[5].IsNull()) {
		throw InvalidInputException("frequency parameter is required and cannot be NULL");
	}
	string frequency_str = input.inputs[5].ToString();

	string escaped_table = KeywordHelper::WriteQuoted(table_name);
	string escaped_group_col = KeywordHelper::WriteOptionallyQuoted(group_col);
	string escaped_date_col = KeywordHelper::WriteOptionallyQuoted(date_col);
	string escaped_value_col = KeywordHelper::WriteOptionallyQuoted(value_col);

	// Generate SQL with CTEs similar to ts_fill_gaps but using target_date as upper bound
	std::ostringstream sql;
	sql << R"(WITH orig_aliased AS (
    SELECT
        )"
	    << escaped_group_col << R"( AS __gid,
        )"
	    << escaped_date_col << R"( AS __did,
        )"
	    << escaped_value_col << R"( AS __vid,
        *
    FROM QUERY_TABLE()"
	    << escaped_table << R"()
),
frequency_parsed AS (
    SELECT
        )"
	    << frequency_str << R"( AS __int_step
    FROM (SELECT 1) t
),
series_ranges AS (
    SELECT
        __gid,
        MIN(__did) AS __min
    FROM orig_aliased
    GROUP BY __gid
),
grid AS (
    SELECT
        sr.__gid,
        UNNEST(GENERATE_SERIES(sr.__min, )"
	    << target_date_sql << R"(, fp.__int_step)) AS __did
    FROM series_ranges sr
    CROSS JOIN frequency_parsed fp
),
with_original_data AS (
    SELECT
        g.__gid,
        g.__did,
        oa.__vid,
        oa.* EXCLUDE (__gid, __did, __vid)
    FROM grid g
    LEFT JOIN orig_aliased oa ON g.__gid = oa.__gid AND g.__did = oa.__did
)
SELECT
    with_original_data.* EXCLUDE (__gid, __did, __vid, )"
	    << escaped_group_col << R"(, )" << escaped_date_col << R"(, )" << escaped_value_col << R"(),
    with_original_data.__gid AS )"
	    << escaped_group_col << R"(,
    with_original_data.__did AS )"
	    << escaped_date_col << R"(,
    with_original_data.__vid AS )"
	    << escaped_value_col << R"(
FROM with_original_data
ORDER BY )"
	    << escaped_group_col << R"(, )" << escaped_date_col << R"()";

	return ParseSubquery(sql.str(), context.GetParserOptions(),
	                     "Failed to parse generated SQL for ts_fill_forward (INTEGER frequency)");
}

// TS_DROP_CONSTANT
unique_ptr<TableRef> TSDropConstantBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	if (input.inputs.size() < 3) {
		throw InvalidInputException(
		    "anofox_fcst_ts_drop_constant requires 3 arguments: table_name, group_col, value_col");
	}

	string table_name = input.inputs[0].ToString();
	string group_col = input.inputs[1].ToString();
	string value_col = input.inputs[2].ToString();

	string escaped_table = KeywordHelper::WriteQuoted(table_name);
	string escaped_group_col = KeywordHelper::WriteOptionallyQuoted(group_col);
	string escaped_value_col = KeywordHelper::WriteOptionallyQuoted(value_col);

	std::ostringstream sql;
	sql << R"(WITH series_variance AS (
    SELECT 
        )"
	    << escaped_group_col << R"( AS __gid
    FROM QUERY_TABLE()"
	    << escaped_table << R"()
    GROUP BY )"
	    << escaped_group_col << R"(
    HAVING COUNT(DISTINCT )"
	    << escaped_value_col << R"() > 1
),
orig_aliased AS (
    SELECT 
        )"
	    << escaped_group_col << R"( AS __gid,
        *
    FROM QUERY_TABLE()"
	    << escaped_table << R"()
)
SELECT 
    oa.* EXCLUDE (__gid)
FROM orig_aliased oa
WHERE EXISTS (SELECT 1 FROM series_variance sv WHERE sv.__gid = oa.__gid))";

	return ParseSubquery(sql.str(), context.GetParserOptions(), "Failed to parse generated SQL for ts_drop_constant");
}

// TS_DROP_SHORT
unique_ptr<TableRef> TSDropShortBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	if (input.inputs.size() < 3) {
		throw InvalidInputException(
		    "anofox_fcst_ts_drop_short requires 3 arguments: table_name, group_col, min_length");
	}

	string table_name = input.inputs[0].ToString();
	string group_col = input.inputs[1].ToString();
	// min_length can be a column reference (VARCHAR) or a literal value (INTEGER)
	// Use ToSQLString() to handle both cases
	string min_length_sql = input.inputs[2].ToSQLString();

	string escaped_table = KeywordHelper::WriteQuoted(table_name);
	string escaped_group_col = KeywordHelper::WriteOptionallyQuoted(group_col);

	// min_length can be a literal value or column reference - use as-is
	string escaped_min_length = min_length_sql;

	std::ostringstream sql;
	sql << R"(WITH series_length AS (
    SELECT 
        )"
	    << escaped_group_col << R"( AS __gid
    FROM QUERY_TABLE()"
	    << escaped_table << R"()
    GROUP BY )"
	    << escaped_group_col << R"(
    HAVING COUNT(*) >= )"
	    << escaped_min_length << R"(
),
orig_aliased AS (
    SELECT 
        )"
	    << escaped_group_col << R"( AS __gid,
        *
    FROM QUERY_TABLE()"
	    << escaped_table << R"()
)
SELECT 
    oa.* EXCLUDE (__gid)
FROM orig_aliased oa
WHERE EXISTS (SELECT 1 FROM series_length sl WHERE sl.__gid = oa.__gid))";

	return ParseSubquery(sql.str(), context.GetParserOptions(), "Failed to parse generated SQL for ts_drop_short");
}

// TS_DROP_ZEROS
unique_ptr<TableRef> TSDropZerosBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	if (input.inputs.size() < 3) {
		throw InvalidInputException("anofox_fcst_ts_drop_zeros requires 3 arguments: table_name, group_col, value_col");
	}

	string table_name = input.inputs[0].ToString();
	string group_col = input.inputs[1].ToString();
	string value_col = input.inputs[2].ToString();

	string escaped_table = KeywordHelper::WriteQuoted(table_name);
	string escaped_group_col = KeywordHelper::WriteOptionallyQuoted(group_col);
	string escaped_value_col = KeywordHelper::WriteOptionallyQuoted(value_col);

	std::ostringstream sql;
	sql << R"(WITH non_zero_series AS (
    SELECT 
        )"
	    << escaped_group_col << R"( AS __gid
    FROM QUERY_TABLE()"
	    << escaped_table << R"()
    GROUP BY )"
	    << escaped_group_col << R"(
    HAVING SUM(CASE WHEN )"
	    << escaped_value_col << R"( != 0 THEN 1 ELSE 0 END) > 0
),
orig_aliased AS (
    SELECT 
        )"
	    << escaped_group_col << R"( AS __gid,
        *
    FROM QUERY_TABLE()"
	    << escaped_table << R"()
)
SELECT 
    oa.* EXCLUDE (__gid)
FROM orig_aliased oa
WHERE EXISTS (SELECT 1 FROM non_zero_series nz WHERE nz.__gid = oa.__gid))";

	return ParseSubquery(sql.str(), context.GetParserOptions(), "Failed to parse generated SQL for ts_drop_zeros");
}

// TS_DROP_LEADING_ZEROS
unique_ptr<TableRef> TSDropLeadingZerosBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	if (input.inputs.size() < 4) {
		throw InvalidInputException(
		    "anofox_fcst_ts_drop_leading_zeros requires 4 arguments: table_name, group_col, date_col, value_col");
	}

	string table_name = input.inputs[0].ToString();
	string group_col = input.inputs[1].ToString();
	string date_col = input.inputs[2].ToString();
	string value_col = input.inputs[3].ToString();

	string escaped_table = KeywordHelper::WriteQuoted(table_name);
	string escaped_group_col = KeywordHelper::WriteOptionallyQuoted(group_col);
	string escaped_date_col = KeywordHelper::WriteOptionallyQuoted(date_col);
	string escaped_value_col = KeywordHelper::WriteOptionallyQuoted(value_col);

	std::ostringstream sql;
	sql << R"(WITH orig_aliased AS (
    SELECT 
        )"
	    << escaped_group_col << R"( AS __gid,
        )"
	    << escaped_date_col << R"( AS __did,
        )"
	    << escaped_value_col << R"( AS __vid,
        *
    FROM QUERY_TABLE()"
	    << escaped_table << R"()
),
with_first_nonzero AS (
    SELECT 
        __gid,
        __did,
        __vid,
        MIN(CASE WHEN __vid != 0 THEN __did END) OVER (PARTITION BY __gid) AS __first_nz,
        orig_aliased.* EXCLUDE (__gid, __did, __vid)
    FROM orig_aliased
)
SELECT 
    with_first_nonzero.* EXCLUDE (__gid, __did, __vid, __first_nz, )"
	    << escaped_group_col << R"(, )" << escaped_date_col << R"(, )" << escaped_value_col << R"(),
    with_first_nonzero.__gid AS )"
	    << escaped_group_col << R"(,
    with_first_nonzero.__did AS )"
	    << escaped_date_col << R"(,
    with_first_nonzero.__vid AS )"
	    << escaped_value_col << R"(
FROM with_first_nonzero
WHERE )" << escaped_date_col
	    << R"( >= __first_nz OR __first_nz IS NULL
ORDER BY )"
	    << escaped_group_col << R"(, )" << escaped_date_col << R"()";

	return ParseSubquery(sql.str(), context.GetParserOptions(),
	                     "Failed to parse generated SQL for ts_drop_leading_zeros");
}

// TS_DROP_TRAILING_ZEROS
unique_ptr<TableRef> TSDropTrailingZerosBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	if (input.inputs.size() < 4) {
		throw InvalidInputException(
		    "anofox_fcst_ts_drop_trailing_zeros requires 4 arguments: table_name, group_col, date_col, value_col");
	}

	string table_name = input.inputs[0].ToString();
	string group_col = input.inputs[1].ToString();
	string date_col = input.inputs[2].ToString();
	string value_col = input.inputs[3].ToString();

	string escaped_table = KeywordHelper::WriteQuoted(table_name);
	string escaped_group_col = KeywordHelper::WriteOptionallyQuoted(group_col);
	string escaped_date_col = KeywordHelper::WriteOptionallyQuoted(date_col);
	string escaped_value_col = KeywordHelper::WriteOptionallyQuoted(value_col);

	std::ostringstream sql;
	sql << R"(WITH orig_aliased AS (
    SELECT 
        )"
	    << escaped_group_col << R"( AS __gid,
        )"
	    << escaped_date_col << R"( AS __did,
        )"
	    << escaped_value_col << R"( AS __vid,
        *
    FROM QUERY_TABLE()"
	    << escaped_table << R"()
),
with_last_nonzero AS (
    SELECT 
        __gid,
        __did,
        __vid,
        MAX(CASE WHEN __vid != 0 THEN __did END) OVER (PARTITION BY __gid) AS __last_nz,
        orig_aliased.* EXCLUDE (__gid, __did, __vid)
    FROM orig_aliased
)
SELECT 
    with_last_nonzero.* EXCLUDE (__gid, __did, __vid, __last_nz, )"
	    << escaped_group_col << R"(, )" << escaped_date_col << R"(, )" << escaped_value_col << R"(),
    with_last_nonzero.__gid AS )"
	    << escaped_group_col << R"(,
    with_last_nonzero.__did AS )"
	    << escaped_date_col << R"(,
    with_last_nonzero.__vid AS )"
	    << escaped_value_col << R"(
FROM with_last_nonzero
WHERE )" << escaped_date_col
	    << R"( <= __last_nz OR __last_nz IS NULL
ORDER BY )"
	    << escaped_group_col << R"(, )" << escaped_date_col << R"()";

	return ParseSubquery(sql.str(), context.GetParserOptions(),
	                     "Failed to parse generated SQL for ts_drop_trailing_zeros");
}

// TS_DROP_GAPPY
unique_ptr<TableRef> TSDropGappyBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	if (input.inputs.size() < 4) {
		throw InvalidInputException(
		    "anofox_fcst_ts_drop_gappy requires 4 arguments: table_name, group_col, date_col, max_gap_pct");
	}

	string table_name = input.inputs[0].ToString();
	string group_col = input.inputs[1].ToString();
	string date_col = input.inputs[2].ToString();

	// max_gap_pct is DOUBLE - validate it's between 0 and 1 (exclusive)
	if (input.inputs[3].IsNull()) {
		throw InvalidInputException("max_gap_pct parameter cannot be NULL");
	}
	double max_gap_pct_value = input.inputs[3].GetValue<double>();
	if (max_gap_pct_value <= 0.0 || max_gap_pct_value >= 1.0) {
		throw InvalidInputException("max_gap_pct must be greater than 0 and less than 1");
	}
	// Convert to SQL string for use in generated SQL
	string max_gap_pct_sql = input.inputs[3].ToSQLString();

	string escaped_table = KeywordHelper::WriteQuoted(table_name);
	string escaped_group_col = KeywordHelper::WriteOptionallyQuoted(group_col);
	string escaped_date_col = KeywordHelper::WriteOptionallyQuoted(date_col);
	string escaped_max_gap_pct = max_gap_pct_sql;

	std::ostringstream sql;
	sql << R"(WITH base_aliased AS (
    SELECT 
        )"
	    << escaped_group_col << R"( AS __gid,
        )"
	    << escaped_date_col << R"( AS __did,
        )"
	    << escaped_group_col << R"(,
        )"
	    << escaped_date_col << R"(
    FROM QUERY_TABLE()"
	    << escaped_table << R"()
),
series_ranges AS (
    SELECT 
        __gid,
        MIN(__did) AS __min,
        MAX(__did) AS __max,
        COUNT(*) AS actual_count
    FROM base_aliased
    GROUP BY __gid
),
expected_counts AS (
    SELECT 
        __gid,
        __min,
        __max,
        actual_count,
        CASE 
            WHEN __max >= __min
            THEN CAST(DATEDIFF('day', __min, __max) AS INTEGER) + 1
            ELSE 1
        END AS expected_count
    FROM series_ranges
),
gap_stats AS (
    SELECT 
        __gid,
        actual_count,
        expected_count,
        CASE 
            WHEN expected_count > 0
            THEN 100.0 * (expected_count - actual_count) / expected_count
            ELSE 0.0
        END AS gap_pct
    FROM expected_counts
),
valid_series AS (
    SELECT 
        __gid
    FROM gap_stats
    WHERE gap_pct <= (CAST()"
	    << escaped_max_gap_pct << R"( AS DOUBLE) * 100.0)
),
orig_aliased AS (
    SELECT 
        )"
	    << escaped_group_col << R"( AS __gid,
        *
    FROM QUERY_TABLE()"
	    << escaped_table << R"()
)
SELECT 
    oa.* EXCLUDE (__gid)
FROM orig_aliased oa
WHERE EXISTS (SELECT 1 FROM valid_series vs WHERE vs.__gid = oa.__gid))";

	return ParseSubquery(sql.str(), context.GetParserOptions(), "Failed to parse generated SQL for ts_drop_gappy");
}

// TS_DROP_EDGE_ZEROS
unique_ptr<TableRef> TSDropEdgeZerosBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	if (input.inputs.size() < 4) {
		throw InvalidInputException(
		    "anofox_fcst_ts_drop_edge_zeros requires 4 arguments: table_name, group_col, date_col, value_col");
	}

	string table_name = input.inputs[0].ToString();
	string group_col = input.inputs[1].ToString();
	string date_col = input.inputs[2].ToString();
	string value_col = input.inputs[3].ToString();

	string escaped_table = KeywordHelper::WriteQuoted(table_name);
	string escaped_group_col = KeywordHelper::WriteOptionallyQuoted(group_col);
	string escaped_date_col = KeywordHelper::WriteOptionallyQuoted(date_col);
	string escaped_value_col = KeywordHelper::WriteOptionallyQuoted(value_col);

	std::ostringstream sql;
	sql << R"(WITH orig_aliased AS (
    SELECT 
        )"
	    << escaped_group_col << R"( AS __gid,
        )"
	    << escaped_date_col << R"( AS __did,
        )"
	    << escaped_value_col << R"( AS __vid,
        *
    FROM QUERY_TABLE()"
	    << escaped_table << R"()
),
with_bounds AS (
    SELECT 
        __gid,
        __did,
        __vid,
        MIN(CASE WHEN __vid != 0 THEN __did END) OVER (PARTITION BY __gid) AS __first_nz,
        MAX(CASE WHEN __vid != 0 THEN __did END) OVER (PARTITION BY __gid) AS __last_nz,
        orig_aliased.* EXCLUDE (__gid, __did, __vid)
    FROM orig_aliased
)
SELECT 
    with_bounds.* EXCLUDE (__gid, __did, __vid, __first_nz, __last_nz, )"
	    << escaped_group_col << R"(, )" << escaped_date_col << R"(, )" << escaped_value_col << R"(),
    with_bounds.__gid AS )"
	    << escaped_group_col << R"(,
    with_bounds.__did AS )"
	    << escaped_date_col << R"(,
    with_bounds.__vid AS )"
	    << escaped_value_col << R"(
FROM with_bounds
WHERE (__first_nz IS NULL OR )"
	    << escaped_date_col << R"( >= __first_nz)
  AND (__last_nz IS NULL OR )"
	    << escaped_date_col << R"( <= __last_nz)
ORDER BY )"
	    << escaped_group_col << R"(, )" << escaped_date_col << R"()";

	return ParseSubquery(sql.str(), context.GetParserOptions(), "Failed to parse generated SQL for ts_drop_edge_zeros");
}

// TS_DIFF
unique_ptr<TableRef> TSDiffBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	if (input.inputs.size() < 5) {
		throw InvalidInputException(
		    "anofox_fcst_ts_diff requires 5 arguments: table_name, group_col, date_col, value_col, order");
	}

	string table_name = input.inputs[0].ToString();
	string group_col = input.inputs[1].ToString();
	string date_col = input.inputs[2].ToString();
	string value_col = input.inputs[3].ToString();
	// order is INTEGER - validate it's > 0
	if (input.inputs[4].IsNull()) {
		throw InvalidInputException("order parameter cannot be NULL");
	}
	int64_t order_value = input.inputs[4].GetValue<int64_t>();
	if (order_value <= 0) {
		throw InvalidInputException("order parameter must be greater than 0");
	}
	string order_str = std::to_string(order_value);

	string escaped_table = KeywordHelper::WriteQuoted(table_name);
	string escaped_group_col = KeywordHelper::WriteOptionallyQuoted(group_col);
	string escaped_date_col = KeywordHelper::WriteOptionallyQuoted(date_col);
	string escaped_value_col = KeywordHelper::WriteOptionallyQuoted(value_col);

	std::ostringstream sql;
	sql << R"(WITH ordered_data AS (
    SELECT 
        )"
	    << escaped_group_col << R"(,
        )"
	    << escaped_date_col << R"(,
        )"
	    << escaped_value_col << R"(,
        LAG()"
	    << escaped_value_col << R"(, )" << order_str << R"() OVER (PARTITION BY )" << escaped_group_col
	    << R"( ORDER BY )" << escaped_date_col << R"() AS lagged_value
    FROM QUERY_TABLE()"
	    << escaped_table << R"()
)
SELECT 
    ordered_data.* EXCLUDE (lagged_value, )"
	    << escaped_group_col << R"(, )" << escaped_date_col << R"(, )" << escaped_value_col << R"(),
    )" << escaped_group_col
	    << R"(,
    )" << escaped_date_col
	    << R"(,
    CASE 
        WHEN )"
	    << escaped_value_col << R"( IS NULL OR lagged_value IS NULL THEN NULL
        ELSE )"
	    << escaped_value_col << R"( - lagged_value
    END AS )"
	    << escaped_value_col << R"(
FROM ordered_data
ORDER BY )"
	    << escaped_group_col << R"(, )" << escaped_date_col << R"()";

	return ParseSubquery(sql.str(), context.GetParserOptions(), "Failed to parse generated SQL for ts_diff");
}

} // namespace duckdb
