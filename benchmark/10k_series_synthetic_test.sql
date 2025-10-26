-- Generate 10,000 Time Series for Testing
-- Each series has 365 days of daily data with different patterns

LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- Drop table if exists
DROP TABLE IF EXISTS timeseries_10k;

-- Create table with 10,000 series × 365 days = 3.65M rows
CREATE TABLE timeseries_10k AS
WITH series_metadata AS (
    -- Generate 10,000 unique series with different characteristics
    SELECT 
        series_id,
        -- Random base level (100-1000)
        100 + (random() * 900)::int AS base_level,
        -- Random trend component (-1 to 1 per day)
        (random() * 2 - 1) * 0.5 AS trend_slope,
        -- Random weekly seasonality amplitude (0-50)
        (random() * 50)::int AS weekly_amplitude,
        -- Random monthly seasonality amplitude (0-30)  
        (random() * 30)::int AS monthly_amplitude,
        -- Random noise level (5-20)
        5 + (random() * 15)::int AS noise_level,
        -- Category assignment (for grouping)
        CASE 
            WHEN series_id % 10 = 0 THEN 'Category_A'
            WHEN series_id % 10 < 5 THEN 'Category_B'
            ELSE 'Category_C'
        END AS category,
        -- Region assignment
        CASE 
            WHEN series_id % 4 = 0 THEN 'North'
            WHEN series_id % 4 = 1 THEN 'South'
            WHEN series_id % 4 = 2 THEN 'East'
            ELSE 'West'
        END AS region
    FROM generate_series(1, 10000) AS t(series_id)
),
time_series_data AS (
    SELECT 
        m.series_id,
        'Series_' || LPAD(m.series_id::VARCHAR, 5, '0') AS series_name,
        m.category,
        m.region,
        DATE '2023-01-01' + INTERVAL (d) DAY AS date,
        d AS day_index,
        -- Generate realistic values with:
        -- 1. Base level
        -- 2. Linear trend
        -- 3. Weekly seasonality (period 7)
        -- 4. Monthly seasonality (period 30)  
        -- 5. Random noise
        GREATEST(0, 
            m.base_level 
            + m.trend_slope * d
            + m.weekly_amplitude * SIN(d * 2 * PI() / 7)
            + m.monthly_amplitude * SIN(d * 2 * PI() / 30)
            + (random() * m.noise_level - m.noise_level / 2)
        )::DOUBLE AS value
    FROM series_metadata m
    CROSS JOIN generate_series(0, 364) AS t(d)
)
SELECT 
    series_id,
    series_name,
    category,
    region,
    date,
    day_index,
    ROUND(value, 2) AS value
FROM time_series_data
ORDER BY series_id, date;

-- Show summary statistics
SELECT 
    '========================================' AS separator;

SELECT 
    'Dataset Created Successfully!' AS status;

SELECT 
    '========================================' AS separator;

SELECT 
    'Total Series:' AS metric,
    COUNT(DISTINCT series_id)::VARCHAR AS value
FROM timeseries_10k
UNION ALL
SELECT 
    'Total Data Points:',
    COUNT(*)::VARCHAR
FROM timeseries_10k
UNION ALL
SELECT 
    'Date Range:',
    MIN(date)::VARCHAR || ' to ' || MAX(date)::VARCHAR
FROM timeseries_10k
UNION ALL
SELECT
    'Avg Value:',
    ROUND(AVG(value), 2)::VARCHAR
FROM timeseries_10k
UNION ALL
SELECT
    'Min Value:',
    ROUND(MIN(value), 2)::VARCHAR
FROM timeseries_10k
UNION ALL
SELECT
    'Max Value:',
    ROUND(MAX(value), 2)::VARCHAR
FROM timeseries_10k;

SELECT 
    '========================================' AS separator;

-- Show sample by category
SELECT 
    'Distribution by Category:' AS info;

SELECT 
    category,
    COUNT(DISTINCT series_id) AS num_series,
    ROUND(AVG(value), 2) AS avg_value,
    ROUND(MIN(value), 2) AS min_value,
    ROUND(MAX(value), 2) AS max_value
FROM timeseries_10k
GROUP BY category
ORDER BY category;

SELECT 
    '========================================' AS separator;

-- Show sample by region
SELECT 
    'Distribution by Region:' AS info;

SELECT 
    region,
    COUNT(DISTINCT series_id) AS num_series,
    ROUND(AVG(value), 2) AS avg_value,
    ROUND(MIN(value), 2) AS min_value,
    ROUND(MAX(value), 2) AS max_value
FROM timeseries_10k
GROUP BY region
ORDER BY region;

SELECT 
    '========================================' AS separator;

-- Show sample data from first 5 series
SELECT 
    'Sample Data (First 5 Series, First 7 Days):' AS info;

SELECT 
    series_name,
    date,
    value
FROM timeseries_10k
WHERE series_id <= 5 AND day_index < 7
ORDER BY series_id, date;

SELECT 
    '========================================' AS separator;

-- Show table size
SELECT 
    'Table Size Information:' AS info;

SELECT 
    COUNT(*) AS total_rows,
    COUNT(DISTINCT series_id) AS unique_series,
    COUNT(DISTINCT date) AS unique_dates,
    COUNT(DISTINCT category) AS unique_categories,
    COUNT(DISTINCT region) AS unique_regions
FROM timeseries_10k;

SELECT 
    '========================================' AS separator;


-- Group 1: Basic Models (6)
SELECT '1. Naive' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'Naive', 10, NULL) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS naive, UNNEST(f.lower_95) AS naive_lower_95, UNNEST(f.upper_95) AS naive_upper_95 FROM forecasts;

SELECT '2. SMA' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'SMA', 10, {'window': 7}) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS sma, UNNEST(f.lower_95) AS sma_lower_95, UNNEST(f.upper_95) AS sma_upper_95 FROM forecasts;

SELECT '3. SeasonalNaive' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'SeasonalNaive', 10, {'seasonal_period': 7}) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS seasonal_naive, UNNEST(f.lower_95) AS seasonal_naive_lower_95, UNNEST(f.upper_95) AS seasonal_naive_upper_95 FROM forecasts;

SELECT '4. SES' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'SES', 10, {'alpha': 0.3}) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS ses, UNNEST(f.lower_95) AS ses_lower_95, UNNEST(f.upper_95) AS ses_upper_95 FROM forecasts;

SELECT '5. SESOptimized' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'SESOptimized', 10, NULL) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS ses_optimized, UNNEST(f.lower_95) AS ses_optimized_lower_95, UNNEST(f.upper_95) AS ses_optimized_upper_95 FROM forecasts;

SELECT '6. RandomWalkWithDrift' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'RandomWalkWithDrift', 10, NULL) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS random_walk_with_drift, UNNEST(f.lower_95) AS random_walk_with_drift_lower_95, UNNEST(f.upper_95) AS random_walk_with_drift_upper_95 FROM forecasts;


SELECT '========================================' AS separator;

-- Group 2: Holt Models (2)
SELECT '7. Holt' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'Holt', 10, NULL) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS holt, UNNEST(f.lower_95) AS holt_lower_95, UNNEST(f.upper_95) AS holt_upper_95 FROM forecasts;

SELECT '8. HoltWinters' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'HoltWinters', 10, {'seasonal_period': 7}) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS holt_winters, UNNEST(f.lower_95) AS holt_winters_lower_95, UNNEST(f.upper_95) AS holt_winters_upper_95 FROM forecasts;

SELECT '========================================' AS separator;

-- Group 3: Theta Variants (4)
SELECT '9. Theta' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'Theta', 10, {'seasonal_period': 7}) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS theta, UNNEST(f.lower_95) AS theta_lower_95, UNNEST(f.upper_95) AS theta_upper_95 FROM forecasts;

SELECT '10. OptimizedTheta' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'OptimizedTheta', 10, {'seasonal_period': 7}) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS optimized_theta, UNNEST(f.lower_95) AS optimized_theta_lower_95, UNNEST(f.upper_95) AS optimized_theta_upper_95 FROM forecasts;

SELECT '11. DynamicTheta' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'DynamicTheta', 10, {'seasonal_period': 7}) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS dynamic_theta, UNNEST(f.lower_95) AS dynamic_theta_lower_95, UNNEST(f.upper_95) AS dynamic_theta_upper_95 FROM forecasts;

SELECT '12. DynamicOptimizedTheta' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'DynamicOptimizedTheta', 10, {'seasonal_period': 7}) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS dynamic_optimized_theta, UNNEST(f.lower_95) AS dynamic_optimized_theta_lower_95, UNNEST(f.upper_95) AS dynamic_optimized_theta_upper_95 FROM forecasts;


SELECT '========================================' AS separator;

-- Group 4: Seasonal ES (3)
SELECT '13. SeasonalES' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'SeasonalES', 10, {'seasonal_period': 7, 'alpha': 0.2, 'gamma': 0.1}) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS seasonal_es, UNNEST(f.lower_95) AS seasonal_es_lower_95, UNNEST(f.upper_95) AS seasonal_es_upper_95 FROM forecasts;

SELECT '14. SeasonalESOptimized' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'SeasonalESOptimized', 10, {'seasonal_period': 7}) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS seasonal_es_optimized, UNNEST(f.lower_95) AS seasonal_es_optimized_lower_95, UNNEST(f.upper_95) AS seasonal_es_optimized_upper_95 FROM forecasts;

SELECT '15. SeasonalWindowAverage' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'SeasonalWindowAverage', 10, {'seasonal_period': 7, 'window': 5}) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS seasonal_window_average, UNNEST(f.lower_95) AS seasonal_window_average_lower_95, UNNEST(f.upper_95) AS seasonal_window_average_upper_95 FROM forecasts;


SELECT '========================================' AS separator;

-- Group 5: ARIMA (2)
SELECT '16. ARIMA' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'ARIMA', 10, {'p': 1, 'd': 0, 'q': 0}) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS arima, UNNEST(f.lower_95) AS arima_lower_95, UNNEST(f.upper_95) AS arima_upper_95 FROM forecasts;

SELECT '17. AutoARIMA' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'AutoARIMA', 10, {'seasonal_period': 7}) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS auto_arima, UNNEST(f.lower_95) AS auto_arima_lower_95, UNNEST(f.upper_95) AS auto_arima_upper_95 FROM forecasts;


SELECT '========================================' AS separator;

-- Group 6: State Space (2)
SELECT '18. ETS' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'ETS', 10, {'trend_type': 1, 'season_type': 1, 'season_length': 7}) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS ets, UNNEST(f.lower_95) AS ets_lower_95, UNNEST(f.upper_95) AS ets_upper_95 FROM forecasts;

SELECT '19. AutoETS' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'AutoETS', 10, {'season_length': 7}) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS auto_ets, UNNEST(f.lower_95) AS auto_ets_lower_95, UNNEST(f.upper_95) AS auto_ets_upper_95 FROM forecasts;


SELECT '========================================' AS separator;

-- Group 7: Multiple Seasonality (6)
SELECT '20. MFLES' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'MFLES', 10, {'seasonal_periods': [7, 30]}) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS mfles, UNNEST(f.lower_95) AS mfles_lower_95, UNNEST(f.upper_95) AS mfles_upper_95 FROM forecasts;

SELECT '21. AutoMFLES' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'AutoMFLES', 10, {'seasonal_periods': [7, 30]}) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS auto_mfles, UNNEST(f.lower_95) AS auto_mfles_lower_95, UNNEST(f.upper_95) AS auto_mfles_upper_95 FROM forecasts;

SELECT '22. MSTL' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'MSTL', 10, {'seasonal_periods': [7, 30]}) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS mstl, UNNEST(f.lower_95) AS mstl_lower_95, UNNEST(f.upper_95) AS mstl_upper_95 FROM forecasts;

SELECT '23. AutoMSTL' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'AutoMSTL', 10, {'seasonal_periods': [7, 30]}) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS auto_mstl, UNNEST(f.lower_95) AS auto_mstl_lower_95, UNNEST(f.upper_95) AS auto_mstl_upper_95 FROM forecasts;

SELECT '24. TBATS' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'TBATS', 10, {'seasonal_periods': [7, 30]}) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS tbats, UNNEST(f.lower_95) AS tbats_lower_95, UNNEST(f.upper_95) AS tbats_upper_95 FROM forecasts;

SELECT '25. AutoTBATS' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'AutoTBATS', 10, {'seasonal_periods': [7, 30]}) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS auto_tbats, UNNEST(f.lower_95) AS auto_tbats_lower_95, UNNEST(f.upper_95) AS auto_tbats_upper_95 FROM forecasts;


SELECT '========================================' AS separator;

-- Group 8: Intermittent Demand (6) - use intermittent_data
SELECT '26. CrostonClassic' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value::DOUBLE, 'CrostonClassic', 10, NULL) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS croston_classic, UNNEST(f.lower_95) AS croston_classic_lower_95, UNNEST(f.upper_95) AS croston_classic_upper_95 FROM forecasts;

SELECT '27. CrostonOptimized' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value::DOUBLE, 'CrostonOptimized', 10, NULL) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS croston_optimized, UNNEST(f.lower_95) AS croston_optimized_lower_95, UNNEST(f.upper_95) AS croston_optimized_upper_95 FROM forecasts;

SELECT '28. CrostonSBA' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value::DOUBLE, 'CrostonSBA', 10, NULL) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS croston_sba, UNNEST(f.lower_95) AS croston_sba_lower_95, UNNEST(f.upper_95) AS croston_sba_upper_95 FROM forecasts;

SELECT '29. ADIDA' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value::DOUBLE, 'ADIDA', 10, NULL) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS adida, UNNEST(f.lower_95) AS adida_lower_95, UNNEST(f.upper_95) AS adida_upper_95 FROM forecasts;

SELECT '30. IMAPA' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value::DOUBLE, 'IMAPA', 10, NULL) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS imapa, UNNEST(f.lower_95) AS imapa_lower_95, UNNEST(f.upper_95) AS imapa_upper_95 FROM forecasts;

SELECT '31. TSB' AS model;
WITH forecasts AS (
    SELECT 
        series_name,
        TS_FORECAST(date, value::DOUBLE, 'TSB', 10, {'alpha_d': 0.1, 'alpha_p': 0.1}) AS f
    FROM timeseries_10k
    GROUP BY series_name
)
SELECT series_name, UNNEST(f.forecast_timestamp) AS date, UNNEST(f.point_forecast) AS tsb, UNNEST(f.lower_95) AS tsb_lower_95, UNNEST(f.upper_95) AS tsb_upper_95 FROM forecasts;