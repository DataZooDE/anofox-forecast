-- Test default confidence level (0.90)
SELECT result.confidence_level 
FROM (
    SELECT TS_FORECAST(value, 'Naive', 5, NULL) AS result
    FROM test_data
);
-- Expected: 0.90

-- Test custom confidence level
SELECT result.confidence_level
FROM (
    SELECT TS_FORECAST(value, 'Naive', 5, {'confidence_level': 0.95}) AS result
    FROM test_data
);
-- Expected: 0.95
