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



SELECT 
    series_id,
    ts_features(date, value) AS features
FROM timeseries_10k
GROUP BY series_id
ORDER BY series_id;

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
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'Naive', 10, MAP{});


SELECT '2. SMA' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'SMA', 10, {'window': 7});

SELECT '3. SeasonalNaive' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'SeasonalNaive', 10, {'seasonal_period': 7});

SELECT '4. SES' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'SES', 10, {'alpha': 0.3});

SELECT '5. SESOptimized' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'SESOptimized', 10, NULL);

SELECT '6. RandomWalkWithDrift' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'RandomWalkWithDrift', 10, NULL);


SELECT '========================================' AS separator;

-- Group 2: Holt Models (2)
SELECT '7. Holt' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'Holt', 10, NULL);

SELECT '8. HoltWinters' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'HoltWinters', 10, {'seasonal_period': 7});

SELECT '========================================' AS separator;

-- Group 3: Theta Variants (4)
SELECT '9. Theta' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'Theta', 10, {'seasonal_period': 7});

SELECT '10. OptimizedTheta' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'OptimizedTheta', 10, {'seasonal_period': 7});

SELECT '11. DynamicTheta' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'DynamicTheta', 10, {'seasonal_period': 7});

SELECT '12. DynamicOptimizedTheta' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'DynamicOptimizedTheta', 10, {'seasonal_period': 7});


SELECT '========================================' AS separator;

-- Group 4: Seasonal ES (3)
SELECT '13. SeasonalES' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'SeasonalES', 10, {'seasonal_period': 7, 'alpha': 0.2, 'gamma': 0.1});

SELECT '14. SeasonalESOptimized' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'SeasonalESOptimized', 10, {'seasonal_period': 7});

SELECT '15. SeasonalWindowAverage' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'SeasonalWindowAverage', 10, {'seasonal_period': 7, 'window': 5});


SELECT '========================================' AS separator;

-- Group 5: ARIMA (2)
SELECT '16. ARIMA' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'ARIMA', 10, {'p': 1, 'd': 0, 'q': 0});

SELECT '17. AutoARIMA' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'AutoARIMA', 10, {'seasonal_period': 7});


SELECT '========================================' AS separator;

-- Group 6: State Space (2)
SELECT '18. ETS' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'ETS', 10, {'trend_type': 1, 'season_type': 1, 'season_length': 7});

SELECT '19. AutoETS' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'AutoETS', 10, {'season_length': 7});


SELECT '========================================' AS separator;

-- Group 7: Multiple Seasonality (6)
SELECT '20. MFLES' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'MFLES', 10, {'seasonal_periods': [7, 30]});

SELECT '21. AutoMFLES' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'AutoMFLES', 10, {'seasonal_periods': [7, 30]});

SELECT '22. MSTL' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'MSTL', 10, {'seasonal_periods': [7, 30]});

SELECT '23. AutoMSTL' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'AutoMSTL', 10, {'seasonal_periods': [7, 30]});

SELECT '24. TBATS' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'TBATS', 10, {'seasonal_periods': [7, 30]});

SELECT '25. AutoTBATS' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'AutoTBATS', 10, {'seasonal_periods': [7, 30]});


SELECT '========================================' AS separator;

-- Group 8: Intermittent Demand (6) - use intermittent_data
SELECT '26. CrostonClassic' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'CrostonClassic', 10, NULL);

SELECT '27. CrostonOptimized' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'CrostonOptimized', 10, NULL);

SELECT '28. CrostonSBA' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'CrostonSBA', 10, NULL);

SELECT '29. ADIDA' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'ADIDA', 10, NULL);

SELECT '30. IMAPA' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'IMAPA', 10, NULL);

SELECT '31. TSB' AS model;
SELECT *
FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'TSB', 10, MAP{'alpha_d': 0.1, 'alpha_p': 0.1});