-- Check calibration per product
SELECT 
    product_id,
    ROUND(TS_COVERAGE(LIST(actual), LIST(lower), LIST(upper)) * 100, 1) AS coverage_pct,
    confidence_level * 100 AS expected_coverage_pct,
    CASE 
        WHEN ABS(TS_COVERAGE(...) - confidence_level) < 0.05 
        THEN '✅ Well calibrated'
        WHEN TS_COVERAGE(...) < confidence_level - 0.10
        THEN '❌ Under-coverage (intervals too narrow)'
        ELSE '⚠️ Over-coverage (intervals too wide)'
    END AS calibration
FROM results
GROUP BY product_id, confidence_level;
