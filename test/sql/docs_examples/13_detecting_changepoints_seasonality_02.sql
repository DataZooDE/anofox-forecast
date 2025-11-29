-- Create sample sales data
CREATE TABLE sales_data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d);  -- 90 days of data

-- Detect with default parameters
SELECT *
FROM anofox_fcst_ts_detect_changepoints('sales_data', date, sales, MAP{})
WHERE is_changepoint = true;

-- More sensitive detection
SELECT *
FROM anofox_fcst_ts_detect_changepoints('sales_data', date, sales, MAP{'hazard_lambda': 50.0})
WHERE is_changepoint = true;

-- With probabilities for confidence scoring
SELECT date_col, is_changepoint, ROUND(changepoint_probability, 4) AS confidence
FROM anofox_fcst_ts_detect_changepoints('sales_data', date, sales, MAP{'include_probabilities': true})
WHERE is_changepoint = true
ORDER BY changepoint_probability DESC;
