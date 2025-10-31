-- Detect trend using correlation
WITH stats AS (
    SELECT * FROM TS_STATS('sales', product_id, date, amount)
)
SELECT 
    series_id,
    trend_corr,
    CASE 
        WHEN trend_corr > 0.3 THEN '📈 Strong upward trend'
        WHEN trend_corr < -0.3 THEN '📉 Strong downward trend'
        ELSE '↔️ No clear trend'
    END AS trend_direction
FROM stats;
