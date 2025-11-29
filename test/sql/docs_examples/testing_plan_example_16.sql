-- Create sample test table
CREATE TABLE test_table AS
SELECT 
    series_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date_col,
    100 + series_id * 10 + (RANDOM() * 10) AS value_col
FROM generate_series(0, 30) t(d)
CROSS JOIN (VALUES (1), (2)) series(series_id);

SELECT * FROM anofox_fcst_ts_drop_constant('test_table', series_id, value_col);
SELECT * FROM anofox_fcst_ts_drop_constant('test_table', series_id, value_col)
WHERE value_col = 0;
-- Verify filters out constant/zero series
