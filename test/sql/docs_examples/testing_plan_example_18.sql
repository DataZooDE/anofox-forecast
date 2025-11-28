-- Create sample large dataset
CREATE TABLE large_dataset AS
SELECT 
    series_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + series_id * 10 + (RANDOM() * 10) AS value
FROM generate_series(0, 30) t(d)
CROSS JOIN (VALUES (1), (2), (3), (4), (5)) series(series_id);

-- Test forecasting with many groups
SELECT series_id, * 
FROM TS_FORECAST_BY(
    'large_dataset',
    series_id,
    date,
    value,
    'Naive',
    12,
    MAP{}
);
-- Verify all groups get forecasts
