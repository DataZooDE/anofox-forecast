#include "duckdb.hpp"
#include "duckdb/catalog/default/default_table_functions.hpp"
#include <map>
#include <vector>

namespace duckdb {

// Array of data preparation macros - all follow consistent signature:
// (table_name, group_col, date_col, value_col)
static const DefaultTableMacro data_prep_macros[] = {

    // TS_FILL_NULLS_FORWARD: Forward fill (LOCF)
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_fill_nulls_forward",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            SELECT 
                group_col,
                date_col,
                COALESCE(value_col, 
                        LAST_VALUE(value_col IGNORE NULLS) 
                            OVER (PARTITION BY group_col ORDER BY date_col 
                                  ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW)
                ) AS value_col
            FROM QUERY_TABLE(table_name)
            ORDER BY group_col, date_col
        )"},

    // TS_FILL_NULLS_BACKWARD: Backward fill
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_fill_nulls_backward",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            SELECT 
                group_col,
                date_col,
                COALESCE(value_col, 
                        FIRST_VALUE(value_col IGNORE NULLS) 
                            OVER (PARTITION BY group_col ORDER BY date_col 
                                  ROWS BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING)
                ) AS value_col
            FROM QUERY_TABLE(table_name)
            ORDER BY group_col, date_col
        )"},

    // TS_FILL_NULLS_MEAN: Fill with series mean
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_fill_nulls_mean",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH base_with_alias AS (
                SELECT 
                    group_col AS __gid,
                    date_col AS __did,
                    value_col AS __vid
                FROM QUERY_TABLE(table_name)
            ),
            series_means AS (
                SELECT 
                    __gid,
                    AVG(__vid) AS __mean
                FROM base_with_alias
                WHERE __vid IS NOT NULL
                GROUP BY __gid
            ),
            with_means AS (
                SELECT 
                    b.__gid,
                    b.__did,
                    b.__vid,
                    sm.__mean
                FROM base_with_alias b
                LEFT JOIN series_means sm ON b.__gid = sm.__gid
            )
            SELECT 
                __gid AS group_col,
                __did AS date_col,
                COALESCE(__vid, __mean) AS value_col
            FROM with_means
            ORDER BY __gid, __did
        )"},

    // TS_FILL_GAPS: Fill missing time gaps with NULL (VARCHAR frequency - date-based)
    // Note: This macro generates a full date range for each group
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_fill_gaps",
     {"table_name", "group_col", "date_col", "value_col", "frequency", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH base_aliased AS (
                SELECT 
                    group_col AS __gid,
                    date_col AS __did,
                    value_col AS __vid,
                    group_col,
                    date_col,
                    value_col
                FROM QUERY_TABLE(table_name)
            ),
            frequency_parsed AS (
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
            series_ranges AS (
                SELECT DISTINCT
                    __gid,
                    MIN(__did) OVER (PARTITION BY __gid) AS __min,
                    MAX(__did) OVER (PARTITION BY __gid) AS __max
                FROM base_aliased
            ),
            expanded AS (
                SELECT 
                    sr.__gid,
                    UNNEST(GENERATE_SERIES(sr.__min, sr.__max, fp.__interval)) AS __did
                FROM series_ranges sr
                CROSS JOIN frequency_parsed fp
            )
            SELECT 
                e.__gid AS group_col,
                e.__did AS date_col,
                b.__vid AS value_col
            FROM expanded e
            LEFT JOIN base_aliased b ON e.__gid = b.__gid AND e.__did = b.__did
            ORDER BY e.__gid, e.__did
        )"},

    // TS_FILL_GAPS: Fill missing time gaps with NULL (INTEGER frequency - integer-based)
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_fill_gaps",
     {"table_name", "group_col", "date_col", "value_col", "frequency", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH base_aliased AS (
                SELECT 
                    group_col AS __gid,
                    date_col AS __did,
                    value_col AS __vid,
                    group_col,
                    date_col,
                    value_col
                FROM QUERY_TABLE(table_name)
            ),
            frequency_parsed AS (
                SELECT 
                    COALESCE(frequency, 1) AS __int_step
                FROM (SELECT 1) t
            ),
            series_ranges AS (
                SELECT DISTINCT
                    __gid,
                    MIN(__did) OVER (PARTITION BY __gid) AS __min,
                    MAX(__did) OVER (PARTITION BY __gid) AS __max
                FROM base_aliased
            ),
            expanded AS (
                SELECT 
                    sr.__gid,
                    UNNEST(GENERATE_SERIES(sr.__min, sr.__max, fp.__int_step)) AS __did
                FROM series_ranges sr
                CROSS JOIN frequency_parsed fp
            )
            SELECT 
                e.__gid AS group_col,
                e.__did AS date_col,
                b.__vid AS value_col
            FROM expanded e
            LEFT JOIN base_aliased b ON e.__gid = b.__gid AND e.__did = b.__did
            ORDER BY e.__gid, e.__did
        )"},

    // TS_DROP_CONSTANT: Drop constant series
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_drop_constant",
     {"table_name", "group_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH series_variance AS (
                SELECT 
                    group_col AS __gid
                FROM QUERY_TABLE(table_name)
                GROUP BY group_col
                HAVING COUNT(DISTINCT value_col) > 1
            ),
            orig_aliased AS (
                SELECT 
                    group_col AS __gid,
                    *
                FROM QUERY_TABLE(table_name)
            )
            SELECT 
                oa.* EXCLUDE (__gid)
            FROM orig_aliased oa
            WHERE EXISTS (SELECT 1 FROM series_variance sv WHERE sv.__gid = oa.__gid)
        )"},

    // TS_DROP_SHORT: Drop short series
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_drop_short",
     {"table_name", "group_col", "min_length", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH series_length AS (
                SELECT 
                    group_col AS __gid
                FROM QUERY_TABLE(table_name)
                GROUP BY group_col
                HAVING COUNT(*) >= min_length
            ),
            orig_aliased AS (
                SELECT 
                    group_col AS __gid,
                    *
                FROM QUERY_TABLE(table_name)
            )
            SELECT 
                oa.* EXCLUDE (__gid)
            FROM orig_aliased oa
            WHERE EXISTS (SELECT 1 FROM series_length sl WHERE sl.__gid = oa.__gid)
        )"},

    // TS_DROP_ZEROS: Drop series with all zeros
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_drop_zeros",
     {"table_name", "group_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH non_zero_series AS (
                SELECT 
                    group_col AS __gid
                FROM QUERY_TABLE(table_name)
                GROUP BY group_col
                HAVING SUM(CASE WHEN value_col != 0 THEN 1 ELSE 0 END) > 0
            ),
            orig_aliased AS (
                SELECT 
                    group_col AS __gid,
                    *
                FROM QUERY_TABLE(table_name)
            )
            SELECT 
                oa.* EXCLUDE (__gid)
            FROM orig_aliased oa
            WHERE EXISTS (SELECT 1 FROM non_zero_series nz WHERE nz.__gid = oa.__gid)
        )"},

    // TS_DROP_LEADING_ZEROS: Remove leading zeros
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_drop_leading_zeros",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH with_first_nonzero AS (
                SELECT 
                    group_col,
                    date_col,
                    value_col,
                    MIN(CASE WHEN value_col != 0 THEN date_col END) OVER (PARTITION BY group_col) AS __first_nz
                FROM QUERY_TABLE(table_name)
            )
            SELECT 
                group_col,
                date_col,
                value_col
            FROM with_first_nonzero
            WHERE date_col >= __first_nz OR __first_nz IS NULL
            ORDER BY group_col, date_col
        )"},

    // TS_DROP_TRAILING_ZEROS: Remove trailing zeros
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_drop_trailing_zeros",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH with_last_nonzero AS (
                SELECT 
                    group_col,
                    date_col,
                    value_col,
                    MAX(CASE WHEN value_col != 0 THEN date_col END) OVER (PARTITION BY group_col) AS __last_nz
                FROM QUERY_TABLE(table_name)
            )
            SELECT 
                group_col,
                date_col,
                value_col
            FROM with_last_nonzero
            WHERE date_col <= __last_nz OR __last_nz IS NULL
            ORDER BY group_col, date_col
        )"},

    // TS_FILL_FORWARD: Extend all series to a target date (VARCHAR frequency - date-based)
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_fill_forward",
     {"table_name", "group_col", "date_col", "value_col", "target_date", "frequency", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH base_aliased AS (
                SELECT 
                    group_col AS __gid,
                    date_col AS __did,
                    value_col AS __vid
                FROM QUERY_TABLE(table_name)
            ),
            frequency_parsed AS (
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
            series_ranges AS (
                SELECT DISTINCT
                    __gid,
                    MIN(__did) OVER (PARTITION BY __gid) AS __min,
                    MAX(__did) OVER (PARTITION BY __gid) AS __max
                FROM base_aliased
            ),
            target_dates AS (
                SELECT 
                    sr.__gid,
                    sr.__min,
                    target_date AS __target
                FROM series_ranges sr
            ),
            expanded AS (
                SELECT 
                    td.__gid,
                    UNNEST(GENERATE_SERIES(td.__min, td.__target, fp.__interval)) AS __did
                FROM target_dates td
                CROSS JOIN frequency_parsed fp
            )
            SELECT 
                e.__gid AS group_col,
                e.__did AS date_col,
                b.__vid AS value_col
            FROM expanded e
            LEFT JOIN base_aliased b ON e.__gid = b.__gid AND e.__did = b.__did
            ORDER BY e.__gid, e.__did
        )"},

    // TS_FILL_FORWARD: Extend all series to a target date (INTEGER frequency - integer-based)
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_fill_forward",
     {"table_name", "group_col", "date_col", "value_col", "target_date", "frequency", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH base_aliased AS (
                SELECT 
                    group_col AS __gid,
                    date_col AS __did,
                    value_col AS __vid
                FROM QUERY_TABLE(table_name)
            ),
            frequency_parsed AS (
                SELECT 
                    COALESCE(frequency, 1) AS __int_step
                FROM (SELECT 1) t
            ),
            series_ranges AS (
                SELECT DISTINCT
                    __gid,
                    MIN(__did) OVER (PARTITION BY __gid) AS __min,
                    MAX(__did) OVER (PARTITION BY __gid) AS __max
                FROM base_aliased
            ),
            target_dates AS (
                SELECT 
                    sr.__gid,
                    sr.__min,
                    target_date AS __target
                FROM series_ranges sr
            ),
            expanded AS (
                SELECT 
                    td.__gid,
                    UNNEST(GENERATE_SERIES(td.__min, td.__target, fp.__int_step)) AS __did
                FROM target_dates td
                CROSS JOIN frequency_parsed fp
            )
            SELECT 
                e.__gid AS group_col,
                e.__did AS date_col,
                b.__vid AS value_col
            FROM expanded e
            LEFT JOIN base_aliased b ON e.__gid = b.__gid AND e.__did = b.__did
            ORDER BY e.__gid, e.__did
        )"},

    // TS_DROP_GAPPY: Drop series with excessive gaps
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_drop_gappy",
     {"table_name", "group_col", "date_col", "max_gap_pct", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH base_aliased AS (
                SELECT 
                    group_col AS __gid,
                    date_col AS __did,
                    group_col,
                    date_col
                FROM QUERY_TABLE(table_name)
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
                WHERE gap_pct <= CAST(max_gap_pct AS DOUBLE)
            ),
            orig_aliased AS (
                SELECT 
                    group_col AS __gid,
                    *
                FROM QUERY_TABLE(table_name)
            )
            SELECT 
                orig_aliased.__gid AS group_col,
                orig_aliased.* EXCLUDE (__gid)
            FROM orig_aliased
            WHERE EXISTS (SELECT 1 FROM valid_series vs WHERE vs.__gid = orig_aliased.__gid)
        )"},

    // TS_DROP_EDGE_ZEROS: Remove both leading and trailing zeros
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_drop_edge_zeros",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH with_bounds AS (
                SELECT 
                    group_col,
                    date_col,
                    value_col,
                    MIN(CASE WHEN value_col != 0 THEN date_col END) OVER (PARTITION BY group_col) AS __first_nz,
                    MAX(CASE WHEN value_col != 0 THEN date_col END) OVER (PARTITION BY group_col) AS __last_nz
                FROM QUERY_TABLE(table_name)
            )
            SELECT 
                group_col,
                date_col,
                value_col
            FROM with_bounds
            WHERE (__first_nz IS NULL OR date_col >= __first_nz)
              AND (__last_nz IS NULL OR date_col <= __last_nz)
            ORDER BY group_col, date_col
        )"},

    // TS_FILL_NULLS_CONST: Fill with constant value
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_fill_nulls_const",
     {"table_name", "group_col", "date_col", "value_col", "fill_value", nullptr},
     {{nullptr, nullptr}},
     R"(
            SELECT 
                group_col,
                date_col,
                COALESCE(value_col, fill_value) AS value_col
            FROM QUERY_TABLE(table_name)
            ORDER BY group_col, date_col
        )"},

    // TS_DIFF: Differencing - Using 1st order difference only (order parameter causes parser issues)
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_diff",
     {"table_name", "group_col", "date_col", "value_col", "order", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH ordered_data AS (
                SELECT 
                    group_col,
                    date_col,
                    value_col,
                    LAG(value_col, 1) OVER (PARTITION BY group_col ORDER BY date_col) AS lagged_value
                FROM QUERY_TABLE(table_name)
            )
            SELECT 
                group_col,
                date_col,
                CASE 
                    WHEN value_col IS NULL OR lagged_value IS NULL THEN NULL
                    ELSE value_col - lagged_value
                END AS value_col
            FROM ordered_data
            ORDER BY group_col, date_col
        )"},

    // End marker
    {nullptr, nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr}};

// Register Data Preparation table macros
void RegisterDataPrepMacros(ExtensionLoader &loader) {
	// Group macros by name to handle overloads
	std::map<string, vector<idx_t>> macro_groups;
	for (idx_t index = 0; data_prep_macros[index].name != nullptr; index++) {
		string name = string(data_prep_macros[index].name);
		macro_groups[name].push_back(index);
	}

	// Register each group (handles overloads)
	for (const auto &group : macro_groups) {
		if (group.second.size() == 1) {
			// Single macro, register normally
			idx_t index = group.second[0];
			auto table_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(data_prep_macros[index]);
			table_info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
			loader.RegisterFunction(*table_info);

			// Register alias
			if (table_info->name.find("anofox_fcst_") == 0) {
				string alias_name = table_info->name.substr(12); // Remove "anofox_fcst_" prefix
				DefaultTableMacro alias_macro = data_prep_macros[index];
				alias_macro.name = alias_name.c_str();
				auto alias_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(alias_macro);
				alias_info->alias_of = table_info->name;
				alias_info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
				loader.RegisterFunction(*alias_info);
			}
		} else {
			// Multiple macros with same name - create overloaded macro with typed parameters
			// For ts_fill_gaps and ts_fill_forward, we have VARCHAR and INTEGER overloads
			if (group.second.size() == 2 &&
			    (group.first == "anofox_fcst_ts_fill_gaps" || group.first == "anofox_fcst_ts_fill_forward")) {
				// Create a single CreateMacroInfo with both overloads
				auto first_info =
				    DefaultTableFunctionGenerator::CreateTableMacroInfo(data_prep_macros[group.second[0]]);
				auto second_info =
				    DefaultTableFunctionGenerator::CreateTableMacroInfo(data_prep_macros[group.second[1]]);

				// Set parameter types: VARCHAR for first (date-based), INTEGER for second (integer-based)
				// The frequency parameter is the 5th parameter (index 4) for ts_fill_gaps
				// The frequency parameter is the 6th parameter (index 5) for ts_fill_forward
				idx_t freq_param_idx = (group.first == "anofox_fcst_ts_fill_gaps") ? 4 : 5;

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
					    DefaultTableFunctionGenerator::CreateTableMacroInfo(data_prep_macros[group.second[0]]);
					alias_info->name = alias_name;
					// Copy the overloads
					for (size_t i = 1; i < first_info->macros.size(); i++) {
						auto alias_macro =
						    DefaultTableFunctionGenerator::CreateTableMacroInfo(data_prep_macros[group.second[i]]);
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
					auto table_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(data_prep_macros[index]);
					table_info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
					loader.RegisterFunction(*table_info);

					// Register alias
					if (table_info->name.find("anofox_fcst_") == 0) {
						string alias_name = table_info->name.substr(12); // Remove "anofox_fcst_" prefix
						DefaultTableMacro alias_macro = data_prep_macros[index];
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
