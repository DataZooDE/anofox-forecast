-- AutoETS provides best automatic results
SELECT 
    product_id,
    region,
    TS_FORECAST(date, sales, 'AutoETS', 30, MAP{'season_length': 7}) AS forecast
FROM sales_history
GROUP BY product_id, region;
