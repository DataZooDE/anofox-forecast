-- Create sample time series data
CREATE TABLE time_series_data AS
SELECT 
    series_id,
    DATE '2024-01-01' + INTERVAL (d) DAY AS date,
    100 + series_id * 10 + 10 * SIN(2 * PI() * d / 7) + (RANDOM() * 5) AS value
FROM generate_series(0, 50) t(d)
CROSS JOIN (VALUES (1), (2), (3)) series(series_id);

-- Compute multiple rolling features in the same window
SELECT 
    series_id,
    date,
    value,
    (anofox_fcst_ts_features(date, value, ['mean', 'variance', 'linear_trend']) OVER (
        PARTITION BY series_id 
        ORDER BY date
        ROWS BETWEEN 10 PRECEDING AND CURRENT ROW
    )) AS rolling_features
FROM time_series_data
ORDER BY series_id, date;

