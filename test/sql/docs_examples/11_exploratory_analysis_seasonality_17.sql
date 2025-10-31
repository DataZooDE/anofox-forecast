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
SELECT * FROM TS_FORECAST_BY('sales_weekly', product_id, week, weekly_sales,
                             'AutoETS', 12, {'seasonal_period': 4});  -- 4 weeks = monthly
