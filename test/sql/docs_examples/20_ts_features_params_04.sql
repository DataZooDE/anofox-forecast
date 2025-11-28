-- Create sample time series data
CREATE TABLE sample_ts AS
SELECT 
    TIMESTAMP '2024-01-01' + INTERVAL (d) DAY AS ts,
    100 + 10 * SIN(2 * PI() * d / 7) + (RANDOM() * 5) AS value
FROM generate_series(0, 30) t(d);

-- Override default parameters for a feature
SELECT feats.*
FROM (
    SELECT ts_features(
        ts,
        value,
        ['ratio_beyond_r_sigma'],
        [{'feature': 'ratio_beyond_r_sigma', 'params': {'r': 1.0}}]
    ) AS feats
    FROM sample_ts
);

