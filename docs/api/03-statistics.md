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
ts_stats_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, frequency VARCHAR,
            seasonal_period := 0) → TABLE
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `group_col` | COLUMN | Column for grouping series |
| `date_col` | COLUMN | Date/timestamp column |
| `value_col` | COLUMN | Value column |
| `frequency` | VARCHAR | Data frequency (`'1d'`, `'1h'`, `'1w'`, `'1mo'`) |
| `seasonal_period` | INTEGER | Period used for `trend_strength`/`seasonality_strength` (default `0`): `0` auto-detects via autocorrelation (same detector forecasting uses); `1` means "non-seasonal" (no period at all); `>= 2` uses that period explicitly. Negative values raise an error. |

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
| `is_constant` | BOOLEAN | Whether series is constant under the scale-relative tolerance `max - min <= 1e-10 * max(1, max\|x\|)` over present values (D-13, G25) — NOT the same thing as `n_unique_values == 1`, which stays an exact bit-identity count |
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
| `trend_strength` | DOUBLE | Wang/Hyndman STL trend strength: `F_T = max(0, 1 - Var(R)/Var(T+R))` from an R-faithful STL decomposition (`stats::stl`, `s.window = 11`, for one usable period; the `forecast::mstl` iteration for several; `stats::supsmu(seq_len(n), x)` trend when no period is usable). `0` for a zero-variance series. `NULL` when the series has an interior-missing value. |
| `seasonality_strength` | DOUBLE | Wang/Hyndman STL seasonal strength: `F_S = max(0, 1 - Var(R)/Var(S+R))`, the **maximum** over every usable period when `seasonal_period := 0` resolves to several (per-period values are not exposed as separate columns — this is "how strong is the strongest seasonal component"). `NULL` when no period is usable (none detected, `seasonal_period := 1`, or the series is too short for the period: `n <= 2 * seasonal_period`), when the per-period variance ratio is `0/0` (e.g. a constant series), or when the series has an interior-missing value. |
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

-- STL-based trend/seasonal strength with an explicit weekly period (seasonal_period := 7)
SELECT
    product_id,
    trend_strength,
    seasonality_strength
FROM ts_stats_by('sales', product_id, date, quantity, '1d', seasonal_period := 7);
```

> **Alias:** `ts_stats` is an alias for `ts_stats_by`

**`ts_stats_agg(date, value)` (aggregate function)** computes the same 36 columns from
a `GROUP BY` instead of a source table name. Since v0.10.0 it: auto-detects the
frequency from the data's own timestamps (`expected_length`/`n_gaps`/`n_gap_runs` are
no longer always `NULL`); keeps `NULL`-valued rows (counted in `n_nulls`, position
preserved in the date-ordered series — matching `ts_stats_by`); and skips only rows
whose **timestamp** is `NULL` (they cannot be placed in time).
`ts_stats_agg` has no `seasonal_period` argument — `trend_strength`/`seasonality_strength`
always auto-detect the period (equivalent to `ts_stats_by(..., seasonal_period := 0)`).

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

Assess data quality for multiple series grouped by identifier. Since v0.10.0
(D-05/D-06) every score is computed from the series' real dates, frequency and
`n_short` — see the breaking-change note below.

**Signature:**
```sql
ts_data_quality_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, n_short INTEGER, frequency VARCHAR) → TABLE
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `group_col` | COLUMN | Column for grouping series |
| `date_col` | COLUMN | Date/timestamp column (used for gap/temporal scoring) |
| `value_col` | COLUMN | Value column |
| `n_short` | INTEGER | Minimum "not short" present-value count (must be `>= 1`); drives the structural length factor and `is_short` below |
| `frequency` | VARCHAR | Data frequency (`'1d'`, `'1h'`, `'1w'`, `'1mo'`, `'1q'`, `'1y'`); `NULL` or `''` auto-detects the modal consecutive step from `date_col` (same tie-break as [Supported Frequencies](22-supported-frequencies.md): smaller interval wins) |

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `unique_id` | (same as group_col) | Series identifier |
| `structural_score` | DOUBLE | `0.7 * (n_present / length) + 0.3 * min(1, n_present / n_short)`, clamped to `[0, 1]` — completeness weighted with a length factor against `n_short` |
| `temporal_score` | DOUBLE | `clamp(1 - 5 * n_gaps / expected_length, 0, 1)`; `1.0` when no gap information is available (e.g. a single observation) |
| `magnitude_score` | DOUBLE | Magnitude dimension score (0-1) - measures value distribution health (IQR outliers, 4-sigma extremes) |
| `behavioral_score` | DOUBLE | `0.0` for a constant series; otherwise `clamp(1 - 2*outlier_ratio - zero_run_ratio, 0, 1)` — robust (Iglewicz-Hoaglin modified z-score, threshold 3.5) outliers and the longest run of consecutive exact zeros (a `NULL`/missing entry breaks a run), both as a fraction of present values. A predictable/smooth series (even a near-constant-step one) carries **no** autocorrelation penalty — being easy to forecast is not a quality defect |
| `overall_score` | DOUBLE | Mean of the four scores above |
| `n_gaps` | UBIGINT | Number of **missing periods**: expected timestamps absent under the declared/detected frequency between the series' first and last date (same meaning as `ts_stats`'s `n_gaps`) |
| `n_gap_runs` | UBIGINT | Number of gap **occurrences** (contiguous runs of missing periods) — the pre-v0.10.0 meaning of `n_gaps` |
| `n_missing` | UBIGINT | Number of missing values (`NULL` or `NaN`) |
| `is_constant` | BOOLEAN | Whether series is constant among present values, under the SAME scale-relative tolerance `max - min <= 1e-10 * max(1, max\|x\|)` as `ts_stats`'s `is_constant` (D-13, G25) |
| `is_short` | BOOLEAN | Whether the present-value count is below `n_short` |

> **Breaking change (v0.10.0, D-05):** before this release, `temporal_score`
> was always `1.0` and `n_gaps` was always `0` — the dates, `frequency`, and `n_short`
> arguments were silently ignored by the underlying scoring. They are now real,
> dates-aware computations; existing queries that assumed `temporal_score == 1.0` or
> `n_gaps == 0` on a gappy series will see different (correct) values. `n_gaps` also
> changed meaning from "gap occurrences" to "missing periods" (matching `ts_stats`'s
> D-04 change) — use the new `n_gap_runs` column for the old meaning.

**Example:**
```sql
-- Get all quality metrics
SELECT * FROM ts_data_quality_by('sales', product_id, date, quantity, 10, '1d');

-- Access specific fields
SELECT
    unique_id,
    overall_score,
    n_gaps,
    n_gap_runs,
    is_short,
    is_constant
FROM ts_data_quality_by('sales', product_id, date, quantity, 10, '1d');

-- Filter for high-quality series
SELECT unique_id
FROM ts_data_quality_by('sales', product_id, date, quantity, 10, '1d')
WHERE overall_score > 0.8;
```

> **Alias:** `ts_data_quality` is an alias for `ts_data_quality_by`

**`ts_data_quality_agg(date, value)` (aggregate function)** computes the same 10
columns from a `GROUP BY` instead of a source table name. It takes no `n_short` or
`frequency` argument: `n_short` is always `30` (the pre-v0.10.0 fixed structural-length
scale) and frequency always auto-detects from the group's own timestamps. Since v0.10.0
it also keeps `NULL`-valued rows (counted in `n_missing`, position preserved in the
date-ordered series — matching `ts_stats_agg`) and skips only rows whose **timestamp**
is `NULL`.

```sql
SELECT product_id,
       (ts_data_quality_agg(date, quantity)).overall_score,
       (ts_data_quality_agg(date, quantity)).n_gaps,
       (ts_data_quality_agg(date, quantity)).is_short
FROM sales
GROUP BY product_id;
```

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

Generate summary of data quality across all series. Takes no `frequency` argument —
each series' frequency is auto-detected from its own `date_col` (same auto-detect
rule as a `NULL` `frequency` on `ts_data_quality_by`).

**Signature:**
```sql
ts_data_quality_summary(source VARCHAR, unique_id_col COLUMN, date_col COLUMN, value_col COLUMN, n_short INTEGER) → TABLE
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `unique_id_col` | COLUMN | Column for grouping series |
| `date_col` | COLUMN | Date/timestamp column (used for auto-detected gap/temporal scoring) |
| `value_col` | COLUMN | Value column |
| `n_short` | INTEGER | Minimum "not short" present-value count; drives each series' `structural_score`/`is_short` before averaging |

**Example:**
```sql
SELECT * FROM ts_data_quality_summary('sales', product_id, date, quantity, 10);
```

---

*See also: [Data Preparation](04-data-preparation.md) | [Supported Frequencies](22-supported-frequencies.md)*
