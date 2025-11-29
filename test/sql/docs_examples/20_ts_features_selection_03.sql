-- Create sample time series data
CREATE TABLE sample_ts AS
SELECT 
    TIMESTAMP '2024-01-01' + INTERVAL (d) DAY AS ts,
    100 + 10 * SIN(2 * PI() * d / 7) + (RANDOM() * 5) AS value
FROM generate_series(0, 30) t(d);

-- Select specific features for better performance
SELECT feats.*
FROM (
    SELECT anofox_fcst_ts_features(ts, value, ['mean', 'variance', 'length']) AS feats
    FROM sample_ts
);

