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

