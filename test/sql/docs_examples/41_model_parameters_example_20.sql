-- Non-seasonal AutoARIMA
SELECT TS_FORECAST(date, value, 'AutoARIMA', 12, MAP{}) AS forecast
FROM data;

-- Seasonal AutoARIMA
SELECT TS_FORECAST(date, value, 'AutoARIMA', 12, 
       MAP{'seasonal_period': 12}) AS forecast
FROM monthly_data;

-- Weekly seasonality
SELECT TS_FORECAST(date, value, 'AutoARIMA', 14, 
       MAP{'seasonal_period': 7}) AS forecast
FROM daily_data;
