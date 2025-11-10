# EDA & Data Preparation for Time Series

## Overview

Comprehensive SQL-based tools for time series EDA (Exploratory Data Analysis) and data preparation using DuckDB's powerful analytical capabilities.

**Key Features**:

- ✅ Zero Python dependencies - Pure SQL
- ✅ Leverages DuckDB's analytical functions
- ✅ Parallel processing out-of-the-box
- ✅ Memory-efficient operations
- ✅ Composable macros for pipelines

---

## Quick Start

```sql
-- Load EDA and Data Preparation macros
.read sql/eda_time_series.sql
.read sql/data_preparation.sql

-- 1. Analyze your data
CREATE TABLE series_stats AS
SELECT * FROM TS_STATS('sales_data', product_id, date, amount);

-- 2. Generate quality report
SELECT * FROM TS_QUALITY_REPORT('series_stats', 30);

-- 3. Prepare your data
CREATE TABLE sales_prepared AS
SELECT * FROM TS_PREPARE_STANDARD(
    'sales_data', product_id, date, amount,
    30,  -- min_length
    'forward'  -- fill_method
);
```

---

## Part 1: EDA (Exploratory Data Analysis)

### 1.1 Per-Series Statistics

**Macro**: `TS_STATS(table_name, group_cols, date_col, value_col)`

Computes comprehensive statistics for each time series.

**Returns**:

- `series_id` - Series identifier
- `length` - Number of observations
- `expected_length` - Expected observations (based on date range)
- `n_gaps` - Missing time points
- `start_date`, `end_date` - Temporal span
- `mean`, `std`, `min`, `max`, `median` - Basic statistics
- `n_null`, `n_nan`, `n_zeros` - Missing/special values
- `n_unique_values` - Distinct values
- `is_constant` - Boolean flag for constant series
- `trend_corr` - Trend correlation (-1 to 1)
- `cv` - Coefficient of variation
- `intermittency` - Proportion of zeros
- `quality_score` - Overall quality (0-1, higher is better)
- `values`, `dates` - Raw data arrays

**Example**:

```sql
CREATE TABLE stats AS
SELECT * FROM TS_STATS('sales', product_id, date, sales_amount);

-- View summary
SELECT 
    COUNT(*) AS total_series,
    ROUND(AVG(quality_score), 4) AS avg_quality,
    SUM(CASE WHEN quality_score < 0.5 THEN 1 ELSE 0 END) AS low_quality_count
FROM stats;
```

### 1.2 Data Quality Checks

**Individual Checks**:

- `TS_CHECK_GAPS(stats_table)` - Gap analysis
- `TS_CHECK_MISSING(stats_table)` - NULL/NaN values
- `TS_CHECK_CONSTANT(stats_table)` - Constant series
- `TS_CHECK_SHORT(stats_table, min_length)` - Short series
- `TS_CHECK_ALIGNMENT(stats_table)` - End date alignment

**Comprehensive Report**:

```sql
-- All checks in one report
SELECT * FROM TS_QUALITY_REPORT('stats', 30);

-- Example output:
-- | check_type              | total_series | series_with_gaps | pct_with_gaps |
-- |-------------------------|--------------|------------------|---------------|
-- | Gap Analysis            | 1000         | 150              | 15.0%         |
-- | Missing Values          | 1000         | 45               | 4.5%          |
-- | Constant Series         | 1000         | 23               | 2.3%          |
-- | Short Series (< 30)     | 1000         | 67               | 6.7%          |
-- | End Date Alignment      | 1000         | 892              | 11 rows       |
```

### 1.3 Pattern Detection

**Leading/Trailing Zeros**:

```sql
SELECT * FROM TS_ANALYZE_ZEROS('sales', product_id, date, sales_amount);

-- Returns:
-- | series_id | n_leading_zeros | n_trailing_zeros | total_edge_zeros |
-- |-----------|-----------------|------------------|------------------|
-- | P001      | 12              | 8                | 20               |
```

**Plateau Detection** (consecutive identical values):

```sql
SELECT * FROM TS_DETECT_PLATEAUS('sales', product_id, date, sales_amount);

-- Returns:
-- | series_id | max_plateau_size | max_plateau_nonzero | max_plateau_zeros | n_plateaus |
-- |-----------|------------------|---------------------|-------------------|------------|
-- | P001      | 45               | 12                  | 45                | 8          |
```

### 1.4 Seasonality Detection

```sql
SELECT * FROM TS_DETECT_SEASONALITY_ALL('sales', product_id, date, sales_amount);

-- Returns:
-- | series_id | detected_periods | primary_period | is_seasonal |
-- |-----------|------------------|----------------|-------------|
-- | P001      | [7, 30]          | 7              | true        |
-- | P002      | []               | NULL           | false       |
```

### 1.5 Distribution Analysis

```sql
SELECT * FROM TS_PERCENTILES('sales', product_id, sales_amount);

-- Returns percentiles: p01, p05, p10, p25, p50, p75, p90, p95, p99, iqr
```

### 1.6 Dataset Summary

```sql
-- Overall statistics
SELECT * FROM TS_DATASET_SUMMARY('stats');

-- Example output:
-- | total_series | total_observations | avg_series_length | date_span_days |
-- |--------------|-------------------|-------------------|----------------|
-- | 1000         | 365000            | 365.0             | 730            |

-- Find problematic series
SELECT * FROM TS_GET_PROBLEMATIC('stats', 0.5);  -- quality_score < 0.5
```

---

## Part 2: Data Preparation

### 2.1 Fill Time Gaps

**Fill missing dates in time series**:

```sql
CREATE TABLE sales_filled AS
SELECT * FROM TS_FILL_GAPS('sales', product_id, date, sales_amount);

-- Before: [2023-01-01, 2023-01-03, 2023-01-05]
-- After:  [2023-01-01, 2023-01-02, 2023-01-03, 2023-01-04, 2023-01-05]
--         values:      [100, NULL, 150, NULL, 200]
```

### 2.2 Fill Forward to Date

**Extend all series to a common end date**:

```sql
CREATE TABLE sales_extended AS
SELECT * FROM TS_FILL_FORWARD(
    'sales', product_id, date, sales_amount, 
    DATE '2023-12-31'
);
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

### 2.6 Outlier Treatment

> **⚠️ Warning**: Automated outlier deletion is dangerous and should be used with extreme caution!
>
> **Why this is risky**:
>
> - "Outliers" may be legitimate extreme values (Black Friday sales, emergency demand)
> - May indicate important business events that should be preserved
> - Can destroy valuable signal in the data
> - Statistical methods don't understand business context
>
> **Best Practices**:
>
> 1. **Investigate first**: Understand why values are extreme
> 2. **Domain expertise**: Consult business stakeholders before removing data
> 3. **Manual review**: Flag outliers for human review rather than auto-delete
> 4. **Cap, don't delete**: If necessary, cap values rather than removing them
> 5. **Document decisions**: Record why outliers were handled a certain way
>
> The examples below are for educational purposes. In production, always validate outlier treatment with domain experts.

**Cap outliers using IQR method** (safer than deletion):

```sql
SELECT * FROM TS_CAP_OUTLIERS_IQR('sales', product_id, date, sales_amount, 1.5);
-- Caps values beyond Q1-1.5*IQR and Q3+1.5*IQR
```

**Remove outliers using Z-score** (use with extreme caution):

```sql
SELECT * FROM TS_REMOVE_OUTLIERS_ZSCORE('sales', product_id, date, sales_amount, 3.0);
-- Removes observations with |z-score| > 3.0
```

### 2.7 Transformations

**Log transformation**:

```sql
SELECT * FROM TS_TRANSFORM_LOG('sales', product_id, date, sales_amount);
```

**Box-Cox transformation**:

```sql
SELECT * FROM TS_TRANSFORM_BOXCOX('sales', product_id, date, sales_amount, 0.5);
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

### 2.8 Standard Pipeline

**All-in-one preparation**:

```sql
CREATE TABLE sales_prepared AS
SELECT * FROM TS_PREPARE_STANDARD(
    'sales_raw',        -- Input table
    product_id,         -- Group column
    date,               -- Date column
    sales_amount,       -- Value column
    30,                 -- Minimum length
    'forward'           -- Fill method: 'forward', 'mean', 'interpolate', 'zero'
);

-- Pipeline steps:
-- 1. Fill time gaps
-- 2. Drop constant series
-- 3. Drop short series (< min_length)
-- 4. Drop leading zeros
-- 5. Fill remaining nulls (using specified method)
```

---

## Complete Workflow Example

```sql
-- Load macros
.read sql/eda_time_series.sql
.read sql/data_preparation.sql

-- ============================================================================
-- Step 1: Load and explore data
-- ============================================================================

CREATE TABLE sales_raw AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN RANDOM() < 0.05 THEN NULL  -- 5% missing
        WHEN RANDOM() < 0.10 THEN 0.0   -- 5% zeros
        ELSE 100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20)
    END AS sales_amount
FROM generate_series(0, 364) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

-- ============================================================================
-- Step 2: EDA - Analyze data quality
-- ============================================================================

-- Generate statistics
CREATE TABLE sales_stats AS
SELECT * FROM TS_STATS('sales_raw', product_id, date, sales_amount);

-- View overall summary
SELECT * FROM TS_DATASET_SUMMARY('sales_stats');

-- Generate quality report
SELECT * FROM TS_QUALITY_REPORT('sales_stats', 30);

-- Detect seasonality
CREATE TABLE sales_seasonality AS
SELECT * FROM TS_DETECT_SEASONALITY_ALL('sales_raw', product_id, date, sales_amount);

SELECT * FROM sales_seasonality;

-- Find problematic series
SELECT * FROM TS_GET_PROBLEMATIC('sales_stats', 0.7);

-- ============================================================================
-- Step 3: Data Preparation
-- ============================================================================

-- Option A: Standard pipeline
CREATE TABLE sales_prepared AS
SELECT * FROM TS_PREPARE_STANDARD(
    'sales_raw', product_id, date, sales_amount,
    30,  -- min_length
    'interpolate'  -- fill method
);

-- Option B: Custom pipeline
CREATE TABLE sales_custom AS
WITH 
step1 AS (
    SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount)
),
step2 AS (
    SELECT * FROM TS_DROP_CONSTANT('step1', product_id, sales_amount)
),
step3 AS (
    SELECT * FROM TS_DROP_EDGE_ZEROS('step2', product_id, date, sales_amount)
),
step4 AS (
    SELECT * FROM TS_FILL_NULLS_INTERPOLATE('step3', product_id, date, sales_amount)
),
step5 AS (
    SELECT * FROM TS_CAP_OUTLIERS_IQR('step4', product_id, date, sales_amount, 1.5)
)
SELECT * FROM step5;

-- ============================================================================
-- Step 4: Validate preparation
-- ============================================================================

-- Re-analyze prepared data
CREATE TABLE prepared_stats AS
SELECT * FROM TS_STATS('sales_prepared', product_id, date, sales_amount);

-- Compare quality scores
SELECT 
    'Before' AS stage,
    ROUND(AVG(quality_score), 4) AS avg_quality,
    SUM(CASE WHEN n_null > 0 THEN 1 ELSE 0 END) AS series_with_nulls
FROM sales_stats
UNION ALL
SELECT 
    'After',
    ROUND(AVG(quality_score), 4),
    SUM(CASE WHEN n_null > 0 THEN 1 ELSE 0 END)
FROM prepared_stats;

-- ============================================================================
-- Step 5: Forecast on prepared data
-- ============================================================================

SELECT * 
FROM TS_FORECAST_BY(
    'sales_prepared', product_id, date, sales_amount,
    'AutoETS', 28,
    {'seasonal_period': 7}
)
LIMIT 10;
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

### 4. Use Parallel Processing

```sql
-- DuckDB automatically parallelizes GROUP BY operations
-- Larger batches = better parallelization
```

---

## Comparison: DuckDB SQL vs Python/Polars

| Feature | DuckDB SQL | Python/Polars |
|---------|------------|---------------|
| **Dependencies** | None | pandas/polars + anofox |
| **Parallelization** | Automatic | Manual threading |
| **Memory** | Optimized (columnar) | Depends on DataFrame |
| **Integration** | Native with forecasting | Requires data transfer |
| **Learning Curve** | Standard SQL | Library-specific API |
| **Portability** | Run anywhere (SQL) | Requires Python env |

---

## API Reference

### EDA Macros

| Macro | Description |
|-------|-------------|
| `TS_STATS` | Per-series comprehensive statistics |
| `TS_QUALITY_REPORT` | All quality checks in one report |
| `TS_CHECK_GAPS` | Gap analysis |
| `TS_CHECK_MISSING` | Missing value analysis |
| `TS_CHECK_CONSTANT` | Constant series detection |
| `TS_CHECK_SHORT` | Short series detection |
| `TS_CHECK_ALIGNMENT` | End date alignment |
| `TS_ANALYZE_ZEROS` | Leading/trailing zero analysis |
| `TS_DETECT_PLATEAUS` | Plateau detection |
| `TS_DETECT_SEASONALITY_ALL` | Seasonality detection |
| `TS_PERCENTILES` | Distribution percentiles |
| `TS_DATASET_SUMMARY` | Overall dataset statistics |
| `TS_GET_PROBLEMATIC` | Low-quality series |

### Data Preparation Macros

| Macro | Description |
|-------|-------------|
| `TS_FILL_GAPS` | Fill missing time gaps |
| `TS_FILL_FORWARD` | Extend series to date |
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
| `TS_CAP_OUTLIERS_IQR` | Cap outliers (IQR) |
| `TS_REMOVE_OUTLIERS_ZSCORE` | Remove outliers (Z-score) |
| `TS_TRANSFORM_LOG` | Log transformation |
| `TS_TRANSFORM_BOXCOX` | Box-Cox transformation |
| `TS_DIFF` | Differencing |
| `TS_NORMALIZE_MINMAX` | Min-Max normalization |
| `TS_STANDARDIZE` | Z-score standardization |
| `TS_PREPARE_STANDARD` | Standard pipeline |

---

## Status

✅ **Production-ready**

All macros are tested and ready for use with DuckDB 1.4.1+

---

## See Also

- **Forecasting**: `00_README.md`, `docs/42_insample_validation.md`
- **Metrics**: `docs/50_evaluation_metrics.md`
- **Examples**: `examples/eda_data_prep_demo.sql`
