-- 1. Check data quality
SELECT * FROM TS_STATS('my_sales', product_id, date, sales);

-- 2. Detect seasonality
SELECT 
    product_id,
    TS_DETECT_SEASONALITY(LIST(sales ORDER BY date)) AS detected_periods
FROM my_sales
GROUP BY product_id;

-- 3. Try different models
-- See guides/11_model_selection.md
