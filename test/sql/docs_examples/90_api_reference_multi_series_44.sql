-- For complex pipelines
CREATE TABLE sales_prep AS SELECT * FROM TS_FILL_GAPS(...);
CREATE TABLE forecasts AS SELECT * FROM TS_FORECAST_BY('sales_prep', ...);
