SELECT TS_FORECAST(date, value, 'RandomWalkWithDrift', 10, MAP{}) AS forecast
FROM trending_data;
