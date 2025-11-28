-- Create sample sales data
CREATE TABLE sales_data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d);

-- Highly sensitive: detect even small changes
SELECT * FROM TS_DETECT_CHANGEPOINTS('sales_data', date, sales, MAP{'hazard_lambda': 50.0})
WHERE is_changepoint = true;

-- Default: balanced detection
SELECT * FROM TS_DETECT_CHANGEPOINTS('sales_data', date, sales, MAP{})
WHERE is_changepoint = true;

-- Conservative: only major shifts
SELECT * FROM TS_DETECT_CHANGEPOINTS('sales_data', date, sales, MAP{'hazard_lambda': 500.0})
WHERE is_changepoint = true;
