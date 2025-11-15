LOAD '../../build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

COPY (
    SELECT
        unique_id,
        UNNEST(ts_features(ds, y, ts_features_config_from_json('data/features_overrides.json')))
    FROM read_parquet('data/time_series_data.parquet')
    GROUP BY unique_id
) TO 'data/anofox_features.parquet' (FORMAT PARQUET);