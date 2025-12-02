# Anofox Forecast Extension - API Reference

**Version:** 0.2.0  
**DuckDB Version:** ≥ v1.4.2  
**Forecasting Engine:** anofox-time

---

## Overview

The Anofox Forecast extension provides comprehensive time series forecasting capabilities directly within DuckDB. All forecasting computations are performed by the **anofox-time** library, which implements efficient time series algorithms in C++.

### Key Features

- **31 Forecasting Models**: From simple naive methods to more advanced statistical models
- **Complete Workflow**: Exploratory data analysis, data quality, data preparation, forecasting, and evaluation
- **76+ Time Series Features**: tsfresh-compatible feature extraction
- **Native Parallelization**: Automatic GROUP BY parallelization across CPU cores
- **Multiple Function Types**: Table functions, aggregates, window functions, scalar functions

### Function Naming Conventions

Functions follow consistent naming patterns:
- `anofox_fcst_ts_*` prefix for all forecast extension functions
- `anofox_fcst_ts_forecast*` for forecasting operations
- `anofox_fcst_ts_*_by` suffix for multi-series operations with GROUP BY
- `anofox_fcst_ts_*_agg` suffix for aggregate functions (internal/low-level)

**Aliases**: All functions are also available without the `anofox_fcst_` prefix for backward compatibility. For example:
- `anofox_fcst_ts_forecast` and `ts_forecast` (both work)
- `anofox_fcst_ts_mae` and `ts_mae` (both work)

### Parameter Conventions

**Important**: All functions use **positional parameters**, NOT named parameters (`:=` syntax).

**Common Parameter Types**:
- `table_name`: Source table - `VARCHAR`
- `date_col`: Date/timestamp column - `DATE`, `TIMESTAMP`, or `INTEGER`
- `value_col`: Time series values - `DOUBLE`
- `group_col`: Grouping column - `ANY` (preserved type)
- `params`: Configuration - `MAP` with string keys and various value types

**Standard Options MAP Keys**:
| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `confidence_level` | DOUBLE | 0.90 | Confidence level for prediction intervals (0-1) |
| `return_insample` | BOOLEAN | false | Return fitted values in output |
| `generate_timestamps` | BOOLEAN | true | Generate forecast timestamps based on training intervals |
| `seasonal_period` | INTEGER | - | Seasonal cycle length (required for seasonal models) |
| `seasonal_periods` | INTEGER[] | - | Multiple seasonal periods (for multi-seasonality models) |

---

## Table of Contents

1. [Exploratory Data Analysis](#exploratory-data-analysis)
   - [Per-Series Statistics](#per-series-statistics)
   - [Quality Assessment](#quality-assessment)
   - [Dataset Summary](#dataset-summary)
2. [Data Quality](#data-quality)
   - [Comprehensive Assessment](#comprehensive-assessment)
   - [Summary by Dimension](#summary-by-dimension)
3. [Data Preparation](#data-preparation)
   - [Gap Filling](#gap-filling)
   - [Series Filtering](#series-filtering)
   - [Edge Cleaning](#edge-cleaning)
   - [Missing Value Imputation](#missing-value-imputation)
4. [Seasonality](#seasonality)
   - [Simple Seasonality Detection](#simple-seasonality-detection)
   - [Detailed Seasonality Analysis](#detailed-seasonality-analysis)
5. [Changepoint Detection](#changepoint-detection)
   - [Single Series Changepoint Detection](#single-series-changepoint-detection)
   - [Multiple Series Changepoint Detection](#multiple-series-changepoint-detection)
   - [Aggregate Function for Changepoint Detection](#aggregate-function-for-changepoint-detection)
6. [Time Series Features](#time-series-features)
   - [Extract Time Series Features](#extract-time-series-features)
   - [List Available Features](#list-available-features)
   - [Load Feature Configuration from JSON](#load-feature-configuration-from-json)
   - [Load Feature Configuration from CSV](#load-feature-configuration-from-csv)
7. [Forecasting](#forecasting)
   - [Single Time Series Forecasting](#single-time-series-forecasting)
   - [Multiple Time Series Forecasting](#multiple-time-series-forecasting)
   - [Aggregate Function for Custom GROUP BY](#aggregate-function-for-custom-group-by)
8. [Evaluation](#evaluation)
   - [Mean Absolute Error](#mean-absolute-error)
   - [Mean Squared Error](#mean-squared-error)
   - [Root Mean Squared Error](#root-mean-squared-error)
   - [Mean Absolute Percentage Error](#mean-absolute-percentage-error)
   - [Symmetric Mean Absolute Percentage Error](#symmetric-mean-absolute-percentage-error)
   - [Mean Absolute Scaled Error](#mean-absolute-scaled-error)
   - [R-squared](#r-squared)
   - [Forecast Bias](#forecast-bias)
   - [Relative Mean Absolute Error](#relative-mean-absolute-error)
   - [Quantile Loss](#quantile-loss)
   - [Mean Quantile Loss](#mean-quantile-loss)
   - [Prediction Interval Coverage](#prediction-interval-coverage)
9. [Supported Models](#supported-models)
   - [Automatic Selection Models](#automatic-selection-models-6)
   - [Basic Models](#basic-models-6)
   - [Exponential Smoothing Models](#exponential-smoothing-models-4)
   - [Theta Methods](#theta-methods-5)
   - [State Space Models](#state-space-models-2)
   - [ARIMA Models](#arima-models-2)
   - [Multiple Seasonality Models](#multiple-seasonality-models-6)
   - [Intermittent Demand Models](#intermittent-demand-models-6)
10. [Parameter Reference](#parameter-reference)
    - [Global Parameters](#global-parameters)
    - [Model-Specific Parameters](#model-specific-parameters)
11. [Function Coverage Matrix](#function-coverage-matrix)
12. [Notes](#notes)

---

## Exploratory Data Analysis

SQL macros for exploratory data analysis and quality assessment.

### Per-Series Statistics

**anofox_fcst_ts_stats** (alias: `ts_stats`)

Computes per-series statistical metrics including length, date ranges, central tendencies (mean, median), dispersion (std), value distributions (min, max, zeros), and quality indicators (nulls, uniqueness, constancy). Returns 24 metrics per series for exploratory analysis and data profiling.

**Signature (Function Overloading):**
```sql
-- For DATE/TIMESTAMP columns (date-based frequency)
anofox_fcst_ts_stats(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP,
    value_col     DOUBLE,
    frequency     VARCHAR
) → TABLE

-- For INTEGER columns (integer-based frequency)
anofox_fcst_ts_stats(
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

**Returns:**
```sql
TABLE(
    series_id            ANY,  -- Type matches group_col
    length               BIGINT,
    start_date           DATE | TIMESTAMP | INTEGER,
    end_date             DATE | TIMESTAMP | INTEGER,
    expected_length      INTEGER,
    mean                 DOUBLE,
    std                  DOUBLE,
    min                  DOUBLE,
    max                  DOUBLE,
    median               DOUBLE,
    n_null               BIGINT,
    n_zeros              BIGINT,
    n_unique_values      BIGINT,
    is_constant          BOOLEAN,
    plateau_size         BIGINT,
    plateau_size_non_zero BIGINT,
    n_zeros_start        BIGINT,
    n_zeros_end          BIGINT,
    n_duplicate_timestamps BIGINT
)
```

**Example:**
```sql
-- DATE/TIMESTAMP columns: Use VARCHAR frequency strings
CREATE TABLE sales_stats AS
SELECT * FROM anofox_fcst_ts_stats('sales_raw', product_id, date, amount, '1d');

-- INTEGER columns: Use INTEGER frequency values
CREATE TABLE int_stats AS
SELECT * FROM anofox_fcst_ts_stats('int_data', series_id, date_col, value, 1);

-- Use NULL for default frequency
SELECT * FROM anofox_fcst_ts_stats('sales_raw', product_id, date, amount, NULL::VARCHAR);
```

---

### Quality Assessment

**anofox_fcst_ts_quality_report** (alias: `ts_quality_report`)

Generates quality assessment report from anofox_fcst_ts_stats output. Evaluates series against configurable thresholds for gaps, missing values, constant series, short series, and temporal alignment. Identifies series requiring data preparation steps.

**Signature:**
```sql
anofox_fcst_ts_quality_report(
    stats_table    VARCHAR,
    min_length     INTEGER
) → TABLE
```

**Parameters:**
- `stats_table`: Table produced by `anofox_fcst_ts_stats`
- `min_length`: Minimum acceptable series length

**Returns:** Quality assessment with configurable minimum length threshold.

**Checks:**
- Gap analysis
- Missing values
- Constant series
- Short series
- End date alignment

---

### Dataset Summary

**anofox_fcst_ts_stats_summary** (alias: `ts_stats_summary`)

Aggregates statistics across all series from anofox_fcst_ts_stats output. Computes dataset-level metrics including total series count, total observations, average series length, and date span. Provides high-level overview for dataset characterization.

**Signature:**
```sql
anofox_fcst_ts_stats_summary(
    stats_table    VARCHAR
) → TABLE
```

**Returns:** Aggregate statistics across all series from anofox_fcst_ts_stats output.

**Returns:**
```sql
TABLE(
    total_series        INTEGER,
    total_observations  BIGINT,
    avg_series_length   DOUBLE,
    date_span           INTEGER
)
```

---

## Data Quality

### Assessment

**anofox_fcst_ts_data_quality** (alias: `ts_data_quality`)

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
    unique_id       ANY,
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

**Example:**
```sql
-- DATE/TIMESTAMP columns: Use VARCHAR frequency strings
SELECT * FROM anofox_fcst_ts_data_quality('sales', product_id, date, amount, 30, '1d')
WHERE dimension = 'Temporal' AND metric = 'timestamp_gaps';

-- INTEGER columns: Use INTEGER frequency values
SELECT * FROM anofox_fcst_ts_data_quality('int_data', series_id, date_col, value, 30, 1)
WHERE dimension = 'Magnitude' AND metric = 'missing_values';
```

---

### Summary by Dimension

**anofox_fcst_ts_data_quality_summary** (alias: `ts_data_quality_summary`)

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

---

## Data Preparation

SQL macros for data cleaning and transformation. Date type support varies by function.

### Gap Filling

#### anofox_fcst_ts_fill_gaps (alias: `ts_fill_gaps`)

**Fill Missing Timestamps**

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
- `table_name`: Table name (VARCHAR). Can be provided as a quoted string literal (e.g., `'table_name'`) or as an unquoted table identifier (e.g., `table_name`). Both forms are accepted and work identically.
- `frequency`: 
  - **For DATE/TIMESTAMP columns**: Required frequency string (Polars-style).
    - `"30m"` or `"30min"` - 30 minutes
    - `"1h"` - 1 hour
    - `"1d"` - 1 day
    - `"1w"` - 1 week
    - `"1mo"` - 1 month
    - `"1q"` - 1 quarter (3 months)
    - `"1y"` - 1 year
  - **For INTEGER columns**: Required integer step size.
    - `1`, `2`, `3`, etc. - Integer step size for `GENERATE_SERIES`

**Type Validation:**
- DuckDB automatically selects the correct overload based on the `frequency` parameter type:
  - VARCHAR frequency → DATE/TIMESTAMP date column required
  - INTEGER frequency → INTEGER/BIGINT date column required
- If there's a type mismatch (e.g., INTEGER date column with VARCHAR frequency), a `Binder Error` will be raised at query time.

**Behavior:** Fills missing timestamps/indices in series with NULL values using the specified frequency interval or step size.

**Examples:**
```sql
-- DATE/TIMESTAMP columns: Use VARCHAR frequency strings
-- Fill gaps with 30-minute frequency (table name as string literal)
SELECT * FROM anofox_fcst_ts_fill_gaps('hourly_data', series_id, timestamp, value, '30m');

-- Fill gaps with weekly frequency (table name as identifier)
SELECT * FROM anofox_fcst_ts_fill_gaps(weekly_data, series_id, date, value, '1w');

-- Fill gaps with daily frequency
SELECT * FROM anofox_fcst_ts_fill_gaps('daily_data', series_id, date, value, '1d');

-- INTEGER columns: Use INTEGER frequency values
-- Fill gaps with step size of 1
SELECT * FROM anofox_fcst_ts_fill_gaps('int_data', series_id, date_col, value, 1);

-- Fill gaps with step size of 2
SELECT * FROM anofox_fcst_ts_fill_gaps('int_data', series_id, date_col, value, 2);
```

---

#### anofox_fcst_ts_fill_forward (alias: `ts_fill_forward`)

**Extend Series to Target Date**

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
- `table_name`: Table name (VARCHAR). Can be provided as a quoted string literal (e.g., `'table_name'`) or as an unquoted table identifier (e.g., `table_name`). Both forms are accepted and work identically.
- `target_date`: Target date/index to extend the series to (type must match `date_col` type)
- `frequency`: 
  - **For DATE/TIMESTAMP columns**: Required frequency string (Polars-style).
    - `"30m"` or `"30min"` - 30 minutes
    - `"1h"` - 1 hour
    - `"1d"` - 1 day
    - `"1w"` - 1 week
    - `"1mo"` - 1 month
    - `"1q"` - 1 quarter (3 months)
    - `"1y"` - 1 year
  - **For INTEGER columns**: Required integer step size.
    - `1`, `2`, `3`, etc. - Integer step size for `GENERATE_SERIES`

**Type Validation:**
- DuckDB automatically selects the correct overload based on the `frequency` parameter type:
  - VARCHAR frequency → DATE/TIMESTAMP date column required
  - INTEGER frequency → INTEGER/BIGINT date column required
- If there's a type mismatch (e.g., INTEGER date column with VARCHAR frequency), a `Binder Error` will be raised at query time.

**Behavior:** Extends series to target date/index, filling gaps with NULL using the specified frequency interval or step size.

**Examples:**
```sql
-- DATE/TIMESTAMP columns: Use VARCHAR frequency strings
-- Extend hourly series to target date (table name as string literal)
SELECT * FROM anofox_fcst_ts_fill_forward('hourly_data', series_id, timestamp, value, '2024-12-31'::TIMESTAMP, '1h');

-- Extend monthly series to target date (table name as identifier)
SELECT * FROM anofox_fcst_ts_fill_forward(monthly_data, series_id, date, value, '2024-12-01'::DATE, '1mo');

-- Extend daily series to target date
SELECT * FROM anofox_fcst_ts_fill_forward('daily_data', series_id, date, value, '2024-12-31'::DATE, '1d');

-- INTEGER columns: Use INTEGER frequency values
-- Extend series to index 100 with step size of 1
SELECT * FROM anofox_fcst_ts_fill_forward('int_data', series_id, date_col, value, 100, 1);

-- Extend series to index 100 with step size of 5
SELECT * FROM anofox_fcst_ts_fill_forward('int_data', series_id, date_col, value, 100, 5);
```

---

### Series Filtering

#### anofox_fcst_ts_drop_constant (alias: `ts_drop_constant`)

**Remove Constant Series**

**Signature:**
```sql
anofox_fcst_ts_drop_constant(
    table_name    VARCHAR,
    group_col     ANY,
    value_col     DOUBLE
) → TABLE
```

**Behavior:** Removes series with constant values (no variation).

> [!WARNING]
> This function may drop intermittent demand series (series with many zeros) as long as gaps have not been filled yet (e.g., with zeros via gap filling functions). If you need to preserve intermittent series, ensure gaps are being filled.

---

#### anofox_fcst_ts_drop_short (alias: `ts_drop_short`)

**Remove Short Series**

**Signature:**
```sql
anofox_fcst_ts_drop_short(
    table_name    VARCHAR,
    group_col     ANY,
    min_length    INTEGER
) → TABLE
```

**Behavior:** Removes series below minimum length.

> [!WARNING]
> This function may drop intermittent demand series (series with many zeros) as long as gaps have not been filled yet (e.g., with zeros via gap filling functions). If you need to preserve intermittent series, ensure gaps are being filled.

---

### Edge Cleaning

#### anofox_fcst_ts_drop_leading_zeros (alias: `ts_drop_leading_zeros`)

**Remove Leading Zeros**

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
> Results may differ if `anofox_fcst_ts_fill_gaps` (or `ts_fill_gaps`) or `anofox_fcst_ts_fill_forward` (or `ts_fill_forward`) has been applied, as these functions may introduce zeros or other values in previously missing timestamps.

---

#### anofox_fcst_ts_drop_trailing_zeros (alias: `ts_drop_trailing_zeros`)

**Remove Trailing Zeros**

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
> Results may differ if `anofox_fcst_ts_fill_gaps` (or `ts_fill_gaps`) or `anofox_fcst_ts_fill_forward` (or `ts_fill_forward`) has been applied, as these functions may introduce zeros or other values in previously missing timestamps.

---

#### anofox_fcst_ts_drop_edge_zeros (alias: `ts_drop_edge_zeros`)

**Remove Both Leading and Trailing Zeros**

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
> Results may differ if `anofox_fcst_ts_fill_gaps` (or `ts_fill_gaps`) or `anofox_fcst_ts_fill_forward` (or `ts_fill_forward`) has been applied, as these functions may introduce zeros or other values in previously missing timestamps.

---

### Missing Value Imputation

#### anofox_fcst_ts_fill_nulls_const (alias: `ts_fill_nulls_const`)

**Fill with Constant Value**

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

---

#### anofox_fcst_ts_fill_nulls_forward (alias: `ts_fill_nulls_forward`)

**Forward Fill (Last Observation Carried Forward)**

**Signature:**
```sql
anofox_fcst_ts_fill_nulls_forward(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE
) → TABLE
```

**Behavior:** Uses `LAST_VALUE(... IGNORE NULLS)` window function.

---

#### anofox_fcst_ts_fill_nulls_backward (alias: `ts_fill_nulls_backward`)

**Backward Fill**

**Signature:**
```sql
anofox_fcst_ts_fill_nulls_backward(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE
) → TABLE
```

**Behavior:** Uses `FIRST_VALUE(... IGNORE NULLS)` window function.

---

#### anofox_fcst_ts_fill_nulls_mean (alias: `ts_fill_nulls_mean`)

**Fill with Series Mean**

**Signature:**
```sql
anofox_fcst_ts_fill_nulls_mean(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE
) → TABLE
```

**Behavior:** Computes mean per series and fills NULLs.

---

### Transformation Operations

#### anofox_fcst_ts_diff (alias: `ts_diff`)

**Compute Differences**

**Signature:**
```sql
anofox_fcst_ts_diff(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE,
    order         INTEGER
) → TABLE
```

**Parameters:**
- `table_name`: Table name (VARCHAR). Can be provided as a quoted string literal (e.g., `'table_name'`) or as an unquoted table identifier (e.g., `table_name`). Both forms are accepted and work identically.
- `order`: Difference order (must be > 0). Computes `value[t] - value[t-order]`.

**Behavior:** Computes differences of the specified order for each series. For `order=1`, computes first differences (value[t] - value[t-1]). For `order=2`, computes second differences, and so on.

**Example:**
```sql
-- Compute first differences (order=1)
SELECT * FROM anofox_fcst_ts_diff('sales_data', product_id, date, amount, 1);

-- Compute second differences (order=2)
SELECT * FROM anofox_fcst_ts_diff('sales_data', product_id, date, amount, 2);
```

---

## Seasonality

### Simple Seasonality Detection

**anofox_fcst_ts_detect_seasonality** (alias: `ts_detect_seasonality`)

Simple Seasonality Detection

**Signature:**
```sql
anofox_fcst_ts_detect_seasonality(
    values    DOUBLE[]
) → INTEGER[]
```

**Returns:** Array of detected seasonal periods sorted by strength.

**Example:**
```sql
SELECT 
    product_id,
    anofox_fcst_ts_detect_seasonality(LIST(value ORDER BY date)) AS periods
FROM sales
GROUP BY product_id;
-- Returns: [7, 30] (weekly and monthly patterns)
```

**Algorithm:** Uses autocorrelation-based detection with minimum period of 4 and threshold of 0.9.

---

### Detailed Seasonality Analysis

**anofox_fcst_ts_analyze_seasonality** (alias: `ts_analyze_seasonality`)

Detailed Seasonality Analysis

**Signature:**
```sql
anofox_fcst_ts_analyze_seasonality(
    timestamps    TIMESTAMP[] | DATE[],
    values        DOUBLE[]
) → STRUCT
```

**Returns:**
```sql
STRUCT(
    detected_periods    INTEGER[],
    primary_period      INTEGER,
    seasonal_strength   DOUBLE,
    trend_strength      DOUBLE
)
```

**Example:**
```sql
SELECT 
    product_id,
    anofox_fcst_ts_analyze_seasonality(
        LIST(timestamp ORDER BY timestamp),
        LIST(value ORDER BY timestamp)
    ) AS analysis
FROM sales
GROUP BY product_id;
```

**Fields:**
- `detected_periods`: All detected periods
- `primary_period`: Main seasonal period (may be NULL)
- `seasonal_strength`: Strength of seasonal component (0-1)
- `trend_strength`: Strength of trend component (0-1)

---

## Changepoint Detection

### Single Series Changepoint Detection

**anofox_fcst_ts_detect_changepoints** (alias: `ts_detect_changepoints`)

Single Series Changepoint Detection

**Signature:**
```sql
anofox_fcst_ts_detect_changepoints(
    table_name    VARCHAR,
    date_col      DATE | TIMESTAMP,
    value_col     DOUBLE,
    params        MAP
) → TABLE
```

**Parameters:**
```sql
MAP{
    'hazard_lambda': DOUBLE,         -- Default: 250.0
    'include_probabilities': BOOLEAN  -- Default: false
}
```

**Returns:**
```sql
TABLE(
    date_col                  DATE | TIMESTAMP,
    value_col                 DOUBLE,
    is_changepoint            BOOLEAN,
    changepoint_probability   DOUBLE
)
```

**Algorithm:** Bayesian Online Changepoint Detection (BOCPD) with Normal-Gamma conjugate prior.

**Parameters:**
- `hazard_lambda`: Expected run length between changepoints (lower = more sensitive)
- `include_probabilities`: Compute Bayesian probabilities (slower but more informative)

---

### Multiple Series Changepoint Detection

**anofox_fcst_ts_detect_changepoints_by** (alias: `ts_detect_changepoints_by`)

Multiple Series Changepoint Detection

**Signature:**
```sql
anofox_fcst_ts_detect_changepoints_by(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP,
    value_col     DOUBLE,
    params        MAP
) → TABLE
```

**Returns:** Same as `anofox_fcst_ts_detect_changepoints` (or `ts_detect_changepoints`), plus `group_col` column.

**Behavioral Notes:**
- Full parallelization on GROUP BY operations
- Detects level shifts, trend changes, variance shifts, regime changes
- Independent detection per series

---

### Aggregate Function for Changepoint Detection

**anofox_fcst_ts_detect_changepoints_agg** (alias: `ts_detect_changepoints_agg`)

Aggregate Function for Changepoint Detection

**Signature:**
```sql
anofox_fcst_ts_detect_changepoints_agg(
    date_col      DATE | TIMESTAMP,
    value_col     DOUBLE,
    params        MAP
) → LIST<STRUCT>
```

**Returns:**
```sql
LIST<STRUCT(
    timestamp              TIMESTAMP,
    value                  DOUBLE,
    is_changepoint         BOOLEAN,
    changepoint_probability DOUBLE
)>
```

**Use Case:** For custom GROUP BY patterns with 2+ group columns.

---

## Time Series Features

### Extract Time Series Features

**anofox_fcst_ts_features** (alias: `ts_features`)

Extract Time Series Features (tsfresh-compatible)

**Signature:**
```sql
anofox_fcst_ts_features(
    ts_column           TIMESTAMP | DATE | BIGINT,
    value_column        DOUBLE,
    feature_selection   LIST(VARCHAR) | STRUCT | NULL,
    feature_params      LIST(STRUCT) | NULL
) → STRUCT
```

**Parameters:**
- `ts_column`: Timestamp column (supports TIMESTAMP, DATE, BIGINT)
- `value_column`: Value column (DOUBLE)
- `feature_selection`: Optional list of feature names, or NULL for all features
- `feature_params`: Optional parameter overrides per feature

**Returns:** STRUCT with one column per feature (76+ features available).

**Highlights:**
- Mirrors 76 feature calculators from [tsfresh](https://tsfresh.readthedocs.io/)
- Safe for both `GROUP BY` and window functions (`OVER ...`)
- Optional `feature_names` restricts output to specific features
- Optional `feature_params` overrides default parameter grids
- Output columns follow `feature__param_key_value` naming

**Example:**
```sql
SELECT 
    product_id,
    anofox_fcst_ts_features(
        date,
        sales,
        ['mean', 'variance', 'autocorrelation__lag_1'],
        [{'feature': 'ratio_beyond_r_sigma', 'params': {'r': 1.0}}]
    ) AS feats
FROM sales
GROUP BY product_id;
```

---

### List Available Features

**anofox_fcst_ts_features_list** (alias: `ts_features_list`)

List Available Features

**Signature:**
```sql
anofox_fcst_ts_features_list() → TABLE
```

**Returns:**
```sql
TABLE(
    column_name        VARCHAR,
    feature_name       VARCHAR,
    parameter_suffix   VARCHAR,
    default_parameters VARCHAR,
    parameter_keys     VARCHAR
)
```

**Use Case:** Discover valid feature names and inspect default parameters.

---

### Load Feature Configuration from JSON

**anofox_fcst_ts_features_config_from_json** (alias: `ts_features_config_from_json`)

Load Feature Configuration from JSON

**Signature:**
```sql
anofox_fcst_ts_features_config_from_json(
    path    VARCHAR
) → STRUCT
```

**Returns:**
```sql
STRUCT(
    feature_names    LIST(VARCHAR),
    overrides        LIST(STRUCT(
        feature       VARCHAR,
        params_json   VARCHAR
    ))
)
```

**File Format:** JSON array of objects with `feature` and optional `params`.

---

### Load Feature Configuration from CSV

**anofox_fcst_ts_features_config_from_csv** (alias: `ts_features_config_from_csv`)

Load Feature Configuration from CSV

**Signature:**
```sql
anofox_fcst_ts_features_config_from_csv(
    path    VARCHAR
) → STRUCT
```

**Returns:** Same as `anofox_fcst_ts_features_config_from_json` (or `ts_features_config_from_json`).

**File Format:** CSV with header row containing `feature` and parameter columns.

---

## Forecasting

### Single Time Series Forecasting

**anofox_fcst_ts_forecast** (alias: `ts_forecast`)

Single Time Series Forecasting

Generate forecasts for a single time series with automatic parameter validation.

**Signature:**
```sql
anofox_fcst_ts_forecast(
    table_name    VARCHAR,
    date_col      DATE | TIMESTAMP | INTEGER,
    target_col    DOUBLE,
    method        VARCHAR,
    horizon       INTEGER,
    params        MAP
) → TABLE
```

**Parameters:**
- `table_name`: Name of the input table
- `date_col`: Date/timestamp column name
- `target_col`: Value column name to forecast
- `method`: Model name (see [Supported Models](#supported-models))
- `horizon`: Number of future periods to forecast (must be > 0)
- `params`: Configuration MAP with model-specific parameters

**Returns:**
```sql
TABLE(
    forecast_step      INTEGER,
    date               DATE | TIMESTAMP | INTEGER,  -- Type matches input
    point_forecast     DOUBLE,
    lower              DOUBLE,  -- Lower bound (dynamic name based on confidence_level, e.g., lower_90)
    upper              DOUBLE,  -- Upper bound (dynamic name based on confidence_level, e.g., upper_90)
    model_name         VARCHAR,
    insample_fitted    DOUBLE[]  -- Empty unless return_insample=true
)
```

**Note:** The `lower` and `upper` bound columns have dynamic names based on the `confidence_level` parameter. For example, with `confidence_level: 0.90`, the columns are named `lower_90` and `upper_90`. With `confidence_level: 0.95`, they are `lower_95` and `upper_95`.

**Example:**
```sql
SELECT * FROM anofox_fcst_ts_forecast(
    'sales',
    date,
    amount,
    'AutoETS',
    28,
    MAP{'seasonal_period': 7, 'confidence_level': 0.95}
);
-- Note: The 'lower' and 'upper' columns will be named 'lower_95' and 'upper_95' based on confidence_level
```

**Behavioral Notes:**
- Timestamp generation based on training data interval (configurable via `generate_timestamps`)
- Prediction intervals computed at specified confidence level (default 0.90)
- Optional in-sample fitted values via `return_insample: true`
- Date column type preserved from input

---

### Multiple Time Series Forecasting

**anofox_fcst_ts_forecast_by** (alias: `ts_forecast_by`)

Multiple Time Series Forecasting with GROUP BY

Generate forecasts for multiple time series with native DuckDB GROUP BY parallelization.

**Signature:**
```sql
anofox_fcst_ts_forecast_by(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    target_col    DOUBLE,
    method        VARCHAR,
    horizon       INTEGER,
    params        MAP
) → TABLE
```

**Parameters:**
- `table_name`: Name of the input table
- `group_col`: Grouping column name (any type, preserved in output)
- `date_col`: Date/timestamp column name
- `target_col`: Value column name to forecast
- `method`: Model name
- `horizon`: Number of future periods to forecast
- `params`: Configuration MAP

**Returns:**
```sql
TABLE(
    group_col          ANY,  -- Type matches input
    forecast_step      INTEGER,
    date               DATE | TIMESTAMP | INTEGER,
    point_forecast     DOUBLE,
    lower              DOUBLE,  -- Dynamic name based on confidence_level (e.g., lower_90)
    upper              DOUBLE,  -- Dynamic name based on confidence_level (e.g., upper_90)
    model_name         VARCHAR,
    insample_fitted    DOUBLE[]
)
```

**Note:** The `lower` and `upper` bound columns have dynamic names based on the `confidence_level` parameter. For example, with `confidence_level: 0.90`, the columns are named `lower_90` and `upper_90`.

**Example:**
```sql
SELECT 
    product_id,
    forecast_step,
    point_forecast
FROM anofox_fcst_ts_forecast_by(
    'product_sales',
    product_id,
    date,
    amount,
    'AutoETS',
    28,
    MAP{'seasonal_period': 7}
)
WHERE forecast_step <= 7
ORDER BY product_id, forecast_step;
```

**Behavioral Notes:**
- Automatic parallelization: series distributed across CPU cores
- Group column type preserved in output
- Independent parameter validation per series
- Efficient for thousands of series

---

### Aggregate Function for Custom GROUP BY

**anofox_fcst_ts_forecast_agg** (alias: `ts_forecast_agg`)

Aggregate Function for Custom GROUP BY

Low-level aggregate function for forecasting with 2+ group columns or custom aggregation patterns.

**Signature:**
```sql
anofox_fcst_ts_forecast_agg(
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE,
    method        VARCHAR,
    horizon       INTEGER,
    params        MAP
) → STRUCT
```

**Returns:**
```sql
STRUCT(
    forecast_step          INTEGER[],  -- Optional, controlled by include_forecast_step parameter
    forecast_timestamp     TIMESTAMP[],
    point_forecast         DOUBLE[],
    lower_<suffix>         DOUBLE[],  -- Dynamic name based on confidence_level (e.g., lower_90, lower_95)
    upper_<suffix>         DOUBLE[],  -- Dynamic name based on confidence_level (e.g., upper_90, upper_95)
    model_name             VARCHAR,
    insample_fitted        DOUBLE[],
    date_col_name          VARCHAR,
    error_message          VARCHAR    -- Optional, present when include_error_message=true and errors occur
)
```

**Note:** 
- The `lower` and `upper` bound columns have dynamic names based on the `confidence_level` parameter. For example, with `confidence_level: 0.90`, the columns are named `lower_90` and `upper_90`. With `confidence_level: 0.95`, they are `lower_95` and `upper_95`.
- The `forecast_step` field is optional and only included when `include_forecast_step: true` (default: true).
- The `error_message` field is optional and only included when `include_error_message: true` (default: true) and an error occurs during forecasting.

**Example:**
```sql
WITH fc AS (
    SELECT 
        product_id,
        location_id,
        anofox_fcst_ts_forecast_agg(date, amount, 'AutoETS', 28, MAP{'seasonal_period': 7}) AS result
    FROM sales
    GROUP BY product_id, location_id
)
SELECT 
    product_id,
    location_id,
    UNNEST(result.forecast_step) AS forecast_step,
    UNNEST(result.point_forecast) AS point_forecast,
    UNNEST(result.lower) AS lower_bound
FROM fc;
```

**Parameters (in `params` MAP):**
- `include_forecast_step` (BOOLEAN, default: true): Include `forecast_step` array in return struct
- `date_col_name` (VARCHAR, default: "date"): Name of the date column (used in return struct)
- `safe_mode` (BOOLEAN, default: true): Enable safe error handling mode
- `include_error_message` (BOOLEAN, default: true): Include `error_message` field in return struct on errors

**Use Case:** When you need multiple grouping columns or custom aggregation patterns beyond single `group_col`.

---

## Evaluation

All metrics accept `DOUBLE[]` arrays and return `DOUBLE`. Use with `GROUP BY` via `LIST()` aggregation.

### Mean Absolute Error

**anofox_fcst_ts_mae** (alias: `ts_mae`)

Mean Absolute Error

**Signature:**
```sql
anofox_fcst_ts_mae(
    actual      DOUBLE[],
    predicted   DOUBLE[]
) → DOUBLE
```

**Formula:** MAE = Σ|y - ŷ| / n

**Example:**
```sql
SELECT 
    product_id,
    anofox_fcst_ts_mae(LIST(actual), LIST(predicted)) AS mae
FROM results
GROUP BY product_id;
```

---

### Mean Squared Error

**anofox_fcst_ts_mse** (alias: `ts_mse`)

Mean Squared Error

**Signature:**
```sql
anofox_fcst_ts_mse(
    actual      DOUBLE[],
    predicted   DOUBLE[]
) → DOUBLE
```

**Formula:** MSE = Σ(y - ŷ)² / n

---

### Root Mean Squared Error

**anofox_fcst_ts_rmse** (alias: `ts_rmse`)

Root Mean Squared Error

**Signature:**
```sql
anofox_fcst_ts_rmse(
    actual      DOUBLE[],
    predicted   DOUBLE[]
) → DOUBLE
```

**Formula:** RMSE = √(MSE)

---

### Mean Absolute Percentage Error

**anofox_fcst_ts_mape** (alias: `ts_mape`)

Mean Absolute Percentage Error

**Signature:**
```sql
anofox_fcst_ts_mape(
    actual      DOUBLE[],
    predicted   DOUBLE[]
) → DOUBLE
```

**Formula:** MAPE = (100/n) × Σ|y - ŷ| / |y|

> [!WARNING]
> Returns NULL if any actual value is zero.

---

### Symmetric Mean Absolute Percentage Error

**anofox_fcst_ts_smape** (alias: `ts_smape`)

Symmetric Mean Absolute Percentage Error

**Signature:**
```sql
anofox_fcst_ts_smape(
    actual      DOUBLE[],
    predicted   DOUBLE[]
) → DOUBLE
```

**Formula:** SMAPE = (200/n) × Σ|y - ŷ| / (|y| + |ŷ|)

**Range:** [0, 200]

> [!WARNING]
> Handles zero values better than MAPE.

---

### Mean Absolute Scaled Error

**anofox_fcst_ts_mase** (alias: `ts_mase`)

Mean Absolute Scaled Error

**Signature:**
```sql
anofox_fcst_ts_mase(
    actual      DOUBLE[],
    predicted   DOUBLE[],
    baseline    DOUBLE[]
) → DOUBLE
```

**Formula:** MASE = MAE / (MAE of baseline method)

**Use Case:** Compare forecast accuracy relative to a baseline (e.g., naive forecast).

---

### R-squared

**anofox_fcst_ts_r2** (alias: `ts_r2`)

R-squared (Coefficient of Determination)

**Signature:**
```sql
anofox_fcst_ts_r2(
    actual      DOUBLE[],
    predicted   DOUBLE[]
) → DOUBLE
```

**Formula:** R² = 1 - (SS_res / SS_tot)

**Range:** (-∞, 1]

---

### Forecast Bias

**anofox_fcst_ts_bias** (alias: `ts_bias`)

Forecast Bias

**Signature:**
```sql
anofox_fcst_ts_bias(
    actual      DOUBLE[],
    predicted   DOUBLE[]
) → DOUBLE
```

**Formula:** Bias = Σ(ŷ - y) / n

**Interpretation:** Positive = over-forecasting, Negative = under-forecasting

---

### Relative Mean Absolute Error

**anofox_fcst_ts_rmae** (alias: `ts_rmae`)

Relative Mean Absolute Error

**Signature:**
```sql
anofox_fcst_ts_rmae(
    actual      DOUBLE[],
    pred1        DOUBLE[],
    pred2        DOUBLE[]
) → DOUBLE
```

**Formula:** RMAE = MAE(pred1) / MAE(pred2)

**Use Case:** Compare relative performance of two forecasting methods.

---

### Quantile Loss

**anofox_fcst_ts_quantile_loss** (alias: `ts_quantile_loss`)

Quantile Loss (Pinball Loss)

**Signature:**
```sql
anofox_fcst_ts_quantile_loss(
    actual      DOUBLE[],
    predicted   DOUBLE[],
    q           DOUBLE
) → DOUBLE
```

**Formula:** QL = Σ max(q × (y - ŷ), (1 - q) × (ŷ - y))

**Parameters:**
- `q`: Quantile level (0 < q < 1)

**Use Case:** Evaluate quantile forecasts (e.g., median, 90th percentile).

---

### Mean Quantile Loss

**anofox_fcst_ts_mqloss** (alias: `ts_mqloss`)

Mean Quantile Loss

**Signature:**
```sql
anofox_fcst_ts_mqloss(
    actual      DOUBLE[],
    quantiles   DOUBLE[][],
    levels      DOUBLE[]
) → DOUBLE
```

**Parameters:**
- `quantiles`: Array of quantile forecast arrays
- `levels`: Corresponding quantile levels (e.g., [0.1, 0.5, 0.9])

**Use Case:** Evaluate multi-quantile forecasts (distribution forecasts).

---

### Prediction Interval Coverage

**anofox_fcst_ts_coverage** (alias: `ts_coverage`)

Prediction Interval Coverage

**Signature:**
```sql
anofox_fcst_ts_coverage(
    actual      DOUBLE[],
    lower       DOUBLE[],
    upper       DOUBLE[]
) → DOUBLE
```

**Formula:** Coverage = (Count of actuals within [lower, upper]) / n

**Range:** [0, 1]

**Use Case:** Evaluate calibration of prediction intervals (should match confidence_level).

**Example:**
```sql
SELECT 
    product_id,
    anofox_fcst_ts_coverage(LIST(actual), LIST(lower), LIST(upper)) * 100 AS coverage_pct
FROM results
GROUP BY product_id;
-- Coverage should be close to confidence_level × 100
```

---

## Supported Models

### Automatic Selection Models (6)

| Model | Best For | Required Parameters |
|-------|----------|---------------------|
| **AutoETS** | General purpose | `seasonal_period` |
| **AutoARIMA** | Complex patterns | `seasonal_period` |
| **AutoTheta** | Theta family selection | `seasonal_period` |
| **AutoMFLES** | Multiple seasonality | `seasonal_periods` (array) |
| **AutoMSTL** | Multiple seasonality | `seasonal_periods` (array) |
| **AutoTBATS** | Complex seasonality | `seasonal_periods` (array) |

### Basic Models (6)

| Model | Description | Parameters |
|-------|-------------|-----------|
| **Naive** | Last value repeated | None |
| **SMA** | Simple moving average | `window` (default: 5) |
| **SeasonalNaive** | Seasonal last value | `seasonal_period` |
| **SES** | Simple exponential smoothing | `alpha` (optional, default: 0.3) |
| **SESOptimized** | Auto-optimized SES | None |
| **RandomWalkDrift** | Random walk with drift | None |

### Exponential Smoothing Models (4)

| Model | Description | Parameters |
|-------|-------------|-----------|
| **Holt** | Trend (no seasonality) | `alpha`, `beta` (optional) |
| **HoltWinters** | Trend + seasonality | `seasonal_period`, `alpha`, `beta`, `gamma` (optional) |
| **SeasonalES** | Seasonal exponential smoothing | `seasonal_period`, `alpha`, `gamma` (optional) |
| **SeasonalESOptimized** | Auto-optimized seasonal ES | `seasonal_period` |

### Theta Methods (5)

| Model | Description | Parameters |
|-------|-------------|-----------|
| **Theta** | Theta decomposition | `seasonal_period`, `theta` (optional) |
| **OptimizedTheta** | Auto-optimized Theta | `seasonal_period` |
| **DynamicTheta** | Adaptive Theta | `seasonal_period`, `theta` (optional) |
| **DynamicOptimizedTheta** | Auto adaptive Theta | `seasonal_period` |
| **AutoTheta** | Auto model selection | `seasonal_period` |

### State Space Models (2)

| Model | Description | Parameters |
|-------|-------------|-----------|
| **ETS** | Error-Trend-Seasonal | `seasonal_period`, `error_type`, `trend_type`, `season_type` |
| **AutoETS** | Automatic ETS selection | `seasonal_period` |

### ARIMA Models (2)

| Model | Description | Parameters |
|-------|-------------|-----------|
| **ARIMA** | Manual ARIMA | `p`, `d`, `q`, `P`, `D`, `Q`, `s` |
| **AutoARIMA** | Automatic ARIMA selection | `seasonal_period` |

> [!WARNING]
> ARIMA models require Eigen3 library.

### Multiple Seasonality Models (6)

| Model | Description | Parameters |
|-------|-------------|-----------|
| **MFLES** | Multiple FLES | `seasonal_periods` (array), `n_iterations` |
| **AutoMFLES** | Auto MFLES | `seasonal_periods` (array) |
| **MSTL** | Multiple STL decomposition | `seasonal_periods` (array), `trend_method`, `seasonal_method` |
| **AutoMSTL** | Auto MSTL | `seasonal_periods` (array) |
| **TBATS** | Trigonometric, Box-Cox, ARMA, Trend, Seasonal | `seasonal_periods` (array), `use_box_cox` |
| **AutoTBATS** | Auto TBATS | `seasonal_periods` (array) |

### Intermittent Demand Models (6)

| Model | Description | Parameters |
|-------|-------------|-----------|
| **CrostonClassic** | Croston's method | None |
| **CrostonOptimized** | Optimized Croston | None |
| **CrostonSBA** | Syntetos-Boylan approximation | None |
| **ADIDA** | Aggregate-Disaggregate | None |
| **IMAPA** | Intermittent Moving Average | None |
| **TSB** | Teunter-Syntetos-Babai | `alpha_d`, `alpha_p` |

**Total: 31 Models**

---

## Parameter Reference

### Global Parameters

These parameters work with **all forecasting models**:

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `confidence_level` | DOUBLE | 0.90 | Confidence level for prediction intervals (0 < value < 1) |
| `return_insample` | BOOLEAN | false | Return fitted values in output |
| `generate_timestamps` | BOOLEAN | true | Generate forecast timestamps based on training intervals |

### Model-Specific Parameters

#### ETS Parameters

```sql
{
    'seasonal_period': INT,      -- Required
    'error_type': INT,           -- 0=additive, 1=multiplicative
    'trend_type': INT,           -- 0=none, 1=additive, 2=damped
    'season_type': INT,          -- 0=none, 1=additive, 2=multiplicative
    'alpha': DOUBLE,             -- Level smoothing (0-1, optional)
    'beta': DOUBLE,              -- Trend smoothing (0-1, optional)
    'gamma': DOUBLE,             -- Seasonal smoothing (0-1, optional)
    'phi': DOUBLE                -- Damping (0-1, optional)
}
```

#### ARIMA Parameters

```sql
{
    'p': INT,                   -- AR order (0-5 typical)
    'd': INT,                   -- Differencing (0-2 typical)
    'q': INT,                   -- MA order (0-5 typical)
    'P': INT,                   -- Seasonal AR (0-2)
    'D': INT,                   -- Seasonal differencing (0-1)
    'Q': INT,                   -- Seasonal MA (0-2)
    's': INT,                   -- Seasonal period
    'include_intercept': BOOL   -- Include constant term
}
```

#### Multiple Seasonality Parameters

```sql
{
    'seasonal_periods': INT[],  -- Array of periods, e.g., [7, 365]
    'n_iterations': INT,        -- For MFLES (optional)
    'trend_method': VARCHAR,     -- For MSTL (optional)
    'seasonal_method': VARCHAR, -- For MSTL (optional)
    'use_box_cox': BOOLEAN      -- For TBATS (optional)
}
```

#### Theta Parameters

```sql
{
    'seasonal_period': INT,     -- Required
    'theta': DOUBLE,            -- Theta parameter (optional, default: 2.0)
    'model': VARCHAR,           -- For AutoTheta: 'STM', 'OTM', 'DSTM', 'DOTM'
    'decomposition_type': VARCHAR -- For AutoTheta
}
```

#### Intermittent Demand Parameters

```sql
{
    'alpha_d': DOUBLE,          -- For TSB: demand smoothing (0-1)
    'alpha_p': DOUBLE           -- For TSB: probability smoothing (0-1)
}
```

---

## Function Coverage Matrix

### Summary Statistics

| Category | Count | Function Types |
|----------|-------|----------------|
| Forecasting | 3 | Table macros (2), Aggregate (1) |
| Evaluation Metrics | 12 | Scalar functions |
| EDA Macros | 5 | Table macros |
| Data Quality | 2 | Table macros |
| Data Preparation | 10 | Table macros |
| Seasonality | 2 | Scalar functions |
| Changepoint Detection | 3 | Table macros (2), Aggregate (1) |
| Time Series Features | 4 | Aggregate (1), Table function (1), Scalar (2) |
| **Total** | **41** | |

### Function Type Breakdown

| Type | Count | Examples |
|------|-------|----------|
| Table Macros | 23 | `anofox_fcst_ts_forecast` (or `ts_forecast`), `TS_STATS`, `anofox_fcst_ts_fill_gaps` (or `ts_fill_gaps`) |
| Aggregate Functions | 5 | `anofox_fcst_ts_forecast_agg` (or `ts_forecast_agg`), `anofox_fcst_ts_features` (or `ts_features`), `anofox_fcst_ts_detect_changepoints_agg` (or `ts_detect_changepoints_agg`) |
| Scalar Functions | 14 | `anofox_fcst_ts_mae` (or `ts_mae`), `anofox_fcst_ts_detect_seasonality` (or `ts_detect_seasonality`), `anofox_fcst_ts_analyze_seasonality` (or `ts_analyze_seasonality`) |
| Table Functions | 1 | `anofox_fcst_ts_features_list` (or `ts_features_list`) |

### GROUP BY Support

| Function Category | GROUP BY Support | Notes |
|-------------------|------------------|-------|
| Forecasting | ✅ | `anofox_fcst_ts_forecast_by` (or `ts_forecast_by`) and `anofox_fcst_ts_forecast_agg` (or `ts_forecast_agg`) |
| Evaluation Metrics | ✅ | Use with `LIST()` aggregation |
| EDA Macros | ✅ | All macros support GROUP BY via `group_col` |
| Data Quality | ✅ | All macros support GROUP BY |
| Data Preparation | ✅ | All macros support GROUP BY |
| Seasonality | ✅ | Use with `LIST()` aggregation |
| Changepoint Detection | ✅ | `anofox_fcst_ts_detect_changepoints_by` (or `ts_detect_changepoints_by`) and `anofox_fcst_ts_detect_changepoints_agg` (or `ts_detect_changepoints_agg`) |
| Time Series Features | ✅ | Aggregate function supports GROUP BY |

### Window Function Support

| Function Category | Window Support | Notes |
|-------------------|----------------|-------|
| Evaluation Metrics | ❌ | Scalar functions only |
| Seasonality | ❌ | Scalar functions only |
| Time Series Features | ✅ | `anofox_fcst_ts_features` (or `ts_features`) supports `OVER` clauses |

---

## Notes

1. **All forecasting calculations** are performed by the **anofox-time** library implemented in C++.

2. **Positional parameters only**: Functions do NOT support named parameters (`:=` syntax). Parameters must be provided in the order specified.

3. **Date type support**: Most functions support DATE, TIMESTAMP, and INTEGER date types. Some gap-filling functions are DATE/TIMESTAMP only (see function documentation).

4. **NULL handling**:
   - Missing values in input arrays will cause errors in metrics functions
   - Data preparation macros handle NULLs explicitly

5. **Performance**:
   - Table macros: O(n) for small series, optimized for large datasets
   - Aggregates: Optimized for GROUP BY parallelism
   - Window functions: Cached computation when frame doesn't change
   - Automatic parallelization across CPU cores for multi-series operations

6. **Minimum sample sizes**:
   - General rule: n ≥ 2 × seasonal_period for seasonal models
   - For inference: n > p + 1 to have sufficient degrees of freedom
   - Some models require minimum lengths (e.g., ARIMA needs sufficient data for differencing)

---

**Last Updated:** 2025-11-26  
**API Version:** 0.2.0

