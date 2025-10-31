-- Default confidence level (0.90 = 90%)
SELECT TS_FORECAST(date, value, 'Theta', 12, MAP{})
FROM data;

-- Custom confidence level (0.95 = 95%)
SELECT TS_FORECAST(date, value, 'AutoETS', 12, MAP{'confidence_level': 0.95})
FROM data;

-- Narrow intervals for conservative estimates (0.80 = 80%)
SELECT TS_FORECAST(date, value, 'Theta', 12, MAP{'confidence_level': 0.80})
FROM data;

-- Disable timestamp generation for maximum speed
SELECT TS_FORECAST(date, value, 'Naive', 12, MAP{'generate_timestamps': false})
FROM data;

-- Combine multiple global parameters
SELECT TS_FORECAST(date, value, 'AutoETS', 12, 
       MAP{'confidence_level': 0.95, 'generate_timestamps': true, 'season_length': 7})
FROM data;
