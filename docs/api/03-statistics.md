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
| `skewness` | DOUBLE | Fisher's bias-corrected G1, equal to `scipy.stats.skew(x, bias=False)`. `NaN` for 2 or fewer observations or a zero-variance series. |
| `kurtosis` | DOUBLE | Fisher's bias-corrected excess kurtosis (G2), equal to `scipy.stats.kurtosis(x, fisher=True, bias=False)`. `NaN` for 3 or fewer observations or a zero-variance series. |
| `tail_index` | DOUBLE | Hill estimator |
| `bimodality_coef` | DOUBLE | SAS small-sample bimodality coefficient computed from the G1/G2 above: `BC = (G1² + 1) / (G2 + 3·(n-1)² / ((n-2)(n-3)))`. `BC > 0.555` (the value for a uniform distribution) suggests bimodality or multimodality. |
| `trimmed_mean` | DOUBLE | 10% trimmed mean |
| `coef_variation` | DOUBLE | Coefficient of variation |
| `q1` | DOUBLE | First quartile |
| `q3` | DOUBLE | Third quartile |
| `iqr` | DOUBLE | Interquartile range |
| `autocorr_lag1` | DOUBLE | Autocorrelation at lag 1 |
| `trend_strength` | DOUBLE | `\|r\|` (absolute Pearson correlation) of a linear fit of the series against its index — not a decomposition-based trend strength; no fixed "strong trend" threshold is defined (fix planned for v0.10.0, Phase 13) |
| `seasonality_strength` | DOUBLE | `max(\|ACF\|)` over lags 2, 4, 7, and 12 only — not a general seasonal-strength decomposition and not sensitive to other periods; no fixed "strong seasonality" threshold is defined (fix planned for v0.10.0, Phase 13) |
| `entropy` | DOUBLE | Pincus (1991) approximate entropy (ApEn), `m=2`, `r=0.2 * sample standard deviation`, Chebyshev distance, self-match included. `NULL` when fewer than 3 present values remain after trimming leading/trailing NULLs, or when an interior value is missing/NaN. A constant series gives `0.0`. |
| `stability` | DOUBLE | Stability measure |
| `expected_length` | UBIGINT | Expected observations based on date range and frequency. Identity: `expected_length = n_distinct_observed_periods + n_gaps`. |
| `n_gaps` | UBIGINT | Number of **missing periods** (expected timestamps absent under the declared or detected frequency) — e.g. a single 10-period gap counts as 10, not 1. **Breaking change (v0.10.0):** before v0.10.0 this column counted gap *occurrences* instead; that count is now `n_gap_runs` below. |
| `n_gap_runs` | UBIGINT | Number of gap **occurrences** (consecutive-timestamp transitions whose step count is `>= 2`) — the pre-v0.10.0 meaning of `n_gaps`. A single 10-period gap counts as 1 run. |

**Example:**
```sql
-- Get all statistics
SELECT * FROM ts_stats_by('sales', product_id, date, quantity, '1d');

-- Access specific fields (group_col output column is named after the group_col you pass in --
-- here product_id, not a hardcoded "id")
SELECT
    product_id,
    length,
    mean,
    std_dev,
    trend_strength
FROM ts_stats_by('sales', product_id, date, quantity, '1d');

-- Detect gaps in time series: n_gaps = missing periods, n_gap_runs = gap occurrences
SELECT
    product_id,
    length AS actual_length,
    expected_length,
    n_gaps,
    n_gap_runs,
    CASE WHEN n_gaps > 0 THEN 'Has gaps' ELSE 'Complete' END AS status
FROM ts_stats_by('sales', product_id, date, quantity, '1d')
WHERE n_gaps > 0;
```

> **Alias:** `ts_stats` is an alias for `ts_stats_by`

**`ts_stats_agg(date, value)` (aggregate function)** computes the same 36 columns from
a `GROUP BY` instead of a source table name. Since v0.10.0 it: auto-detects the
frequency from the data's own timestamps (`expected_length`/`n_gaps`/`n_gap_runs` are
no longer always `NULL`); keeps `NULL`-valued rows (counted in `n_nulls`, position
preserved in the date-ordered series — matching `ts_stats_by`); and skips only rows
whose **timestamp** is `NULL` (they cannot be placed in time).

```sql
SELECT product_id,
       (ts_stats_agg(date, quantity)).length,
       (ts_stats_agg(date, quantity)).n_gaps,
       (ts_stats_agg(date, quantity)).n_gap_runs
FROM sales
GROUP BY product_id;
```

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
| `temporal_score` | DOUBLE | **Currently always `1.0`** — the underlying `compute_data_quality` call never receives the date array (hardcoded `None`), so `n_short` and `frequency` are also silently ignored by this score (fix planned for v0.10.0, Phase 13) |
| `magnitude_score` | DOUBLE | Magnitude dimension score (0-1) - measures value distribution health |
| `behavioral_score` | DOUBLE | Behavioral dimension score (0-1) - measures predictability |
| `overall_score` | DOUBLE | Overall quality score (0-1, higher is better); currently inflated by the always-1.0 `temporal_score` component |
| `n_gaps` | UBIGINT | **Currently always `0`** for the same reason as `temporal_score` above (fix planned for v0.10.0, Phase 13) |
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
