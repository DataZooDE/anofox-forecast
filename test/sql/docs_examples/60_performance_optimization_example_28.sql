-- Only forecast last 90 days of each series (faster)
CREATE TABLE recent_sales AS
SELECT * FROM sales
WHERE date >= CURRENT_DATE - INTERVAL '90 days';

SELECT * FROM TS_FORECAST_BY('recent_sales', product_id, date, amount, 'AutoETS', 28, {...});
