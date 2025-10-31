SELECT TS_FORECAST(date, value, 'DynamicOptimizedTheta', 12, 
       MAP{'seasonal_period': 12}) AS forecast
FROM complex_data;
