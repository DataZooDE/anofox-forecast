-- Remove seasonality for trend analysis
WITH seasonality AS (
    SELECT * FROM TS_ANALYZE_SEASONALITY(
        LIST(date ORDER BY date),
        LIST(sales_amount ORDER BY date)
    )
    FROM sales
    WHERE product_id = 'P001'
)
-- Future: seasonal_component would be extracted
-- Current: Use models that handle seasonality (ETS, TBATS)
SELECT 'Use seasonal models like ETS or AutoETS' AS recommendation;
