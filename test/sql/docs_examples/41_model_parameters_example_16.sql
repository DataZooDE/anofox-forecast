SELECT TS_FORECAST(date, value, 'OptimizedTheta', 12, 
       MAP{'seasonal_period': 12}) AS forecast
FROM data;
