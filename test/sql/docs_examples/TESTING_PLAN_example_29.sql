-- Old-style aggregate call (no new parameters)
SELECT TS_FORECAST(value, 'Naive', 5, NULL)
FROM test_data;
-- Expected: Success, insample_fitted is empty, confidence_level is 0.90

-- Old-style table macro call
SELECT * FROM ts_forecast(
    test_data,
    'date',
    'value',
    'Naive',
    5,
    {}
);
-- Expected: Success, new columns included
