-- MSTL Decomposition Tests (Plain SQL)

-- Test 1: Basic Decomposition with Additive Check
CREATE TABLE test_mstl_basic AS
SELECT 
    ('2024-01-01'::DATE + INTERVAL (i) DAY) AS date_col,
    'series_1' AS group_col,
    100.0 + i + sin(i * 2 * 3.14159 / 7.0) * 10 AS value_col
FROM generate_series(0, 100) t(i);

-- Run MSTL with seasonal period 7
CREATE TABLE result_mstl_basic AS
SELECT * FROM ts_mstl_decomposition(
    table_name='test_mstl_basic', 
    group_col='group_col', 
    date_col='date_col', 
    value_col='value_col', 
    params={'seasonal_periods': [7]}
);

-- Verify output columns
SELECT column_name FROM information_schema.columns 
WHERE table_name = 'result_mstl_basic' AND column_name IN ('trend', 'seasonal_7', 'residual')
ORDER BY column_name;

-- Verify additivity (Original ~= Trend + Seasonal + Residual)
SELECT AVG(ABS(value_col - (trend + seasonal_7 + residual))) < 0.001 as is_additive
FROM result_mstl_basic;

-- Test 2: Multiple Seasonal Periods
CREATE TABLE test_mstl_multi AS
SELECT 
    ('2024-01-01'::DATE + INTERVAL (i) DAY) AS date_col,
    'series_multi' AS group_col,
    100.0 + sin(i * 2 * 3.14159 / 7.0) * 10 + sin(i * 2 * 3.14159 / 30.0) * 5 AS value_col
FROM generate_series(0, 200) t(i);

CREATE TABLE result_mstl_multi AS
SELECT * FROM ts_mstl_decomposition(
    table_name='test_mstl_multi', 
    group_col='group_col', 
    date_col='date_col', 
    value_col='value_col', 
    params={'seasonal_periods': [7, 30]}
);

-- Verify output columns contain seasonal_7 and seasonal_30
SELECT COUNT(*) as count_seasonal_cols
FROM information_schema.columns 
WHERE table_name = 'result_mstl_multi' AND column_name IN ('seasonal_7', 'seasonal_30');

-- Test 3: Error Handling - Missing seasonal_periods (Commented out as it aborts execution in plain SQL script)
-- SELECT * FROM ts_mstl_decomposition(
--     table_name='test_mstl_basic', 
--     group_col='group_col', 
--     date_col='date_col', 
--     value_col='value_col', 
--     params={}
-- );

-- Test 4: Macro Alias (anofox_fcst_ts_mstl_decomposition)
CREATE TABLE result_mstl_alias AS
SELECT * FROM anofox_fcst_ts_mstl_decomposition(
    table_name='test_mstl_basic', 
    group_col='group_col', 
    date_col='date_col', 
    value_col='value_col', 
    params={'seasonal_periods': [7]}
);

SELECT COUNT(*) as count_alias_result FROM result_mstl_alias;

-- Cleanup
DROP TABLE IF EXISTS test_mstl_basic;

DROP TABLE IF EXISTS result_mstl_basic;

DROP TABLE IF EXISTS test_mstl_multi;

DROP TABLE IF EXISTS result_mstl_multi;

DROP TABLE IF EXISTS result_mstl_alias;

