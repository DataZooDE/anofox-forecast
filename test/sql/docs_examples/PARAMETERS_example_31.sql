SELECT TS_FORECAST(date, demand, 'CrostonSBA', 12, MAP{}) AS forecast
FROM lumpy_demand_data;
