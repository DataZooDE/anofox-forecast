SELECT TS_FORECAST(date, demand, 'IMAPA', 12, MAP{}) AS forecast
FROM complex_intermittent_data;
