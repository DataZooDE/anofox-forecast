-- ARIMA(1,1,1) - simple ARIMA
SELECT TS_FORECAST(date, value, 'ARIMA', 12, 
       MAP{'p': 1, 'd': 1, 'q': 1}) AS forecast
FROM data;

-- ARIMA(2,1,2) - higher order
SELECT TS_FORECAST(date, value, 'ARIMA', 12, 
       MAP{'p': 2, 'd': 1, 'q': 2}) AS forecast
FROM complex_data;

-- Seasonal ARIMA(1,1,1)(1,1,1)[12]
SELECT TS_FORECAST(date, value, 'ARIMA', 12, 
       MAP{'p': 1, 'd': 1, 'q': 1, 'P': 1, 'D': 1, 'Q': 1, 's': 12}) AS forecast
FROM monthly_seasonal_data;

-- AR(3) model
SELECT TS_FORECAST(date, value, 'ARIMA', 12, 
       MAP{'p': 3, 'd': 0, 'q': 0}) AS forecast
FROM autoregressive_data;

-- MA(2) model
SELECT TS_FORECAST(date, value, 'ARIMA', 12, 
       MAP{'p': 0, 'd': 0, 'q': 2}) AS forecast
FROM moving_average_data;
