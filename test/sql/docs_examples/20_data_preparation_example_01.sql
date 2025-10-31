-- Load EDA and Data Preparation macros
.read sql/eda_time_series.sql
.read sql/data_preparation.sql

-- 1. Analyze your data
CREATE TABLE series_stats AS
SELECT * FROM TS_STATS('sales_data', product_id, date, amount);

-- 2. Generate quality report
SELECT * FROM TS_QUALITY_REPORT('series_stats', 30);

-- 3. Prepare your data
CREATE TABLE sales_prepared AS
SELECT * FROM TS_PREPARE_STANDARD(
    'sales_data', product_id, date, amount,
    30,  -- min_length
    'forward'  -- fill_method
);
