-- Nightly ETL job
BEGIN TRANSACTION;

-- 1. Refresh sales data (incremental load)
INSERT INTO sales SELECT * FROM staging_sales;

-- 2. Update statistics
CREATE OR REPLACE TABLE sales_stats AS
SELECT * FROM TS_STATS('sales', product_id, date, amount);

-- 3. Generate forecasts (only active products)
CREATE OR REPLACE TABLE forecasts AS
SELECT * FROM TS_FORECAST_BY(
    (SELECT * FROM sales WHERE product_id IN (SELECT series_id FROM sales_stats WHERE quality_score >= 0.6)),
    product_id, date, amount, 'AutoETS', 28, {'seasonal_period': 7}
);

-- 4. Export for consumption
COPY forecasts TO 'forecast_output.parquet' (FORMAT PARQUET);

COMMIT;
