-- Create sample sales data
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

-- Get quality stats
SELECT * FROM TS_STATS('sales', product_id, date, amount, '1d')
LIMIT 5;

-- Prepare data
CREATE TABLE sales_prepared AS
WITH filled AS (
    SELECT 
        group_col AS product_id,
        date_col AS date,
        value_col AS amount
    FROM TS_FILL_GAPS('sales', product_id, date, amount, '1d')
)
SELECT * FROM TS_DROP_CONSTANT('filled', product_id, amount);

-- Generate forecast
SELECT * FROM TS_FORECAST_BY('sales_prepared', product_id, date, amount,
                             'AutoETS', 28, MAP{'seasonal_period': 7})
LIMIT 5;
