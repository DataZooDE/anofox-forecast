-- Analyze your data before forecasting
SELECT * FROM TS_STATS('my_sales', product_id, date, sales);
SELECT * FROM TS_QUALITY_REPORT('stats', 30);
