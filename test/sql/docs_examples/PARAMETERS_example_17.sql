SELECT TS_FORECAST(date, value, 'DynamicTheta', 12, 
       MAP{'seasonal_period': 7, 'theta': 2.5}) AS forecast
FROM dynamic_data;
