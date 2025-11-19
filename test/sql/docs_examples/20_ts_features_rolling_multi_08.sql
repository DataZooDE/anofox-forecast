-- Compute multiple rolling features in the same window
SELECT 
    series_id,
    date,
    value,
    (ts_features(date, value, ['mean', 'variance', 'linear_trend']) OVER (
        PARTITION BY series_id 
        ORDER BY date
        ROWS BETWEEN 10 PRECEDING AND CURRENT ROW
    )) AS rolling_features
FROM time_series_data
ORDER BY series_id, date;

