-- Option A: Forward fill (use last known value)
SELECT * FROM TS_FILL_NULLS_FORWARD('sales', product_id, date, sales_amount);

-- Option B: Mean imputation
SELECT * FROM TS_FILL_NULLS_MEAN('sales', product_id, date, sales_amount);

-- Option C: Drop series with too many nulls
WITH clean AS (
    SELECT * FROM sales_stats WHERE n_null < length * 0.05  -- < 5% missing
)
SELECT s.*
FROM sales s
WHERE s.product_id IN (SELECT series_id FROM clean);
