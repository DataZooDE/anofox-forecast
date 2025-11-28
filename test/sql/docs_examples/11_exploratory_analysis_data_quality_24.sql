-- Create sample stats table
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + product_id * 20 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

CREATE TABLE stats AS
SELECT * FROM TS_STATS('sales', product_id, date, sales_amount, '1d');

-- Create sample raw sales data
CREATE TABLE sales_raw AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN RANDOM() < 0.05 THEN NULL
        ELSE 100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20)
    END AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

-- Create a reusable preparation view
CREATE VIEW sales_autoprepared AS
WITH stats AS (
    SELECT * FROM TS_STATS('sales_raw', product_id, date, sales_amount, '1d')
),
quality_series AS (
    SELECT series_id FROM stats WHERE length >= 30  -- Keep series with at least 30 observations
),
filled_temp AS (
    SELECT 
        group_col,
        date_col,
        value_col
    FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount, '1d')
),
filled_temp2 AS (
    SELECT f.*
    FROM filled_temp f
    WHERE f.group_col::VARCHAR IN (SELECT series_id::VARCHAR FROM quality_series)
),
no_constant_temp AS (
    SELECT 
        group_col,
        date_col,
        value_col
    FROM TS_DROP_CONSTANT('filled_temp2', group_col, value_col)
),
no_constant AS (
    SELECT 
        group_col AS product_id,
        date_col AS date,
        value_col AS sales_amount
    FROM no_constant_temp
),
complete_temp AS (
    SELECT * FROM TS_FILL_NULLS_FORWARD('no_constant', product_id, date, sales_amount)
),
complete AS (
    SELECT 
        product_id,
        date,
        value_col AS sales_amount
    FROM complete_temp
)
SELECT * FROM complete;

-- Use in forecasting
SELECT * FROM TS_FORECAST_BY('sales_autoprepared', product_id, date, sales_amount,
                             'AutoETS', 28, MAP{'seasonal_period': 7});
