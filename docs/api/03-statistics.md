# Statistics

> Time series statistics and data quality assessment

## Overview

Statistical functions compute descriptive metrics for time series data. These functions help you understand your data before forecasting.

**Use this document to:**
- Compute 36 statistical metrics per series (mean, std, skewness, trend strength, etc.)
- Assess data quality across four dimensions (structural, temporal, magnitude, behavioral)
- Generate quality reports to identify problematic series before forecasting
- Filter out low-quality or constant series that would produce poor forecasts

---

## Quick Start

Compute statistics using table macros:

```sql
-- Multiple series statistics
SELECT * FROM ts_stats_by('sales', product_id, date, quantity, '1d');

-- Data quality per series
SELECT * FROM ts_data_quality_by('sales', product_id, date, quantity, 10, '1d');
```

---

## Table Macros

### ts_stats_by

Compute statistics for multiple time series grouped by identifier.

**Signature:**
```sql
ts_stats_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, frequency VARCHAR) → TABLE
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `group_col` | COLUMN | Column for grouping series |
| `date_col` | COLUMN | Date/timestamp column |
| `value_col` | COLUMN | Value column |
| `frequency` | VARCHAR | Data frequency (`'1d'`, `'1h'`, `'1w'`, `'1mo'`) |

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `<group_col>` | (same as input) | Series identifier |
| `length` | UBIGINT | Total number of observations |
| `n_nulls` | UBIGINT | Number of NULL values |
| `n_nan` | UBIGINT | Number of NaN values |
| `n_zeros` | UBIGINT | Number of zero values |
| `n_positive` | UBIGINT | Number of positive values |
| `n_negative` | UBIGINT | Number of negative values |
| `n_unique_values` | UBIGINT | Count of distinct values |
| `is_constant` | BOOLEAN | Whether series has only one unique value |
| `n_zeros_start` | UBIGINT | Count of leading zeros |
| `n_zeros_end` | UBIGINT | Count of trailing zeros |
| `plateau_size` | UBIGINT | Longest run of constant values |
| `plateau_size_nonzero` | UBIGINT | Longest run of constant non-zero values |
| `mean` | DOUBLE | Arithmetic mean |
| `median` | DOUBLE | Median (50th percentile) |
| `std_dev` | DOUBLE | Standard deviation |
| `variance` | DOUBLE | Variance |
| `min` | DOUBLE | Minimum value |
| `max` | DOUBLE | Maximum value |
| `range` | DOUBLE | Range (max - min) |
| `sum` | DOUBLE | Sum of all values |
| `skewness` | DOUBLE | Skewness (Fisher's G1) |
| `kurtosis` | DOUBLE | Excess kurtosis (Fisher's G2) |
| `tail_index` | DOUBLE | Hill estimator |
| `bimodality_coef` | DOUBLE | Bimodality coefficient |
| `trimmed_mean` | DOUBLE | 10% trimmed mean |
| `coef_variation` | DOUBLE | Coefficient of variation |
| `q1` | DOUBLE | First quartile |
| `q3` | DOUBLE | Third quartile |
| `iqr` | DOUBLE | Interquartile range |
| `autocorr_lag1` | DOUBLE | Autocorrelation at lag 1 |
| `trend_strength` | DOUBLE | Trend strength (0-1) |
| `seasonality_strength` | DOUBLE | Seasonality strength (0-1) |
| `entropy` | DOUBLE | Approximate entropy |
| `stability` | DOUBLE | Stability measure |
| `expected_length` | UBIGINT | Expected observations based on date range and frequency |
| `n_gaps` | UBIGINT | Number of gaps (missing time periods) in the series |

**Example:**
```sql
-- Get all statistics
SELECT * FROM ts_stats_by('sales', product_id, date, quantity, '1d');

-- Access specific fields
SELECT
    id,
    length,
    mean,
    std_dev,
    trend_strength
FROM ts_stats_by('sales', product_id, date, quantity, '1d');

-- Detect gaps in time series
SELECT
    id,
    length AS actual_length,
    expected_length,
    n_gaps,
    CASE WHEN n_gaps > 0 THEN 'Has gaps' ELSE 'Complete' END AS status
FROM ts_stats_by('sales', product_id, date, quantity, '1d')
WHERE n_gaps > 0;
```

> **Alias:** `ts_stats` is an alias for `ts_stats_by`

---

### ts_data_quality_by

Assess data quality for multiple series grouped by identifier.

**Signature:**
```sql
ts_data_quality_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, n_short INTEGER, frequency VARCHAR) → TABLE
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `group_col` | COLUMN | Column for grouping series |
| `date_col` | COLUMN | Date/timestamp column |
| `value_col` | COLUMN | Value column |
| `n_short` | INTEGER | Minimum series length threshold |
| `frequency` | VARCHAR | Data frequency (`'1d'`, `'1h'`, `'1w'`, `'1mo'`) |

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `unique_id` | (same as group_col) | Series identifier |
| `structural_score` | DOUBLE | Structural dimension score (0-1) - measures data completeness |
| `temporal_score` | DOUBLE | Temporal dimension score (0-1) - measures regularity of timestamps |
| `magnitude_score` | DOUBLE | Magnitude dimension score (0-1) - measures value distribution health |
| `behavioral_score` | DOUBLE | Behavioral dimension score (0-1) - measures predictability |
| `overall_score` | DOUBLE | Overall quality score (0-1, higher is better) |
| `n_gaps` | UBIGINT | Number of detected gaps in the series |
| `n_missing` | UBIGINT | Number of missing values |
| `is_constant` | BOOLEAN | Whether series is constant (no variation) |

**Example:**
```sql
-- Get all quality metrics
SELECT * FROM ts_data_quality_by('sales', product_id, date, quantity, 10, '1d');

-- Access specific fields
SELECT
    unique_id,
    overall_score,
    n_gaps,
    is_constant
FROM ts_data_quality_by('sales', product_id, date, quantity, 10, '1d');

-- Filter for high-quality series
SELECT unique_id
FROM ts_data_quality_by('sales', product_id, date, quantity, 10, '1d')
WHERE overall_score > 0.8;
```

> **Alias:** `ts_data_quality` is an alias for `ts_data_quality_by`

---

### ts_quality_report

Generate quality report from a stats table.

```sql
SELECT * FROM ts_quality_report(stats_table, min_length);
```

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `n_passed` | BIGINT | Series meeting quality criteria |
| `n_nan_issues` | BIGINT | Series with NaN values |
| `n_missing_issues` | BIGINT | Series with NULL values |
| `n_constant` | BIGINT | Constant series |
| `n_total` | BIGINT | Total series count |

---

### ts_stats_summary

Generate summary statistics across all series.

```sql
SELECT * FROM ts_stats_summary(stats_table);
```

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `n_series` | BIGINT | Total number of series |
| `avg_length` | DOUBLE | Average series length |
| `min_length` | BIGINT | Minimum series length |
| `max_length` | BIGINT | Maximum series length |
| `total_nulls` | BIGINT | Total NULL values |
| `total_nans` | BIGINT | Total NaN values |

**Example:**
```sql
-- First compute stats, then summarize
CREATE TABLE stats AS SELECT * FROM ts_stats('sales', product_id, date, quantity, '1d');
SELECT * FROM ts_stats_summary('stats');
```

---

### ts_data_quality_summary

Generate summary of data quality across all series.

**Signature:**
```sql
ts_data_quality_summary(source VARCHAR, unique_id_col COLUMN, date_col COLUMN, value_col COLUMN, n_short INTEGER) → TABLE
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `unique_id_col` | COLUMN | Column for grouping series |
| `date_col` | COLUMN | Date/timestamp column |
| `value_col` | COLUMN | Value column |
| `n_short` | INTEGER | Minimum series length threshold |

**Example:**
```sql
SELECT * FROM ts_data_quality_summary('sales', product_id, date, quantity, 10);
```

---

*See also: [Data Preparation](04-data-preparation.md) | [Supported Frequencies](22-supported-frequencies.md)*
