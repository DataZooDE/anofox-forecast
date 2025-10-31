-- Compare two forecasting methods per product
SELECT 
    product_id,
    TS_RMAE(LIST(actual), LIST(method1_pred), LIST(method2_pred)) AS rmae,
    CASE 
        WHEN TS_RMAE(LIST(actual), LIST(method1_pred), LIST(method2_pred)) < 1.0
        THEN 'Method 1 is better'
        ELSE 'Method 2 is better'
    END AS winner
FROM method_comparison
GROUP BY product_id;
