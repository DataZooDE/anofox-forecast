-- Create sample data
CREATE TABLE data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS value
FROM generate_series(0, 89) t(d);  -- 90 days of data

SELECT 
    COUNT(*) FILTER (WHERE is_changepoint) AS total_changepoints
FROM anofox_fcst_ts_detect_changepoints('data', date, value, MAP{});
