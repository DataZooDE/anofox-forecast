SELECT TS_FORECAST(date, demand, 'CrostonClassic', 12, MAP{}) AS forecast
FROM sparse_demand_data
WHERE product_category = 'spare_parts';
