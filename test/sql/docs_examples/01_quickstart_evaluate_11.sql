-- 1. Check data quality
SELECT * FROM TS_STATS('my_sales', product_id, date, sales);

-- 2. Detect seasonality
SELECT * FROM TS_DETECT_SEASONALITY_ALL('my_sales', product_id, date, sales);

-- 3. Try different models
-- See guides/11_model_selection.md
