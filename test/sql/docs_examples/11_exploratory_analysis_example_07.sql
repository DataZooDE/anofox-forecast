CREATE TABLE sales_custom_prep AS
WITH 
-- Fill gaps first
filled AS (
    SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount)
),
-- Drop problematic series
filtered AS (
    SELECT f.*
    FROM filled f
    WHERE f.product_id NOT IN (
    )
),
-- Remove edge zeros
no_edges AS (
    SELECT * FROM TS_DROP_EDGE_ZEROS('filtered', product_id, date, sales_amount)
),
-- Fill nulls with interpolation (more sophisticated)
interpolated AS (
    SELECT 
        product_id,
        date,
        -- Linear interpolation
        COALESCE(sales_amount,
            AVG(sales_amount) OVER (
                PARTITION BY product_id 
                ORDER BY date 
                ROWS BETWEEN 3 PRECEDING AND 3 FOLLOWING
            )
        ) AS sales_amount
    FROM no_edges
)
SELECT * FROM interpolated;
