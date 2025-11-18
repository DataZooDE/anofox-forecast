# Timeseries Feature Test Workflow

This directory contains the scripts needed to generate synthetic data, run
tsfresh-based feature extraction, and compare the Python output with the DuckDB
`ts_features` table function.

## Feature Definition Source

The complete list of features and their configuration used for benchmarking is specified in the [features_overrides.json](data/features_overrides.json) file in this directory.

- This JSON file defines each feature, its parameters, and any customization or overrides applied during extraction.
- Both DuckDB (`ts_features`) and Python (`tsfresh`) scripts consume this file to ensure feature definitions are **identical and synchronized** for fair, consistent comparison.

> **See:** [`data/features_overrides.json`](data/features_overrides.json) for the exact configuration and set of features used.

For a complete, up-to-date catalog of all features used in benchmarking (including full names, parameters, and configuration), see:

- [`../../data/all_features_overrides.json`](../../data/all_features_overrides.json) — JSON listing of **all available** tsfresh features and parameters (located at the repo root).
- [`../../data/all_features_overrides.csv`](../../data/all_features_overrides.csv) — CSV listing of **all available** tsfresh features and parameters (located at the repo root).

These files are the single source of truth for which timeseries features are included and how they are parameterized in both DuckDB and Python (tsfresh) scripts.


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

## 4. Generate expected values for C++ unit tests

The script `generate_cpp_unit_test_expected_values.py` generates expected feature values from tsfresh for C++ unit tests. It uses the same test series (365 values, seed=42) as defined in `create_data.sql` to ensure consistency between Python and C++ implementations.

To generate expected values:

```shell
cd benchmark/timeseries_features
uv run python generate_cpp_unit_test_expected_values.py > expected_values.cpp
```

The output can be copied into `anofox-time/tests/features/test_tsfresh_features.cpp` to update the expected value constants used in unit tests.
