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

-- All-in-one preparation (if standard pipeline was implemented)
-- Step 1: Fill time gaps
CREATE TEMP TABLE step1 AS
SELECT 
    group_col AS product_id,
    date_col AS date,
    value_col AS sales_amount
FROM anofox_fcst_ts_fill_gaps('sales_raw', product_id, date, sales_amount, '1d');

-- Step 2: Drop constant series
CREATE TEMP TABLE step2 AS
SELECT * FROM anofox_fcst_ts_drop_constant('step1', product_id, sales_amount);

-- Step 3: Drop short series
CREATE TEMP TABLE step3 AS
SELECT * FROM anofox_fcst_ts_drop_short('step2', product_id, 30);

-- Step 4: Remove leading zeros
CREATE TEMP TABLE step4 AS
SELECT * FROM anofox_fcst_ts_drop_leading_zeros('step3', product_id, date, sales_amount);

-- Step 5: Fill remaining nulls
CREATE TABLE sales_prepared AS
SELECT * FROM anofox_fcst_ts_fill_nulls_forward('step4', product_id, date, sales_amount);
