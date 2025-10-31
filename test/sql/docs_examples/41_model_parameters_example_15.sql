-- Non-seasonal Theta
SELECT TS_FORECAST(date, value, 'Theta', 12, MAP{}) AS forecast
FROM non_seasonal_data;

-- Seasonal Theta
SELECT TS_FORECAST(date, value, 'Theta', 12, 
       MAP{'seasonal_period': 12, 'theta': 2.0}) AS forecast
FROM seasonal_data;

-- Custom theta parameter
SELECT TS_FORECAST(date, value, 'Theta', 12, 
       MAP{'theta': 1.5}) AS forecast
FROM data;
