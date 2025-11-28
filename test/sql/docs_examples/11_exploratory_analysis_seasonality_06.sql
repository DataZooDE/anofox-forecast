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
FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount, '1d');

-- Step 2: Drop constant series
CREATE TEMP TABLE step2 AS
SELECT * FROM TS_DROP_CONSTANT('step1', product_id, sales_amount);

-- Step 3: Drop short series
CREATE TEMP TABLE step3 AS
SELECT * FROM TS_DROP_SHORT('step2', product_id, 30);

-- Step 4: Remove leading zeros
CREATE TEMP TABLE step4 AS
SELECT * FROM TS_DROP_LEADING_ZEROS('step3', product_id, date, sales_amount);

-- Step 5: Fill remaining nulls
CREATE TABLE sales_prepared AS
SELECT * FROM TS_FILL_NULLS_FORWARD('step4', product_id, date, sales_amount);
