-- Default parameters
SELECT TS_FORECAST(date, value, 'Holt', 12, MAP{}) AS forecast
FROM trending_data;

-- Strong trend tracking
SELECT TS_FORECAST(date, value, 'Holt', 12, 
       MAP{'alpha': 0.8, 'beta': 0.3}) AS forecast
FROM accelerating_data;

-- Smooth trend
SELECT TS_FORECAST(date, value, 'Holt', 12, 
       MAP{'alpha': 0.2, 'beta': 0.05}) AS forecast
FROM stable_trend_data;
