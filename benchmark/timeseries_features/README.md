# Timeseries Feature Test Workflow

This directory contains the scripts needed to generate synthetic data, run
tsfresh-based feature extraction, and compare the Python output with the DuckDB
`ts_features` table function.

## 1. Generate synthetic data

Run the DuckDB script to build a reproducible dataset with 100 series × 365 days
(`~36k` rows) and export it as a parquet file:

```shell
duckdb benchmark/timeseries_features/create_data.sql
```

This produces `benchmark/timeseries_features/data/time_series_data.parquet` with
columns `unique_id`, `ds`, `y`.

## 2. Compute tsfresh features

Use the Fire-based CLI to aggregate tsfresh features per `unique_id`. The script
automatically applies the feature definitions stored in
`benchmark/timeseries_features/data/features_overrides.json`.

```shell
cd benchmark
uv run python timeseries_features/tsfresh_features.py \
  --input_path timeseries_features/data/time_series_data.parquet \
  --output_path timeseries_features/data/tsfresh_results.parquet
```

> `features_overrides.json` lists the subset of tsfresh metrics (and their
> parameters) to calculate. A catalog of all available feature names can be
> found in `data/all_features_overrides.csv` at the repo root.

## 3. Compare DuckDB vs tsfresh

Run the DuckDB comparison script to load both parquet outputs and inspect the
feature columns side by side:

```shell
duckdb benchmark/timeseries_features/compare_results.sql
```

The script:

- Creates `ts_features_tsfresh` from the parquet generated in step 2.
- Recomputes DuckDB `ts_features` using the same JSON overrides.
- Selects comparable columns (e.g., `ratio_beyond_r_sigma*`) to verify that the
  results match for every `unique_id`.
