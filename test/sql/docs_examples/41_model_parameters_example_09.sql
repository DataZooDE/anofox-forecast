SELECT TS_FORECAST(date, value, 'SESOptimized', 10, MAP{}) AS forecast
FROM data;
