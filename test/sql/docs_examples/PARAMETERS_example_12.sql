SELECT TS_FORECAST(date, value, 'SeasonalWindowAverage', 14, 
       MAP{'seasonal_period': 7, 'window': 3}) AS forecast
FROM noisy_seasonal_data;
