-- Create sample sales data
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20) AS sales_amount
FROM generate_series(0, 300) t(d)  -- Data until Oct 2023
CROSS JOIN (VALUES ('P001'), ('P002')) products(product_id);

-- DATE/TIMESTAMP columns: Use VARCHAR frequency strings
-- Create hourly data
CREATE TABLE hourly_data AS
SELECT 
    series_id,
    TIMESTAMP '2024-01-01 00:00:00' + INTERVAL (h) HOUR AS timestamp,
    50 + 20 * SIN(2 * PI() * h / 24) + (RANDOM() * 10) AS value
FROM generate_series(0, 100) t(h)
CROSS JOIN (VALUES (1), (2)) series(series_id);

-- Extend hourly series to target date
SELECT * FROM anofox_fcst_ts_fill_forward('hourly_data', series_id, timestamp, value, '2024-12-31'::TIMESTAMP, '1h');

-- Create monthly data
CREATE TABLE monthly_data AS
SELECT 
    series_id,
    DATE '2024-01-01' + INTERVAL (m) MONTH AS date,
    200 + 50 * SIN(2 * PI() * m / 12) + (RANDOM() * 30) AS value
FROM generate_series(0, 10) t(m)
CROSS JOIN (VALUES (1), (2)) series(series_id);

-- Extend monthly series to target date
SELECT * FROM anofox_fcst_ts_fill_forward('monthly_data', series_id, date, value, '2024-12-01'::DATE, '1mo');

-- Extend daily series to target date (default frequency)
CREATE TABLE sales_extended AS
SELECT * FROM anofox_fcst_ts_fill_forward(
    'sales', product_id, date, sales_amount, 
    DATE '2023-12-31', '1d'
);

-- Use NULL (must cast to VARCHAR for DATE/TIMESTAMP columns)
CREATE TABLE daily_data AS
SELECT 
    series_id,
    DATE '2024-01-01' + INTERVAL (d) DAY AS date,
    100 + (RANDOM() * 20) AS value
FROM generate_series(0, 30) t(d)
CROSS JOIN (VALUES (1)) series(series_id);

SELECT * FROM anofox_fcst_ts_fill_forward('daily_data', series_id, date, value, '2024-12-31'::DATE, NULL::VARCHAR);

-- INTEGER columns: Use INTEGER frequency values
-- Create integer-based time series
CREATE TABLE int_data AS
SELECT 
    series_id,
    d AS date_col,
    100 + 10 * SIN(2 * PI() * d / 10) + (RANDOM() * 5) AS value
FROM generate_series(1, 50) t(d)
CROSS JOIN (VALUES (1), (2)) series(series_id);

-- Extend series to index 100 with step size of 1
SELECT * FROM anofox_fcst_ts_fill_forward('int_data', series_id, date_col, value, 100, 1);

-- Extend series to index 100 with step size of 5
SELECT * FROM anofox_fcst_ts_fill_forward('int_data', series_id, date_col, value, 100, 5);

-- Use NULL (defaults to step size 1 for INTEGER columns)
SELECT * FROM anofox_fcst_ts_fill_forward('int_data', series_id, date_col, value, 100, NULL);
