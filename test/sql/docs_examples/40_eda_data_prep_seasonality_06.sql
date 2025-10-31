-- All-in-one preparation (if standard pipeline was implemented)
CREATE TABLE sales_prepared AS
WITH 
-- Step 1: Fill time gaps
step1 AS (
    SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount)
),
-- Step 2: Drop constant series
step2 AS (
    SELECT * FROM TS_DROP_CONSTANT('step1', product_id, sales_amount)
),
-- Step 3: Drop short series
step3 AS (
    SELECT * FROM TS_DROP_SHORT('step2', product_id, date, 30)
),
-- Step 4: Remove leading zeros
step4 AS (
    SELECT * FROM TS_DROP_LEADING_ZEROS('step3', product_id, date, sales_amount)
),
-- Step 5: Fill remaining nulls
step5 AS (
    SELECT * FROM TS_FILL_NULLS_FORWARD('step4', product_id, date, sales_amount)
)
SELECT * FROM step5;
