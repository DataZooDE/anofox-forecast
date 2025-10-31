CREATE TABLE stats AS
SELECT * FROM TS_STATS('sales', product_id, date, sales_amount);

-- View summary
SELECT 
    COUNT(*) AS total_series,
    ROUND(AVG(quality_score), 4) AS avg_quality,
    SUM(CASE WHEN quality_score < 0.5 THEN 1 ELSE 0 END) AS low_quality_count
FROM stats;
