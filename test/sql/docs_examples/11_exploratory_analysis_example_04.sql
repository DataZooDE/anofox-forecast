-- Find series with quality_score < 0.7
SELECT * FROM TS_GET_PROBLEMATIC('sales_stats', 0.7);
