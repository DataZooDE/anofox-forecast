#include "duckdb.hpp"
#include "duckdb/catalog/default/default_table_functions.hpp"
#include <map>
#include <vector>

namespace duckdb {

// Array of EDA macros - all follow consistent signature:
// (table_name, group_col, date_col, value_col)
static const DefaultTableMacro eda_macros[] = {

    // TS_STATS: Per-series comprehensive statistics (VARCHAR frequency - date-based)
    {DEFAULT_SCHEMA,
     "ts_stats",
     {"table_name", "group_col", "date_col", "value_col", "frequency", nullptr},
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
            features_agg AS (
                SELECT 
                    group_col AS series_id,
                    ts_features(date_col, value_col, [
                        'mean', 'standard_deviation', 'minimum', 'maximum', 'median',
                        'n_zeros', 'n_unique_values', 'is_constant',
                        'plateau_size', 'plateau_size_non_zero', 'n_zeros_start', 'n_zeros_end'
                    ]) AS feats
                FROM QUERY_TABLE(table_name)
                GROUP BY group_col
            ),
            temporal_metadata AS (
                SELECT 
                    group_col AS series_id,
                    COUNT(*) AS length,
                    MIN(date_col) AS start_date,
                    MAX(date_col) AS end_date
                FROM QUERY_TABLE(table_name)
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
                        WHEN tm.end_date >= tm.start_date
                        THEN CAST(EXTRACT(EPOCH FROM (tm.end_date - tm.start_date)) / EXTRACT(EPOCH FROM fp.__interval) AS INTEGER) + 1
                        ELSE 1
                    END AS expected_length
                FROM temporal_metadata tm
                CROSS JOIN frequency_parsed fp
            ),
            duplicate_timestamps AS (
                SELECT 
                    group_col AS series_id,
                    SUM(CASE WHEN key_count > 1 THEN key_count - 1 ELSE 0 END) AS n_duplicate_timestamps
                FROM (
                    SELECT 
                        group_col,
                        date_col,
                        COUNT(*) AS key_count
                    FROM QUERY_TABLE(table_name)
                    GROUP BY group_col, date_col
                ) key_counts
                GROUP BY group_col
            ),
            null_counts AS (
                SELECT 
                    group_col AS series_id,
                    COUNT(CASE WHEN value_col IS NULL THEN 1 END) AS n_null
                FROM QUERY_TABLE(table_name)
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
                COALESCE(dt.n_duplicate_timestamps, 0) AS n_duplicate_timestamps
            FROM features_agg f
            INNER JOIN expected_length_calc elc ON f.series_id = elc.series_id
            INNER JOIN null_counts n ON f.series_id = n.series_id
            LEFT JOIN duplicate_timestamps dt ON f.series_id = dt.series_id
            ORDER BY f.series_id
        )"},

    // TS_STATS: Per-series comprehensive statistics (INTEGER frequency - integer-based)
    {DEFAULT_SCHEMA,
     "ts_stats",
     {"table_name", "group_col", "date_col", "value_col", "frequency", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH frequency_parsed AS (
                SELECT 
                    COALESCE(frequency, 1) AS __int_step
                FROM (SELECT 1) t
            ),
            features_agg AS (
                SELECT 
                    group_col AS series_id,
                    ts_features(date_col, value_col, [
                        'mean', 'standard_deviation', 'minimum', 'maximum', 'median',
                        'n_zeros', 'n_unique_values', 'is_constant',
                        'plateau_size', 'plateau_size_non_zero', 'n_zeros_start', 'n_zeros_end'
                    ]) AS feats
                FROM QUERY_TABLE(table_name)
                GROUP BY group_col
            ),
            temporal_metadata AS (
                SELECT 
                    group_col AS series_id,
                    COUNT(*) AS length,
                    MIN(date_col) AS start_date,
                    MAX(date_col) AS end_date
                FROM QUERY_TABLE(table_name)
                GROUP BY group_col
            ),
            expected_length_calc AS (
                SELECT 
                    tm.series_id,
                    tm.length,
                    tm.start_date,
                    tm.end_date,
                    fp.__int_step,
                    CASE 
                        WHEN tm.end_date >= tm.start_date
                        THEN CAST((tm.end_date - tm.start_date) / fp.__int_step AS INTEGER) + 1
                        ELSE 1
                    END AS expected_length
                FROM temporal_metadata tm
                CROSS JOIN frequency_parsed fp
            ),
            duplicate_timestamps AS (
                SELECT 
                    group_col AS series_id,
                    SUM(CASE WHEN key_count > 1 THEN key_count - 1 ELSE 0 END) AS n_duplicate_timestamps
                FROM (
                    SELECT 
                        group_col,
                        date_col,
                        COUNT(*) AS key_count
                    FROM QUERY_TABLE(table_name)
                    GROUP BY group_col, date_col
                ) key_counts
                GROUP BY group_col
            ),
            null_counts AS (
                SELECT 
                    group_col AS series_id,
                    COUNT(CASE WHEN value_col IS NULL THEN 1 END) AS n_null
                FROM QUERY_TABLE(table_name)
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
                COALESCE(dt.n_duplicate_timestamps, 0) AS n_duplicate_timestamps
            FROM features_agg f
            INNER JOIN expected_length_calc elc ON f.series_id = elc.series_id
            INNER JOIN null_counts n ON f.series_id = n.series_id
            LEFT JOIN duplicate_timestamps dt ON f.series_id = dt.series_id
            ORDER BY f.series_id
        )"},

    // TS_STATS_SUMMARY: Aggregate statistics from TS_STATS output
    {DEFAULT_SCHEMA,
     "ts_stats_summary",
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

    // TS_QUALITY_REPORT: Quality assessment report from TS_STATS output
    {DEFAULT_SCHEMA,
     "ts_quality_report",
     {"stats_table", "min_length", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH stats AS (
                SELECT * FROM QUERY_TABLE(stats_table)
            ),
            params AS (
                SELECT COALESCE(CAST(min_length AS INTEGER), 30) AS min_length_threshold
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
            ORDER BY check_type
        )"},

    // End marker
    {nullptr, nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr}};

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
		} else {
			// Multiple macros with same name - create overloaded macro with typed parameters
			// For ts_stats, we have VARCHAR and INTEGER overloads
			if (group.second.size() == 2 && group.first == "ts_stats") {
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
			} else {
				// For other cases, register normally
				for (idx_t i = 0; i < group.second.size(); i++) {
					idx_t index = group.second[i];
					auto table_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(eda_macros[index]);
					table_info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
					loader.RegisterFunction(*table_info);
				}
			}
		}
	}
}

} // namespace duckdb
