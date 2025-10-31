-- These should fail gracefully with clear error message
SELECT TS_FORECAST(value, 'ARIMA', 5, {'p': 1, 'd': 1, 'q': 1});
-- Expected error: "Unknown model: 'ARIMA'"

SELECT TS_FORECAST(value, 'AutoARIMA', 5, NULL);
-- Expected error: "Unknown model: 'AutoARIMA'"

-- Verify ARIMA models don't appear in supported list
SELECT * FROM ts_list_models() WHERE model_name LIKE '%ARIMA%';
-- Expected: 0 rows

-- Verify other models still work
SELECT TS_FORECAST(value, 'Naive', 5, NULL);
SELECT TS_FORECAST(value, 'AutoETS', 5, {'seasonal_period': 12});
-- Expected: Success
