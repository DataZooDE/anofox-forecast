LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- Load the M5 benchmark data
INSTALL httpfs;
LOAD 'httpfs';

CREATE OR REPLACE TABLE m5 AS 
SELECT item_id, CAST(timestamp AS TIMESTAMP) AS timestamp, demand FROM 'https://m5-benchmarks.s3.amazonaws.com/data/train/target.parquet'
ORDER BY item_id, timestamp;


-- Show summary statistics
SELECT 
    '========================================' AS separator;

SELECT 
    'Dataset Loaded Successfully!' AS status;

SELECT 
    '========================================' AS separator;

SELECT 
    'Total Series:' AS metric,
    COUNT(DISTINCT item_id)::VARCHAR AS value
FROM m5
UNION ALL
SELECT 
    'Total Data Points:',
    COUNT(*)::VARCHAR
FROM m5
UNION ALL
SELECT 
    'Date Range:',
    MIN(timestamp)::VARCHAR || ' to ' || MAX(timestamp)::VARCHAR
FROM m5
UNION ALL
SELECT
    'Avg Value:',
    ROUND(AVG(demand), 2)::VARCHAR
FROM m5
UNION ALL
SELECT
    'Min Value:',
    ROUND(MIN(demand), 2)::VARCHAR
FROM m5
UNION ALL
SELECT
    'Max Value:',
    ROUND(MAX(demand), 2)::VARCHAR
FROM m5;


SELECT 
    '========================================' AS separator;

-- Show table size
SELECT 
    'Table Size Information:' AS info;

SELECT 
    COUNT(*) AS total_rows,
    COUNT(DISTINCT item_id) AS unique_items,
    COUNT(DISTINCT timestamp) AS unique_timestamps,
FROM m5;

SELECT
    '========================================' AS separator;


-- Group 5: ARIMA (2)
SELECT '16. ARIMA' AS model;
WITH forecasts AS (
    SELECT 
        item_id,
        TS_FORECAST(timestamp, demand, 'ARIMA', 10, {'p': 1, 'd': 0, 'q': 0}) AS f
    FROM m5
    GROUP BY item_id
)
SELECT item_id, UNNEST(f.forecast_timestamp) AS timestamp, UNNEST(f.point_forecast) AS arima, UNNEST(f.lower_95) AS arima_lower_95, UNNEST(f.upper_95) AS arima_upper_95 FROM forecasts;


SELECT '17. AutoARIMA' AS model;
WITH forecasts AS (
    SELECT 
        item_id,
        TS_FORECAST(timestamp, demand, 'AutoARIMA', 10, {'seasonal_period': 12}) AS f
    FROM m5
    GROUP BY item_id
)
SELECT item_id, UNNEST(f.forecast_timestamp) AS timestamp, UNNEST(f.point_forecast) AS auto_arima, UNNEST(f.lower_95) AS auto_arima_lower_95, UNNEST(f.upper_95) AS auto_arima_upper_95 FROM forecasts;
