-- ============================================================================
-- EDA (Exploratory Data Analysis) for Time Series
-- ============================================================================
-- Comprehensive data quality checks and statistical analysis for time series
-- Uses DuckDB's analytical capabilities

-- Load extension
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- ============================================================================
-- SECTION 1: Time Series Statistics Per Series
-- ============================================================================

-- Create a macro for per-series statistics
CREATE OR REPLACE MACRO TS_STATS(table_name, group_cols, date_col, value_col) AS TABLE (
    WITH series_raw AS (
        SELECT * FROM QUERY_TABLE(table_name)
    ),
    series_stats AS (
        SELECT 
            group_cols AS series_id,
            -- Basic statistics
            COUNT(*) AS length,
            MIN(date_col) AS start_date,
            MAX(date_col) AS end_date,
            
            -- Target statistics
            SUM(value_col) AS sum_value,
            AVG(value_col) AS mean_value,
            STDDEV(value_col) AS std_value,
            MIN(value_col) AS min_value,
            MAX(value_col) AS max_value,
            MEDIAN(value_col) AS median_value,
            
            -- Missing and special values
            COUNT(CASE WHEN value_col IS NULL THEN 1 END) AS n_null,
            COUNT(CASE WHEN isnan(value_col) THEN 1 END) AS n_nan,
            COUNT(CASE WHEN value_col = 0 THEN 1 END) AS n_zeros,
            
            -- Unique values
            COUNT(DISTINCT value_col) AS n_unique_values,
            COUNT(DISTINCT value_col) = 1 AS is_constant,
            
            -- Trend indicators
            CORR(EPOCH(date_col), value_col) AS trend_correlation,
            
            -- Collect for advanced analysis
            LIST(value_col ORDER BY date_col) AS values,
            LIST(date_col ORDER BY date_col) AS dates
            
        FROM series_raw
        GROUP BY group_cols
    ),
    series_extended AS (
        SELECT 
            *,
            -- Expected length (based on date range and daily frequency)
            DATE_DIFF('day', start_date, end_date) + 1 AS expected_length,
            
            -- Calculate gaps
            (DATE_DIFF('day', start_date, end_date) + 1) - length AS n_gaps,
            
            -- Calculate CV (Coefficient of Variation)
            CASE 
                WHEN mean_value != 0 THEN ABS(std_value / mean_value)
                ELSE NULL 
            END AS cv,
            
            -- Intermittency (% zeros)
            CAST(n_zeros AS DOUBLE) / length AS intermittency,
            
            -- Data quality score (0-1, higher is better)
            1.0 - (
                (CAST(n_null AS DOUBLE) / length) * 0.4 +
                (CASE WHEN is_constant THEN 0.3 ELSE 0.0 END) +
                (CAST(n_gaps AS DOUBLE) / expected_length) * 0.3
            ) AS quality_score
            
        FROM series_stats
    )
    SELECT 
        series_id,
        length,
        expected_length,
        n_gaps,
        start_date,
        end_date,
        ROUND(mean_value, 2) AS mean,
        ROUND(std_value, 2) AS std,
        ROUND(min_value, 2) AS min,
        ROUND(max_value, 2) AS max,
        ROUND(median_value, 2) AS median,
        n_null,
        n_nan,
        n_zeros,
        n_unique_values,
        is_constant,
        ROUND(trend_correlation, 4) AS trend_corr,
        ROUND(cv, 4) AS cv,
        ROUND(intermittency, 4) AS intermittency,
        ROUND(quality_score, 4) AS quality_score,
        values,
        dates
    FROM series_extended
    ORDER BY series_id
);

-- ============================================================================
-- SECTION 2: Data Quality Checks
-- ============================================================================

-- Macro: Check for series with gaps
CREATE OR REPLACE MACRO TS_CHECK_GAPS(stats_table) AS TABLE (
    SELECT 
        'Gap Analysis' AS check_type,
        COUNT(*) AS total_series,
        SUM(CASE WHEN n_gaps = 0 THEN 1 ELSE 0 END) AS series_no_gaps,
        SUM(CASE WHEN n_gaps > 0 THEN 1 ELSE 0 END) AS series_with_gaps,
        ROUND(100.0 * SUM(CASE WHEN n_gaps > 0 THEN 1 ELSE 0 END) / COUNT(*), 2) || '%' AS pct_with_gaps,
        SUM(n_gaps) AS total_gaps
    FROM QUERY_TABLE(stats_table)
);

-- Macro: Check for null/NaN values
CREATE OR REPLACE MACRO TS_CHECK_MISSING(stats_table) AS TABLE (
    SELECT 
        'Missing Values' AS check_type,
        COUNT(*) AS total_series,
        SUM(CASE WHEN n_null > 0 OR n_nan > 0 THEN 1 ELSE 0 END) AS series_with_missing,
        SUM(n_null) AS total_nulls,
        SUM(n_nan) AS total_nans,
        ROUND(100.0 * SUM(n_null + n_nan) / SUM(length), 2) || '%' AS pct_missing
    FROM QUERY_TABLE(stats_table)
);

-- Macro: Check for constant series
CREATE OR REPLACE MACRO TS_CHECK_CONSTANT(stats_table) AS TABLE (
    SELECT 
        'Constant Series' AS check_type,
        COUNT(*) AS total_series,
        SUM(CASE WHEN is_constant THEN 1 ELSE 0 END) AS constant_series,
        ROUND(100.0 * SUM(CASE WHEN is_constant THEN 1 ELSE 0 END) / COUNT(*), 2) || '%' AS pct_constant
    FROM QUERY_TABLE(stats_table)
);

-- Macro: Check for short series
CREATE OR REPLACE MACRO TS_CHECK_SHORT(stats_table, min_length) AS TABLE (
    SELECT 
        'Short Series (< ' || min_length || ')' AS check_type,
        COUNT(*) AS total_series,
        SUM(CASE WHEN length < min_length THEN 1 ELSE 0 END) AS short_series,
        ROUND(100.0 * SUM(CASE WHEN length < min_length THEN 1 ELSE 0 END) / COUNT(*), 2) || '%' AS pct_short,
        MIN(length) AS min_series_length,
        MAX(length) AS max_series_length
    FROM QUERY_TABLE(stats_table)
);

-- Macro: Check for end date alignment
CREATE OR REPLACE MACRO TS_CHECK_ALIGNMENT(stats_table) AS TABLE (
    WITH max_end AS (
        SELECT MAX(end_date) AS global_max_date
        FROM QUERY_TABLE(stats_table)
    )
    SELECT 
        'End Date Alignment' AS check_type,
        COUNT(*) AS total_series,
        SUM(CASE WHEN end_date = (SELECT global_max_date FROM max_end) THEN 1 ELSE 0 END) AS aligned_series,
        SUM(CASE WHEN end_date < (SELECT global_max_date FROM max_end) THEN 1 ELSE 0 END) AS misaligned_series,
        (SELECT global_max_date FROM max_end) AS latest_date
    FROM QUERY_TABLE(stats_table)
);

-- Macro: Comprehensive data quality report
CREATE OR REPLACE MACRO TS_QUALITY_REPORT(stats_table, min_length) AS TABLE (
    SELECT * FROM TS_CHECK_GAPS(stats_table)
    UNION ALL
    SELECT * FROM TS_CHECK_MISSING(stats_table)
    UNION ALL
    SELECT * FROM TS_CHECK_CONSTANT(stats_table)
    UNION ALL
    SELECT * FROM TS_CHECK_SHORT(stats_table, min_length)
    UNION ALL
    SELECT * FROM TS_CHECK_ALIGNMENT(stats_table)
);

-- ============================================================================
-- SECTION 3: Advanced Pattern Detection
-- ============================================================================

-- Macro: Detect leading/trailing zeros
CREATE OR REPLACE MACRO TS_ANALYZE_ZEROS(table_name, group_cols, date_col, value_col) AS TABLE (
    WITH series_data AS (
        SELECT * FROM QUERY_TABLE(table_name)
    ),
    series_with_zeros AS (
        SELECT 
            group_cols AS series_id,
            value_col,
            date_col,
            ROW_NUMBER() OVER (PARTITION BY group_cols ORDER BY date_col) AS rn_forward,
            ROW_NUMBER() OVER (PARTITION BY group_cols ORDER BY date_col DESC) AS rn_backward,
            value_col = 0 AS is_zero
        FROM series_data
    ),
    leading_zeros AS (
        SELECT 
            series_id,
            SUM(CASE WHEN is_zero THEN 1 ELSE 0 END) AS n_leading_zeros
        FROM (
            SELECT 
                series_id,
                is_zero,
                SUM(CASE WHEN NOT is_zero THEN 1 ELSE 0 END) OVER (PARTITION BY series_id ORDER BY rn_forward) AS non_zero_seen
            FROM series_with_zeros
        )
        WHERE non_zero_seen = 0
        GROUP BY series_id
    ),
    trailing_zeros AS (
        SELECT 
            series_id,
            SUM(CASE WHEN is_zero THEN 1 ELSE 0 END) AS n_trailing_zeros
        FROM (
            SELECT 
                series_id,
                is_zero,
                SUM(CASE WHEN NOT is_zero THEN 1 ELSE 0 END) OVER (PARTITION BY series_id ORDER BY rn_backward) AS non_zero_seen
            FROM series_with_zeros
        )
        WHERE non_zero_seen = 0
        GROUP BY series_id
    )
    SELECT 
        COALESCE(l.series_id, t.series_id) AS series_id,
        COALESCE(l.n_leading_zeros, 0) AS n_leading_zeros,
        COALESCE(t.n_trailing_zeros, 0) AS n_trailing_zeros,
        COALESCE(l.n_leading_zeros, 0) + COALESCE(t.n_trailing_zeros, 0) AS total_edge_zeros
    FROM leading_zeros l
    FULL OUTER JOIN trailing_zeros t ON l.series_id = t.series_id
    WHERE COALESCE(l.n_leading_zeros, 0) + COALESCE(t.n_trailing_zeros, 0) > 0
);

-- Macro: Detect plateaus (consecutive identical values)
CREATE OR REPLACE MACRO TS_DETECT_PLATEAUS(table_name, group_cols, date_col, value_col) AS TABLE (
    WITH series_data AS (
        SELECT * FROM QUERY_TABLE(table_name)
    ),
    value_changes AS (
        SELECT 
            group_cols AS series_id,
            value_col,
            date_col,
            value_col != LAG(value_col) OVER (PARTITION BY group_cols ORDER BY date_col) AS is_change,
            SUM(CASE WHEN value_col != LAG(value_col) OVER (PARTITION BY group_cols ORDER BY date_col) THEN 1 ELSE 0 END) 
                OVER (PARTITION BY group_cols ORDER BY date_col) AS plateau_id
        FROM series_data
    ),
    plateaus AS (
        SELECT 
            series_id,
            value_col,
            plateau_id,
            COUNT(*) AS plateau_length,
            MIN(date_col) AS plateau_start,
            MAX(date_col) AS plateau_end
        FROM value_changes
        GROUP BY series_id, value_col, plateau_id
        HAVING COUNT(*) > 1
    )
    SELECT 
        series_id,
        MAX(plateau_length) AS max_plateau_size,
        MAX(CASE WHEN value_col != 0 THEN plateau_length ELSE 0 END) AS max_plateau_nonzero,
        MAX(CASE WHEN value_col = 0 THEN plateau_length ELSE 0 END) AS max_plateau_zeros,
        COUNT(*) AS n_plateaus
    FROM plateaus
    GROUP BY series_id
);

-- ============================================================================
-- SECTION 4: Seasonality Detection (using extension)
-- ============================================================================

-- Macro: Detect seasonality for all series
CREATE OR REPLACE MACRO TS_DETECT_SEASONALITY_ALL(table_name, group_cols, date_col, value_col) AS TABLE (
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
);

-- ============================================================================
-- SECTION 5: Distribution Analysis
-- ============================================================================

-- Macro: Value distribution percentiles
CREATE OR REPLACE MACRO TS_PERCENTILES(table_name, group_cols, value_col) AS TABLE (
    WITH series_data AS (
        SELECT * FROM QUERY_TABLE(table_name)
    )
    SELECT 
        group_cols AS series_id,
        ROUND(QUANTILE_CONT(value_col, 0.01), 2) AS p01,
        ROUND(QUANTILE_CONT(value_col, 0.05), 2) AS p05,
        ROUND(QUANTILE_CONT(value_col, 0.10), 2) AS p10,
        ROUND(QUANTILE_CONT(value_col, 0.25), 2) AS p25,
        ROUND(QUANTILE_CONT(value_col, 0.50), 2) AS p50,
        ROUND(QUANTILE_CONT(value_col, 0.75), 2) AS p75,
        ROUND(QUANTILE_CONT(value_col, 0.90), 2) AS p90,
        ROUND(QUANTILE_CONT(value_col, 0.95), 2) AS p95,
        ROUND(QUANTILE_CONT(value_col, 0.99), 2) AS p99,
        ROUND(QUANTILE_CONT(value_col, 0.75) - QUANTILE_CONT(value_col, 0.25), 2) AS iqr
    FROM series_data
    GROUP BY group_cols
);

-- ============================================================================
-- SECTION 6: Summary Dashboard
-- ============================================================================

-- Macro: Overall dataset summary
CREATE OR REPLACE MACRO TS_DATASET_SUMMARY(stats_table) AS TABLE (
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
);

-- Macro: Get problematic series (low quality score)
CREATE OR REPLACE MACRO TS_GET_PROBLEMATIC(stats_table, quality_threshold) AS TABLE (
    SELECT 
        series_id,
        length,
        n_gaps,
        n_null,
        n_nan,
        is_constant,
        quality_score,
        CASE 
            WHEN n_null > 0 OR n_nan > 0 THEN '⚠️ Missing values'
            WHEN is_constant THEN '⚠️ Constant'
            WHEN n_gaps > length * 0.1 THEN '⚠️ Many gaps'
            ELSE '⚠️ Other issues'
        END AS primary_issue
    FROM QUERY_TABLE(stats_table)
    WHERE quality_score < quality_threshold
    ORDER BY quality_score
);

-- ============================================================================
-- End of EDA Macros
-- ============================================================================

SELECT '✅ EDA macros loaded successfully!' AS status;
SELECT 'Use TS_STATS(), TS_QUALITY_REPORT(), TS_DATASET_SUMMARY(), etc.' AS info;

