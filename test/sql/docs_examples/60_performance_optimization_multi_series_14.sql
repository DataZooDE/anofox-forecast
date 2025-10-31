CREATE TABLE sales_prepared AS
SELECT * FROM TS_FILL_GAPS('sales', product_id, date, amount);

CREATE TABLE forecasts AS
SELECT * FROM TS_FORECAST_BY('sales_prepared', product_id, date, amount, 'AutoETS', 28, {...});
