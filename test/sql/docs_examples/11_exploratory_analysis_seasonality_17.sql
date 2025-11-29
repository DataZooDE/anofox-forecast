-- Create sample daily sales data
CREATE TABLE sales_daily AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + product_id * 20 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

-- Daily data too noisy? Aggregate to weekly
CREATE TABLE sales_weekly AS
SELECT 
    product_id,
    DATE_TRUNC('week', date) AS week,
    SUM(sales_amount) AS weekly_sales,
    AVG(sales_amount) AS avg_daily_sales,
    COUNT(*) AS days_in_week
FROM sales_daily
GROUP BY product_id, week;

-- Forecast on weekly data
SELECT * FROM anofox_fcst_ts_forecast_by('sales_weekly', product_id, week, weekly_sales,
                             'AutoETS', 12, MAP{'seasonal_period': 4});  -- 4 weeks = monthly
