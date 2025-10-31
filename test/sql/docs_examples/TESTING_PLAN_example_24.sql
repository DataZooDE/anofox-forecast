SELECT TS_FORECAST(value, 'Naive', 5, NULL)
FROM (VALUES (NULL), (NULL), (NULL)) t(value);
-- Expected: Appropriate error
