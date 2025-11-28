-- Create sample time series data
CREATE TABLE sample_ts AS
SELECT 
    TIMESTAMP '2024-01-01' + INTERVAL (d) DAY AS ts,
    100 + 10 * SIN(2 * PI() * d / 7) + (RANDOM() * 5) AS value
FROM generate_series(0, 30) t(d);

-- Load feature configuration from JSON file
SELECT feats.*
FROM (
    SELECT ts_features(
        ts,
        value,
        ts_features_config_from_json('benchmark/timeseries_features/data/features_overrides.json')
    ) AS feats
    FROM sample_ts
);

