CREATE TEMP TABLE filtered_sales AS
SELECT * FROM sales WHERE product_id IN ('P001', 'P002', 'P003');

SELECT * FROM TS_FORECAST_BY('filtered_sales', product_id, date, amount, 'AutoETS', 28, {...});
