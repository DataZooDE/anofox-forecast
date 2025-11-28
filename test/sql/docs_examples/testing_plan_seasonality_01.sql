-- Create sample test data
CREATE TABLE test_data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    value
FROM (VALUES 
    (0, 100.0), (1, 102.0), (2, 105.0), (3, 103.0), (4, 104.0),
    (5, 106.0), (6, 108.0), (7, 107.0), (8, 109.0), (9, 110.0)
) t(d, value);

-- Test with return_insample = true
SELECT *
FROM TS_FORECAST('test_data', date, value, 'Naive', 5, MAP{'return_insample': true})
LIMIT 5;

-- Verify insample_fitted has correct length (training data length)
-- Verify fitted values make sense for the model
