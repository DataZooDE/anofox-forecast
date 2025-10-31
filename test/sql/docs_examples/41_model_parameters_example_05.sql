-- Use default window (5)
SELECT TS_FORECAST(date, value, 'SMA', 7, MAP{}) AS forecast
FROM time_series_data;

-- Custom window
SELECT TS_FORECAST(date, value, 'SMA', 7, MAP{'window': 10}) AS forecast
FROM time_series_data;

-- Short-term smoothing
SELECT TS_FORECAST(date, value, 'SMA', 7, MAP{'window': 3}) AS forecast
FROM time_series_data;
