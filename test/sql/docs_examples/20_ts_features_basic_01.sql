-- Compute all default features for a time series
CREATE TABLE sample_ts AS
SELECT 
    (TIMESTAMP '2024-01-01' + i * INTERVAL '1 day') AS ts,
    (100 + i * 2 + SIN(i * 2 * PI() / 7) * 10)::DOUBLE AS value
FROM generate_series(0, 30) t(i);

SELECT feats.*
FROM (
    SELECT anofox_fcst_ts_features(ts, value) AS feats
    FROM sample_ts
);

