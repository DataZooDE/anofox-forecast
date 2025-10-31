-- Daily sales with weekly seasonality
SELECT 
    store_id,
    TS_FORECAST(date, sales, 'AutoETS', 30, MAP{'season_length': 7}) AS forecast
FROM daily_sales
GROUP BY store_id;
