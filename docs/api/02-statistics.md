# Statistics

> Time series statistics and data quality assessment

## Overview

Statistical functions compute descriptive metrics for time series data. These functions help you understand your data before forecasting.

## Scalar Functions

### ts_stats

Computes 34 statistical metrics for a time series array.

**Signature:**
```sql
ts_stats(values DOUBLE[]) -> STRUCT
```

**Returns:**
```sql
STRUCT(
    -- Count statistics
    length               UBIGINT,   -- Total number of observations
    n_nulls              UBIGINT,   -- Number of NULL values
    n_nan                UBIGINT,   -- Number of NaN values
    n_zeros              UBIGINT,   -- Number of zero values
    n_positive           UBIGINT,   -- Number of positive values
    n_negative           UBIGINT,   -- Number of negative values
    n_unique_values      UBIGINT,   -- Count of distinct values
    is_constant          BOOLEAN,   -- Whether series has only one unique value
    n_zeros_start        UBIGINT,   -- Count of leading zeros
    n_zeros_end          UBIGINT,   -- Count of trailing zeros
    plateau_size         UBIGINT,   -- Longest run of constant values
    plateau_size_nonzero UBIGINT,   -- Longest run of constant non-zero values

    -- Descriptive statistics
    mean                 DOUBLE,    -- Arithmetic mean
    median               DOUBLE,    -- Median (50th percentile)
    std_dev              DOUBLE,    -- Standard deviation
    variance             DOUBLE,    -- Variance
    min                  DOUBLE,    -- Minimum value
    max                  DOUBLE,    -- Maximum value
    range                DOUBLE,    -- Range (max - min)
    sum                  DOUBLE,    -- Sum of all values

    -- Distribution shape
    skewness             DOUBLE,    -- Skewness (Fisher's G1)
    kurtosis             DOUBLE,    -- Excess kurtosis (Fisher's G2)
    tail_index           DOUBLE,    -- Hill estimator
    bimodality_coef      DOUBLE,    -- Bimodality coefficient
    trimmed_mean         DOUBLE,    -- 10% trimmed mean
    coef_variation       DOUBLE,    -- Coefficient of variation
    q1                   DOUBLE,    -- First quartile
    q3                   DOUBLE,    -- Third quartile
    iqr                  DOUBLE,    -- Interquartile range

    -- Time series statistics
    autocorr_lag1        DOUBLE,    -- Autocorrelation at lag 1
    trend_strength       DOUBLE,    -- Trend strength (0-1)
    seasonality_strength DOUBLE,    -- Seasonality strength (0-1)
    entropy              DOUBLE,    -- Approximate entropy
    stability            DOUBLE     -- Stability measure
)
```

**Example:**
```sql
-- Single series
SELECT ts_stats([1.0, 2.0, 3.0, 4.0, 5.0]) AS stats;

-- Multiple series with GROUP BY
SELECT
    product_id,
    (ts_stats(LIST(value ORDER BY date))).mean AS avg_value,
    (ts_stats(LIST(value ORDER BY date))).trend_strength AS trend
FROM sales
GROUP BY product_id;
```

---

### ts_data_quality

Assesses data quality across four dimensions.

**Signature:**
```sql
ts_data_quality(values DOUBLE[]) -> STRUCT
```

**Returns:**
```sql
STRUCT(
    structural_score  DOUBLE,   -- Structural dimension score (0-1)
    temporal_score    DOUBLE,   -- Temporal dimension score (0-1)
    magnitude_score   DOUBLE,   -- Magnitude dimension score (0-1)
    behavioral_score  DOUBLE,   -- Behavioral dimension score (0-1)
    overall_score     DOUBLE,   -- Overall quality score (0-1)
    n_gaps            UBIGINT,  -- Number of detected gaps
    n_missing         UBIGINT,  -- Number of missing values
    is_constant       BOOLEAN   -- Whether series is constant
)
```

**Example:**
```sql
SELECT
    product_id,
    (ts_data_quality(LIST(value ORDER BY date))).overall_score AS quality
FROM sales
GROUP BY product_id
HAVING (ts_data_quality(LIST(value ORDER BY date))).overall_score < 0.8;
```

---

## Aggregate Functions

### ts_stats_agg

Compute statistics per group using GROUP BY.

**Signature:**
```sql
ts_stats_agg(date_col TIMESTAMP, value_col DOUBLE) → STRUCT
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `date_col` | TIMESTAMP | Date/timestamp column |
| `value_col` | DOUBLE | Value column |

**Returns:** Same STRUCT as `_ts_stats` with 34 statistical metrics.

**Example:**
```sql
-- Compute statistics per product using GROUP BY
SELECT
    product_id,
    (ts_stats_agg(date, value)).mean AS mean,
    (ts_stats_agg(date, value)).std_dev AS std_dev,
    (ts_stats_agg(date, value)).length AS length
FROM sales
GROUP BY product_id;
```

---

### ts_data_quality_agg

Assess data quality per group using GROUP BY.

**Signature:**
```sql
ts_data_quality_agg(date_col TIMESTAMP, value_col DOUBLE) → STRUCT
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `date_col` | TIMESTAMP | Date/timestamp column |
| `value_col` | DOUBLE | Value column |

**Returns:** STRUCT with quality metrics:
- `structural_score`, `temporal_score`, `magnitude_score`, `behavioral_score`
- `overall_score` (0-1, higher is better)
- `n_gaps`, `n_missing`, `is_constant`

**Example:**
```sql
-- Assess data quality per product
SELECT
    product_id,
    (ts_data_quality_agg(date, value)).overall_score AS quality_score,
    (ts_data_quality_agg(date, value)).is_constant AS is_constant
FROM sales
GROUP BY product_id;
```

---

## Table Macros

### ts_stats (Table Macro)

Compute statistics for grouped time series.

**Signature:**
```sql
ts_stats(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, frequency VARCHAR) → TABLE
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `group_col` | COLUMN | Column for grouping series |
| `date_col` | COLUMN | Date/timestamp column |
| `value_col` | COLUMN | Value column |
| `frequency` | VARCHAR | Data frequency (`'1d'`, `'1h'`, `'1w'`, `'1mo'`) |

**Example:**
```sql
SELECT * FROM ts_stats('sales', product_id, date, quantity, '1d');
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

### ts_data_quality (Table Macro)

Assess data quality per series.

**Signature:**
```sql
ts_data_quality(source VARCHAR, unique_id_col COLUMN, date_col COLUMN, value_col COLUMN, n_short INTEGER, frequency VARCHAR) → TABLE
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `unique_id_col` | COLUMN | Column for grouping series |
| `date_col` | COLUMN | Date/timestamp column |
| `value_col` | COLUMN | Value column |
| `n_short` | INTEGER | Minimum series length threshold |
| `frequency` | VARCHAR | Data frequency (`'1d'`, `'1h'`, `'1w'`, `'1mo'`) |

**Example:**
```sql
SELECT * FROM ts_data_quality('sales', product_id, date, quantity, 10, '1d');
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

*See also: [Table Macros](01-table-macros.md) | [Data Preparation](03-data-preparation.md)*
