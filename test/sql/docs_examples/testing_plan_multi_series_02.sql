-- Create sample test data
CREATE TABLE test_data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS value
FROM generate_series(0, 89) t(d);

-- For each model, verify it returns forecasts
SELECT *
FROM TS_FORECAST('test_data', date, value, 'Naive', 12, MAP{})
LIMIT 5;
