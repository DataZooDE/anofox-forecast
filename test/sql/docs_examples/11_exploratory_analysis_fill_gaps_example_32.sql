-- Create sample sales data with gaps
CREATE TABLE sales_raw AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20) AS sales_amount
FROM generate_series(0, 364) t(d)
CROSS JOIN (VALUES ('P001'), ('P002')) products(product_id)
WHERE d % 3 != 0;  -- Create gaps by skipping some days

-- DATE/TIMESTAMP columns: Use VARCHAR frequency strings
-- Fill gaps with daily frequency (default)
CREATE TABLE fixed AS
SELECT * FROM anofox_fcst_ts_fill_gaps('sales_raw', product_id, date, sales_amount, '1d');

-- Create hourly data with gaps
CREATE TABLE hourly_data AS
SELECT 
    series_id,
    TIMESTAMP '2024-01-01 00:00:00' + INTERVAL (h) HOUR AS timestamp,
    50 + 20 * SIN(2 * PI() * h / 24) + (RANDOM() * 10) AS value
FROM generate_series(0, 167) t(h)  -- 7 days
CROSS JOIN (VALUES (1), (2)) series(series_id)
WHERE h % 2 != 0;  -- Create gaps

-- Fill gaps with 30-minute frequency
SELECT * FROM anofox_fcst_ts_fill_gaps('hourly_data', series_id, timestamp, value, '30m');

-- Create weekly data
CREATE TABLE weekly_data AS
SELECT 
    series_id,
    DATE '2024-01-01' + INTERVAL (w * 7) DAY AS date,
    200 + 50 * SIN(2 * PI() * w / 52) + (RANDOM() * 30) AS value
FROM generate_series(0, 51) t(w)
CROSS JOIN (VALUES (1), (2)) series(series_id)
WHERE w % 2 != 0;  -- Create gaps

-- Fill gaps with weekly frequency
SELECT * FROM anofox_fcst_ts_fill_gaps('weekly_data', series_id, date, value, '1w');

-- Use NULL (must cast to VARCHAR for DATE/TIMESTAMP columns)
CREATE TABLE daily_data AS
SELECT 
    series_id,
    DATE '2024-01-01' + INTERVAL (d) DAY AS date,
    100 + (RANDOM() * 20) AS value
FROM generate_series(0, 30) t(d)
CROSS JOIN (VALUES (1)) series(series_id)
WHERE d % 2 != 0;

SELECT * FROM anofox_fcst_ts_fill_gaps('daily_data', series_id, date, value, NULL::VARCHAR);

-- INTEGER columns: Use INTEGER frequency values
-- Create integer-based time series
CREATE TABLE int_data AS
SELECT 
    series_id,
    d AS date_col,
    100 + 10 * SIN(2 * PI() * d / 10) + (RANDOM() * 5) AS value
FROM generate_series(1, 100) t(d)
CROSS JOIN (VALUES (1), (2)) series(series_id)
WHERE d % 3 != 0;  -- Create gaps

-- Fill gaps with step size of 1
SELECT * FROM anofox_fcst_ts_fill_gaps('int_data', series_id, date_col, value, 1);

-- Fill gaps with step size of 2
SELECT * FROM anofox_fcst_ts_fill_gaps('int_data', series_id, date_col, value, 2);

-- Use NULL (defaults to step size 1 for INTEGER columns)
SELECT * FROM anofox_fcst_ts_fill_gaps('int_data', series_id, date_col, value, NULL);
