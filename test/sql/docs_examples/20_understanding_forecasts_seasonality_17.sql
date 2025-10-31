SELECT 
    product_id,
    ROUND(TS_BIAS(LIST(actual), LIST(forecast)), 2) AS bias,
    CASE 
        WHEN TS_BIAS(...) > 5 THEN '⚠️ Over-forecasting'
        WHEN TS_BIAS(...) < -5 THEN '⚠️ Under-forecasting'
        ELSE '✅ Unbiased'
    END AS assessment
FROM results
GROUP BY product_id;
