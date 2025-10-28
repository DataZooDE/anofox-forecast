#include "duckdb.hpp"
#include "duckdb/catalog/default/default_table_functions.hpp"

namespace duckdb {

// Array of EDA macros
static const DefaultTableMacro eda_macros[] = {
        
        // TS_STATS: Per-series comprehensive statistics
        {DEFAULT_SCHEMA, "ts_stats", {"table_name", "group_cols", "date_col", "value_col", nullptr}, {{nullptr, nullptr}}, R"(
            WITH series_raw AS (
                SELECT * FROM QUERY_TABLE(table_name)
            ),
            series_stats AS (
                SELECT 
                    group_cols AS series_id,
                    COUNT(*) AS length,
                    MIN(date_col) AS start_date,
                    MAX(date_col) AS end_date,
                    SUM(value_col) AS sum_value,
                    AVG(value_col) AS mean_value,
                    STDDEV(value_col) AS std_value,
                    MIN(value_col) AS min_value,
                    MAX(value_col) AS max_value,
                    MEDIAN(value_col) AS median_value,
                    COUNT(CASE WHEN value_col IS NULL THEN 1 END) AS n_null,
                    COUNT(CASE WHEN isnan(value_col) THEN 1 END) AS n_nan,
                    COUNT(CASE WHEN value_col = 0 THEN 1 END) AS n_zeros,
                    COUNT(DISTINCT value_col) AS n_unique_values,
                    COUNT(DISTINCT value_col) = 1 AS is_constant,
                    CORR(EPOCH(date_col), value_col) AS trend_correlation,
                    LIST(value_col ORDER BY date_col) AS values,
                    LIST(date_col ORDER BY date_col) AS dates
                FROM series_raw
                GROUP BY group_cols
            ),
            series_extended AS (
                SELECT 
                    *,
                    DATE_DIFF('day', start_date, end_date) + 1 AS expected_length,
                    (DATE_DIFF('day', start_date, end_date) + 1) - length AS n_gaps,
                    CASE WHEN mean_value != 0 THEN ABS(std_value / mean_value) ELSE NULL END AS cv,
                    CAST(n_zeros AS DOUBLE) / length AS intermittency,
                    1.0 - (
                        (CAST(n_null AS DOUBLE) / length) * 0.4 +
                        (CASE WHEN is_constant THEN 0.3 ELSE 0.0 END) +
                        (CAST(n_gaps AS DOUBLE) / (DATE_DIFF('day', start_date, end_date) + 1)) * 0.3
                    ) AS quality_score
                FROM series_stats
            )
            SELECT 
                series_id, length, expected_length, n_gaps, start_date, end_date,
                ROUND(mean_value, 2) AS mean, ROUND(std_value, 2) AS std,
                ROUND(min_value, 2) AS min, ROUND(max_value, 2) AS max,
                ROUND(median_value, 2) AS median,
                n_null, n_nan, n_zeros, n_unique_values, is_constant,
                ROUND(trend_correlation, 4) AS trend_corr,
                ROUND(cv, 4) AS cv, ROUND(intermittency, 4) AS intermittency,
                ROUND(quality_score, 4) AS quality_score, values, dates
            FROM series_extended
            ORDER BY series_id
        )"},
        
        // TS_QUALITY_REPORT: Comprehensive quality checks
        {DEFAULT_SCHEMA, "ts_quality_report", {"stats_table", "min_length", nullptr}, {{nullptr, nullptr}}, R"(
            WITH gap_check AS (
                SELECT 
                    'Gap Analysis' AS check_type,
                    CAST(COUNT(*) AS VARCHAR) AS total_series,
                    CAST(SUM(CASE WHEN n_gaps = 0 THEN 1 ELSE 0 END) AS VARCHAR) AS series_no_gaps,
                    CAST(SUM(CASE WHEN n_gaps > 0 THEN 1 ELSE 0 END) AS VARCHAR) AS series_with_gaps,
                    ROUND(100.0 * SUM(CASE WHEN n_gaps > 0 THEN 1 ELSE 0 END) / COUNT(*), 2) || '%' AS pct_with_gaps,
                    CAST(SUM(n_gaps) AS VARCHAR) AS total_gaps
                FROM QUERY_TABLE(stats_table)
            ),
            missing_check AS (
                SELECT 
                    'Missing Values' AS check_type,
                    CAST(COUNT(*) AS VARCHAR) AS total_series,
                    CAST(SUM(CASE WHEN n_null > 0 OR n_nan > 0 THEN 1 ELSE 0 END) AS VARCHAR) AS series_with_missing,
                    CAST(SUM(n_null) AS VARCHAR) AS total_nulls,
                    CAST(SUM(n_nan) AS VARCHAR) AS total_nans,
                    ROUND(100.0 * SUM(n_null + n_nan) / SUM(length), 2) || '%' AS pct_missing
                FROM QUERY_TABLE(stats_table)
            ),
            constant_check AS (
                SELECT 
                    'Constant Series' AS check_type,
                    CAST(COUNT(*) AS VARCHAR) AS total_series,
                    CAST(SUM(CASE WHEN is_constant THEN 1 ELSE 0 END) AS VARCHAR) AS constant_series,
                    ROUND(100.0 * SUM(CASE WHEN is_constant THEN 1 ELSE 0 END) / COUNT(*), 2) || '%' AS pct_constant,
                    '' AS extra1, '' AS extra2
                FROM QUERY_TABLE(stats_table)
            ),
            short_check AS (
                SELECT 
                    'Short Series (< ' || min_length || ')' AS check_type,
                    CAST(COUNT(*) AS VARCHAR) AS total_series,
                    CAST(SUM(CASE WHEN length < min_length THEN 1 ELSE 0 END) AS VARCHAR) AS short_series,
                    ROUND(100.0 * SUM(CASE WHEN length < min_length THEN 1 ELSE 0 END) / COUNT(*), 2) || '%' AS pct_short,
                    CAST(MIN(length) AS VARCHAR) AS min_length_val,
                    CAST(MAX(length) AS VARCHAR) AS max_length_val
                FROM QUERY_TABLE(stats_table)
            )
            SELECT check_type, total_series, series_no_gaps AS metric1, series_with_gaps AS metric2, pct_with_gaps AS metric3, total_gaps AS metric4
            FROM gap_check
            UNION ALL
            SELECT check_type, total_series, series_with_missing, total_nulls, total_nans, pct_missing
            FROM missing_check
            UNION ALL
            SELECT check_type, total_series, constant_series, pct_constant, extra1, extra2
            FROM constant_check
            UNION ALL
            SELECT check_type, total_series, short_series, pct_short, min_length_val, max_length_val
            FROM short_check
        )"},
        
        // TS_DATASET_SUMMARY: Overall dataset statistics
        {DEFAULT_SCHEMA, "ts_dataset_summary", {"stats_table", nullptr}, {{nullptr, nullptr}}, R"(
            SELECT 
                COUNT(*) AS total_series,
                SUM(length) AS total_observations,
                ROUND(AVG(length), 2) AS avg_series_length,
                MIN(length) AS min_series_length,
                MAX(length) AS max_series_length,
                MIN(start_date) AS earliest_date,
                MAX(end_date) AS latest_date,
                DATE_DIFF('day', MIN(start_date), MAX(end_date)) AS date_span_days,
                ROUND(AVG(mean), 2) AS avg_mean_value,
                ROUND(AVG(std), 2) AS avg_std_value,
                ROUND(AVG(quality_score), 4) AS avg_quality_score,
                SUM(CASE WHEN quality_score >= 0.8 THEN 1 ELSE 0 END) AS high_quality_series,
                SUM(CASE WHEN quality_score < 0.5 THEN 1 ELSE 0 END) AS low_quality_series
            FROM QUERY_TABLE(stats_table)
        )"},
        
        // TS_GET_PROBLEMATIC: Low quality series
        {DEFAULT_SCHEMA, "ts_get_problematic", {"stats_table", "quality_threshold", nullptr}, {{nullptr, nullptr}}, R"(
            SELECT 
                series_id, length, n_gaps, n_null, n_nan, is_constant, quality_score,
                CASE 
                    WHEN n_null > 0 OR n_nan > 0 THEN '⚠️ Missing values'
                    WHEN is_constant THEN '⚠️ Constant'
                    WHEN n_gaps > length * 0.1 THEN '⚠️ Many gaps'
                    ELSE '⚠️ Other issues'
                END AS primary_issue
            FROM QUERY_TABLE(stats_table)
            WHERE quality_score < quality_threshold
            ORDER BY quality_score
        )"},
        
        // TS_DETECT_SEASONALITY_ALL: Detect seasonality for all series
        {DEFAULT_SCHEMA, "ts_detect_seasonality_all", {"table_name", "group_cols", "date_col", "value_col", nullptr}, {{nullptr, nullptr}}, R"(
            WITH series_data AS (
                SELECT * FROM QUERY_TABLE(table_name)
            ),
            series_agg AS (
                SELECT 
                    group_cols AS series_id,
                    LIST(value_col ORDER BY date_col) AS values
                FROM series_data
                GROUP BY group_cols
            )
            SELECT 
                series_id,
                TS_DETECT_SEASONALITY(values) AS detected_periods,
                CASE 
                    WHEN LEN(TS_DETECT_SEASONALITY(values)) > 0 
                    THEN TS_DETECT_SEASONALITY(values)[1]
                    ELSE NULL 
                END AS primary_period,
                LEN(TS_DETECT_SEASONALITY(values)) > 0 AS is_seasonal
            FROM series_agg
        )"},
        
        // End marker
        {nullptr, nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr}
    };

// Register EDA (Exploratory Data Analysis) table macros
void RegisterEDAMacros(ExtensionLoader &loader) {
    for (idx_t index = 0; eda_macros[index].name != nullptr; index++) {
        auto table_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(eda_macros[index]);
        loader.RegisterFunction(*table_info);
    }
}

} // namespace duckdb

