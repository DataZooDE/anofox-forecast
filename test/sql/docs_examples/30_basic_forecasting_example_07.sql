-- Check if 95% intervals actually cover 95% of actuals
SELECT 
    product_id,
    ROUND(TS_COVERAGE(LIST(actual), LIST(lower), LIST(upper)) * 100, 1) AS coverage_pct
FROM results
GROUP BY product_id;

-- Target: ~95% for well-calibrated 95% CI
