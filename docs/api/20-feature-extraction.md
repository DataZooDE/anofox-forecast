# Feature Extraction

> Extract tsfresh-compatible time series features

## Overview

Feature extraction functions compute 117 statistical features from time series data, compatible with tsfresh.

**Use this document to:**
- Extract 117 tsfresh-compatible features for machine learning pipelines
- Compute features per series for clustering, classification, or anomaly detection
- Access individual features (mean, std, trend_strength, entropy, etc.) from result structs
- Build feature tables for downstream ML models outside DuckDB
- Filter series by feature values (e.g., high trend strength, sufficient length)

---

## Quick Start

Extract features using table macros (recommended):

```sql
-- Single series
SELECT * FROM ts_features_table('daily_sales', date, value);

-- Multiple series
SELECT * FROM ts_features_by('sales', product_id, date, quantity);

-- Access specific features from result
SELECT id, (features).mean, (features).standard_deviation
FROM ts_features_by('sales', product_id, date, quantity);
```

Using aggregate functions with GROUP BY:

```sql
SELECT product_id, ts_features_agg(date, value) AS features
FROM sales
GROUP BY product_id;
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

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `id` | (same as group_col) | Group identifier |
| `features` | STRUCT | 117-field feature struct |

**Example:**
```sql
-- Extract features per product
SELECT * FROM ts_features_by('sales', product_id, date, quantity);

-- Filter by specific feature values
SELECT id, (features).mean, (features).trend_strength
FROM ts_features_by('sales', product_id, date, quantity)
WHERE (features).length > 30;
```

---

### ts_features_table

Extract features from a single-series table (no grouping).

**Signature:**
```sql
ts_features_table(source VARCHAR, date_col COLUMN, value_col COLUMN) → TABLE
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `date_col` | COLUMN | Date/timestamp column |
| `value_col` | COLUMN | Value column |

**Returns:** Single row with `features` STRUCT containing 117 feature columns.

**Example:**
```sql
-- Extract features from a single-series table
SELECT * FROM ts_features_table('daily_revenue', date, amount);

-- Access specific features from result
SELECT (features).mean, (features).standard_deviation
FROM ts_features_table('daily_revenue', date, amount);
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

## Aggregate Functions

### ts_features_agg

Aggregate function that extracts 117 tsfresh-compatible features from grouped time series.

**Signatures:**
```sql
-- Basic
ts_features_agg(timestamp_col TIMESTAMP, value_col DOUBLE) → STRUCT

-- With feature selection
ts_features_agg(timestamp_col, value_col, feature_selection LIST(VARCHAR)) → STRUCT

-- With custom parameters
ts_features_agg(timestamp_col, value_col, feature_selection, feature_params LIST(STRUCT)) → STRUCT
```

**Returns:** A STRUCT containing 117 named feature columns including:

| Feature | Description |
|---------|-------------|
| `abs_energy` | Sum of squared values |
| `absolute_sum_of_changes` | Sum of absolute differences |
| `autocorrelation_lag1` | Autocorrelation at lag 1 |
| `benford_correlation` | Correlation with Benford's law |
| `binned_entropy` | Entropy of binned distribution |
| `cid_ce` | Complexity-invariant distance |
| `count_above_mean` | Count of values above mean |
| `count_below_mean` | Count of values below mean |
| `first_value` | First value in series |
| `kurtosis` | Kurtosis |
| `last_value` | Last value in series |
| `length` | Series length |
| `linear_trend_intercept` | Linear trend intercept |
| `linear_trend_r_squared` | R² of linear fit |
| `linear_trend_slope` | Linear trend slope |
| `longest_strike_above_mean` | Longest run above mean |
| `longest_strike_below_mean` | Longest run below mean |
| `maximum` | Maximum value |
| `mean` | Mean value |
| `mean_abs_change` | Mean absolute change |
| `median` | Median value |
| `minimum` | Minimum value |
| `number_peaks` | Number of peaks |
| `quantile_0.25` | 25th percentile |
| `quantile_0.75` | 75th percentile |
| `range` | Range (max - min) |
| `skewness` | Skewness |
| `standard_deviation` | Standard deviation |
| `variance` | Variance |
| `sample_entropy` | Sample entropy |
| `approximate_entropy` | Approximate entropy |
| `permutation_entropy` | Permutation entropy |
| `lempel_ziv_complexity` | Lempel-Ziv complexity |
| `spectral_centroid` | Spectral centroid from FFT |
| *...and 80+ more* | See `ts_features_list()` |

**Example:**
```sql
-- Extract features per product
SELECT
    product_id,
    ts_features_agg(date, value) AS features
FROM sales
GROUP BY product_id;

-- Access specific features
SELECT
    product_id,
    (ts_features_agg(date, value)).mean AS avg_value,
    (ts_features_agg(date, value)).linear_trend_slope AS trend
FROM sales
GROUP BY product_id;

-- With feature selection
SELECT
    product_id,
    ts_features_agg(date, value, ['mean', 'variance', 'skewness']) AS features
FROM sales
GROUP BY product_id;
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
-- Get template to customize
SELECT ts_features_config_template();
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

## Common Feature Categories

### Basic Statistics
- `mean`, `median`, `minimum`, `maximum`, `range`
- `variance`, `standard_deviation`, `skewness`, `kurtosis`
- `sum`, `length`, `first_value`, `last_value`

### Trend Features
- `linear_trend_slope`, `linear_trend_intercept`, `linear_trend_r_squared`
- `mean_change`, `mean_abs_change`, `mean_second_derivative_central`

### Autocorrelation
- `autocorrelation_lag1` through `autocorrelation_lag10`
- `partial_autocorrelation_lag1` through `partial_autocorrelation_lag5`

### Entropy & Complexity
- `sample_entropy`, `approximate_entropy`, `permutation_entropy`
- `binned_entropy`, `lempel_ziv_complexity`

### Spectral
- `spectral_centroid`, `spectral_variance`
- `fft_coefficient_0_real`, `fft_coefficient_0_imag`

### Distribution
- `quantile_0.1`, `quantile_0.25`, `quantile_0.75`, `quantile_0.9`
- `count_above_mean`, `count_below_mean`, `percentage_above_mean`
- `ratio_beyond_r_sigma_1`, `ratio_beyond_r_sigma_2`, `ratio_beyond_r_sigma_3`

---

*See also: [Statistics](03-statistics.md) | [Changepoint Detection](06-changepoint-detection.md)*
