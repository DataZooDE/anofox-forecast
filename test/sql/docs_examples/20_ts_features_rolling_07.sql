-- Compute rolling features using window functions
CREATE TABLE rolling_ts AS
SELECT 
    (i % 2) AS series_id,
    (TIMESTAMP '2024-01-01' + i * INTERVAL '1 day') AS ts,
    i::DOUBLE AS value
FROM generate_series(0, 10) t(i);

SELECT 
    series_id,
    ts,
    value,
    (anofox_fcst_ts_features(ts, value, ['mean', 'length']) OVER (
        PARTITION BY series_id 
        ORDER BY ts
        ROWS BETWEEN 2 PRECEDING AND CURRENT ROW
    )) AS rolling_stats
FROM rolling_ts
ORDER BY series_id, ts;

