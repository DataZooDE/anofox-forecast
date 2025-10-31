-- Translate forecast accuracy to business metrics
WITH accuracy AS (
    SELECT 
        sku,
        TS_MAPE(LIST(actual), LIST(forecast)) AS mape,
        TS_COVERAGE(LIST(actual), LIST(lower), LIST(upper)) AS coverage
    FROM validation_results
    GROUP BY sku
),
business_impact AS (
    SELECT 
        sku,
        ROUND(mape, 2) AS mape_pct,
        ROUND(coverage * 100, 1) AS coverage_pct,
        CASE 
            WHEN mape <= 10 THEN '🌟 Excellent (< 10% error)'
            WHEN mape <= 20 THEN '✅ Good (10-20% error)'
            WHEN mape <= 30 THEN '⚠️ Acceptable (20-30% error)'
            ELSE '❌ Poor (> 30% error)'
        END AS accuracy_grade,
        CASE 
            WHEN mape <= 10 THEN 'Optimize inventory further'
            WHEN mape <= 20 THEN 'Standard inventory policies work'
            WHEN mape <= 30 THEN 'Increase safety stock'
            ELSE 'Review forecasting approach'
        END AS recommendation
    FROM accuracy
)
SELECT * FROM business_impact ORDER BY mape_pct;
