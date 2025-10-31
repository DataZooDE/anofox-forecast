-- Use fast models with GROUP BY parallelization
SELECT 
    product_id,
    TS_FORECAST(date, sales, 'Theta', 30, MAP{'seasonal_period': 7}) AS forecast
FROM sales_history
WHERE date >= CURRENT_DATE - INTERVAL 90 DAY
GROUP BY product_id;
