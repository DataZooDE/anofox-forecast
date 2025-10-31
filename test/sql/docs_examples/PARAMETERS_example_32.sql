SELECT TS_FORECAST(date, demand, 'ADIDA', 12, MAP{}) AS forecast
FROM very_sparse_data;
