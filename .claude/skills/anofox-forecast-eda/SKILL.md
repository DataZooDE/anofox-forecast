---
name: anofox-forecast-eda
description: >
  Exploratory data analysis and data quality for the anofox_forecast
  DuckDB extension — 34 per-series statistics, data-quality scoring,
  quality-report summaries, and 117 tsfresh-compatible feature
  extraction. Use before forecasting to understand series characteristics
  (length, gaps, trend, seasonality strength, intermittency) or to build
  ML feature vectors for downstream models.
version: 0.15.3
user-invocable: false
---

# Anofox Forecast — EDA & Data Quality Cheat Sheet

**Extension:** `anofox_forecast` v0.15.3 | **DuckDB:** v1.4.5 LTS / v1.5.4+ | **Dual naming:** `ts_*` and `anofox_fcst_ts_*`

Understand your data before modelling it: per-series statistics, data-quality scores, feature vectors for ML.

## Statistics

### `ts_stats` (table function) — 34 metrics per series

```sql
ts_stats(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN,
         frequency VARCHAR) → TABLE
```

Returns 34 columns: `length`, `n_nulls`, `n_nan`, `n_zeros`, `n_positive`, `n_negative`, `n_unique_values`, `is_constant`, `n_zeros_start`, `n_zeros_end`, `plateau_size`, `plateau_size_nonzero`, `mean`, `median`, `std_dev`, `variance`, `min`, `max`, `range`, `sum`, `skewness`, `kurtosis`, `tail_index`, `bimodality_coef`, `trimmed_mean`, `coef_variation`, `q1`, `q3`, `iqr`, `autocorr_lag1`, `trend_strength`, `seasonality_strength`, `entropy`, `stability`, plus date-derived `expected_length` and `n_gaps`.

```sql
-- Every stat for every series
SELECT * FROM ts_stats('sales', product_id, ds, y, '1d');

-- Quick sanity check
SELECT product_id, length, n_nulls, n_gaps, trend_strength, seasonality_strength
FROM ts_stats('sales', product_id, ds, y, '1d')
WHERE length < 30 OR n_gaps > 0
ORDER BY n_gaps DESC;
```

### `ts_stats_agg` (aggregate)

For custom `GROUP BY` shapes. Takes `(date, value)` — DO NOT wrap in `LIST(...)`.

```sql
ts_stats_agg(date_col TIMESTAMP, value_col DOUBLE)
    → STRUCT(length UBIGINT, n_nulls UBIGINT, …, mean DOUBLE, …)
```

```sql
SELECT product_id,
       ts_stats_agg(ds, y).mean AS mean,
       ts_stats_agg(ds, y).seasonality_strength AS seas_str
FROM sales GROUP BY product_id;
```

### `ts_stats_by` — alias for `ts_stats`

### `ts_stats_summary` — aggregate stats across all groups

Summarise the per-group stats into panel-level statistics (mean, std, min, max of each metric).

```sql
SELECT * FROM ts_stats_summary('sales', product_id, ds, y, '1d');
```

## Data quality

### `ts_data_quality` (table function) — per-series score card

```sql
ts_data_quality(source VARCHAR, unique_id_col COLUMN, date_col COLUMN, value_col COLUMN,
                n_short INTEGER, frequency VARCHAR) → TABLE
```

Returns `unique_id` (group column **renamed** — the extension normalises it to `unique_id` in this output, even if the input col was `product_id`), plus `overall_score`, `structural_score`, `temporal_score`, `magnitude_score`, `behavioral_score`, `n_gaps`, `n_missing`, `is_constant`.

`n_short` is the series-length threshold below which a series is flagged short (typical: 14 for daily, 12 for monthly).

```sql
SELECT * FROM ts_data_quality('sales', product_id, ds, y, 14, '1d');

-- Rank series by quality (note: output col is `unique_id`, not `product_id`)
SELECT unique_id, overall_score, structural_score, temporal_score
FROM ts_data_quality('sales', product_id, ds, y, 14, '1d')
ORDER BY overall_score;
```

### `ts_data_quality_agg` (aggregate)

Same as `ts_stats_agg` — takes `(date, value)`, returns a STRUCT.

```sql
SELECT product_id,
       ts_data_quality_agg(ds, y).overall_score AS q
FROM sales GROUP BY product_id;
```

### `ts_data_quality_summary` — panel-level roll-up

```sql
SELECT * FROM ts_data_quality_summary('sales', product_id, ds, y, 14);
```

### `ts_quality_report` — human-readable report

## Feature extraction (117 tsfresh-compatible features)

### `ts_features_by` — extract all 117 features

```sql
ts_features_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN) → TABLE
```

Returns the group column + 116 feature columns: `mean`, `standard_deviation`, `skewness`, `kurtosis`, `length`, `linear_trend_slope`, `autocorrelation_lag1`, …

```sql
-- Extract all features
SELECT * FROM ts_features_by('sales', product_id, ds, y);

-- Filter by feature values
SELECT product_id, mean, linear_trend_slope
FROM ts_features_by('sales', product_id, ds, y)
WHERE length > 30 AND autocorrelation_lag1 > 0.5;
```

### `ts_features` (scalar)

Scalar over `(date, value)`. Same 116 outputs as struct fields.

### `ts_features_list` / `ts_features_table`

Discover the feature catalogue:

```sql
SELECT column_name, feature_name, parameter_suffix
FROM ts_features_list()
WHERE feature_name LIKE '%autocorr%';
```

### Custom feature subsets

Configure via JSON or CSV:

```sql
-- Load a JSON config
SELECT * FROM ts_features_by('sales', id, ds, y,
    ts_features_config_from_json('{"features": ["mean", "std", "autocorrelation_lag1"]}'));

-- Or CSV
SELECT * FROM ts_features_by('sales', id, ds, y,
    ts_features_config_from_csv('mean,std,skewness'));
```

### `ts_features_agg` — aggregate variant

For custom GROUP BY:

```sql
SELECT id, ts_features_agg(ds, y).mean, ts_features_agg(ds, y).autocorrelation_lag1
FROM sales GROUP BY id;
```

## Gotchas

- **`ts_stats` and `ts_data_quality` are TABLE functions** — call from FROM, not SELECT. Older docs incorrectly showed them as scalars over `LIST(...)`.
- **`_agg` variants take `(date, value)` directly** — no `LIST()` wrapping. Wrapping in `LIST(...)` errors as "No function matches …".
- **Seasonality strength thresholds**: `trend_strength` and `seasonality_strength` in `ts_stats` are ∈ [0, 1] — treat > 0.6 as strong, < 0.2 as weak. Use to gate model selection downstream.
- **JSON extension is required** by a subset of quality / feature functions. Enable auto-load once per session: `SET autoinstall_known_extensions=1; SET autoload_known_extensions=1;`.

## Canonical EDA pipeline

```sql
-- 1. Quality gate
CREATE OR REPLACE TABLE quality AS
SELECT * FROM ts_data_quality('raw', product_id, ds, y, 14, '1d');

-- 2. Series-level stats
CREATE OR REPLACE TABLE stats AS
SELECT * FROM ts_stats('raw', product_id, ds, y, '1d');

-- 3. Filter — keep only high-quality, forecastable series
CREATE OR REPLACE TABLE forecastable AS
SELECT r.*
FROM raw r
JOIN quality q ON r.product_id = q.product_id
JOIN stats  s ON r.product_id = s.product_id
WHERE q.overall_score >= 0.6
  AND s.length >= 30
  AND NOT s.is_constant;

-- 4. Extract features for ML (optional)
CREATE OR REPLACE TABLE features AS
SELECT * FROM ts_features_by('forecastable', product_id, ds, y);
```

See also: `anofox-forecast-data-prep` (impute + drop bad series based on these stats), `anofox-forecast-detection` (`trend_strength` / `seasonality_strength` inform detection thresholds), `anofox-forecast-models` (use quality score / features to gate model selection).

Reference docs:
- `docs/api/03-statistics.md`
- `docs/api/20-feature-extraction.md`
- `docs/api/21-feature-reference.md`
