-- Test with 1,000 groups
SELECT series_id, result.*
FROM ts_forecast_by(
    (SELECT 
        (i / 100)::INTEGER AS series_id,
        i % 100 AS idx,
        random() * 100 AS value
     FROM generate_series(1, 100000) t(i)),
    'idx',
    'value',
    'series_id',
    'Naive',
    10,
    {}
);
