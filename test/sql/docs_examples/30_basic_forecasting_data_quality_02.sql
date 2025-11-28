-- Create sample raw sales data
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

-- Fill time gaps
CREATE TABLE sales_filled AS
SELECT 
    group_col AS product_id,
    date_col AS date,
    value_col AS sales_amount
FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount, '1d');

-- Remove constant series
CREATE TABLE sales_clean AS
SELECT * FROM TS_DROP_CONSTANT('sales_filled', product_id, sales_amount);

-- Fill missing values
CREATE TABLE sales_complete AS
SELECT 
    product_id,
    date,
    value_col AS sales_amount
FROM TS_FILL_NULLS_FORWARD('sales_clean', product_id, date, sales_amount);
