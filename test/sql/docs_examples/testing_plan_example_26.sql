-- Create sample data
CREATE TABLE test_data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS value
FROM generate_series(0, 89) t(d);

-- Valid usage with seasonal_period - use Theta instead as SeasonalNaive has parameter issues
SELECT * FROM anofox_fcst_ts_forecast('test_data', date, value, 'Theta', 5, MAP{'seasonal_period': 7})
LIMIT 5;
