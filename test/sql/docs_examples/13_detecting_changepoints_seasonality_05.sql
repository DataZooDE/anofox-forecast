-- Create sample sales data
CREATE TABLE sales_data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d);

-- Highly sensitive: detect even small changes
SELECT * FROM anofox_fcst_ts_detect_changepoints('sales_data', date, sales, MAP{'hazard_lambda': 50.0})
WHERE is_changepoint = true;

-- Default: balanced detection
SELECT * FROM anofox_fcst_ts_detect_changepoints('sales_data', date, sales, MAP{})
WHERE is_changepoint = true;

-- Conservative: only major shifts
SELECT * FROM anofox_fcst_ts_detect_changepoints('sales_data', date, sales, MAP{'hazard_lambda': 500.0})
WHERE is_changepoint = true;
