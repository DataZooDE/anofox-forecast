SELECT TS_FORECAST(date, value, 'SeasonalESOptimized', 12, 
       MAP{'seasonal_period': 12}) AS forecast
FROM monthly_data;
