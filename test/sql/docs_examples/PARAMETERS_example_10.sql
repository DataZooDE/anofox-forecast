SELECT TS_FORECAST(date, sales, 'SeasonalES', 12, 
       MAP{'seasonal_period': 7, 'alpha': 0.3, 'gamma': 0.2}) AS forecast
FROM weekly_sales;
