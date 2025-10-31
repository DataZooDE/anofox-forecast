CREATE TEMP TABLE electronics AS
SELECT * FROM sales WHERE category = 'Electronics';

SELECT * FROM TS_FORECAST_BY('electronics', product_id, date, amount, 'AutoETS', 28, {...});
