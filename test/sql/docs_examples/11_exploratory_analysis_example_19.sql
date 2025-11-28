-- Create sample retail raw data
CREATE TABLE retail_raw AS
SELECT 
    store_id,
    sku,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + (ROW_NUMBER() OVER (PARTITION BY store_id, sku ORDER BY store_id, sku) % 4) * 10 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales_qty
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES ('S001'), ('S002')) stores(store_id)
CROSS JOIN (VALUES ('SKU001'), ('SKU002')) skus(sku);

-- Create series key column first
CREATE TEMP TABLE retail_with_key AS
SELECT 
    store_id || '_' || sku AS series_key,
    date,
    sales_qty
FROM retail_raw;

-- Typical retail data issues
CREATE TABLE retail_prepared AS
WITH 
-- Step 1: Fill gaps (stores closed on some days)
filled_temp AS (
    SELECT * FROM TS_FILL_GAPS('retail_with_key', series_key, date, sales_qty, '1d')
),
filled AS (
    SELECT 
        group_col AS series_key,
        date_col AS date,
        value_col AS sales_qty
    FROM filled_temp
),
-- Step 2: Separate back to store and SKU
parsed AS (
    SELECT 
        SPLIT_PART(series_key, '_', 1) AS store_id,
        SPLIT_PART(series_key, '_', 2) AS sku,
        date,
        sales_qty
    FROM filled
),
-- Step 3: Drop products with < 90 days history
sufficient_history AS (
    SELECT * FROM TS_DROP_SHORT('parsed', sku, 90)
),
-- Step 4: Fill nulls (missed scans)
filled_nulls_temp AS (
    SELECT * FROM TS_FILL_NULLS_CONST('sufficient_history', sku, date, sales_qty, 0.0)
),
filled_nulls AS (
    SELECT 
        sku,
        date,
        value_col AS sales_qty
    FROM filled_nulls_temp
),
-- Step 5: Remove products with no recent sales
active_products AS (
    SELECT * FROM TS_DROP_TRAILING_ZEROS('filled_nulls', sku, date, sales_qty)
)
SELECT * FROM active_products;
