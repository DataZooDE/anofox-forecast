-- Test with return_insample = true
SELECT TS_FORECAST(
    value,
    'Naive',
    5,
    {'return_insample': true}
) AS result
FROM (VALUES (100.0), (102.0), (105.0), (103.0)) t(value);

-- Verify insample_fitted has correct length (training data length)
-- Verify fitted values make sense for the model
