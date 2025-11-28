-- Create sample raw data
CREATE TABLE sales_raw AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN RANDOM() < 0.05 THEN NULL
        ELSE 100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20)
    END AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

-- Create stats for raw data
CREATE TABLE sales_stats AS
SELECT * FROM TS_STATS('sales_raw', product_id, date, sales_amount, '1d');

-- Prepare data (fill gaps, drop constants, etc.)
CREATE TABLE sales_prepared AS
SELECT 
    group_col AS product_id,
    date_col AS date,
    value_col AS sales_amount
FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount, '1d');

-- Generate stats for prepared data
CREATE TABLE prepared_stats AS
SELECT * FROM TS_STATS('sales_prepared', product_id, date, sales_amount, '1d');

-- Compare quality
SELECT 
    'Raw data' AS stage,
    COUNT(*) AS num_series,
    ROUND(AVG(CAST(length AS DOUBLE)), 2) AS avg_length,
    SUM(CASE WHEN n_null > 0 THEN 1 ELSE 0 END) AS series_with_nulls,
    SUM(CASE WHEN expected_length > length THEN 1 ELSE 0 END) AS series_with_gaps,
    SUM(CASE WHEN is_constant THEN 1 ELSE 0 END) AS constant_series
FROM sales_stats
UNION ALL
SELECT 
    'Prepared',
    COUNT(*),
    ROUND(AVG(CAST(length AS DOUBLE)), 2),
    SUM(CASE WHEN n_null > 0 THEN 1 ELSE 0 END),
    SUM(CASE WHEN expected_length > length THEN 1 ELSE 0 END),
    SUM(CASE WHEN is_constant THEN 1 ELSE 0 END)
FROM prepared_stats;
