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

**Returns:** TABLE with the original group column (preserving its name) and 116 feature columns including `mean`, `standard_deviation`, `skewness`, `kurtosis`, `length`, `linear_trend_slope`, `autocorrelation_lag1`, etc.

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

*See also: [Feature Reference](21-feature-reference.md) | [Statistics](03-statistics.md) | [Changepoint Detection](06-changepoint-detection.md)*
