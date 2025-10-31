-- Generate stats for prepared data
CREATE TABLE prepared_stats AS
SELECT * FROM TS_STATS('sales_prepared', product_id, date, sales_amount);

-- Compare quality
SELECT 
    'Raw data' AS stage,
    COUNT(*) AS num_series,
    ROUND(AVG(quality_score), 4) AS avg_quality,
    SUM(CASE WHEN n_null > 0 THEN 1 ELSE 0 END) AS series_with_nulls,
    SUM(CASE WHEN n_gaps > 0 THEN 1 ELSE 0 END) AS series_with_gaps,
    SUM(CASE WHEN is_constant THEN 1 ELSE 0 END) AS constant_series
FROM sales_stats
UNION ALL
SELECT 
    'Prepared',
    COUNT(*),
    ROUND(AVG(quality_score), 4),
    SUM(CASE WHEN n_null > 0 THEN 1 ELSE 0 END),
    SUM(CASE WHEN n_gaps > 0 THEN 1 ELSE 0 END),
    SUM(CASE WHEN is_constant THEN 1 ELSE 0 END)
FROM prepared_stats;
