-- Prepare sales data
CREATE TABLE product_sales_clean AS
WITH stats AS (
    SELECT * FROM TS_STATS('product_sales_raw', product_id, date, revenue)
),
-- Keep only products with sufficient history
good_quality AS (
    SELECT series_id FROM stats WHERE quality_score >= 0.7 AND length >= 90
),
-- Fill gaps
filled AS (
    SELECT * FROM TS_FILL_GAPS('product_sales_raw', product_id, date, revenue)
    WHERE product_id IN (SELECT series_id FROM good_quality)
),
-- Handle nulls
complete AS (
    SELECT * FROM TS_FILL_NULLS_FORWARD('filled', product_id, date, revenue)
)
SELECT * FROM complete;
