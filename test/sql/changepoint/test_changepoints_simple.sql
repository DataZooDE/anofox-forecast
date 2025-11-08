-- Simple changepoint detection test

LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- Create test data with a clear changepoint
DROP TABLE IF EXISTS cp_test;
CREATE TABLE cp_test AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS ds,
    CASE 
        WHEN d < 50 THEN 100 + (RANDOM() * 5)
        ELSE 200 + (RANDOM() * 5)
    END AS y
FROM generate_series(0, 99) t(d);

SELECT '=== Test Data Sample (first 10, changepoint at 50) ===' AS test;
SELECT * FROM cp_test ORDER BY ds LIMIT 10;

SELECT '=== Detect Changepoints ===' AS test;

-- Use the aggregate function directly
WITH changepoints AS (
    SELECT TS_DETECT_CHANGEPOINTS_AGG(ds, y) AS result
    FROM cp_test
),
unnested AS (
    SELECT UNNEST(result) AS row_data
    FROM changepoints
)
SELECT 
    row_data.timestamp AS ds,
    row_data.value AS y,
    row_data.is_changepoint
FROM unnested
WHERE row_data.is_changepoint = true
ORDER BY row_data.timestamp;

SELECT '=== All Data with Changepoint Indicator ===' AS test;
WITH changepoints AS (
    SELECT TS_DETECT_CHANGEPOINTS_AGG(ds, y) AS result
    FROM cp_test
),
unnested AS (
    SELECT UNNEST(result) AS row_data
    FROM changepoints
)
SELECT 
    row_data.timestamp AS ds,
    row_data.value AS y,
    row_data.is_changepoint
FROM unnested
ORDER BY row_data.timestamp
LIMIT 20;
