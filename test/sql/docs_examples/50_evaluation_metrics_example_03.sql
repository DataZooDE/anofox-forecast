-- Create sample method comparison data with actuals and predictions
CREATE TABLE method_comparison AS
SELECT 
    product_id,
    forecast_step,
    100.0 + forecast_step * 2.0 + (RANDOM() * 5) AS actual,
    100.0 + forecast_step * 2.1 AS method1_pred,
    100.0 + forecast_step * 2.2 AS method2_pred
FROM generate_series(1, 10) t(forecast_step)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

-- Compare two forecasting methods per product
SELECT 
    product_id,
    TS_RMAE(LIST(actual ORDER BY forecast_step), LIST(method1_pred ORDER BY forecast_step), LIST(method2_pred ORDER BY forecast_step)) AS rmae,
    CASE 
        WHEN TS_RMAE(LIST(actual ORDER BY forecast_step), LIST(method1_pred ORDER BY forecast_step), LIST(method2_pred ORDER BY forecast_step)) < 1.0
        THEN 'Method 1 is better'
        ELSE 'Method 2 is better'
    END AS winner
FROM method_comparison
GROUP BY product_id;
