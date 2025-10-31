SELECT TS_FORECAST(date, value, 'Naive', 7, MAP{}) AS forecast
FROM time_series_data;
