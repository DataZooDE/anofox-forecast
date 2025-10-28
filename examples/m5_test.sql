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


-- Create train / test split for the last 12 months
CREATE OR REPLACE TABLE m5_train AS
SELECT * FROM m5 WHERE timestamp < DATE '2016-04-25';

CREATE OR REPLACE TABLE m5_test AS
SELECT * FROM m5 WHERE timestamp >= DATE '2016-04-25';


-- Perform baseline forecast and evaluate performance
CREATE OR REPLACE TABLE forecast_results AS (
    SELECT *
    FROM TS_FORECAST_BY('m5_train', [item_id, product_id], timestamp, demand, 'SeasonalNaive', 28, {'seasonal_period': 7})
    UNION ALL
    SELECT *
    FROM TS_FORECAST_BY('m5_train', [item_id, product_id], timestamp, demand, 'Theta', 28, {'confidence_level': 0.95})
    UNION ALL
    SELECT *
    FROM TS_FORECAST_BY('m5_train', [item_id, product_id], timestamp, demand, 'AutoETS', 28, {'seasonal_period': 7})
    UNION ALL
    SELECT *
    FROM TS_FORECAST_BY('m5_train', [item_id, product_id], timestamp, demand, 'ARIMA', 28, {'p': 2, 'd': 1, 'q': 2, 'P': 1, 'D': 1, 'Q': 1, 's': 7})
);



-- MAE and Bias of Naive Forecast
CREATE OR REPLACE TABLE evaluation_results AS (
SELECT 
    item_id,
    TS_MAE(LIST(demand), LIST(point_forecast)) AS naive_mae,
    TS_BIAS(LIST(demand), LIST(point_forecast)) AS naive_bias
FROM (
    -- Join Naive Forecast with Test Data
    SELECT 
        m.item_id,
        m.timestamp,
        m.demand,
        n.point_forecast
    FROM naive_forecast n
    JOIN m5_test m ON n.item_id = m.item_id AND n.forecast_timestamp = m.timestamp
)
GROUP BY item_id
);


-- ETS Forecast
CREATE OR REPLACE TABLE ets_forecast AS (
    SELECT *
    FROM TS_FORECAST_BY('m5_train', item_id, timestamp, demand, 'ETS', 28, MAP{'seasonal_period': 7, 'error_type': 0, 'trend_type': 1, 'season_type': 1})
);


-- Calculate MAE and Bias of Theta Forecast and append to evaluation_results
CREATE OR REPLACE TABLE evaluation_results AS (
    SELECT * FROM evaluation_results e
    LEFT JOIN (
        SELECT 
            item_id,
            TS_MAE(LIST(demand), LIST(point_forecast)) AS ets_mae,
            TS_BIAS(LIST(demand), LIST(point_forecast)) AS ets_bias
        FROM (
            -- Join ETS Forecast with Test Data
            SELECT 
                m.item_id,
                m.timestamp,
                m.demand,
                n.point_forecast
            FROM ets_forecast n
            JOIN m5_test m ON n.item_id = m.item_id AND n.forecast_timestamp = m.timestamp
            )
            GROUP BY item_id
    ) t ON e.item_id = t.item_id
);

SELECT * FROM evaluation_results;