-- Hourly electricity with daily and weekly patterns
SELECT 
    region,
    TS_FORECAST(hour, kwh, 'MSTL', 168, MAP{'seasonal_periods': [24, 168]}) AS forecast
FROM energy_consumption
GROUP BY region;
