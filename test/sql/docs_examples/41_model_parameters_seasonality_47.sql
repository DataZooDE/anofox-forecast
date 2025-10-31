-- Intermittent spare parts demand
SELECT 
    part_number,
    TS_FORECAST(date, demand, 'CrostonSBA', 90, MAP{}) AS forecast
FROM parts_demand
GROUP BY part_number;
