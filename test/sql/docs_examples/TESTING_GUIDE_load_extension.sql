   query I
   SELECT len(result.forecast) = 12
   FROM (SELECT TS_FORECAST(value, 'Naive', 12, NULL) AS result FROM test_data);
   ----
   true
