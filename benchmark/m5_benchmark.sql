LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- Load the M5 benchmark data
INSTALL httpfs;
LOAD 'httpfs';

CREATE OR REPLACE TABLE m5 AS 
SELECT item_id, CAST(timestamp AS TIMESTAMP) AS ds, demand AS y FROM 'https://m5-benchmarks.s3.amazonaws.com/data/train/target.parquet'
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
SELECT * FROM m5 WHERE ds < DATE '2016-04-25';

CREATE OR REPLACE TABLE m5_test AS
SELECT * FROM m5 WHERE ds >= DATE '2016-04-25';


-- Perform baseline forecast and evaluate performance
CREATE OR REPLACE TABLE forecast_results AS (
    SELECT *
    FROM TS_FORECAST_BY('m5_train', item_id, ds, y, 'SeasonalNaive', 28, {'seasonal_period': 7})
    UNION ALL
    SELECT *
    FROM TS_FORECAST_BY('m5_train', item_id, ds, y, 'Theta', 28, {'seasonal_period': 7})
    UNION ALL
    SELECT *
    FROM TS_FORECAST_BY('m5_train', item_id, ds, y, 'AutoARIMA', 28, {'seasonal_period': 7})
    UNION ALL
    SELECT *
    FROM TS_FORECAST_BY('m5_train', item_id, ds, y, 'OptimizedTheta', 28, MAP{'seasonal_period': 7})
);


-- MAE and Bias of Forecasts
CREATE OR REPLACE TABLE evaluation_results AS (
SELECT 
    item_id,
    model_name,
    TS_MAE(LIST(y), LIST(point_forecast)) AS mae,
    TS_BIAS(LIST(y), LIST(point_forecast)) AS bias
FROM (
    -- Join Naive Forecast with Test Data
    SELECT 
        m.item_id,
        m.ds,
        m.y,
        n.model_name,
        n.point_forecast
    FROM forecast_results n
    JOIN m5_test m ON n.item_id = m.item_id AND n.date_col = m.ds
)
GROUP BY item_id, model_name
);

-- Summarise evaluation results by model
SELECT model_name, AVG(mae) AS avg_mae, AVG(bias) AS avg_bias FROM evaluation_results GROUP BY model_name ORDER BY avg_mae;


-- Create Aggregated Forecasts for all models and evaluate performance
WITH aggregated_forecasts AS (
    SELECT 
        model_name,
        date_col AS ds,
        sum(point_forecast) AS point_forecast
    FROM forecast_results
    GROUP BY model_name, date_col
),
aggregated_test AS (
    SELECT 
        ds,
        sum(y) AS y
    FROM m5_test
    GROUP BY ds
),
evaluation_results AS (
    SELECT 
        model_name,
        TS_MAE(LIST(y), LIST(point_forecast)) AS mae,
        TS_BIAS(LIST(y), LIST(point_forecast)) AS bias
    FROM aggregated_forecasts
    JOIN aggregated_test ON aggregated_forecasts.ds = aggregated_test.ds
    GROUP BY model_name
)
SELECT model_name, AVG(mae) AS avg_mae, AVG(bias) AS avg_bias FROM evaluation_results GROUP BY model_name ORDER BY avg_mae;