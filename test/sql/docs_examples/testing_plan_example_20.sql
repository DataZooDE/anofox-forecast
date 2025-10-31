-- These should work
SELECT TS_FORECAST(value, 'ARIMA', 5, {'p': 1, 'd': 1, 'q': 1});
SELECT TS_FORECAST(value, 'AutoARIMA', 5, {'seasonal_period': 12});

-- Verify ARIMA models appear in supported list
SELECT * FROM ts_list_models() WHERE model_name LIKE '%ARIMA%';
