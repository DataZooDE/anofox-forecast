SELECT TS_FORECAST(date, demand, 'CrostonOptimized', 12, MAP{}) AS forecast
FROM intermittent_data;
