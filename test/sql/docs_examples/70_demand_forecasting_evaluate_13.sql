-- Track forecast accuracy over time
CREATE VIEW forecast_accuracy_dashboard AS
WITH last_month_actuals AS (
    SELECT 
        sku,
        sale_date,
        quantity_sold AS actual
    FROM product_sales
    WHERE sale_date BETWEEN CURRENT_DATE - INTERVAL '30 days' AND CURRENT_DATE
),
last_month_forecasts AS (
    SELECT 
        sku,
        forecast_date AS sale_date,
        forecasted_quantity AS forecast,
        min_quantity_95ci AS lower,
        max_quantity_95ci AS upper
    FROM forecast_history  -- Historical forecasts
    WHERE forecast_date BETWEEN CURRENT_DATE - INTERVAL '30 days' AND CURRENT_DATE
)
SELECT 
    f.sku,
    COUNT(*) AS days_evaluated,
    ROUND(TS_MAE(LIST(a.actual), LIST(f.forecast)), 2) AS mae,
    ROUND(TS_MAPE(LIST(a.actual), LIST(f.forecast)), 2) AS mape_pct,
    ROUND(TS_COVERAGE(LIST(a.actual), LIST(f.lower), LIST(f.upper)) * 100, 1) AS coverage_pct,
    CASE 
        WHEN TS_MAPE(LIST(a.actual), LIST(f.forecast)) <= 15 THEN '🌟 Excellent'
        WHEN TS_MAPE(LIST(a.actual), LIST(f.forecast)) <= 25 THEN '✅ Good'
        ELSE '⚠️ Needs Improvement'
    END AS performance
FROM last_month_forecasts f
JOIN last_month_actuals a ON f.sku = a.sku AND f.sale_date = a.sale_date
GROUP BY f.sku
ORDER BY mape_pct;
