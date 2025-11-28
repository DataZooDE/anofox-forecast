-- Create sample test table
CREATE TABLE test_table AS
SELECT 
    series_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date_col,
    100 + series_id * 10 + (RANDOM() * 10) AS value_col
FROM generate_series(0, 30) t(d)
CROSS JOIN (VALUES (1), (2)) series(series_id);

SELECT 
    series_id,
    date_col AS date,
    value_col AS value
FROM TS_FILL_NULLS_FORWARD('test_table', series_id, date_col, value_col)
LIMIT 5;

SELECT 
    series_id,
    date_col AS date,
    value_col AS value
FROM TS_FILL_NULLS_BACKWARD('test_table', series_id, date_col, value_col)
LIMIT 5;
-- Verify fills NULL values appropriately
