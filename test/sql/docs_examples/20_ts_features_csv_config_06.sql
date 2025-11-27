-- Load feature configuration from CSV file
SELECT feats.*
FROM (
    SELECT ts_features(
        ts,
        value,
        ts_features_config_from_csv('benchmark/timeseries_features/data/features_overrides.csv')
    ) AS feats
    FROM sample_ts
);

