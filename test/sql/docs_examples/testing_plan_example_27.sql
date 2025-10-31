-- Test with 10,000 data points
SELECT TS_FORECAST(value, 'Naive', 100, NULL)
FROM generate_series(1, 10000) t(idx)
CROSS JOIN (VALUES (random() * 100)) v(value);
