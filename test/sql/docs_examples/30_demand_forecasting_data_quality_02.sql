-- Generate quality report
CREATE TABLE quality_stats AS
SELECT * FROM TS_STATS('product_sales', sku, sale_date, quantity_sold);

-- How many products have issues?
SELECT 
    COUNT(*) AS total_products,
    SUM(CASE WHEN quality_score < 0.7 THEN 1 ELSE 0 END) AS low_quality,
    ROUND(100.0 * SUM(CASE WHEN quality_score < 0.7 THEN 1 ELSE 0 END) / COUNT(*), 1) || '%' AS pct_low_quality
FROM quality_stats;

-- Identify problematic products
SELECT sku, quality_score, n_gaps, n_null
FROM quality_stats
WHERE quality_score < 0.7
ORDER BY quality_score
LIMIT 10;
