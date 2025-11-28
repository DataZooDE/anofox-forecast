-- Create test data with many groups
CREATE TABLE large_multi_series AS
SELECT 
    (i / 100)::INTEGER AS series_id,
    DATE '2023-01-01' + INTERVAL (i % 100) DAY AS date,
    RANDOM() * 100 AS value
FROM generate_series(1, 10000) t(i);

-- Test with many groups
SELECT 
    series_id,
    *
FROM TS_FORECAST_BY(
    'large_multi_series',
    series_id,
    date,
    value,
    'Naive',
    10,
    MAP{}
)
LIMIT 10;
