SELECT TS_FORECAST(value, 'Naive', 5, NULL)
FROM (VALUES (100.0)) t(value);
-- Expected: Appropriate error or default behavior
