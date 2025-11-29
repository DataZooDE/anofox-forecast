-- Create sample data
CREATE TABLE data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS value
FROM generate_series(0, 89) t(d);  -- 90 days of data

SELECT MAX(date_col) AS last_change
FROM anofox_fcst_ts_detect_changepoints('data', date, value, MAP{})
WHERE is_changepoint = true;
