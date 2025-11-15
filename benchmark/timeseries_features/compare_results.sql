LOAD '../../build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

CREATE OR REPLACE TABLE ts_features_tsfresh AS (
    SELECT * FROM read_parquet('data/tsfresh_results.parquet')
);

CREATE OR REPLACE TABLE ts_features_duckdb AS (
    SELECT
        unique_id,
        UNNEST(ts_features(ds, y, ts_features_config_from_json('data/features_overrides.json')))
    FROM read_parquet('data/time_series_data.parquet')
    GROUP BY unique_id
);


-- Compare results
WITH tsfresh_long AS (
    SELECT
        unique_id,
        REGEXP_REPLACE(t_column, '^value__', '') AS feature_name,
        tsfresh_value
    FROM ts_features_tsfresh
    UNPIVOT (tsfresh_value FOR t_column IN (COLUMNS('value__*')))
),
duckdb_long AS (
    SELECT
        unique_id,
        d_column AS feature_name,
        duckdb_value
    FROM ts_features_duckdb
    UNPIVOT (duckdb_value FOR d_column IN (COLUMNS(* EXCLUDE (unique_id))))
),
joined AS (
    SELECT
        d.unique_id,
        d.feature_name,
        d.duckdb_value,
        t.tsfresh_value,
        ABS(d.duckdb_value - t.tsfresh_value) AS abs_diff
    FROM duckdb_long d
    JOIN tsfresh_long t
        ON d.unique_id = t.unique_id
       AND d.feature_name = t.feature_name
)
SELECT
    feature_name,
    MAX(abs_diff) AS max_abs_diff,
    AVG(abs_diff) AS avg_abs_diff,
    COUNT(*) FILTER (WHERE abs_diff > 1e-9) AS mismatched_rows
FROM joined
GROUP BY feature_name
ORDER BY feature_name;