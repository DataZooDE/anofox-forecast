SELECT TS_FORECAST(value, 'Naive', 5, NULL) 
FROM (SELECT NULL::DOUBLE AS value WHERE FALSE);
-- Expected: Appropriate error
