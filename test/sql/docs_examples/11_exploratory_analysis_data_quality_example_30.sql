-- Create sample sales data with gaps and missing values
CREATE TABLE sales_raw AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN RANDOM() < 0.05 THEN NULL  -- 5% missing
        WHEN RANDOM() < 0.10 THEN 0.0   -- 5% zeros
        ELSE 100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20)
    END AS sales_amount
FROM generate_series(0, 364) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

-- DATE/TIMESTAMP columns: Use VARCHAR frequency strings
-- Generate comprehensive health card (n_short parameter defaults to 30 if NULL)
CREATE TABLE health_card AS
SELECT * FROM anofox_fcst_ts_data_quality('sales_raw', product_id, date, sales_amount, 30, '1d');

-- View all issues
SELECT * FROM health_card ORDER BY dimension, metric;

-- Filter specific issues
SELECT * FROM anofox_fcst_ts_data_quality('sales_raw', product_id, date, sales_amount, 30, '1d')
WHERE dimension = 'Temporal' AND metric = 'timestamp_gaps'
LIMIT 5;

-- INTEGER columns: Use INTEGER frequency values
-- Create sample integer-based time series (convert to DATE for compatibility)
CREATE TABLE int_data AS
SELECT 
    series_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date_col,
    CASE 
        WHEN RANDOM() < 0.05 THEN NULL
        ELSE 100 + 10 * SIN(2 * PI() * d / 10) + (RANDOM() * 5)
    END AS value
FROM generate_series(1, 100) t(d)
CROSS JOIN (VALUES (1), (2), (3)) series(series_id);

SELECT * FROM anofox_fcst_ts_data_quality('int_data', series_id, date_col, value, 30, '1d')
WHERE dimension = 'Magnitude' AND metric = 'missing_values'
LIMIT 5;
