# EDA & Data Preparation for Time Series

## Overview

A SQL-based tools for time series EDA (Exploratory Data Analysis) and data preparation using DuckDB's powerful analytical capabilities.

---

## Quick Start

```sql
-- 1. Get dataset overview
SELECT * FROM TS_STATS_SUMMARY('series_stats');

-- 2. Check data quality
SELECT * FROM TS_DATA_QUALITY('sales_data', product_id, date, amount, 30);

-- 3. Prepare your data (extend to common end date)
CREATE TABLE sales_extended AS
SELECT * FROM TS_FILL_FORWARD('sales_data', product_id, date, amount, DATE '2023-12-31');
```

---

## Part 1: EDA (Exploratory Data Analysis)

### 1.1 Dataset Summary

**Macro**: `TS_STATS_SUMMARY(stats_table)`

Provides overall statistics across all series in the dataset.

**Returns**:
- `total_series` - Total number of series (INTEGER)
- `total_observations` - Total number of observations (INTEGER)
- `avg_series_length` - Average length per series (DOUBLE)
- `date_span` - Date range in days (INTEGER)
- `frequency` - Inferred frequency category (VARCHAR)

**Example**:
```sql
-- First generate stats
CREATE TABLE stats AS
SELECT * FROM TS_STATS('sales', product_id, date, sales_amount);

-- Then get summary
SELECT * FROM TS_STATS_SUMMARY('stats');

-- Example output:
-- | total_series | total_observations | avg_series_length | date_span | frequency |
-- |-------------|-------------------|------------------|-----------|-----------|
-- | 1000        | 365000            | 365.0             | 730       | 1d     |
```

### 1.2 Data Quality Health Card

**Macro**: `TS_DATA_QUALITY(table_name, unique_id_col, date_col, value_col, n_short)`

Comprehensive data quality assessment across four dimensions with actionable recommendations.

**Returns**:
- `unique_id` - Series identifier
- `dimension` - Quality dimension (Structural, Temporal, Magnitude, Behavioural)
- `metric` - Specific metric name
- `value` - Metric value (INTEGER or DOUBLE)
- `value_pct` - Metric as percentage (DOUBLE, NULL if not applicable)

**Example**:
```sql
-- Generate comprehensive health card
SELECT * FROM TS_DATA_QUALITY('sales_data', product_id, date, amount, 30);

-- Example output:
-- | unique_id | dimension    | metric           | value | value_pct |
-- |-----------|--------------|------------------|-------|-----------|
-- | Store_A   | Structural  | key_uniqueness   | 0     | NULL      |
-- | Store_A   | Temporal    | timestamp_gaps   | 23    | 0.152     |
-- | Store_A   | Temporal    | series_length    | 12    | NULL      |
-- | Store_A   | Magnitude   | missing_values   | 13    | 0.085     |
-- | Store_A   | Magnitude   | static_values    | 0     | NULL      |
-- | Store_A   | Behavioural | intermittency    | 104   | 0.523     |
```

**Four Dimensions**:

1. **Structural Integrity**: Key uniqueness, ID cardinality
2. **Temporal Integrity**: Frequency inference, timestamp gaps, series alignment, series length
3. **Magnitude & Value Validity**: Missing values, value bounds, static values
4. **Behavioural/Statistical**: Seasonality, trend detection, intermittency

**Summary Function**:

```sql
-- Get summary by dimension and metric
SELECT * FROM TS_DATA_QUALITY_SUMMARY('sales_data', product_id, date, amount, 30);
```

**Workflow Example**:

```sql
-- 1. Generate health card
CREATE TABLE health_card AS
SELECT * FROM TS_DATA_QUALITY('sales_raw', product_id, date, amount, 30);

-- 2. Review issues by dimension
SELECT * FROM health_card WHERE dimension = 'Temporal' ORDER BY metric, unique_id;

-- 3. Get summary statistics
SELECT * FROM TS_DATA_QUALITY_SUMMARY('sales_raw', product_id, date, amount, 30);

-- 4. Take action based on findings
-- (e.g., apply TS_FILL_GAPS for timestamp gaps, TS_DROP_CONSTANT for static values)
```

### 1.3 Legacy Quality Report

**Macro**: `TS_QUALITY_REPORT(stats_table, min_length)`

All quality checks in one report (legacy format for backward compatibility).

**Individual Checks** (also available separately):
- `TS_CHECK_GAPS(stats_table)` - Gap analysis
- `TS_CHECK_MISSING(stats_table)` - NULL/NaN values
- `TS_CHECK_CONSTANT(stats_table)` - Constant series
- `TS_CHECK_SHORT(stats_table, min_length)` - Short series
- `TS_CHECK_ALIGNMENT(stats_table)` - End date alignment

**Example**:
```sql
-- All checks in one report
SELECT * FROM TS_QUALITY_REPORT('stats', 30);

-- Example output:
-- | check_type              | total_series | series_with_gaps | pct_with_gaps |
-- |-------------------------|--------------|------------------|---------------|
-- | Gap Analysis            | 1000         | 150              | 15.0%         |
-- | Missing Values           | 1000         | 45               | 4.5%          |
-- | Constant Series          | 1000         | 23               | 2.3%          |
-- | Short Series (< 30)      | 1000         | 67               | 6.7%          |
-- | End Date Alignment       | 1000         | 892              | 11 rows       |
```

### 1.4 Per-Series Statistics

**Macro**: `TS_STATS(table_name, group_col, date_col, value_col)`

Computes comprehensive statistics for each time series.

**Returns**:
- `series_id` - Series identifier
- `length` - Number of observations
- `start_date`, `end_date` - Temporal span
- `expected_length` - Expected observations based on date range (INTEGER)
- `mean`, `std`, `min`, `max`, `median` - Basic statistics
- `n_null` - Count of NULL values
- `n_zeros` - Count of zero values
- `n_unique_values` - Count of distinct values (INTEGER)
- `is_constant` - Boolean flag for constant series
- `plateau_size` - Size of longest consecutive run of identical values (INTEGER)
- `plateau_size_non_zero` - Size of longest consecutive run of identical non-zero values (INTEGER)
- `n_zeros_start` - Number of zeros at the beginning of the series (INTEGER)
- `n_zeros_end` - Number of zeros at the end of the series (INTEGER)

**Example**:
```sql
CREATE TABLE stats AS
SELECT * FROM TS_STATS('sales', product_id, date, sales_amount);

-- View summary
SELECT 
    COUNT(*) AS total_series,
    ROUND(AVG(length), 2) AS avg_length,
    SUM(CASE WHEN is_constant THEN 1 ELSE 0 END) AS constant_series_count,
    SUM(CASE WHEN n_zeros_start > 0 OR n_zeros_end > 0 THEN 1 ELSE 0 END) AS series_with_edge_zeros
FROM stats;
```

### 1.5 Pattern Detection

**Leading/Trailing Zeros**:
```sql
SELECT * FROM TS_ANALYZE_ZEROS('sales', product_id, date, sales_amount);

-- Returns:
-- | series_id | n_leading_zeros | n_trailing_zeros | total_edge_zeros | n_zeros | series_length |
-- |-----------|-----------------|------------------|------------------|---------|----------------|
-- | P001      | 12              | 8                | 20               | 45      | 365            |
```

**Plateau Detection** (consecutive identical values):
```sql
SELECT * FROM TS_DETECT_PLATEAUS('sales', product_id, date, sales_amount);

-- Returns:
-- | series_id | max_plateau_size | max_plateau_nonzero | max_plateau_zeros | n_plateaus | series_length |
-- |-----------|------------------|---------------------|-------------------|------------|---------------|
-- | P001      | 45               | 12                  | 45                | 8          | 365           |
```

**Seasonality Detection**:
```sql
SELECT * FROM TS_DETECT_SEASONALITY('sales', product_id, date, sales_amount);

-- Returns:
-- | series_id | detected_periods | primary_period | secondary_period | tertiary_period | is_seasonal |
-- |-----------|------------------|----------------|------------------|-----------------|-------------|
-- | P001      | [7, 30, 365]     | 7              | 30               | 365             | true        |
-- | P002      | []               | NULL           | NULL             | NULL            | false       |
```

---

## Part 2: Data Preparation

### 2.1 Fill Forward to Date

**Macro**: `TS_FILL_FORWARD(table_name, group_col, date_col, value_col, target_date)`

Extends all series to a common end date. Missing dates are filled with NULL values (not forward-filled).

**Parameters**:
- `target_date` - Target end date (DATE, required)

**Example**:
```sql
-- Extend to specific date
CREATE TABLE sales_extended AS
SELECT * FROM TS_FILL_FORWARD(
    'sales', product_id, date, sales_amount, 
    DATE '2023-12-31'
);

-- Note: Missing dates will have NULL values in value_col
-- Use TS_FILL_NULLS_FORWARD afterwards if you want to forward-fill values
```

### 2.2 Fill Time Gaps

**Fill missing dates in time series**:
```sql
CREATE TABLE sales_filled AS
SELECT * FROM TS_FILL_GAPS('sales', product_id, date, sales_amount);

-- Before: [2023-01-01, 2023-01-03, 2023-01-05]
-- After:  [2023-01-01, 2023-01-02, 2023-01-03, 2023-01-04, 2023-01-05]
--         values:      [100, NULL, 150, NULL, 200]
```

### 2.3 Drop Series by Criteria

**Drop constant series**:
```sql
CREATE TABLE sales_variable AS
SELECT * FROM TS_DROP_CONSTANT('sales', product_id, sales_amount);
```

**Drop short series**:
```sql
CREATE TABLE sales_long_enough AS
SELECT * FROM TS_DROP_SHORT('sales', product_id, date, 30);  -- min 30 obs
```

**Drop series with excessive gaps**:
```sql
CREATE TABLE sales_complete AS
SELECT * FROM TS_DROP_GAPPY('sales', product_id, date, 0.10);  -- max 10% gaps
```

### 2.4 Remove Edge Zeros

**Drop leading zeros**:
```sql
CREATE TABLE sales_no_leading AS
SELECT * FROM TS_DROP_LEADING_ZEROS('sales', product_id, date, sales_amount);
```

**Drop trailing zeros**:
```sql
CREATE TABLE sales_no_trailing AS
SELECT * FROM TS_DROP_TRAILING_ZEROS('sales', product_id, date, sales_amount);
```

**Drop both**:
```sql
CREATE TABLE sales_no_edge_zeros AS
SELECT * FROM TS_DROP_EDGE_ZEROS('sales', product_id, date, sales_amount);
```

### 2.5 Fill Missing Values

**Constant fill**:
```sql
SELECT * FROM TS_FILL_NULLS_CONST('sales', product_id, date, sales_amount, 0.0);
```

**Forward fill** (last observation carried forward):
```sql
SELECT * FROM TS_FILL_NULLS_FORWARD('sales', product_id, date, sales_amount);
```

**Backward fill** (next observation carried backward):
```sql
SELECT * FROM TS_FILL_NULLS_BACKWARD('sales', product_id, date, sales_amount);
```

**Linear interpolation**:
```sql
SELECT * FROM TS_FILL_NULLS_INTERPOLATE('sales', product_id, date, sales_amount);
```

**Fill with series mean**:
```sql
SELECT * FROM TS_FILL_NULLS_MEAN('sales', product_id, date, sales_amount);
```

### 2.6 Transformations

**Log transformation**:
```sql
SELECT * FROM TS_TRANSFORM_LOG('sales', product_id, date, sales_amount);
```

**Box-Cox transformation**:
```sql
-- Note: TS_TRANSFORM_BOXCOX is currently unavailable due to DuckDB parser limitations
-- Use TS_TRANSFORM_LOG for lambda=0 case, or implement Box-Cox transformation manually
```

**Differencing**:
```sql
SELECT * FROM TS_DIFF('sales', product_id, date, sales_amount, 1);  -- 1st difference
```

**Min-Max normalization** (per series):
```sql
SELECT * FROM TS_NORMALIZE_MINMAX('sales', product_id, date, sales_amount);
-- Scales to [0, 1]
```

**Standardization** (Z-score, per series):
```sql
SELECT * FROM TS_STANDARDIZE('sales', product_id, date, sales_amount);
-- mean=0, std=1
```

---

## Performance Tips

### 1. Use CTEs for Multi-Step Pipelines
```sql
WITH step1 AS (...), step2 AS (...), step3 AS (...)
SELECT * FROM step3;
```

### 2. Materialize Intermediate Results
```sql
CREATE TABLE sales_step1 AS SELECT * FROM TS_FILL_GAPS(...);
CREATE TABLE sales_step2 AS SELECT * FROM TS_DROP_CONSTANT('sales_step1', ...);
```

### 3. Filter Early
```sql
-- Filter before heavy operations
WITH filtered AS (
    SELECT * FROM sales WHERE product_category = 'Electronics'
)
SELECT * FROM TS_STATS('filtered', ...);
```

--- 

## API Reference

### EDA Macros

| Macro | Description |
|-------|-------------|
| `TS_STATS_SUMMARY` | Overall dataset statistics from TS_STATS |
| `TS_DATA_QUALITY` | Comprehensive health card with metrics and values across 4 dimensions |
| `TS_DATA_QUALITY_SUMMARY` | Aggregated summary by dimension and metric |
| `TS_QUALITY_REPORT` | All quality checks in one report (legacy) |
| `TS_CHECK_GAPS` | Gap analysis |
| `TS_CHECK_MISSING` | Missing value analysis |
| `TS_CHECK_CONSTANT` | Constant series detection |
| `TS_CHECK_SHORT` | Short series detection |
| `TS_CHECK_ALIGNMENT` | End date alignment |
| `TS_STATS` | Per-series comprehensive statistics |
| `TS_ANALYZE_ZEROS` | Leading/trailing zero analysis |
| `TS_DETECT_PLATEAUS` | Plateau detection |
| `TS_DETECT_SEASONALITY` | Seasonality detection |

**Dimensions** (for Data Quality Health Card):
- **Structural**: Key uniqueness, ID cardinality
- **Temporal**: Frequency inference, timestamp gaps, series alignment, series length
- **Magnitude**: Missing values, value bounds, static values
- **Behavioural**: Seasonality, trend detection, intermittency

### Data Preparation Macros

| Macro | Description |
|-------|-------------|
| `TS_FILL_FORWARD` | Extend series to target_date |
| `TS_FILL_GAPS` | Fill missing time gaps |
| `TS_DROP_CONSTANT` | Drop constant series |
| `TS_DROP_SHORT` | Drop short series |
| `TS_DROP_GAPPY` | Drop series with many gaps |
| `TS_DROP_LEADING_ZEROS` | Remove leading zeros |
| `TS_DROP_TRAILING_ZEROS` | Remove trailing zeros |
| `TS_DROP_EDGE_ZEROS` | Remove both edge zeros |
| `TS_FILL_NULLS_CONST` | Fill with constant |
| `TS_FILL_NULLS_FORWARD` | Forward fill |
| `TS_FILL_NULLS_BACKWARD` | Backward fill |
| `TS_FILL_NULLS_INTERPOLATE` | Linear interpolation |
| `TS_FILL_NULLS_MEAN` | Fill with series mean |
| `TS_TRANSFORM_LOG` | Log transformation |
| `TS_DIFF` | Differencing |
| `TS_NORMALIZE_MINMAX` | Min-Max normalization |
| `TS_STANDARDIZE` | Z-score standardization |

---
