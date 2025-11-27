# EDA & Data Preparation - Complete Workflow Guide

## Introduction

Data quality directly impacts forecast accuracy. This guide covers exploratory data analysis, data quality assessment, and data preparation using SQL macros that operate on time series at scale.

**Note**: This guide follows the API documentation in `API_REFERENCE.md`, which is the authoritative source for function signatures, parameters, and behavior. For complete function reference, see the [API Reference](../docs/API_REFERENCE.md).

**API Coverage**:

- **5 EDA macros**: `TS_STATS`, `TS_STATS_SUMMARY`, `TS_QUALITY_REPORT` (plus seasonality and changepoint detection)
- **2 Data Quality macros**: `TS_DATA_QUALITY`, `TS_DATA_QUALITY_SUMMARY`
- **12 Data Preparation macros**: Gap filling, series filtering, edge cleaning, and missing value imputation

This guide provides practical workflows and examples for:

1. **Exploratory Data Analysis**: Understanding your time series data structure and patterns
2. **Data Quality Assessment**: Comprehensive evaluation across four dimensions (Structural, Temporal, Magnitude, Behavioural)
3. **Data Preparation**: Cleaning, gap filling, filtering, and imputation to prepare data for forecasting

---

## Table of Contents

1. [Exploratory Data Analysis](#exploratory-data-analysis)
   - [Per-Series Statistics](#per-series-statistics)
   - [Dataset Summary](#dataset-summary)
   - [Quality Assessment](#quality-assessment)
2. [Data Quality](#data-quality)
   - [Comprehensive Assessment](#comprehensive-assessment)
   - [Summary by Dimension](#summary-by-dimension)
3. [Data Preparation](#data-preparation)
   - [Gap Filling](#gap-filling)
     - [TS_FILL_GAPS](#ts_fill_gaps)
     - [TS_FILL_FORWARD](#ts_fill_forward)
   - [Series Filtering](#series-filtering)
     - [TS_DROP_CONSTANT](#ts_drop_constant)
     - [TS_DROP_SHORT](#ts_drop_short)
   - [Edge Cleaning](#edge-cleaning)
     - [TS_DROP_LEADING_ZEROS](#ts_drop_leading_zeros)
     - [TS_DROP_TRAILING_ZEROS](#ts_drop_trailing_zeros)
     - [TS_DROP_EDGE_ZEROS](#ts_drop_edge_zeros)
   - [Missing Value Imputation](#missing-value-imputation)
     - [TS_FILL_NULLS_CONST](#ts_fill_nulls_const)
     - [TS_FILL_NULLS_FORWARD](#ts_fill_nulls_forward)
     - [TS_FILL_NULLS_BACKWARD](#ts_fill_nulls_backward)
     - [TS_FILL_NULLS_MEAN](#ts_fill_nulls_mean)
4. [Complete Workflow Examples](#complete-workflow-examples)
5. [Common Data Issues & Solutions](#common-data-issues--solutions)
6. [Preparation Checklist](#preparation-checklist)

---

## Exploratory Data Analysis

### Per-Series Statistics

**TS_STATS**

Computes per-series statistical metrics including length, date ranges, central tendencies (mean, median), dispersion (std), value distributions (min, max, zeros), and quality indicators (nulls, uniqueness, constancy). Returns 24 metrics per series for exploratory analysis and data profiling.

**Example:**

```sql
-- Compute comprehensive stats for all series
CREATE TABLE sales_stats AS
SELECT * FROM TS_STATS('sales_raw', product_id, date, sales_amount);

-- View results
SELECT * FROM sales_stats LIMIT 5;
```

**Output Schema**:
Returns comprehensive statistics per series including:

- **Basic stats**: count, mean, std, min, max, median
- **Data quality**: null_count, gap_count, zero_count, constant_flag
- **Pattern indicators**: cv (coefficient of variation), intermittency_rate
- **Trend metrics**: trend_correlation, first_last_ratio

### Dataset Summary

**TS_STATS_SUMMARY**

Aggregates statistics across all series from TS_STATS output. Computes dataset-level metrics including total series count, total observations, average series length, and date span. Provides high-level overview for dataset characterization.

**Example:**

```sql
-- Get overall picture
SELECT * FROM TS_STATS_SUMMARY('sales_stats');
```

### Quality Assessment

**TS_QUALITY_REPORT**

Generates quality assessment report from TS_STATS output. Evaluates series against configurable thresholds for gaps, missing values, constant series, short series, and temporal alignment. Identifies series requiring data preparation steps.

**Example:**

```sql
-- Comprehensive quality checks
SELECT * FROM TS_QUALITY_REPORT('sales_stats', 30);
```

[↑ Go to top](#eda--data-preparation---complete-workflow-guide)

---

## Data Quality

### Comprehensive Assessment

**TS_DATA_QUALITY**

Assesses data quality across four dimensions (Structural, Temporal, Magnitude, Behavioural) for each time series. Returns per-series metrics including key uniqueness, timestamp gaps, missing values, value distributions, and pattern characteristics. Output is normalized by dimension and metric for cross-series comparison.

**Signature (Function Overloading):**

```sql
-- For DATE/TIMESTAMP columns (date-based frequency)
TS_DATA_QUALITY(
    table_name      VARCHAR,
    unique_id_col   ANY,
    date_col        DATE | TIMESTAMP,
    value_col       DOUBLE,
    n_short         INTEGER,
    frequency       VARCHAR
) → TABLE

-- For INTEGER columns (integer-based frequency)
TS_DATA_QUALITY(
    table_name      VARCHAR,
    unique_id_col   ANY,
    date_col        INTEGER | BIGINT,
    value_col       DOUBLE,
    n_short         INTEGER,
    frequency       INTEGER
) → TABLE
```

**Parameters:**

- `n_short`: Optional threshold for short series detection (default: 30)
- `frequency`:
  - **For DATE/TIMESTAMP columns**: Optional frequency string (Polars-style). Defaults to `"1d"` if NULL or not provided.
    - `"30m"` or `"30min"` - 30 minutes
    - `"1h"` - 1 hour
    - `"1d"` - 1 day (default)
    - `"1w"` - 1 week
    - `"1mo"` - 1 month
    - `"1q"` - 1 quarter (3 months)
    - `"1y"` - 1 year
  - **For INTEGER columns**: Optional integer step size. Defaults to `1` if NULL or not provided.
    - `1`, `2`, `3`, etc. - Integer step size for `GENERATE_SERIES`

**Type Validation:**

- DuckDB automatically selects the correct overload based on the `frequency` parameter type:
  - VARCHAR frequency → DATE/TIMESTAMP date column required
  - INTEGER frequency → INTEGER/BIGINT date column required
- If there's a type mismatch (e.g., INTEGER date column with VARCHAR frequency), a `Binder Error` will be raised at query time.

**Returns:**

```sql
TABLE(
    unique_id       ANY,  -- Type matches unique_id_col
    dimension       VARCHAR,  -- Structural, Temporal, Magnitude, Behavioural
    metric          VARCHAR,
    value           BIGINT,
    value_pct       DOUBLE
)
```

**Dimensions and Metrics:**

**Structural Dimension:**

- `key_uniqueness`: Number of duplicate key combinations (unique_id + date_col). Counts how many rows have duplicate (unique_id, date_col) pairs. Value = 0 indicates all keys are unique.
- `id_cardinality`: Total number of distinct series IDs in the dataset. Reported for `unique_id = 'ALL_SERIES'` only.

**Temporal Dimension:**

- `series_length`: Number of observations in the series. Count of non-NULL rows per series.
- `timestamp_gaps`: Number of missing timestamps based on expected frequency. Calculated as `expected_count - actual_count` where expected_count is derived from the date range and frequency parameter. `value_pct` indicates the percentage of missing timestamps.
- `series_alignment`: Number of distinct start/end dates across all series. Reported for `unique_id = 'ALL_SERIES'` only. Value = 1 indicates all series start/end on the same dates.
- `frequency_inference`: Number of distinct inferred frequencies across series. Reported for `unique_id = 'ALL_SERIES'` only. Indicates frequency diversity in the dataset.

**Magnitude Dimension:**

- `missing_values`: Count and percentage of NULL values in the value column. `value` = count of NULLs, `value_pct` = percentage of NULLs.
- `value_bounds`: Count and percentage of negative values (for non-negative expected data). `value` = count of negative values, `value_pct` = proportion of negative values. Useful for detecting data quality issues when values should be non-negative.
- `static_values`: Boolean flag (1/0) indicating if series has no variation (constant values). Value = 1 if all values are identical (or standard deviation = 0), 0 otherwise.

**Behavioural Dimension:**

- `intermittency`: Count and percentage of zero values (including NULLs). `value` = count of zeros/NULLs, `value_pct` = proportion. High intermittency indicates sparse time series.
- `seasonality_check`: Boolean flag (1/0) indicating if seasonality was detected. Value = 1 if any seasonal periods detected, 0 otherwise. Only computed for series with ≥7 observations.
- `trend_detection`: Correlation coefficient between row number and value (trend strength). `value` = NULL, `value_pct` = absolute correlation (0-1). Higher values indicate stronger trend. Only computed for series with ≥3 observations.

**Examples:**

```sql
-- DATE/TIMESTAMP columns: Use VARCHAR frequency strings
-- Generate comprehensive health card (n_short parameter defaults to 30 if NULL)
CREATE TABLE health_card AS
SELECT * FROM TS_DATA_QUALITY('sales_raw', product_id, date, sales_amount, 30, '1d');

-- View all issues
SELECT * FROM health_card ORDER BY dimension, metric;

-- Filter specific issues
SELECT * FROM TS_DATA_QUALITY('sales', product_id, date, amount, 30, '1d')
WHERE dimension = 'Temporal' AND metric = 'timestamp_gaps';

-- INTEGER columns: Use INTEGER frequency values
SELECT * FROM TS_DATA_QUALITY('int_data', series_id, date_col, value, 30, 1)
WHERE dimension = 'Magnitude' AND metric = 'missing_values';

```

### Summary by Dimension

**TS_DATA_QUALITY_SUMMARY**

Aggregates quality metrics across all series, grouped by dimension and metric. Computes summary statistics (counts, percentages) for each quality dimension to provide dataset-level quality overview. Useful for identifying systemic data quality issues affecting multiple series.

**Signature:**

```sql
TS_DATA_QUALITY_SUMMARY(
    table_name      VARCHAR,
    unique_id_col   ANY,
    date_col        DATE | TIMESTAMP | INTEGER,
    value_col       DOUBLE,
    n_short         INTEGER
) → TABLE
```

**Returns:** Aggregated summary by dimension and metric.

**Example:**

```sql
-- Get summary by dimension (n_short parameter defaults to 30 if NULL)
SELECT * FROM TS_DATA_QUALITY_SUMMARY('sales_raw', product_id, date, sales_amount, 30);

```

[↑ Go to top](#eda--data-preparation---complete-workflow-guide)

---

## Data Preparation

SQL macros for data cleaning and transformation. Date type support varies by function.

### Gap Filling

#### TS_FILL_GAPS

**Fill Missing Timestamps**

Fills missing timestamps/indices in series with NULL values using the specified frequency interval or step size.

**Signature (Function Overloading):**

```sql
-- For DATE/TIMESTAMP columns (date-based frequency)
TS_FILL_GAPS(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP,
    value_col     DOUBLE,
    frequency     VARCHAR
) → TABLE

-- For INTEGER columns (integer-based frequency)
TS_FILL_GAPS(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      INTEGER | BIGINT,
    value_col     DOUBLE,
    frequency     INTEGER
) → TABLE
```

**Parameters:**

- `frequency`:
  - **For DATE/TIMESTAMP columns**: Optional frequency string (Polars-style). Defaults to `"1d"` if NULL or not provided.
    - `"30m"` or `"30min"` - 30 minutes
    - `"1h"` - 1 hour
    - `"1d"` - 1 day (default)
    - `"1w"` - 1 week
    - `"1mo"` - 1 month
    - `"1q"` - 1 quarter (3 months)
    - `"1y"` - 1 year
  - **For INTEGER columns**: Optional integer step size. Defaults to `1` if NULL or not provided.
    - `1`, `2`, `3`, etc. - Integer step size for `GENERATE_SERIES`

**Type Validation:**

- DuckDB automatically selects the correct overload based on the `frequency` parameter type:
  - VARCHAR frequency → DATE/TIMESTAMP date column required
  - INTEGER frequency → INTEGER/BIGINT date column required
- If there's a type mismatch (e.g., INTEGER date column with VARCHAR frequency), a `Binder Error` will be raised at query time.

**Examples:**

```sql
-- DATE/TIMESTAMP columns: Use VARCHAR frequency strings
-- Fill gaps with daily frequency (default)
CREATE TABLE fixed AS
SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount, '1d');

-- Fill gaps with 30-minute frequency
SELECT * FROM TS_FILL_GAPS('hourly_data', series_id, timestamp, value, '30m');

-- Fill gaps with weekly frequency
SELECT * FROM TS_FILL_GAPS('weekly_data', series_id, date, value, '1w');

-- Use NULL (must cast to VARCHAR for DATE/TIMESTAMP columns)
SELECT * FROM TS_FILL_GAPS('daily_data', series_id, date, value, NULL::VARCHAR);

-- INTEGER columns: Use INTEGER frequency values
-- Fill gaps with step size of 1
SELECT * FROM TS_FILL_GAPS('int_data', series_id, date_col, value, 1);

-- Fill gaps with step size of 2
SELECT * FROM TS_FILL_GAPS('int_data', series_id, date_col, value, 2);

-- Use NULL (defaults to step size 1 for INTEGER columns)
SELECT * FROM TS_FILL_GAPS('int_data', series_id, date_col, value, NULL);

```

#### TS_FILL_FORWARD

**Extend Series to Target Date**

Extends series to target date/index, filling gaps with NULL using the specified frequency interval or step size.

**Signature (Function Overloading):**

```sql
-- For DATE/TIMESTAMP columns (date-based frequency)
TS_FILL_FORWARD(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP,
    value_col     DOUBLE,
    target_date   DATE | TIMESTAMP,
    frequency     VARCHAR
) → TABLE

-- For INTEGER columns (integer-based frequency)
TS_FILL_FORWARD(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      INTEGER | BIGINT,
    value_col     DOUBLE,
    target_date   INTEGER | BIGINT,
    frequency     INTEGER
) → TABLE
```

**Parameters:**

- `target_date`: Target date/index to extend the series to (type must match `date_col` type)
- `frequency`: Same as `TS_FILL_GAPS` (see above)

**Type Validation:**

- Same as `TS_FILL_GAPS` (see above)

**Examples:**

```sql
-- DATE/TIMESTAMP columns: Use VARCHAR frequency strings
-- Extend hourly series to target date
SELECT * FROM TS_FILL_FORWARD('hourly_data', series_id, timestamp, value, '2024-12-31'::TIMESTAMP, '1h');

-- Extend monthly series to target date
SELECT * FROM TS_FILL_FORWARD('monthly_data', series_id, date, value, '2024-12-01'::DATE, '1mo');

-- Extend daily series to target date (default frequency)
CREATE TABLE sales_extended AS
SELECT * FROM TS_FILL_FORWARD(
    'sales', product_id, date, sales_amount, 
    DATE '2023-12-31', '1d'
);

-- Use NULL (must cast to VARCHAR for DATE/TIMESTAMP columns)
SELECT * FROM TS_FILL_FORWARD('daily_data', series_id, date, value, '2024-12-31'::DATE, NULL::VARCHAR);

-- INTEGER columns: Use INTEGER frequency values
-- Extend series to index 100 with step size of 1
SELECT * FROM TS_FILL_FORWARD('int_data', series_id, date_col, value, 100, 1);

-- Extend series to index 100 with step size of 5
SELECT * FROM TS_FILL_FORWARD('int_data', series_id, date_col, value, 100, 5);

-- Use NULL (defaults to step size 1 for INTEGER columns)
SELECT * FROM TS_FILL_FORWARD('int_data', series_id, date_col, value, 100, NULL);

```

[↑ Go to top](#eda--data-preparation---complete-workflow-guide)

### Series Filtering

#### TS_DROP_CONSTANT

**Remove Constant Series**

Removes series with constant values (no variation).

**Signature:**

```sql
TS_DROP_CONSTANT(
    table_name    VARCHAR,
    group_col     ANY,
    value_col     DOUBLE
) → TABLE
```

> [!WARNING]
> This function may drop intermittent demand series (series with many zeros) as long as gaps have not been filled yet (e.g., with zeros via gap filling functions). If you need to preserve intermittent series, ensure gaps are being filled.

**Example:**

```sql
-- Detect constant series
SELECT * FROM sales_stats WHERE is_constant = true;

-- Remove constant series
CREATE TABLE sales_no_constant AS
SELECT * FROM TS_DROP_CONSTANT('sales', product_id, sales_amount);

```

#### TS_DROP_SHORT

**Remove Short Series**

Removes series below minimum length.

**Signature:**

```sql
TS_DROP_SHORT(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    min_length    INTEGER
) → TABLE
```

> [!WARNING]
> This function may drop intermittent demand series (series with many zeros) as long as gaps have not been filled yet (e.g., with zeros via gap filling functions). If you need to preserve intermittent series, ensure gaps are being filled.

**Example:**

```sql
-- Remove series with less than 30 observations
CREATE TABLE sales_long_enough AS
SELECT * FROM TS_DROP_SHORT('sales', product_id, date, 30);

```

[↑ Go to top](#eda--data-preparation---complete-workflow-guide)

### Edge Cleaning

#### TS_DROP_LEADING_ZEROS

**Remove Leading Zeros**

Removes leading zeros from time series.

**Signature:**

```sql
TS_DROP_LEADING_ZEROS(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE
) → TABLE
```

> [!WARNING]
> Results may differ if `TS_FILL_GAPS` or `TS_FILL_FORWARD` has been applied, as these functions may introduce zeros or other values in previously missing timestamps.

**Example:**

```sql
-- Remove leading zeros
CREATE TABLE sales_no_leading_zeros AS
SELECT * FROM TS_DROP_LEADING_ZEROS('sales', product_id, date, sales_amount);

```

#### TS_DROP_TRAILING_ZEROS

**Remove Trailing Zeros**

Removes trailing zeros from time series.

**Signature:**

```sql
TS_DROP_TRAILING_ZEROS(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE
) → TABLE
```

> [!WARNING]
> Results may differ if `TS_FILL_GAPS` or `TS_FILL_FORWARD` has been applied, as these functions may introduce zeros or other values in previously missing timestamps.

**Example:**

```sql
-- Remove trailing zeros
CREATE TABLE sales_no_trailing_zeros AS
SELECT * FROM TS_DROP_TRAILING_ZEROS('sales', product_id, date, sales_amount);

```

#### TS_DROP_EDGE_ZEROS

**Remove Both Leading and Trailing Zeros**

Removes both leading and trailing zeros from time series.

**Signature:**

```sql
TS_DROP_EDGE_ZEROS(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE
) → TABLE
```

> [!WARNING]
> Results may differ if `TS_FILL_GAPS` or `TS_FILL_FORWARD` has been applied, as these functions may introduce zeros or other values in previously missing timestamps.

**Example:**

```sql
-- Detect edge zeros
WITH zero_analysis AS (
    SELECT 
        product_id,
        date,
        sales_amount,
        ROW_NUMBER() OVER (PARTITION BY product_id ORDER BY date) AS rn,
        SUM(CASE WHEN sales_amount != 0 THEN 1 ELSE 0 END) 
            OVER (PARTITION BY product_id ORDER BY date) AS nonzero_count
    FROM sales
)
SELECT 
    product_id,
    MIN(CASE WHEN sales_amount != 0 THEN date END) AS first_sale,
    MAX(CASE WHEN sales_amount != 0 THEN date END) AS last_sale,
    COUNT(*) AS total_days,
    SUM(CASE WHEN sales_amount = 0 THEN 1 ELSE 0 END) AS zero_days
FROM zero_analysis
GROUP BY product_id
HAVING zero_days > 0;

-- Fix: Remove edge zeros
CREATE TABLE sales_no_edge_zeros AS
SELECT * FROM TS_DROP_EDGE_ZEROS('sales', product_id, date, sales_amount);
```

[↑ Go to top](#eda--data-preparation---complete-workflow-guide)

### Missing Value Imputation

#### TS_FILL_NULLS_CONST

**Fill with Constant Value**

Fills NULL values with a specified constant value.

**Signature:**

```sql
TS_FILL_NULLS_CONST(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE,
    fill_value    DOUBLE
) → TABLE
```

**Example:**

```sql
-- Fill NULLs with 0
CREATE TABLE sales_filled_zero AS
SELECT * FROM TS_FILL_NULLS_CONST('sales', product_id, date, sales_amount, 0.0);

-- Fill NULLs with a specific value (e.g., -1 for missing data indicator)
CREATE TABLE sales_filled_marker AS
SELECT * FROM TS_FILL_NULLS_CONST('sales', product_id, date, sales_amount, -1.0);

```

#### TS_FILL_NULLS_FORWARD

**Forward Fill (Last Observation Carried Forward)**

Uses `LAST_VALUE(... IGNORE NULLS)` window function to forward fill NULL values.

**Signature:**

```sql
TS_FILL_NULLS_FORWARD(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE
) → TABLE
```

**Example:**

```sql
-- Forward fill (use last known value)
CREATE TABLE sales_forward_filled AS
SELECT * FROM TS_FILL_NULLS_FORWARD('sales', product_id, date, sales_amount);
```

**Alternative Example:**

```sql
-- Option A: Forward fill (use last known value)
SELECT * FROM TS_FILL_NULLS_FORWARD('sales', product_id, date, sales_amount);

-- Option B: Mean imputation
SELECT * FROM TS_FILL_NULLS_MEAN('sales', product_id, date, sales_amount);

-- Option C: Drop series with too many nulls
WITH clean AS (
    SELECT * FROM sales_stats WHERE n_null < length * 0.05  -- < 5% missing
)
SELECT s.*
FROM sales s
WHERE s.product_id IN (SELECT series_id FROM clean);
```

#### TS_FILL_NULLS_BACKWARD

**Backward Fill**

Uses `FIRST_VALUE(... IGNORE NULLS)` window function to backward fill NULL values.

**Signature:**

```sql
TS_FILL_NULLS_BACKWARD(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE
) → TABLE
```

**Example:**

```sql
-- Backward fill (use next known value)
CREATE TABLE sales_backward_filled AS
SELECT * FROM TS_FILL_NULLS_BACKWARD('sales', product_id, date, sales_amount);

```

#### TS_FILL_NULLS_MEAN

**Fill with Series Mean**

Computes mean per series and fills NULLs with that mean value.

**Signature:**

```sql
TS_FILL_NULLS_MEAN(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE
) → TABLE
```

**Example:**

```sql
-- Fill NULLs with series mean
CREATE TABLE sales_mean_filled AS
SELECT * FROM TS_FILL_NULLS_MEAN('sales', product_id, date, sales_amount);
```

**Alternative: Drop series with too many nulls instead**

```sql
WITH clean AS (
    SELECT * FROM sales_stats WHERE n_null < length * 0.05  -- < 5% missing
)
SELECT s.*
FROM sales s
WHERE s.product_id IN (SELECT series_id FROM clean);
```

[↑ Go to top](#eda--data-preparation---complete-workflow-guide)

---

## Complete Workflow Examples

### Standard Pipeline (Recommended)

```sql
-- All-in-one preparation (if standard pipeline was implemented)
CREATE TABLE sales_prepared AS
WITH 
-- Step 1: Fill time gaps
step1 AS (
    SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount)
),
-- Step 2: Drop constant series
step2 AS (
    SELECT * FROM TS_DROP_CONSTANT('step1', product_id, sales_amount)
),
-- Step 3: Drop short series
step3 AS (
    SELECT * FROM TS_DROP_SHORT('step2', product_id, date, 30)
),
-- Step 4: Remove leading zeros
step4 AS (
    SELECT * FROM TS_DROP_LEADING_ZEROS('step3', product_id, date, sales_amount)
),
-- Step 5: Fill remaining nulls
step5 AS (
    SELECT * FROM TS_FILL_NULLS_FORWARD('step4', product_id, date, sales_amount)
)
SELECT * FROM step5;
```

### Custom Pipeline (Advanced)

Tailor to your specific needs:

```sql
CREATE TABLE sales_custom_prep AS
WITH 
-- Fill gaps first
filled AS (
    SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount)
),
-- Drop problematic series
filtered AS (
    SELECT f.*
    FROM filled f
    WHERE f.product_id NOT IN (
    )
),
-- Remove edge zeros
no_edges AS (
    SELECT * FROM TS_DROP_EDGE_ZEROS('filtered', product_id, date, sales_amount)
),
-- Fill nulls with interpolation (more sophisticated)
interpolated AS (
    SELECT 
        product_id,
        date,
        -- Linear interpolation
        COALESCE(sales_amount,
            AVG(sales_amount) OVER (
                PARTITION BY product_id 
                ORDER BY date 
                ROWS BETWEEN 3 PRECEDING AND 3 FOLLOWING
            )
        ) AS sales_amount
    FROM no_edges
)
SELECT * FROM interpolated;
```

### Automated Data Prep Pipeline

```sql
-- Create a reusable preparation view
CREATE VIEW sales_autoprepared AS
WITH stats AS (
    SELECT * FROM TS_STATS('sales_raw', product_id, date, sales_amount)
),
quality_series AS (
    SELECT series_id FROM stats WHERE quality_score >= 0.6
),
filled AS (
    SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount)
    WHERE product_id IN (SELECT series_id FROM quality_series)
),
no_constant AS (
    SELECT * FROM TS_DROP_CONSTANT('filled', product_id, sales_amount)
),
complete AS (
    SELECT * FROM TS_FILL_NULLS_FORWARD('no_constant', product_id, date, sales_amount)
)
SELECT * FROM complete;

-- Use in forecasting
SELECT * FROM TS_FORECAST_BY('sales_autoprepared', product_id, date, sales_amount,
                             'AutoETS', 28, {'seasonal_period': 7});
```

### Validate Preparation

Compare before/after:

```sql
-- Generate stats for prepared data
CREATE TABLE prepared_stats AS
SELECT * FROM TS_STATS('sales_prepared', product_id, date, sales_amount);

-- Compare quality
SELECT 
    'Raw data' AS stage,
    COUNT(*) AS num_series,
    ROUND(AVG(quality_score), 4) AS avg_quality,
    SUM(CASE WHEN n_null > 0 THEN 1 ELSE 0 END) AS series_with_nulls,
    SUM(CASE WHEN n_gaps > 0 THEN 1 ELSE 0 END) AS series_with_gaps,
    SUM(CASE WHEN is_constant THEN 1 ELSE 0 END) AS constant_series
FROM sales_stats
UNION ALL
SELECT 
    'Prepared',
    COUNT(*),
    ROUND(AVG(quality_score), 4),
    SUM(CASE WHEN n_null > 0 THEN 1 ELSE 0 END),
    SUM(CASE WHEN n_gaps > 0 THEN 1 ELSE 0 END),
    SUM(CASE WHEN is_constant THEN 1 ELSE 0 END)
FROM prepared_stats;
```

**Expected Improvements**:

- Series with nulls: 45 → 0
- Series with gaps: 150 → 0
- Constant series: 23 → 0

[↑ Go to top](#eda--data-preparation---complete-workflow-guide)

---

## Common Data Issues & Solutions

### Issue 1: Missing Time Points

**Problem**: Dates are not continuous

**Solution**: Use `TS_FILL_GAPS` to fill missing timestamps

```sql
-- Detect
SELECT series_id, n_gaps, quality_score
FROM sales_stats
WHERE n_gaps > 0
ORDER BY n_gaps DESC
LIMIT 10;

-- Fix
CREATE TABLE fixed AS
SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount);
```

### Issue 2: Missing Values (NULLs)

**Problem**: Some values are NULL

**Solutions**: Multiple imputation options available

```sql
-- Option A: Forward fill (use last known value)
SELECT * FROM TS_FILL_NULLS_FORWARD('sales', product_id, date, sales_amount);

-- Option B: Mean imputation
SELECT * FROM TS_FILL_NULLS_MEAN('sales', product_id, date, sales_amount);

-- Option C: Drop series with too many nulls
WITH clean AS (
    SELECT * FROM sales_stats WHERE n_null < length * 0.05  -- < 5% missing
)
SELECT s.*
FROM sales s
WHERE s.product_id IN (SELECT series_id FROM clean);
```

### Issue 3: Constant Series

**Problem**: All values are the same

**Solution**: Remove constant series with `TS_DROP_CONSTANT`

```sql
-- Detect
SELECT * FROM sales_stats WHERE is_constant = true;

-- Fix
SELECT * FROM TS_DROP_CONSTANT('sales', product_id, sales_amount);
```

### Issue 4: Short Series

**Problem**: Not enough historical data

**Solution**: Filter short series with `TS_DROP_SHORT`

```sql
-- Detect
SELECT * FROM sales_stats WHERE length < 30;

-- Fix: Drop or aggregate
SELECT * FROM TS_DROP_SHORT('sales', product_id, date, 30);

-- Or: Aggregate similar products
WITH aggregated AS (
    SELECT 
        category AS product_id,  -- Aggregate by category
        date,
        SUM(sales_amount) AS sales_amount
    FROM sales
    JOIN product_catalog USING (product_id)
    GROUP BY category, date
)
SELECT * FROM aggregated;
```

### Issue 5: Leading/Trailing Zeros

**Problem**: Product not yet launched or discontinued

**Solution**: Remove edge zeros with `TS_DROP_EDGE_ZEROS`

```sql
-- Detect
WITH zero_analysis AS (
    SELECT 
        product_id,
        date,
        sales_amount,
        ROW_NUMBER() OVER (PARTITION BY product_id ORDER BY date) AS rn,
        SUM(CASE WHEN sales_amount != 0 THEN 1 ELSE 0 END) 
            OVER (PARTITION BY product_id ORDER BY date) AS nonzero_count
    FROM sales
)
SELECT 
    product_id,
    MIN(CASE WHEN sales_amount != 0 THEN date END) AS first_sale,
    MAX(CASE WHEN sales_amount != 0 THEN date END) AS last_sale,
    COUNT(*) AS total_days,
    SUM(CASE WHEN sales_amount = 0 THEN 1 ELSE 0 END) AS zero_days
FROM zero_analysis
GROUP BY product_id
HAVING zero_days > 0;

-- Fix: Remove edge zeros
SELECT * FROM TS_DROP_EDGE_ZEROS('sales', product_id, date, sales_amount);
```

### Issue 6: Outliers

**Problem**: Extreme values distorting the pattern

```sql
-- Detect outliers using IQR method
WITH series_bounds AS (
    SELECT 
        product_id,
        QUANTILE_CONT(sales_amount, 0.25) AS q1,
        QUANTILE_CONT(sales_amount, 0.75) AS q3,
        QUANTILE_CONT(sales_amount, 0.75) - QUANTILE_CONT(sales_amount, 0.25) AS iqr
    FROM sales
    WHERE sales_amount IS NOT NULL
    GROUP BY product_id
),
outliers AS (
    SELECT 
        s.product_id,
        s.date,
        s.sales_amount,
        CASE 
            WHEN s.sales_amount > b.q3 + 1.5 * b.iqr THEN 'Upper outlier'
            WHEN s.sales_amount < b.q1 - 1.5 * b.iqr THEN 'Lower outlier'
            ELSE 'Normal'
        END AS outlier_type
    FROM sales s
    JOIN series_bounds b ON s.product_id = b.product_id
)
SELECT product_id, COUNT(*) AS n_outliers
FROM outliers
WHERE outlier_type != 'Normal'
GROUP BY product_id
HAVING COUNT(*) > 0;

-- Fix: Cap outliers (keep them but reduce impact)
-- (Would use TS_CAP_OUTLIERS_IQR if it was in integrated macros)
WITH series_bounds AS (
    SELECT 
        product_id,
        QUANTILE_CONT(sales_amount, 0.25) AS q1,
        QUANTILE_CONT(sales_amount, 0.75) AS q3,
        (QUANTILE_CONT(sales_amount, 0.75) - QUANTILE_CONT(sales_amount, 0.25)) AS iqr
    FROM sales
    WHERE sales_amount IS NOT NULL
    GROUP BY product_id
)
SELECT 
    s.product_id,
    s.date,
    CASE 
        WHEN s.sales_amount > b.q3 + 1.5 * b.iqr THEN b.q3 + 1.5 * b.iqr
        WHEN s.sales_amount < b.q1 - 1.5 * b.iqr THEN b.q1 - 1.5 * b.iqr
        ELSE s.sales_amount
    END AS sales_amount
FROM sales s
JOIN series_bounds b ON s.product_id = b.product_id;
```

### Issue 7: Different End Dates

**Problem**: Series end on different dates

**Solution**: Use `TS_FILL_FORWARD` to align end dates

```sql
-- Detect
WITH end_dates AS (
    SELECT 
        MAX(end_date) AS latest_date,
        COUNT(DISTINCT end_date) AS n_different_ends
    FROM sales_stats
)
SELECT * FROM end_dates;

-- Fix: Extend all series to common date
CREATE TABLE sales_aligned AS
SELECT * FROM TS_FILL_FORWARD(
    'sales',
    product_id,
    date,
    sales_amount,
    (SELECT MAX(date) FROM sales),  -- Latest date
    '1d'
);
```

[↑ Go to top](#eda--data-preparation---complete-workflow-guide)

---

## Preparation Checklist

### Before Forecasting

- [ ] Check data quality: `TS_STATS()`, `TS_DATA_QUALITY()`
- [ ] Fill time gaps: `TS_FILL_GAPS()`
- [ ] Fill up to end date: `TS_FILL_FORWARD()`
- [ ] Handle missing values: `TS_FILL_NULLS_*()`
- [ ] Remove constant series: `TS_DROP_CONSTANT()`
- [ ] Check minimum length: `TS_DROP_SHORT()`
- [ ] Remove leading zeros: `TS_DROP_LEADING_ZEROS()`
- [ ] Detect seasonality: `TS_DETECT_SEASONALITY()`
- [ ] Detect changepoints: `TS_DETECT_CHANGEPOINTS_BY()`
- [ ] Remove edge zeros: `TS_DROP_EDGE_ZEROS()` (if applicable)
- [ ] Validate: Re-run `TS_STATS()` on prepared data

### Quality Gates

Define minimum standards:

```sql
-- Only forecast high-quality series
WITH quality_check AS (
    SELECT series_id
    FROM sales_stats
    WHERE quality_score >= 0.7        -- High quality
      AND length >= 60                -- Sufficient history
      AND n_unique_values > 5         -- Not near-constant
      AND intermittency < 0.30        -- Not too sparse
)
SELECT s.*
FROM sales_prepared s
WHERE s.product_id IN (SELECT series_id FROM quality_check);
```

[↑ Go to top](#eda--data-preparation---complete-workflow-guide)
