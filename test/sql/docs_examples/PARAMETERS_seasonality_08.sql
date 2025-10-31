-- Default smoothing
SELECT TS_FORECAST(date, value, 'SES', 10, MAP{}) AS forecast
FROM stationary_data;

-- High responsiveness (alpha = 0.8)
SELECT TS_FORECAST(date, value, 'SES', 10, MAP{'alpha': 0.8}) AS forecast
FROM volatile_data;

-- Low responsiveness (alpha = 0.1)
SELECT TS_FORECAST(date, value, 'SES', 10, MAP{'alpha': 0.1}) AS forecast
FROM stable_data;
