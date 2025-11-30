# EDA & Data Preparation - Complete Workflow Guide

## Introduction

Data quality directly impacts forecast accuracy. This guide covers exploratory data analysis, data quality assessment, and data preparation using SQL macros that operate on time series at scale.

**Note**: This guide follows the API documentation in `API_REFERENCE.md`, which is the authoritative source for function signatures, parameters, and behavior. For complete function reference, see the [API Reference](../docs/API_REFERENCE.md).

**API Coverage**:

- **5 EDA macros**: `anofox_fcst_ts_stats`, `anofox_fcst_ts_stats_summary`, `anofox_fcst_ts_quality_report` (plus seasonality and changepoint detection)
- **2 Data Quality macros**: `anofox_fcst_ts_data_quality`, `anofox_fcst_ts_data_quality_summary`
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
     - [anofox_fcst_ts_fill_gaps](#ts_fill_gaps)
     - [anofox_fcst_ts_fill_forward](#ts_fill_forward)
   - [Series Filtering](#series-filtering)
     - [anofox_fcst_ts_drop_constant](#ts_drop_constant)
     - [anofox_fcst_ts_drop_short](#ts_drop_short)
   - [Edge Cleaning](#edge-cleaning)
     - [anofox_fcst_ts_drop_leading_zeros](#ts_drop_leading_zeros)
     - [anofox_fcst_ts_drop_trailing_zeros](#ts_drop_trailing_zeros)
     - [anofox_fcst_ts_drop_edge_zeros](#ts_drop_edge_zeros)
   - [Missing Value Imputation](#missing-value-imputation)
     - [anofox_fcst_ts_fill_nulls_const](#ts_fill_nulls_const)
     - [anofox_fcst_ts_fill_nulls_forward](#ts_fill_nulls_forward)
     - [anofox_fcst_ts_fill_nulls_backward](#ts_fill_nulls_backward)
     - [anofox_fcst_ts_fill_nulls_mean](#ts_fill_nulls_mean)
4. [Complete Workflow Examples](#complete-workflow-examples)
5. [Common Data Issues & Solutions](#common-data-issues--solutions)
6. [Preparation Checklist](#preparation-checklist)

---

## Exploratory Data Analysis

### Per-Series Statistics

**anofox_fcst_ts_stats**

Computes per-series statistical metrics including length, date ranges, central tendencies (mean, median), dispersion (std), value distributions (min, max, zeros), and quality indicators (nulls, uniqueness, constancy). Returns 24 metrics per series for exploratory analysis and data profiling.

**Example:**

```sql
-- Create sample sales data
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

-- Compute comprehensive stats for all series
CREATE TABLE sales_stats AS
SELECT * FROM anofox_fcst_ts_stats('sales_raw', product_id, date, sales_amount, '1d');

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

**anofox_fcst_ts_stats_summary**

Aggregates statistics across all series from anofox_fcst_ts_stats output. Computes dataset-level metrics including total series count, total observations, average series length, and date span. Provides high-level overview for dataset characterization.

**Example:**

```sql
-- Create sample sales data
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

-- Generate statistics first
CREATE TABLE sales_stats AS
SELECT * FROM anofox_fcst_ts_stats('sales_raw', product_id, date, sales_amount, '1d');

-- Get overall picture
SELECT * FROM anofox_fcst_ts_stats_summary('sales_stats');
```

### Quality Assessment

**anofox_fcst_ts_quality_report**

Generates quality assessment report from anofox_fcst_ts_stats output. Evaluates series against configurable thresholds for gaps, missing values, constant series, short series, and temporal alignment. Identifies series requiring data preparation steps.

**Example:**

```sql
-- Create sample sales data
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

-- Generate statistics first
CREATE TABLE sales_stats AS
SELECT * FROM anofox_fcst_ts_stats('sales_raw', product_id, date, sales_amount, '1d');

-- Comprehensive quality checks (anofox_fcst_ts_quality_report now implemented)
SELECT * FROM anofox_fcst_ts_quality_report('sales_stats', 30);
```

[↑ Go to top](#eda--data-preparation---complete-workflow-guide)

---

## Data Quality

### Comprehensive Assessment

**anofox_fcst_ts_data_quality**

Assesses data quality across four dimensions (Structural, Temporal, Magnitude, Behavioural) for each time series. Returns per-series metrics including key uniqueness, timestamp gaps, missing values, value distributions, and pattern characteristics. Output is normalized by dimension and metric for cross-series comparison.

**Signature (Function Overloading):**

```sql
-- For DATE/TIMESTAMP columns (date-based frequency)
anofox_fcst_ts_data_quality(
    table_name      VARCHAR,
    unique_id_col   ANY,
    date_col        DATE | TIMESTAMP,
    value_col       DOUBLE,
    n_short         INTEGER,
    frequency       VARCHAR
) → TABLE

-- For INTEGER columns (integer-based frequency)
anofox_fcst_ts_data_quality(
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
-- Create sample sales data with gaps and missing values
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

-- DATE/TIMESTAMP columns: Use VARCHAR frequency strings
-- Generate comprehensive health card (n_short parameter defaults to 30 if NULL)
CREATE TABLE health_card AS
SELECT * FROM anofox_fcst_ts_data_quality('sales_raw', product_id, date, sales_amount, 30, '1d');

-- View all issues
SELECT * FROM health_card ORDER BY dimension, metric;

-- Filter specific issues
SELECT * FROM anofox_fcst_ts_data_quality('sales_raw', product_id, date, sales_amount, 30, '1d')
WHERE dimension = 'Temporal' AND metric = 'timestamp_gaps'
LIMIT 5;

-- INTEGER columns: Use INTEGER frequency values
-- Create sample integer-based time series (convert to DATE for compatibility)
CREATE TABLE int_data AS
SELECT 
    series_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date_col,
    CASE 
        WHEN RANDOM() < 0.05 THEN NULL
        ELSE 100 + 10 * SIN(2 * PI() * d / 10) + (RANDOM() * 5)
    END AS value
FROM generate_series(1, 100) t(d)
CROSS JOIN (VALUES (1), (2), (3)) series(series_id);

SELECT * FROM anofox_fcst_ts_data_quality('int_data', series_id, date_col, value, 30, '1d')
WHERE dimension = 'Magnitude' AND metric = 'missing_values'
LIMIT 5;
```

### Summary by Dimension

**anofox_fcst_ts_data_quality_summary**

Aggregates quality metrics across all series, grouped by dimension and metric. Computes summary statistics (counts, percentages) for each quality dimension to provide dataset-level quality overview. Useful for identifying systemic data quality issues affecting multiple series.

**Signature:**

```sql
anofox_fcst_ts_data_quality_summary(
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
-- Create sample sales data
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

-- Get summary by dimension (n_short parameter defaults to 30 if NULL)
SELECT * FROM anofox_fcst_ts_data_quality_summary('sales_raw', product_id, date, sales_amount, 30);
```

[↑ Go to top](#eda--data-preparation---complete-workflow-guide)

---

## Data Preparation

SQL macros for data cleaning and transformation. Date type support varies by function.

### Gap Filling

#### anofox_fcst_ts_fill_gaps

**Fill Missing Timestamps**

Fills missing timestamps/indices in series with NULL values using the specified frequency interval or step size.

**Signature (Function Overloading):**

```sql
-- For DATE/TIMESTAMP columns (date-based frequency)
anofox_fcst_ts_fill_gaps(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP,
    value_col     DOUBLE,
    frequency     VARCHAR
) → TABLE

-- For INTEGER columns (integer-based frequency)
anofox_fcst_ts_fill_gaps(
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
-- Create sample sales data with gaps
CREATE TABLE sales_raw AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20) AS sales_amount
FROM generate_series(0, 364) t(d)
CROSS JOIN (VALUES ('P001'), ('P002')) products(product_id)
WHERE d % 3 != 0;  -- Create gaps by skipping some days

-- DATE/TIMESTAMP columns: Use VARCHAR frequency strings
-- Fill gaps with daily frequency (default)
CREATE TABLE fixed AS
SELECT * FROM anofox_fcst_ts_fill_gaps('sales_raw', product_id, date, sales_amount, '1d');

-- Create hourly data with gaps
CREATE TABLE hourly_data AS
SELECT 
    series_id,
    TIMESTAMP '2024-01-01 00:00:00' + INTERVAL (h) HOUR AS timestamp,
    50 + 20 * SIN(2 * PI() * h / 24) + (RANDOM() * 10) AS value
FROM generate_series(0, 167) t(h)  -- 7 days
CROSS JOIN (VALUES (1), (2)) series(series_id)
WHERE h % 2 != 0;  -- Create gaps

-- Fill gaps with 30-minute frequency
SELECT * FROM anofox_fcst_ts_fill_gaps('hourly_data', series_id, timestamp, value, '30m');

-- Create weekly data
CREATE TABLE weekly_data AS
SELECT 
    series_id,
    DATE '2024-01-01' + INTERVAL (w * 7) DAY AS date,
    200 + 50 * SIN(2 * PI() * w / 52) + (RANDOM() * 30) AS value
FROM generate_series(0, 51) t(w)
CROSS JOIN (VALUES (1), (2)) series(series_id)
WHERE w % 2 != 0;  -- Create gaps

-- Fill gaps with weekly frequency
SELECT * FROM anofox_fcst_ts_fill_gaps('weekly_data', series_id, date, value, '1w');

-- Use NULL (must cast to VARCHAR for DATE/TIMESTAMP columns)
CREATE TABLE daily_data AS
SELECT 
    series_id,
    DATE '2024-01-01' + INTERVAL (d) DAY AS date,
    100 + (RANDOM() * 20) AS value
FROM generate_series(0, 30) t(d)
CROSS JOIN (VALUES (1)) series(series_id)
WHERE d % 2 != 0;

SELECT * FROM anofox_fcst_ts_fill_gaps('daily_data', series_id, date, value, NULL::VARCHAR);

-- INTEGER columns: Use INTEGER frequency values
-- Create integer-based time series
CREATE TABLE int_data AS
SELECT 
    series_id,
    d AS date_col,
    100 + 10 * SIN(2 * PI() * d / 10) + (RANDOM() * 5) AS value
FROM generate_series(1, 100) t(d)
CROSS JOIN (VALUES (1), (2)) series(series_id)
WHERE d % 3 != 0;  -- Create gaps

-- Fill gaps with step size of 1
SELECT * FROM anofox_fcst_ts_fill_gaps('int_data', series_id, date_col, value, 1);

-- Fill gaps with step size of 2
SELECT * FROM anofox_fcst_ts_fill_gaps('int_data', series_id, date_col, value, 2);

-- Use NULL (defaults to step size 1 for INTEGER columns)
SELECT * FROM anofox_fcst_ts_fill_gaps('int_data', series_id, date_col, value, NULL);
```

#### anofox_fcst_ts_fill_forward

**Extend Series to Target Date**

Extends series to target date/index, filling gaps with NULL using the specified frequency interval or step size.

**Signature (Function Overloading):**

```sql
-- For DATE/TIMESTAMP columns (date-based frequency)
anofox_fcst_ts_fill_forward(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP,
    value_col     DOUBLE,
    target_date   DATE | TIMESTAMP,
    frequency     VARCHAR
) → TABLE

-- For INTEGER columns (integer-based frequency)
anofox_fcst_ts_fill_forward(
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
- `frequency`: Same as `anofox_fcst_ts_fill_gaps` (see above)

**Type Validation:**

- Same as `anofox_fcst_ts_fill_gaps` (see above)

**Examples:**

```sql
-- Create sample sales data
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20) AS sales_amount
FROM generate_series(0, 300) t(d)  -- Data until Oct 2023
CROSS JOIN (VALUES ('P001'), ('P002')) products(product_id);

-- DATE/TIMESTAMP columns: Use VARCHAR frequency strings
-- Create hourly data
CREATE TABLE hourly_data AS
SELECT 
    series_id,
    TIMESTAMP '2024-01-01 00:00:00' + INTERVAL (h) HOUR AS timestamp,
    50 + 20 * SIN(2 * PI() * h / 24) + (RANDOM() * 10) AS value
FROM generate_series(0, 100) t(h)
CROSS JOIN (VALUES (1), (2)) series(series_id);

-- Extend hourly series to target date
SELECT * FROM anofox_fcst_ts_fill_forward('hourly_data', series_id, timestamp, value, '2024-12-31'::TIMESTAMP, '1h');

-- Create monthly data
CREATE TABLE monthly_data AS
SELECT 
    series_id,
    DATE '2024-01-01' + INTERVAL (m) MONTH AS date,
    200 + 50 * SIN(2 * PI() * m / 12) + (RANDOM() * 30) AS value
FROM generate_series(0, 10) t(m)
CROSS JOIN (VALUES (1), (2)) series(series_id);

-- Extend monthly series to target date
SELECT * FROM anofox_fcst_ts_fill_forward('monthly_data', series_id, date, value, '2024-12-01'::DATE, '1mo');

-- Extend daily series to target date (default frequency)
CREATE TABLE sales_extended AS
SELECT * FROM anofox_fcst_ts_fill_forward(
    'sales', product_id, date, sales_amount, 
    DATE '2023-12-31', '1d'
);

-- Use NULL (must cast to VARCHAR for DATE/TIMESTAMP columns)
CREATE TABLE daily_data AS
SELECT 
    series_id,
    DATE '2024-01-01' + INTERVAL (d) DAY AS date,
    100 + (RANDOM() * 20) AS value
FROM generate_series(0, 30) t(d)
CROSS JOIN (VALUES (1)) series(series_id);

SELECT * FROM anofox_fcst_ts_fill_forward('daily_data', series_id, date, value, '2024-12-31'::DATE, NULL::VARCHAR);

-- INTEGER columns: Use INTEGER frequency values
-- Create integer-based time series
CREATE TABLE int_data AS
SELECT 
    series_id,
    d AS date_col,
    100 + 10 * SIN(2 * PI() * d / 10) + (RANDOM() * 5) AS value
FROM generate_series(1, 50) t(d)
CROSS JOIN (VALUES (1), (2)) series(series_id);

-- Extend series to index 100 with step size of 1
SELECT * FROM anofox_fcst_ts_fill_forward('int_data', series_id, date_col, value, 100, 1);

-- Extend series to index 100 with step size of 5
SELECT * FROM anofox_fcst_ts_fill_forward('int_data', series_id, date_col, value, 100, 5);

-- Use NULL (defaults to step size 1 for INTEGER columns)
SELECT * FROM anofox_fcst_ts_fill_forward('int_data', series_id, date_col, value, 100, NULL);
```

[↑ Go to top](#eda--data-preparation---complete-workflow-guide)

### Series Filtering

#### anofox_fcst_ts_drop_constant

**Remove Constant Series**

Removes series with constant values (no variation).

**Signature:**

```sql
anofox_fcst_ts_drop_constant(
    table_name    VARCHAR,
    group_col     ANY,
    value_col     DOUBLE
) → TABLE
```

> [!WARNING]
> This function may drop intermittent demand series (series with many zeros) as long as gaps have not been filled yet (e.g., with zeros via gap filling functions). If you need to preserve intermittent series, ensure gaps are being filled.

**Example:**

```sql
-- Create sample sales data with some constant series
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN product_id = 'P001' THEN 100.0  -- Constant series
        WHEN product_id = 'P002' THEN 50.0 + 10 * SIN(2 * PI() * d / 7)  -- Variable series
        ELSE 75.0  -- Another constant series
    END AS sales_amount
FROM generate_series(0, 90) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

-- Generate stats to detect constant series
CREATE TABLE sales_stats AS
SELECT * FROM anofox_fcst_ts_stats('sales', product_id, date, sales_amount, '1d');

-- Detect constant series
SELECT * FROM sales_stats WHERE is_constant = true;

-- Remove constant series
CREATE TABLE sales_no_constant AS
SELECT * FROM anofox_fcst_ts_drop_constant('sales', product_id, sales_amount);

-- Verify result
SELECT DISTINCT product_id FROM sales_no_constant;
```

#### anofox_fcst_ts_drop_short

**Remove Short Series**

Removes series below minimum length.

**Signature:**

```sql
anofox_fcst_ts_drop_short(
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
-- Create sample sales data with some short series
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20) AS sales_amount
FROM generate_series(0, 90) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id)
WHERE 
    (product_id = 'P001' AND d <= 10) OR  -- Short series (11 days)
    (product_id = 'P002' AND d <= 20) OR  -- Short series (21 days)
    (product_id = 'P003' AND d <= 90);    -- Long series (91 days)

-- Remove series with less than 30 observations
CREATE TABLE sales_long_enough AS
SELECT * FROM anofox_fcst_ts_drop_short('sales', product_id, 30);

-- Verify result
SELECT DISTINCT product_id FROM sales_long_enough;
```

[↑ Go to top](#eda--data-preparation---complete-workflow-guide)

### Edge Cleaning

#### anofox_fcst_ts_drop_leading_zeros

**Remove Leading Zeros**

Removes leading zeros from time series.

**Signature:**

```sql
anofox_fcst_ts_drop_leading_zeros(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE
) → TABLE
```

> [!WARNING]
> Results may differ if `anofox_fcst_ts_fill_gaps` or `anofox_fcst_ts_fill_forward` has been applied, as these functions may introduce zeros or other values in previously missing timestamps.

**Example:**

```sql
-- Create sample sales data with leading zeros
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN d < 10 THEN 0.0  -- Leading zeros
        ELSE 100 + 50 * SIN(2 * PI() * (d - 10) / 7) + (RANDOM() * 20)
    END AS sales_amount
FROM generate_series(0, 90) t(d)
CROSS JOIN (VALUES ('P001'), ('P002')) products(product_id);

-- Remove leading zeros
CREATE TABLE sales_no_leading_zeros AS
SELECT * FROM anofox_fcst_ts_drop_leading_zeros('sales', product_id, date, sales_amount);

-- Verify result (should start from day 10)
SELECT 
    product_id,
    MIN(date) AS first_date,
    COUNT(*) AS remaining_days
FROM sales_no_leading_zeros
GROUP BY product_id;
```

#### anofox_fcst_ts_drop_trailing_zeros

**Remove Trailing Zeros**

Removes trailing zeros from time series.

**Signature:**

```sql
anofox_fcst_ts_drop_trailing_zeros(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE
) → TABLE
```

> [!WARNING]
> Results may differ if `anofox_fcst_ts_fill_gaps` or `anofox_fcst_ts_fill_forward` has been applied, as these functions may introduce zeros or other values in previously missing timestamps.

**Example:**

```sql
-- Create sample sales data with trailing zeros
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN d > 80 THEN 0.0  -- Trailing zeros
        ELSE 100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20)
    END AS sales_amount
FROM generate_series(0, 90) t(d)
CROSS JOIN (VALUES ('P001'), ('P002')) products(product_id);

-- Remove trailing zeros
CREATE TABLE sales_no_trailing_zeros AS
SELECT * FROM anofox_fcst_ts_drop_trailing_zeros('sales', product_id, date, sales_amount);

-- Verify result (should end at day 80)
SELECT 
    product_id,
    MAX(date) AS last_date,
    COUNT(*) AS remaining_days
FROM sales_no_trailing_zeros
GROUP BY product_id;
```

#### anofox_fcst_ts_drop_edge_zeros

**Remove Both Leading and Trailing Zeros**

Removes both leading and trailing zeros from time series.

**Signature:**

```sql
anofox_fcst_ts_drop_edge_zeros(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE
) → TABLE
```

> [!WARNING]
> Results may differ if `anofox_fcst_ts_fill_gaps` or `anofox_fcst_ts_fill_forward` has been applied, as these functions may introduce zeros or other values in previously missing timestamps.

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
SELECT * FROM anofox_fcst_ts_drop_edge_zeros('sales', product_id, date, sales_amount);
```

[↑ Go to top](#eda--data-preparation---complete-workflow-guide)

### Missing Value Imputation

#### anofox_fcst_ts_fill_nulls_const

**Fill with Constant Value**

Fills NULL values with a specified constant value.

**Signature:**

```sql
anofox_fcst_ts_fill_nulls_const(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE,
    fill_value    DOUBLE
) → TABLE
```

**Example:**

```sql
-- Create sample sales data with NULL values
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN RANDOM() < 0.15 THEN NULL  -- 15% missing
        ELSE 100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20)
    END AS sales_amount
FROM generate_series(0, 90) t(d)
CROSS JOIN (VALUES ('P001'), ('P002')) products(product_id);

-- Fill NULLs with 0
CREATE TABLE sales_filled_zero AS
SELECT 
    product_id,
    date,
    value_col AS sales_amount
FROM anofox_fcst_ts_fill_nulls_const('sales', product_id, date, sales_amount, 0.0);

-- Fill NULLs with a specific value (e.g., -1 for missing data indicator)
CREATE TABLE sales_filled_marker AS
SELECT 
    product_id,
    date,
    value_col AS sales_amount
FROM anofox_fcst_ts_fill_nulls_const('sales', product_id, date, sales_amount, -1.0);

-- Verify results
SELECT 
    product_id,
    COUNT(*) AS total_rows,
    COUNT(sales_amount) AS non_null_rows,
    SUM(CASE WHEN sales_amount = 0.0 THEN 1 ELSE 0 END) AS zero_count
FROM sales_filled_zero
GROUP BY product_id;
```

#### anofox_fcst_ts_fill_nulls_forward

**Forward Fill (Last Observation Carried Forward)**

Uses `LAST_VALUE(... IGNORE NULLS)` window function to forward fill NULL values.

**Signature:**

```sql
anofox_fcst_ts_fill_nulls_forward(
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
SELECT * FROM anofox_fcst_ts_fill_nulls_forward('sales', product_id, date, sales_amount);
```

**Alternative Example:**

```sql
-- Create sample sales data with NULL values
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN RANDOM() < 0.15 THEN NULL  -- 15% missing
        ELSE 100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20)
    END AS sales_amount
FROM generate_series(0, 90) t(d)
CROSS JOIN (VALUES ('P001'), ('P002')) products(product_id);

-- Generate stats for reference
CREATE TABLE sales_stats AS
SELECT * FROM anofox_fcst_ts_stats('sales', product_id, date, sales_amount, '1d');

-- Option A: Forward fill (use last known value)
SELECT * FROM anofox_fcst_ts_fill_nulls_forward('sales', product_id, date, sales_amount);

-- Option B: Mean imputation
SELECT * FROM anofox_fcst_ts_fill_nulls_mean('sales', product_id, date, sales_amount);

-- Option C: Drop series with too many nulls
WITH clean AS (
    SELECT * FROM sales_stats WHERE n_null < length * 0.05  -- < 5% missing
)
SELECT s.*
FROM sales s
WHERE s.product_id IN (SELECT series_id FROM clean);
```

#### anofox_fcst_ts_fill_nulls_backward

**Backward Fill**

Uses `FIRST_VALUE(... IGNORE NULLS)` window function to backward fill NULL values.

**Signature:**

```sql
anofox_fcst_ts_fill_nulls_backward(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE
) → TABLE
```

**Example:**

```sql
-- Create sample sales data with NULL values
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN RANDOM() < 0.15 THEN NULL  -- 15% missing
        ELSE 100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20)
    END AS sales_amount
FROM generate_series(0, 90) t(d)
CROSS JOIN (VALUES ('P001'), ('P002')) products(product_id);

-- Backward fill (use next known value)
CREATE TABLE sales_backward_filled AS
SELECT 
    product_id,
    date,
    value_col AS sales_amount
FROM anofox_fcst_ts_fill_nulls_backward('sales', product_id, date, sales_amount);

-- Verify results (should have no NULLs)
SELECT 
    product_id,
    COUNT(*) AS total_rows,
    COUNT(sales_amount) AS non_null_rows
FROM sales_backward_filled
GROUP BY product_id;
```

#### anofox_fcst_ts_fill_nulls_mean

**Fill with Series Mean**

Computes mean per series and fills NULLs with that mean value.

**Signature:**

```sql
anofox_fcst_ts_fill_nulls_mean(
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
SELECT * FROM anofox_fcst_ts_fill_nulls_mean('sales', product_id, date, sales_amount);
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
-- Create sample raw sales data
CREATE TABLE sales_raw AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN RANDOM() < 0.05 THEN NULL
        ELSE 100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20)
    END AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

-- All-in-one preparation (if standard pipeline was implemented)
-- Step 1: Fill time gaps
CREATE TEMP TABLE step1 AS
SELECT 
    group_col AS product_id,
    date_col AS date,
    value_col AS sales_amount
FROM anofox_fcst_ts_fill_gaps('sales_raw', product_id, date, sales_amount, '1d');

-- Step 2: Drop constant series
CREATE TEMP TABLE step2 AS
SELECT * FROM anofox_fcst_ts_drop_constant('step1', product_id, sales_amount);

-- Step 3: Drop short series
CREATE TEMP TABLE step3 AS
SELECT * FROM anofox_fcst_ts_drop_short('step2', product_id, 30);

-- Step 4: Remove leading zeros
CREATE TEMP TABLE step4 AS
SELECT * FROM anofox_fcst_ts_drop_leading_zeros('step3', product_id, date, sales_amount);

-- Step 5: Fill remaining nulls
CREATE TABLE sales_prepared AS
SELECT * FROM anofox_fcst_ts_fill_nulls_forward('step4', product_id, date, sales_amount);
```

### Custom Pipeline (Advanced)

Tailor to your specific needs:

```sql
-- Create sample raw sales data
CREATE TABLE sales_raw AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN RANDOM() < 0.05 THEN NULL
        ELSE 100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20)
    END AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

-- Fill gaps first
CREATE TEMP TABLE filled AS
SELECT 
    group_col AS product_id,
    date_col AS date,
    value_col AS sales_amount
FROM anofox_fcst_ts_fill_gaps('sales_raw', product_id, date, sales_amount, '1d');

-- Remove edge zeros
CREATE TEMP TABLE no_edges AS
SELECT * FROM anofox_fcst_ts_drop_edge_zeros('filled', product_id, date, sales_amount);

-- Fill nulls with interpolation (more sophisticated)
CREATE TABLE sales_custom_prep AS
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
FROM no_edges;
```

### Automated Data Prep Pipeline

```sql
-- Create sample stats table
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + product_id * 20 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

CREATE TABLE stats AS
SELECT * FROM anofox_fcst_ts_stats('sales', product_id, date, sales_amount, '1d');

-- Create sample raw sales data
CREATE TABLE sales_raw AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN RANDOM() < 0.05 THEN NULL
        ELSE 100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20)
    END AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

-- Create a reusable preparation view
CREATE VIEW sales_autoprepared AS
WITH stats AS (
    SELECT * FROM anofox_fcst_ts_stats('sales_raw', product_id, date, sales_amount, '1d')
),
quality_series AS (
    SELECT series_id FROM stats WHERE length >= 30  -- Keep series with at least 30 observations
),
filled_temp AS (
    SELECT 
        group_col,
        date_col,
        value_col
    FROM anofox_fcst_ts_fill_gaps('sales_raw', product_id, date, sales_amount, '1d')
),
filled_temp2 AS (
    SELECT f.*
    FROM filled_temp f
    WHERE f.group_col::VARCHAR IN (SELECT series_id::VARCHAR FROM quality_series)
),
no_constant_temp AS (
    SELECT 
        group_col,
        date_col,
        value_col
    FROM anofox_fcst_ts_drop_constant('filled_temp2', group_col, value_col)
),
no_constant AS (
    SELECT 
        group_col AS product_id,
        date_col AS date,
        value_col AS sales_amount
    FROM no_constant_temp
),
complete_temp AS (
    SELECT * FROM anofox_fcst_ts_fill_nulls_forward('no_constant', product_id, date, sales_amount)
),
complete AS (
    SELECT 
        product_id,
        date,
        value_col AS sales_amount
    FROM complete_temp
)
SELECT * FROM complete;

-- Use in forecasting
SELECT * FROM anofox_fcst_ts_forecast_by('sales_autoprepared', product_id, date, sales_amount,
                             'AutoETS', 28, MAP{'seasonal_period': 7});
```

### Validate Preparation

Compare before/after:

```sql
-- Create sample raw data
CREATE TABLE sales_raw AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN RANDOM() < 0.05 THEN NULL
        ELSE 100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20)
    END AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

-- Create stats for raw data
CREATE TABLE sales_stats AS
SELECT * FROM anofox_fcst_ts_stats('sales_raw', product_id, date, sales_amount, '1d');

-- Prepare data (fill gaps, drop constants, etc.)
CREATE TABLE sales_prepared AS
SELECT 
    group_col AS product_id,
    date_col AS date,
    value_col AS sales_amount
FROM anofox_fcst_ts_fill_gaps('sales_raw', product_id, date, sales_amount, '1d');

-- Generate stats for prepared data
CREATE TABLE prepared_stats AS
SELECT * FROM anofox_fcst_ts_stats('sales_prepared', product_id, date, sales_amount, '1d');

-- Compare quality
SELECT 
    'Raw data' AS stage,
    COUNT(*) AS num_series,
    ROUND(AVG(CAST(length AS DOUBLE)), 2) AS avg_length,
    SUM(CASE WHEN n_null > 0 THEN 1 ELSE 0 END) AS series_with_nulls,
    SUM(CASE WHEN expected_length > length THEN 1 ELSE 0 END) AS series_with_gaps,
    SUM(CASE WHEN is_constant THEN 1 ELSE 0 END) AS constant_series
FROM sales_stats
UNION ALL
SELECT 
    'Prepared',
    COUNT(*),
    ROUND(AVG(CAST(length AS DOUBLE)), 2),
    SUM(CASE WHEN n_null > 0 THEN 1 ELSE 0 END),
    SUM(CASE WHEN expected_length > length THEN 1 ELSE 0 END),
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

**Solution**: Use `anofox_fcst_ts_fill_gaps` to fill missing timestamps

```sql
-- Create sample sales data with gaps
CREATE TABLE sales_raw AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20) AS sales_amount
FROM generate_series(0, 364) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id)
WHERE d % 3 != 0;  -- Create gaps by skipping some days

-- Generate statistics
CREATE TABLE sales_stats AS
SELECT * FROM anofox_fcst_ts_stats('sales_raw', product_id, date, sales_amount, '1d');

-- Detect gaps (check expected_length vs length)
SELECT 
    series_id, 
    expected_length - length AS n_gaps,
    length,
    expected_length
FROM sales_stats
WHERE expected_length > length
ORDER BY (expected_length - length) DESC
LIMIT 10;

-- Fix: Fill gaps
CREATE TABLE fixed AS
SELECT * FROM anofox_fcst_ts_fill_gaps('sales_raw', product_id, date, sales_amount, '1d');
```

### Issue 2: Missing Values (NULLs)

**Problem**: Some values are NULL

**Solutions**: Multiple imputation options available

```sql
-- Create sample sales data with NULL values
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN RANDOM() < 0.15 THEN NULL  -- 15% missing
        ELSE 100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20)
    END AS sales_amount
FROM generate_series(0, 90) t(d)
CROSS JOIN (VALUES ('P001'), ('P002')) products(product_id);

-- Generate stats for reference
CREATE TABLE sales_stats AS
SELECT * FROM anofox_fcst_ts_stats('sales', product_id, date, sales_amount, '1d');

-- Option A: Forward fill (use last known value)
SELECT * FROM anofox_fcst_ts_fill_nulls_forward('sales', product_id, date, sales_amount);

-- Option B: Mean imputation
SELECT * FROM anofox_fcst_ts_fill_nulls_mean('sales', product_id, date, sales_amount);

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

**Solution**: Remove constant series with `anofox_fcst_ts_drop_constant`

```sql
-- Create sample sales data with constant series
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN product_id = 'P001' THEN 100.0  -- Constant series
        WHEN product_id = 'P002' THEN 50.0 + 10 * SIN(2 * PI() * d / 7)  -- Variable series
        ELSE 75.0  -- Another constant series
    END AS sales_amount
FROM generate_series(0, 90) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

-- Generate statistics
CREATE TABLE sales_stats AS
SELECT * FROM anofox_fcst_ts_stats('sales', product_id, date, sales_amount, '1d');

-- Detect constant series
SELECT * FROM sales_stats WHERE is_constant = true;

-- Fix: Remove constant series
SELECT * FROM anofox_fcst_ts_drop_constant('sales', product_id, sales_amount);
```

### Issue 4: Short Series

**Problem**: Not enough historical data

**Solution**: Filter short series with `anofox_fcst_ts_drop_short`

```sql
-- Create sample sales data with short series
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20) AS sales_amount
FROM generate_series(0, 90) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id)
WHERE 
    (product_id = 'P001' AND d <= 10) OR  -- Short series (11 days)
    (product_id = 'P002' AND d <= 20) OR  -- Short series (21 days)
    (product_id = 'P003' AND d <= 90);    -- Long series (91 days)

-- Generate statistics
CREATE TABLE sales_stats AS
SELECT * FROM anofox_fcst_ts_stats('sales', product_id, date, sales_amount, '1d');

-- Detect short series
SELECT * FROM sales_stats WHERE length < 30;

-- Fix: Drop short series
SELECT * FROM anofox_fcst_ts_drop_short('sales', product_id, 30);
```

### Issue 5: Leading/Trailing Zeros

**Problem**: Product not yet launched or discontinued

**Solution**: Remove edge zeros with `anofox_fcst_ts_drop_edge_zeros`

```sql
-- Create sample sales data with edge zeros
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN d < 10 THEN 0.0  -- Leading zeros
        WHEN d > 80 THEN 0.0  -- Trailing zeros
        ELSE 100 + 50 * SIN(2 * PI() * (d - 10) / 7) + (RANDOM() * 20)
    END AS sales_amount
FROM generate_series(0, 90) t(d)
CROSS JOIN (VALUES ('P001'), ('P002')) products(product_id);

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
SELECT * FROM anofox_fcst_ts_drop_edge_zeros('sales', product_id, date, sales_amount);
```

### Issue 6: Outliers

**Problem**: Extreme values distorting the pattern

```sql
-- Create sample sales data
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + product_id * 20 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 20) + 
    CASE WHEN RANDOM() < 0.05 THEN 200 ELSE 0 END AS sales_amount  -- Some outliers
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

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

**Solution**: Use `anofox_fcst_ts_fill_forward` to align end dates

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
SELECT * FROM anofox_fcst_ts_fill_forward(
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

- [ ] Check data quality: `anofox_fcst_ts_stats()`, `anofox_fcst_ts_data_quality()`
- [ ] Fill time gaps: `anofox_fcst_ts_fill_gaps()`
- [ ] Fill up to end date: `anofox_fcst_ts_fill_forward()`
- [ ] Handle missing values: `anofox_fcst_ts_fill_nulls_*()`
- [ ] Remove constant series: `anofox_fcst_ts_drop_constant()`
- [ ] Check minimum length: `anofox_fcst_ts_drop_short()`
- [ ] Remove leading zeros: `anofox_fcst_ts_drop_leading_zeros()`
- [ ] Detect seasonality: `anofox_fcst_ts_detect_seasonality()`
- [ ] Detect changepoints: `anofox_fcst_ts_detect_changepoints_by()`
- [ ] Remove edge zeros: `anofox_fcst_ts_drop_edge_zeros()` (if applicable)
- [ ] Validate: Re-run `anofox_fcst_ts_stats()` on prepared data

### Quality Gates

Define minimum standards:

```sql
-- Create sample sales and stats
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + product_id * 20 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

CREATE TABLE sales_stats AS
SELECT * FROM anofox_fcst_ts_stats('sales', product_id, date, sales_amount, '1d');

-- Create prepared sales data
CREATE TABLE sales_prepared AS
SELECT 
    group_col AS product_id,
    date_col AS date,
    value_col AS sales_amount
FROM anofox_fcst_ts_fill_gaps('sales', product_id, date, sales_amount, '1d');

-- Only forecast high-quality series
WITH quality_check AS (
    SELECT series_id
    FROM sales_stats
    WHERE length >= 60                -- Sufficient history
      AND n_unique_values > 5         -- Not near-constant
      AND is_constant = false         -- Not constant
)
SELECT s.*
FROM sales_prepared s
WHERE s.product_id::VARCHAR IN (SELECT series_id::VARCHAR FROM quality_check);
```

[↑ Go to top](#eda--data-preparation---complete-workflow-guide)
