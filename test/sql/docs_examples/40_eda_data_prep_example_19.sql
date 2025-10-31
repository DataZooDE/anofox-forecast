-- Typical retail data issues
CREATE TABLE retail_prepared AS
WITH 
-- Step 1: Fill gaps (stores closed on some days)
filled AS (
    SELECT * FROM TS_FILL_GAPS('retail_raw', store_id || '_' || sku AS series_key, 
                               date, sales_qty)
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
    SELECT * FROM TS_DROP_SHORT('parsed', sku, date, 90)
),
-- Step 4: Fill nulls (missed scans)
filled_nulls AS (
    SELECT * FROM TS_FILL_NULLS_CONST('sufficient_history', sku, date, sales_qty, 0.0)
),
-- Step 5: Remove products with no recent sales
active_products AS (
    SELECT * FROM TS_DROP_TRAILING_ZEROS('filled_nulls', sku, date, sales_qty)
)
SELECT * FROM active_products;
