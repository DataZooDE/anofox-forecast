SELECT TS_FORECAST(date, value, 'AutoMSTL', 30, 
       MAP{'seasonal_periods': [7, 365]}) AS forecast
FROM daily_data;
