# Feature Extraction

> Extract tsfresh-inspired time series features

## Overview

Feature extraction functions compute 117 statistical features from time series data, inspired by tsfresh's feature catalogue; a documented subset deviates from tsfresh's exact formulas (see docs/api/21-feature-reference.md).

**Use this document to:**
- Extract 117 tsfresh-inspired features for machine learning pipelines
- Compute features per series for clustering, classification, or anomaly detection
- Access individual features (mean, std, trend_strength, entropy, etc.) from result structs
- Build feature tables for downstream ML models outside DuckDB
- Filter series by feature values (e.g., high trend strength, sufficient length)

**NULL handling (Phase 12, D-03/D-04/D-11):** `ts_features_agg`, `ts_features` (the same
aggregate under a second name) and `ts_features_by` share one Rust single-series NULL
policy — no NULL-valued row is spliced out independently of the others. Leading/trailing
`NULL` values are trimmed before extraction; an interior `NULL` that survives trimming is a
hard error naming its 1-based position (e.g. `ts_features_agg: interior NULL at position
11`); a series with no non-NULL value returns `NULL` feature values (the whole struct for
`ts_features_agg`, a row of `NULL` feature columns for `ts_features_by`) instead of an
error. The three surfaces agree on the same input series.

```sql
-- ts_features_agg equals both the trimmed series and ts_features_by on the same table.
CREATE OR REPLACE TABLE fx AS
SELECT 'A' AS id, (DATE '2024-01-01' + i * INTERVAL 1 DAY)::TIMESTAMP AS ds,
       CASE WHEN i IN (0, 9) THEN NULL ELSE (10 + i)::DOUBLE END AS y
FROM range(10) t(i);

SELECT (SELECT (ts_features_agg(ds, y)).mean FROM fx) =
       (SELECT mean FROM ts_features_by('fx', id, ds, y));
-- true
```

## tsfresh parity

All 117 features are verified against tsfresh 0.21.2 (or, for 9 features with no tsfresh
counterpart, a direct numpy/scipy reference) on a fixture suite of 9 series — white noise,
random walk, seasonal AR(12), trend+season, intermittent/many-zeros, a short series, plus
constant/all-zero/tiny-length edge cases (`validation/reference/fixtures/tsfresh_features/`).
Every feature matches within `abs <= 1e-9 + 1e-9*|ref|`, or carries a documented, intentional
deviation. The parity gate runs in `cargo test` (`tsfresh_features_reference.rs`) and is
enforced on every change to the feature set.

- **9 features have no tsfresh counterpart** and are checked against a direct numpy/scipy
  formula instead: `range`, `first_value`, `last_value`, `zero_crossing_rate`,
  `percentage_above_mean`, `count_unique`, `number_peaks_threshold_1`/`_2`,
  `linear_trend_r_squared`.
- **2 features carry a documented intentional deviation** from tsfresh/pandas on
  zero-variance series: `skewness`/`kurtosis` return `NaN` (matching `scipy.stats.skew`/
  `kurtosis(bias=False)` and `ts_stats`) where tsfresh/pandas return `0` by convention — an
  undefined moment is `NaN`, not `0`, by this project's own choice (D-03).
- **Fixed parameter choices** (not configurable at the SQL layer): `sample_entropy`/
  `approximate_entropy` use `m=2, r=0.2*population std`; `binned_entropy` uses numpy
  `np.histogram`'s exact 10-bin algorithm; `permutation_entropy` uses `dimension=3, tau=1`,
  unnormalised (not divided by `ln(dimension!)`); `lempel_ziv_complexity` uses `bins=2`
  range-midpoint quantisation with tsfresh's own dictionary-growth algorithm;
  `agg_linear_trend_*` uses `chunk_len=max(length/10, 2), f_agg=mean`.

See [Feature Reference](21-feature-reference.md) for the full per-feature Definition /
tsfresh equivalent / Deviation table, and
[`validation/reference/README.md`](../../validation/reference/README.md) for the complete
intentional-deviation and tolerance-override list.

---

## Quick Start

Extract features per group using `ts_features_by`:

```sql
-- Extract all features per product
SELECT * FROM ts_features_by('sales', product_id, date, quantity);

-- Access specific features from result (group column name preserved)
SELECT product_id, mean, standard_deviation
FROM ts_features_by('sales', product_id, date, quantity);
```

---

## Table Macros

### ts_features_by

Extract features per group from a multi-series table.

**Signature:**
```sql
ts_features_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN) → TABLE
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `group_col` | COLUMN | Column for grouping series |
| `date_col` | COLUMN | Date/timestamp column |
| `value_col` | COLUMN | Value column |

**Returns:** TABLE with the original group column (preserving its name) and 117 feature columns including `mean`, `standard_deviation`, `skewness`, `kurtosis`, `length`, `linear_trend_slope`, `autocorrelation_lag1`, etc.

**Example:**
```sql
-- Extract features per product
SELECT * FROM ts_features_by('sales', product_id, date, quantity);

-- Filter by specific feature values (note: group column name is preserved)
SELECT product_id, mean, linear_trend_slope
FROM ts_features_by('sales', product_id, date, quantity)
WHERE length > 30;
```

---

## List Available Features

### ts_features_list

Returns available feature metadata as a table.

**Signature:**
```sql
ts_features_list() → TABLE(
    column_name        VARCHAR,
    feature_name       VARCHAR,
    parameter_suffix   VARCHAR,
    default_parameters VARCHAR,
    parameter_keys     VARCHAR
)
```

**Example:**
```sql
SELECT * FROM ts_features_list();

-- Get just feature names
SELECT feature_name FROM ts_features_list();
```

---

## Advanced: Feature Configuration

> **Note:** These functions help manage feature sets for large-scale extraction.

### ts_features_config_template

Generate a template configuration for feature extraction.

**Signature:**
```sql
ts_features_config_template() → STRUCT
```

**Returns:** Configuration template with all available features and their default parameters.

**Example:**
```sql
-- Get template to customize (table function, call from FROM not SELECT)
SELECT * FROM ts_features_config_template();
```

---

### ts_features_config_from_csv

Load feature configuration from a CSV file.

**Signature:**
```sql
ts_features_config_from_csv(path VARCHAR) → STRUCT(
    feature_names VARCHAR[],
    overrides     VARCHAR[]
)
```

**CSV Format:**
```csv
feature_name,enabled,custom_param
mean,true,
variance,true,
autocorrelation,true,lag=5
```

**Example:**
```sql
-- Load custom feature set from CSV
SELECT (ts_features_config_from_csv('features.csv')).feature_names;
```

---

### ts_features_config_from_json

Load feature configuration from a JSON file.

**Signature:**
```sql
ts_features_config_from_json(path VARCHAR) → STRUCT(
    feature_names VARCHAR[],
    overrides     STRUCT(feature VARCHAR, params_json VARCHAR)[]
)
```

**JSON Format:**
```json
{
  "features": ["mean", "variance", "skewness"],
  "parameters": {
    "autocorrelation": {"lag": 10}
  }
}
```

**Example:**
```sql
-- Load feature configuration
SELECT ts_features_config_from_json('config.json');
```

---

*See also: [Feature Reference](21-feature-reference.md) | [Statistics](03-statistics.md) | [Changepoint Detection](06-changepoint-detection.md)*
