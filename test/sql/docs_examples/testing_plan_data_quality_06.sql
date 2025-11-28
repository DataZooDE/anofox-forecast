-- Create test table and stats
CREATE TABLE test_table AS
SELECT 
    series_id AS id_col,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date_col,
    100 + series_id * 10 + (RANDOM() * 10) AS value_col
FROM generate_series(0, 30) t(d)
CROSS JOIN (VALUES (1), (2)) series(series_id);

CREATE TABLE test_stats AS
SELECT * FROM TS_STATS('test_table', id_col, date_col, value_col, '1d');

SELECT * FROM TS_QUALITY_REPORT('test_stats', 30);
-- Verify detects: nulls, zeros, constants, gaps
