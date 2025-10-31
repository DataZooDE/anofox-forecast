-- Default smoothing
SELECT TS_FORECAST(date, demand, 'TSB', 12, MAP{}) AS forecast
FROM intermittent_data;

-- Custom smoothing parameters
SELECT TS_FORECAST(date, demand, 'TSB', 12, 
       MAP{'alpha_d': 0.2, 'alpha_p': 0.15}) AS forecast
FROM lumpy_data;
