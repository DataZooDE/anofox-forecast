-- Minimal Reproduction Test for DuckDB Batch Index Collision
-- This script demonstrates the batch index error using the test operator
-- that simulates MSTL's CPU-intensive Final phase with artificial delay

-- Load the extension
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- Generate test data similar to mstl_10k_series
-- Adjust series count and delay_ms to reproduce the issue
CREATE OR REPLACE TABLE test_large_data AS
SELECT 
    ('2023-01-01'::DATE + INTERVAL (t) DAY) AS date_col,
    'series_' || series_id::VARCHAR AS group_col,
    (series_id * 0.01 + t * 0.1 + (random() - 0.5) * 5) AS value_col
FROM generate_series(0, 9999) series(series_id)  -- 10,000 series
CROSS JOIN generate_series(0, 599) t(t);  -- 600 rows per series

-- Test 1: Serial execution (should work)
PRAGMA threads=1;
CREATE OR REPLACE TABLE test_result_serial AS
SELECT * FROM test_batch_index_reproduction(
    TABLE test_large_data,
    'group_col',
    'value_col',
    10  -- 10ms delay per group
);

-- Test 2: Parallel execution (may trigger batch index error)
PRAGMA threads=0;  -- Use all available threads
CREATE OR REPLACE TABLE test_result_parallel AS
SELECT * FROM test_batch_index_reproduction(
    TABLE test_large_data,
    'group_col',
    'value_col',
    10  -- 10ms delay per group
);

-- Test 3: SELECT instead of CREATE TABLE (to isolate insert vs select)
-- This helps determine if the issue is in PhysicalBatchInsert
SELECT COUNT(*) FROM test_batch_index_reproduction(
    TABLE test_large_data,
    'group_col',
    'value_col',
    10
);

-- Test 4: INSERT INTO instead of CREATE TABLE AS
-- Different code path may avoid the issue
CREATE TABLE test_result_insert (LIKE test_large_data, processed_value DOUBLE);
INSERT INTO test_result_insert
SELECT * FROM test_batch_index_reproduction(
    TABLE test_large_data,
    'group_col',
    'value_col',
    10
);

