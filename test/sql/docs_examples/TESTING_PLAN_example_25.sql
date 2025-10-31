-- Negative horizon
SELECT TS_FORECAST(value, 'Naive', -5, NULL);
-- Expected: Error

-- Invalid confidence level
SELECT TS_FORECAST(value, 'Naive', 5, {'confidence_level': 1.5});
-- Expected: Error

-- Invalid model name
SELECT TS_FORECAST(value, 'NonExistentModel', 5, NULL);
-- Expected: Clear error message
