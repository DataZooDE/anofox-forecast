SELECT TS_FORECAST(date, value, 'AutoMFLES', 30, 
       MAP{'seasonal_periods': [7, 365]}) AS forecast
FROM daily_data;
