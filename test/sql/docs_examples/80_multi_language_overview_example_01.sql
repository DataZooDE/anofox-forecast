-- Create sample data
CREATE TABLE sales AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d);  -- 90 days of data

SELECT * FROM anofox_fcst_ts_forecast('sales', date, sales, 'AutoETS', 28, 
                          MAP{'seasonal_period': 7, 'confidence_level': 0.95})
