-- ❌ WRONG - This won't work (metrics need arrays)
SELECT product_id, TS_MAE(actual, predicted)
FROM results
GROUP BY product_id;

-- ✅ CORRECT - Use LIST() to create arrays
SELECT 
    product_id,
    TS_MAE(LIST(actual), LIST(predicted)) AS mae
FROM results
GROUP BY product_id;
