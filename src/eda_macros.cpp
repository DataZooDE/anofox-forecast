#include "duckdb.hpp"
#include "duckdb/catalog/default/default_table_functions.hpp"

namespace duckdb {

// Array of EDA macros - all follow consistent signature:
// (table_name, group_col, date_col, value_col)
static const DefaultTableMacro eda_macros[] = {

    // TS_STATS: Per-series comprehensive statistics
    {DEFAULT_SCHEMA,
     "ts_stats",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH features_agg AS (
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
                    MAX(date_col) AS end_date,
                    CASE 
                        WHEN MAX(date_col) >= MIN(date_col)
                        THEN CAST(DATEDIFF('day', MIN(date_col), MAX(date_col)) AS INTEGER) + 1
                        ELSE 1
                    END AS expected_length
                FROM QUERY_TABLE(table_name)
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
                t.length,
                t.start_date,
                t.end_date,
                t.expected_length,
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
                CAST(f.feats.n_zeros_end AS BIGINT) AS n_zeros_end
            FROM features_agg f
            INNER JOIN temporal_metadata t ON f.series_id = t.series_id
            INNER JOIN null_counts n ON f.series_id = n.series_id
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
            series_intervals AS (
                SELECT 
                    series_id,
                    CASE 
                        WHEN expected_length > 1 AND end_date >= start_date
                        THEN CAST(DATEDIFF('day', start_date, end_date) AS DOUBLE) / GREATEST(expected_length - 1, 1)
                        ELSE NULL
                    END AS avg_interval_days
                FROM stats
            ),
            aggregated AS (
                SELECT 
                    COUNT(DISTINCT s.series_id) AS total_series,
                    SUM(s.length) AS total_observations,
                    ROUND(AVG(CAST(s.length AS DOUBLE)), 2) AS avg_series_length,
                    MIN(s.start_date) AS overall_start_date,
                    MAX(s.end_date) AS overall_end_date,
                    ROUND(AVG(si.avg_interval_days), 2) AS avg_interval_days
                FROM stats s
                LEFT JOIN series_intervals si ON s.series_id = si.series_id
            )
            SELECT 
                total_series,
                total_observations,
                avg_series_length,
                CASE 
                    WHEN overall_end_date >= overall_start_date
                    THEN CAST(DATEDIFF('day', overall_start_date, overall_end_date) AS INTEGER)
                    ELSE 0
                END AS date_span,
                CASE 
                    WHEN avg_interval_days IS NULL THEN 'Unknown'
                    WHEN avg_interval_days < 0.5 THEN 'Sub-hourly'
                    WHEN avg_interval_days < 1.0 THEN 'Hourly'
                    WHEN avg_interval_days < 7.0 THEN 'Daily'
                    WHEN avg_interval_days < 30.0 THEN 'Weekly'
                    WHEN avg_interval_days < 90.0 THEN 'Monthly'
                    ELSE 'Quarterly+'
                END AS frequency
            FROM aggregated
        )"},

    // End marker
    {nullptr, nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr}};

// Register EDA (Exploratory Data Analysis) table macros
void RegisterEDAMacros(ExtensionLoader &loader) {
	for (idx_t index = 0; eda_macros[index].name != nullptr; index++) {
		auto table_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(eda_macros[index]);
		table_info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
		loader.RegisterFunction(*table_info);
	}
}

} // namespace duckdb
