-- This script creates a parquet file containing time series features computed using DuckDB's ts_features table function.
--
-- Workflow:
--   1. Loads the DuckDB extension.
--   2. Reads the time series data from a parquet file.
--   3. Computes the features using ts_features.
--   4. Exports the results to a parquet file.
--
-- Output: A parquet file containing the time series features.

LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

COPY (
    SELECT
        unique_id,
        UNNEST(ts_features(ds, y,
            (ts_features_config_from_json('data/features_overrides_fixed.json')).feature_names,
            (ts_features_config_from_json('data/features_overrides_fixed.json')).overrides
        ))
    FROM read_parquet('data/time_series_data.parquet')
    GROUP BY unique_id
) TO 'data/anofox_features.parquet' (FORMAT PARQUET);