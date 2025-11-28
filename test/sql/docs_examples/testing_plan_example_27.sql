-- Create test data with 10,000 data points
CREATE TABLE large_test_data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (idx) DAY AS date,
    RANDOM() * 100 AS value
FROM generate_series(1, 10000) t(idx);

-- Test with 10,000 data points
SELECT * FROM TS_FORECAST('large_test_data', date, value, 'Naive', 100, MAP{})
LIMIT 5;
