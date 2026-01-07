# Anofox Forecast Extension - API Reference

**Version:** 0.2.4
**DuckDB Version:** >= v1.4.3
**Forecasting Engine:** anofox-fcst-core (Rust)

---

## Overview

The Anofox Forecast extension brings comprehensive time series analysis and forecasting capabilities directly into DuckDB. It enables analysts and data scientists to perform sophisticated time series operations using familiar SQL syntax, without needing external tools or data movement.

**Key Benefits:**
- **SQL-native**: All operations are expressed as SQL functions and macros
- **High Performance**: Core algorithms implemented in Rust for speed and safety
- **Comprehensive**: 32 forecasting models, 117 features, seasonality detection, changepoint detection
- **Flexible API**: Three API styles to fit different workflows

All computations are performed by the **anofox-fcst-core** library, implemented in Rust.

### Quick Start

```sql
-- Load the extension
LOAD anofox_forecast;

-- Generate forecasts for multiple products
SELECT * FROM ts_forecast_by('sales', product_id, date, quantity, 'AutoETS', 30, MAP{});

-- Analyze seasonality
SELECT ts_detect_periods(LIST(quantity ORDER BY date)) FROM sales GROUP BY product_id;

-- Compute time series statistics
SELECT * FROM ts_stats('sales', product_id, date, quantity);
```

### API Variants

The extension provides **three API styles** to accommodate different use cases:

#### 1. Scalar Functions (Array-Based)
Low-level functions that operate on arrays. Composable with `GROUP BY` and `LIST()`.

```sql
-- Scalar function with array input
SELECT
    product_id,
    ts_stats(LIST(value ORDER BY date)) AS stats
FROM sales
GROUP BY product_id;
```

#### 2. Table Macros (Table-Based)
High-level macros that operate directly on tables. Column names are passed as identifiers (unquoted).

```sql
-- Table macro with positional parameters
SELECT * FROM anofox_fcst_ts_forecast_by('sales', product_id, date, value, 'naive', 12, MAP{});
```

#### 3. Aggregate Functions
Aggregate functions for use with custom `GROUP BY` patterns.

```sql
-- Aggregate function with GROUP BY
SELECT
    product_id,
    anofox_fcst_ts_forecast_agg(ts, value, 'ets', 12, MAP{}) AS forecast
FROM sales
GROUP BY product_id;
```

### Key Features

- **Time Series Statistics**: 24 statistical metrics per series
- **Data Quality Assessment**: 4-dimensional quality scoring
- **Seasonality Detection**: Autocorrelation-based period detection
- **MSTL Decomposition**: Multiple seasonal-trend decomposition
- **Changepoint Detection**: Structural break detection
- **Feature Extraction**: 117 tsfresh-compatible features
- **Forecasting**: Multiple models including Naive, SES, ETS
- **Evaluation Metrics**: 12 forecast accuracy metrics

### Function Naming Conventions

All functions are available with two naming patterns:
- `ts_*` - Short form (e.g., `ts_stats`, `ts_mae`)
- `anofox_fcst_ts_*` - Prefixed form (e.g., `anofox_fcst_ts_stats`, `anofox_fcst_ts_mae`)

Both forms are identical in functionality.

---

## Table of Contents

1. [Table Macros (Table-Level API)](#table-macros-table-level-api)
2. [Exploratory Data Analysis](#exploratory-data-analysis)
   - [Time Series Statistics](#time-series-statistics)
   - [Data Quality Assessment](#data-quality-assessment)
3. [Data Preparation](#data-preparation)
   - [Series Filtering](#series-filtering)
   - [Edge Cleaning](#edge-cleaning)
   - [Missing Value Imputation](#missing-value-imputation)
   - [Differencing](#differencing)
4. [Seasonality](#seasonality)
   - [Period Detection](#period-detection)
   - [Seasonality Analysis](#seasonality-analysis)
   - [Seasonal Strength](#seasonal-strength)
   - [Windowed Seasonal Strength](#windowed-seasonal-strength)
   - [Seasonality Classification](#seasonality-classification)
   - [Seasonality Change Detection](#seasonality-change-detection)
   - [Instantaneous Period](#instantaneous-period)
   - [Amplitude Modulation Detection](#amplitude-modulation-detection)
5. [Peak Detection](#peak-detection)
   - [Detect Peaks](#detect-peaks)
   - [Peak Timing Analysis](#peak-timing-analysis)
6. [Detrending](#detrending)
   - [Detrend](#detrend)
7. [Time Series Decomposition](#time-series-decomposition)
   - [Decompose](#decompose)
   - [MSTL Decomposition](#mstl-decomposition)
8. [Changepoint Detection](#changepoint-detection)
   - [Changepoint Detection Aggregate](#changepoint-detection-aggregate)
9. [Feature Extraction](#feature-extraction)
   - [Extract Features](#extract-features)
   - [List Available Features](#list-available-features)
   - [Feature Extraction Aggregate](#feature-extraction-aggregate)
   - [Feature Configuration](#feature-configuration)
10. [Forecasting](#forecasting)
   - [ts_forecast (Scalar)](#ts_forecast-scalar)
   - [ts_forecast (Table Macro)](#anofox_fcst_ts_forecast--ts_forecast-table-macro)
   - [ts_forecast_by (Table Macro)](#anofox_fcst_ts_forecast_by--ts_forecast_by-table-macro)
   - [ts_forecast_agg (Aggregate)](#anofox_fcst_ts_forecast_agg--ts_forecast_agg-aggregate-function)
11. [Evaluation Metrics](#evaluation-metrics)
   - [Mean Absolute Error (MAE)](#mean-absolute-error-mae)
   - [Mean Squared Error (MSE)](#mean-squared-error-mse)
   - [Root Mean Squared Error (RMSE)](#root-mean-squared-error-rmse)
   - [Mean Absolute Percentage Error (MAPE)](#mean-absolute-percentage-error-mape)
   - [Symmetric MAPE (sMAPE)](#symmetric-mape-smape)
   - [Mean Absolute Scaled Error (MASE)](#mean-absolute-scaled-error-mase)
   - [R-squared](#r-squared)
   - [Forecast Bias](#forecast-bias)
   - [Relative MAE (rMAE)](#relative-mae-rmae)
   - [Quantile Loss](#quantile-loss)
   - [Mean Quantile Loss](#mean-quantile-loss)
   - [Prediction Interval Coverage](#prediction-interval-coverage)

---

## Table Macros (Table-Level API)

Table macros provide a high-level API for working directly with tables. They are the **recommended way** to use the extension for most use cases, offering a clean SQL interface without needing to manually aggregate data into arrays.

### How Table Macros Work

Table macros take a **table name as a string** and **column names as unquoted identifiers**. They automatically handle:
- Grouping data by the specified group column
- Ordering data by the date column
- Aggregating values into arrays for processing
- Unpacking results back into tabular format

**Syntax Pattern:**
```sql
SELECT * FROM macro_name('table_name', group_col, date_col, value_col, ...);
```

**Key Points:**
- Table name is a **quoted string**: `'my_table'`
- Column names are **unquoted identifiers**: `product_id`, `date`, `value`
- Results are returned as a table that can be filtered, joined, or further processed

**Example Comparison:**
```sql
-- Using table macro (recommended)
SELECT * FROM ts_stats('sales_data', product_id, sale_date, quantity);

-- Equivalent using scalar function with GROUP BY
SELECT
    product_id,
    ts_stats(LIST(quantity ORDER BY sale_date)) AS stats
FROM sales_data
GROUP BY product_id;
```

---

### ts_stats (Table Macro)

Compute statistics for grouped time series.

```sql
SELECT * FROM ts_stats(table_name, group_col, date_col, value_col);
```

**Parameters:**
- `table_name` - Source table (identifier)
- `group_col` - Column for grouping series
- `date_col` - Date/timestamp column
- `value_col` - Value column

**Example:**
```sql
SELECT * FROM ts_stats(sales_data, product_id, sale_date, quantity);
```

### ts_quality_report

Generate quality report from a stats table.

```sql
SELECT * FROM ts_quality_report(stats_table, min_length := 10);
```

### ts_data_quality

Assess data quality per series.

```sql
SELECT * FROM ts_data_quality(source, id_col, date_col, value_col, n_short := 10);
```

### ts_drop_short

Filter out series with fewer than `min_length` observations.

```sql
SELECT * FROM ts_drop_short(source, group_col, min_length := 10);
```

### ts_drop_constant

Filter out constant series.

```sql
SELECT * FROM ts_drop_constant(source, group_col, value_col);
```

### ts_drop_leading_zeros / ts_drop_trailing_zeros / ts_drop_edge_zeros

Remove leading/trailing/both zeros from series.

```sql
SELECT * FROM ts_drop_leading_zeros(source, group_col, date_col, value_col);
SELECT * FROM ts_drop_trailing_zeros(source, group_col, date_col, value_col);
SELECT * FROM ts_drop_edge_zeros(source, group_col, date_col, value_col);
```

### ts_fill_nulls_const / ts_fill_nulls_forward / ts_fill_nulls_backward / ts_fill_nulls_mean

Fill NULL values with various strategies.

```sql
SELECT * FROM ts_fill_nulls_const(source, group_col, date_col, value_col, fill_value := 0);
SELECT * FROM ts_fill_nulls_forward(source, group_col, date_col, value_col);
SELECT * FROM ts_fill_nulls_backward(source, group_col, date_col, value_col);
SELECT * FROM ts_fill_nulls_mean(source, group_col, date_col, value_col);
```

### ts_fill_gaps

Fills gaps in time series data by generating a complete date sequence at a specified frequency. Missing timestamps are inserted with NULL values, enabling proper handling of irregular time series.

**Purpose:**
Time series data often has missing observations due to weekends, holidays, sensor failures, or irregular data collection. Many forecasting algorithms and statistical methods require regularly-spaced observations. This function creates a complete, regular time grid with NULLs where data was missing, which can then be imputed using functions like `ts_fill_nulls_forward` or `ts_fill_nulls_mean`.

```sql
SELECT * FROM ts_fill_gaps(source, group_col, date_col, value_col, frequency);
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name (quoted string) |
| `group_col` | IDENTIFIER | Column for grouping series (unquoted) |
| `date_col` | IDENTIFIER | Date/timestamp column (unquoted) |
| `value_col` | IDENTIFIER | Value column (unquoted) |
| `frequency` | VARCHAR | Interval frequency (see [Frequency Formats](#frequency-formats)) |

**Returns:** A table with columns:
- `group_col` - The grouping identifier
- `time_col` - Complete timestamp sequence (min to max per group)
- `value_col` - Original values where they existed, NULL for inserted timestamps

**Example:**
```sql
-- Sample data with gaps (missing Jan 2nd)
CREATE TABLE sales_data AS
SELECT 'ProductA' as product_id, '2024-01-01'::TIMESTAMP as sale_date, 100.0 as quantity
UNION ALL SELECT 'ProductA', '2024-01-03'::TIMESTAMP, 150.0;

-- Fill the gap at daily frequency
SELECT * FROM ts_fill_gaps('sales_data', product_id, sale_date, quantity, '1d');
-- Result:
-- | product_id | time_col            | value_col |
-- |------------|---------------------|-----------|
-- | ProductA   | 2024-01-01 00:00:00 | 100.0     |
-- | ProductA   | 2024-01-02 00:00:00 | NULL      |  <- Gap filled with NULL
-- | ProductA   | 2024-01-03 00:00:00 | 150.0     |

-- Chain with imputation to fill the NULL values
WITH filled AS (
    SELECT * FROM ts_fill_gaps('sales_data', product_id, sale_date, quantity, '1d')
)
SELECT
    group_col,
    time_col,
    ts_fill_nulls_forward(LIST(value_col ORDER BY time_col)) AS imputed_values
FROM filled
GROUP BY group_col;
```

**Notes:**
- The function operates per group, finding the min and max timestamps for each group
- Only gaps within the observed range are filled (no extrapolation)
- Use `ts_fill_forward` to extend the series beyond the last observation

---

### ts_fill_forward

Extends time series data forward to a target date at a specified frequency. All extended timestamps receive NULL values, which is useful for preparing forecast horizons or aligning multiple series to a common end date.

**Purpose:**
When preparing data for forecasting, you often need to create placeholder rows for future timestamps that will receive forecasted values. This function extends each series from its last observation to a specified target date, creating the structure needed for forecast output.

```sql
SELECT * FROM ts_fill_forward(source, group_col, date_col, value_col, target_date, frequency);
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name (quoted string) |
| `group_col` | IDENTIFIER | Column for grouping series (unquoted) |
| `date_col` | IDENTIFIER | Date/timestamp column (unquoted) |
| `value_col` | IDENTIFIER | Value column (unquoted) |
| `target_date` | TIMESTAMP | Target date to extend series to |
| `frequency` | VARCHAR | Interval frequency (see [Frequency Formats](#frequency-formats)) |

**Returns:** A table with all original rows plus new rows extending to `target_date`:
- `group_col` - The grouping identifier
- `time_col` - Timestamps including extended future dates
- `value_col` - Original values for historical data, NULL for future timestamps

**Example:**
```sql
-- Historical data ending Jan 2nd
CREATE TABLE sales_data AS
SELECT 'ProductA' as product_id, '2024-01-01'::TIMESTAMP as sale_date, 100.0 as quantity
UNION ALL SELECT 'ProductA', '2024-01-02'::TIMESTAMP, 110.0;

-- Extend to Jan 5th for a 3-day forecast horizon
SELECT * FROM ts_fill_forward(
    'sales_data', product_id, sale_date, quantity,
    '2024-01-05'::TIMESTAMP, '1d'
);
-- Result:
-- | product_id | time_col            | value_col |
-- |------------|---------------------|-----------|
-- | ProductA   | 2024-01-01 00:00:00 | 100.0     |
-- | ProductA   | 2024-01-02 00:00:00 | 110.0     |
-- | ProductA   | 2024-01-03 00:00:00 | NULL      |  <- Extended
-- | ProductA   | 2024-01-04 00:00:00 | NULL      |  <- Extended
-- | ProductA   | 2024-01-05 00:00:00 | NULL      |  <- Extended

-- Typical workflow: Prepare forecast table structure
WITH forecast_ready AS (
    SELECT * FROM ts_fill_forward(
        'sales_data', product_id, sale_date, quantity,
        '2024-01-05'::TIMESTAMP, '1d'
    )
)
SELECT
    group_col,
    time_col,
    CASE
        WHEN value_col IS NOT NULL THEN value_col
        ELSE 0  -- Placeholder for forecast values
    END AS value
FROM forecast_ready;
```

**Notes:**
- Original data is preserved unchanged
- Only extends forward from each group's maximum timestamp
- If `target_date` is before or equal to the max timestamp of a group, no extension occurs for that group
- Combine with `ts_fill_gaps` to handle both internal gaps and forward extension

---

### ts_fill_gaps_operator

Low-level operator version of `ts_fill_gaps`. Functionally identical but named for internal consistency with the operator pattern.

```sql
SELECT * FROM ts_fill_gaps_operator(source, group_col, date_col, value_col, frequency);
```

**Parameters:** Same as `ts_fill_gaps`

---

### Frequency Formats

The `frequency` parameter in gap-filling and forecasting functions supports two formats for backward compatibility with the C++ API.

#### Polars-style (Compact Format)

The compact format uses a numeric prefix followed by a unit suffix. This format is **recommended** for compatibility with the original C++ API.

| Unit | Suffix | Examples | Description |
|------|--------|----------|-------------|
| Minutes | `m` or `min` | `'30m'`, `'15min'` | Minutes (1-59 typical) |
| Hours | `h` | `'1h'`, `'6h'`, `'12h'` | Hours |
| Days | `d` | `'1d'`, `'7d'`, `'14d'` | Calendar days |
| Weeks | `w` | `'1w'`, `'2w'` | Weeks (7 days) |
| Months | `mo` | `'1mo'`, `'3mo'`, `'6mo'` | Calendar months |
| Quarters | `q` | `'1q'`, `'2q'` | Quarters (converted to 3/6 months) |
| Years | `y` | `'1y'` | Calendar years |

**Examples:**
```sql
-- Daily data (most common for business data)
SELECT * FROM ts_fill_gaps('sales', product_id, date, value, '1d');

-- Hourly sensor data
SELECT * FROM ts_fill_gaps('sensors', sensor_id, timestamp, reading, '1h');

-- Weekly aggregates
SELECT * FROM ts_fill_gaps('weekly_sales', store_id, week_start, revenue, '1w');

-- Monthly financial data
SELECT * FROM ts_fill_gaps('financials', account_id, month_end, balance, '1mo');

-- Quarterly reporting
SELECT * FROM ts_fill_gaps('quarterly', division_id, quarter_end, revenue, '1q');

-- 15-minute intervals (e.g., energy data)
SELECT * FROM ts_fill_gaps('energy', meter_id, timestamp, kwh, '15m');
```

#### DuckDB INTERVAL (Native Format)

The native DuckDB INTERVAL format provides full flexibility and supports all DuckDB interval syntax.

| Unit | Examples | Description |
|------|----------|-------------|
| Seconds | `'1 second'`, `'30 seconds'` | Seconds (rare for time series) |
| Minutes | `'1 minute'`, `'15 minutes'` | Minutes |
| Hours | `'1 hour'`, `'6 hours'` | Hours |
| Days | `'1 day'`, `'7 days'` | Calendar days |
| Weeks | `'1 week'`, `'2 weeks'` | Weeks |
| Months | `'1 month'`, `'3 months'` | Calendar months |
| Years | `'1 year'` | Calendar years |

**Examples:**
```sql
-- Equivalent to '1d'
SELECT * FROM ts_fill_gaps('sales', product_id, date, value, '1 day');

-- Equivalent to '6h'
SELECT * FROM ts_fill_gaps('sensors', sensor_id, timestamp, reading, '6 hours');

-- Equivalent to '3mo'
SELECT * FROM ts_fill_gaps('quarterly', division_id, date, revenue, '3 months');
```

#### Conversion Table

| Polars-style | DuckDB INTERVAL | Typical Use Case |
|--------------|-----------------|------------------|
| `'1d'` | `'1 day'` | Daily sales, transactions |
| `'7d'` | `'7 days'` | Weekly data (explicit days) |
| `'1w'` | `'1 week'` | Weekly data (semantic week) |
| `'1h'` | `'1 hour'` | Hourly sensor data |
| `'15m'` | `'15 minutes'` | High-frequency IoT, energy |
| `'1mo'` | `'1 month'` | Monthly financials |
| `'3mo'` | `'3 months'` | Quarterly data |
| `'1q'` | `'3 months'` | Quarterly data |
| `'1y'` | `'1 year'` | Annual data |

**Notes:**
- Both formats are automatically detected and converted internally
- Polars-style is recommended for portability with C++ API code
- Use DuckDB INTERVAL for complex intervals not covered by Polars-style
- The quarter suffix `q` is converted to months (1q = 3 months, 2q = 6 months)

### ts_diff (Table Macro)

Compute differences for each group.

```sql
SELECT * FROM ts_diff(source, group_col, date_col, value_col, diff_order := 1);
```

### ts_mstl_decomposition (Table Macro)

MSTL decomposition for grouped series.

```sql
SELECT * FROM ts_mstl_decomposition(source, group_col, date_col, value_col, periods := [7]);
```

### ts_detect_changepoints / ts_detect_changepoints_by

Detect changepoints in series.

```sql
SELECT * FROM ts_detect_changepoints(source, date_col, value_col, min_size := 2, penalty := 0);
SELECT * FROM ts_detect_changepoints_by(source, group_col, date_col, value_col, min_size := 2, penalty := 0);
```

### Forecast Functions (Table Macros & Aggregate)

See the [Forecasting](#forecasting) section for complete documentation of:
- `anofox_fcst_ts_forecast` / `ts_forecast` - Single series forecasting (table macro)
- `anofox_fcst_ts_forecast_by` / `ts_forecast_by` - Multi-series forecasting (table macro)
- `anofox_fcst_ts_forecast_agg` / `ts_forecast_agg` - Aggregate forecasting function

**Quick Examples:**
```sql
-- Single series from table
SELECT * FROM anofox_fcst_ts_forecast('sales', date, amount, 'naive', 12, MAP{});

-- Multiple series from table
SELECT * FROM anofox_fcst_ts_forecast_by('sales', product_id, date, amount, 'ets', 12, MAP{});

-- Aggregate function
SELECT product_id, anofox_fcst_ts_forecast_agg(ts, value, 'naive', 12, MAP{}) AS forecast
FROM sales GROUP BY product_id;
```

---

## Exploratory Data Analysis

### Time Series Statistics

**ts_stats** (alias: `anofox_fcst_ts_stats`)

Computes 24 statistical metrics for a time series array.

**Signature:**
```sql
ts_stats(values DOUBLE[]) → STRUCT
```

**Parameters:**
- `values`: Array of time series values (DOUBLE[])

**Returns:**
```sql
STRUCT(
    length               UBIGINT,   -- Total number of observations
    n_nulls              UBIGINT,   -- Number of NULL values
    n_zeros              UBIGINT,   -- Number of zero values
    n_positive           UBIGINT,   -- Number of positive values
    n_negative           UBIGINT,   -- Number of negative values
    mean                 DOUBLE,    -- Arithmetic mean
    median               DOUBLE,    -- Median (50th percentile)
    std_dev              DOUBLE,    -- Standard deviation
    variance             DOUBLE,    -- Variance
    min                  DOUBLE,    -- Minimum value
    max                  DOUBLE,    -- Maximum value
    range                DOUBLE,    -- Range (max - min)
    sum                  DOUBLE,    -- Sum of all values
    skewness             DOUBLE,    -- Skewness
    kurtosis             DOUBLE,    -- Kurtosis
    coef_variation       DOUBLE,    -- Coefficient of variation (std_dev / mean)
    q1                   DOUBLE,    -- First quartile (25th percentile)
    q3                   DOUBLE,    -- Third quartile (75th percentile)
    iqr                  DOUBLE,    -- Interquartile range (Q3 - Q1)
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

-- Access individual fields
SELECT
    (ts_stats([1.0, 2.0, 3.0, 4.0, 5.0])).length AS len,
    (ts_stats([1.0, 2.0, 3.0, 4.0, 5.0])).mean AS mean;
```

---

### Data Quality Assessment

**ts_data_quality** (alias: `anofox_fcst_ts_data_quality`)

Assesses data quality across four dimensions: Structural, Temporal, Magnitude, and Behavioral.

**Signature:**
```sql
ts_data_quality(values DOUBLE[]) → STRUCT
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

## Data Preparation

### Series Filtering

**ts_drop_constant** (alias: `anofox_fcst_ts_drop_constant`)

Filters out constant values from an array, returning NULL if all values are constant.

**Signature:**
```sql
ts_drop_constant(values DOUBLE[]) → DOUBLE[]
```

**Example:**
```sql
SELECT ts_drop_constant([3.0, 3.0, 3.0, 3.0]);  -- Returns NULL
SELECT ts_drop_constant([1.0, 2.0, 3.0, 4.0]);  -- Returns [1.0, 2.0, 3.0, 4.0]
```

---

**ts_drop_short** (alias: `anofox_fcst_ts_drop_short`)

Returns NULL if array length is below threshold.

**Signature:**
```sql
ts_drop_short(values DOUBLE[], min_length INTEGER) → DOUBLE[]
```

**Example:**
```sql
SELECT ts_drop_short([1.0, 2.0, 3.0], 5);  -- Returns NULL (length < 5)
SELECT ts_drop_short([1.0, 2.0, 3.0, 4.0, 5.0], 5);  -- Returns the array
```

---

### Edge Cleaning

**ts_drop_leading_zeros** (alias: `anofox_fcst_ts_drop_leading_zeros`)

Removes leading zeros from an array.

**Signature:**
```sql
ts_drop_leading_zeros(values DOUBLE[]) → DOUBLE[]
```

**Example:**
```sql
SELECT ts_drop_leading_zeros([0.0, 0.0, 1.0, 2.0, 3.0]);
-- Returns: [1.0, 2.0, 3.0]
```

---

**ts_drop_trailing_zeros** (alias: `anofox_fcst_ts_drop_trailing_zeros`)

Removes trailing zeros from an array.

**Signature:**
```sql
ts_drop_trailing_zeros(values DOUBLE[]) → DOUBLE[]
```

**Example:**
```sql
SELECT ts_drop_trailing_zeros([1.0, 2.0, 3.0, 0.0, 0.0]);
-- Returns: [1.0, 2.0, 3.0]
```

---

**ts_drop_edge_zeros** (alias: `anofox_fcst_ts_drop_edge_zeros`)

Removes both leading and trailing zeros from an array.

**Signature:**
```sql
ts_drop_edge_zeros(values DOUBLE[]) → DOUBLE[]
```

**Example:**
```sql
SELECT ts_drop_edge_zeros([0.0, 0.0, 1.0, 2.0, 3.0, 0.0, 0.0]);
-- Returns: [1.0, 2.0, 3.0]
```

---

### Missing Value Imputation

**ts_fill_nulls_const** (alias: `anofox_fcst_ts_fill_nulls_const`)

Replaces NULL values with a constant.

**Signature:**
```sql
ts_fill_nulls_const(values DOUBLE[], fill_value DOUBLE) → DOUBLE[]
```

**Example:**
```sql
SELECT ts_fill_nulls_const([1.0, NULL, 3.0, NULL, 5.0], 0.0);
-- Returns: [1.0, 0.0, 3.0, 0.0, 5.0]
```

---

**ts_fill_nulls_forward** (alias: `anofox_fcst_ts_fill_nulls_forward`)

Forward fills NULL values (last observation carried forward).

**Signature:**
```sql
ts_fill_nulls_forward(values DOUBLE[]) → DOUBLE[]
```

**Example:**
```sql
SELECT ts_fill_nulls_forward([1.0, NULL, NULL, 4.0, NULL]);
-- Returns: [1.0, 1.0, 1.0, 4.0, 4.0]
```

---

**ts_fill_nulls_backward** (alias: `anofox_fcst_ts_fill_nulls_backward`)

Backward fills NULL values.

**Signature:**
```sql
ts_fill_nulls_backward(values DOUBLE[]) → DOUBLE[]
```

**Example:**
```sql
SELECT ts_fill_nulls_backward([NULL, NULL, 3.0, NULL, 5.0]);
-- Returns: [3.0, 3.0, 3.0, 5.0, 5.0]
```

---

**ts_fill_nulls_mean** (alias: `anofox_fcst_ts_fill_nulls_mean`)

Fills NULL values with the series mean.

**Signature:**
```sql
ts_fill_nulls_mean(values DOUBLE[]) → DOUBLE[]
```

**Example:**
```sql
SELECT ts_fill_nulls_mean([1.0, NULL, 3.0, NULL, 5.0]);
-- Returns: [1.0, 3.0, 3.0, 3.0, 5.0] (mean = 3.0)
```

---

### Differencing

**ts_diff** (alias: `anofox_fcst_ts_diff`)

Computes differences of specified order.

**Signature:**
```sql
ts_diff(values DOUBLE[], order INTEGER) → DOUBLE[]
```

**Parameters:**
- `values`: Input array
- `order`: Difference order (must be > 0)

**Example:**
```sql
-- First differences
SELECT ts_diff([1.0, 2.0, 4.0, 7.0], 1);
-- Returns: [1.0, 2.0, 3.0]

-- Second differences
SELECT ts_diff([1.0, 2.0, 4.0, 7.0, 11.0], 2);
-- Returns: [1.0, 1.0, 1.0]
```

---

## Seasonality

### Period Detection

**ts_detect_periods** (alias: `anofox_fcst_ts_detect_periods`)

Detects seasonal periods using multiple methods from fdars-core.

> **See also:** [Individual Period Detection Methods](#individual-period-detection-methods) for direct access to 11 specialized algorithms with full control over parameters and detailed return values including `period`, `frequency`, `power`, and `confidence` fields.

**Signature:**
```sql
ts_detect_periods(values DOUBLE[]) → STRUCT
ts_detect_periods(values DOUBLE[], method VARCHAR) → STRUCT
```

**Parameters:**
- `values`: Time series values (DOUBLE[])
- `method`: Detection method (VARCHAR, optional, default: 'auto')
  - `'fft'` - FFT periodogram-based estimation
  - `'acf'` - Autocorrelation function approach
  - `'regression'` - Fourier regression grid search
  - `'multi'` - Iterative residual subtraction for concurrent periodicities
  - `'wavelet'` - Wavelet-based period detection
  - `'auto'` - Automatic method selection (default)

**Returns:**
```sql
STRUCT(
    periods          INTEGER[],     -- Detected periods sorted by strength
    confidences      DOUBLE[],      -- Confidence score for each period (0-1)
    primary_period   INTEGER,       -- Dominant period
    method_used      VARCHAR        -- Method that was used
)
```

**Example:**
```sql
-- Detect periods using FFT
SELECT ts_detect_periods(LIST(value ORDER BY date), 'fft') AS periods
FROM sales GROUP BY product_id;

-- Default auto-selection
SELECT (ts_detect_periods([1,2,3,4,1,2,3,4,1,2,3,4]::DOUBLE[])).primary_period;
-- Returns: 4

-- Access just the periods array
SELECT (ts_detect_periods([1,2,3,4,1,2,3,4,1,2,3,4]::DOUBLE[])).periods;
-- Returns: [4]
```

---

### Individual Period Detection Methods

The extension provides 11 specialized period detection algorithms, each optimized for different data characteristics. These methods are available through the `ts_detect_periods()` wrapper or can be called individually for more control.

#### Method Comparison

| Method | Speed | Noise Robustness | Best Use Case | Min Observations |
|--------|-------|------------------|---------------|------------------|
| FFT | Very Fast | Low | Clean signals | 4 |
| ACF | Fast | Medium | Cyclical patterns | 4 |
| Autoperiod | Fast | High | General purpose | 8 |
| CFD-Autoperiod | Fast | Very High | Trending data | 9 |
| Lomb-Scargle | Medium | High | Irregular sampling | 4 |
| AIC | Slow | High | Model selection | 8 |
| SSA | Medium | Medium | Complex patterns | 16 |
| STL | Slow | Medium | Decomposition | 16 |
| Matrix Profile | Slow | Very High | Pattern repetition | 32 |
| SAZED | Medium | High | Frequency resolution | 16 |
| Multi-Period | Medium | High | Multiple seasonalities | 8 |

---

#### ts_estimate_period_fft

**Description:**
Fast Fourier Transform (FFT) based periodogram analysis that identifies the dominant frequency in the signal by computing the discrete Fourier transform and finding the frequency bin with maximum spectral power.

**Signature:**
```sql
ts_estimate_period_fft(values DOUBLE[]) → STRUCT
```

**Mathematical Formula:**
```
DFT: X[k] = Σ_{t=0}^{N-1} x[t] · e^{-2πikt/N}

Power Spectrum: P[k] = |X[k]|² / N

Period: p = N / k_max  where k_max = argmax_{k>0} P[k]

Confidence: C = P[k_max] / mean(P)
```

**Returns:**
```sql
STRUCT(
    period     DOUBLE,   -- Estimated period (in samples)
    frequency  DOUBLE,   -- Dominant frequency (1/period)
    power      DOUBLE,   -- Power at the dominant frequency
    confidence DOUBLE,   -- Ratio of peak power to mean power
    method     VARCHAR   -- "fft"
)
```

**Example:**
```sql
SELECT ts_estimate_period_fft([1,2,3,4,1,2,3,4,1,2,3,4]::DOUBLE[]);
-- Returns: {period: 4.0, frequency: 0.25, power: ..., confidence: ..., method: "fft"}
```

**Reference:**
- Cooley, J.W. & Tukey, J.W. (1965). "An Algorithm for the Machine Calculation of Complex Fourier Series." *Mathematics of Computation*, 19(90), 297-301.

---

#### ts_estimate_period_acf

**Description:**
Autocorrelation Function (ACF) based period detection that measures the correlation of the signal with lagged versions of itself. The period is identified as the first significant peak in the autocorrelation function after lag 0.

**Signature:**
```sql
ts_estimate_period_acf(values DOUBLE[]) → STRUCT
ts_estimate_period_acf(values DOUBLE[], max_lag INTEGER) → STRUCT
```

**Mathematical Formula:**
```
ACF(k) = Cov(X_t, X_{t+k}) / Var(X)
       = Σ_{t=1}^{n-k} (x_t - μ)(x_{t+k} - μ) / Σ_{t=1}^{n} (x_t - μ)²

Period: p = argmax_{k>min_lag} ACF(k)  where ACF(k) > threshold
```

**Returns:** Same as `ts_estimate_period_fft` (period, frequency, power, confidence, method)

**Example:**
```sql
SELECT ts_estimate_period_acf([1,2,3,4,1,2,3,4,1,2,3,4]::DOUBLE[]);
```

**Reference:**
- Box, G.E.P. & Jenkins, G.M. (1976). *Time Series Analysis: Forecasting and Control*. Holden-Day.

---

#### ts_autoperiod

**Description:**
A hybrid two-stage approach combining FFT for initial period detection with ACF validation. The FFT provides a fast initial estimate, while ACF validation confirms the periodicity exists in the time domain.

**Signature:**
```sql
ts_autoperiod(values DOUBLE[]) → STRUCT
ts_autoperiod(values DOUBLE[], acf_threshold DOUBLE) → STRUCT
```

**Mathematical Process:**
```
Stage 1 (FFT): p_fft = estimate_period_fft(x)
Stage 2 (ACF Validation): v = ACF(p_fft)
Detection: detected = (v > threshold)  [default threshold = 0.3]
```

**Returns:**
```sql
STRUCT(
    period         DOUBLE,   -- Detected period (from FFT)
    fft_confidence DOUBLE,   -- FFT peak-to-mean power ratio
    acf_validation DOUBLE,   -- ACF value at detected period
    detected       BOOLEAN,  -- TRUE if acf_validation > threshold
    method         VARCHAR   -- "autoperiod"
)
```

**Example:**
```sql
SELECT ts_autoperiod([1,2,3,4,1,2,3,4,1,2,3,4]::DOUBLE[]);
-- Check if period was confidently detected
SELECT (ts_autoperiod(LIST(value ORDER BY date))).detected FROM sales GROUP BY product_id;
```

**Reference:**
- Vlachos, M., Yu, P., & Castelli, V. (2005). "On Periodicity Detection and Structural Periodic Similarity." *SIAM International Conference on Data Mining*.

---

#### ts_cfd_autoperiod

**Description:**
Clustered Filtered Detrended (CFD) variant of autoperiod that applies first-differencing before FFT analysis to remove linear trends, making it more robust for trending time series.

**Signature:**
```sql
ts_cfd_autoperiod(values DOUBLE[]) → STRUCT
ts_cfd_autoperiod(values DOUBLE[], acf_threshold DOUBLE) → STRUCT
```

**Mathematical Process:**
```
Stage 1 (Differencing): y[t] = x[t+1] - x[t]
Stage 2 (FFT on differenced): p = estimate_period_fft(y)
Stage 3 (ACF on original): v = ACF_original(p)
Detection: detected = (v > threshold)  [default threshold = 0.25]
```

**Returns:** Same as `ts_autoperiod` (period, fft_confidence, acf_validation, detected, method)

**Example:**
```sql
-- Better for data with trends
SELECT ts_cfd_autoperiod([1,3,5,7,2,4,6,8,3,5,7,9]::DOUBLE[]);
```

**Reference:**
- Elfeky, M.G., Aref, W.G., & Elmagarmid, A.K. (2005). "Periodicity Detection in Time Series Databases." *IEEE Transactions on Knowledge and Data Engineering*, 17(7), 875-887.

---

#### ts_estimate_period_lomb_scargle

**Description:**
The Lomb-Scargle periodogram is a generalization of the Fourier transform designed for unevenly sampled data. It fits sinusoids at each test frequency and provides statistical significance through the false alarm probability.

**Signature:**
```sql
ts_estimate_period_lomb_scargle(values DOUBLE[]) → STRUCT
ts_estimate_period_lomb_scargle(values DOUBLE[], times DOUBLE[]) → STRUCT
ts_estimate_period_lomb_scargle(values DOUBLE[], times DOUBLE[], min_period DOUBLE, max_period DOUBLE) → STRUCT
```

**Mathematical Formula:**
```
τ(ω) = arctan(Σsin(2ωt_i) / Σcos(2ωt_i)) / (2ω)

P(ω) = (1/2σ²) · [ (Σy_i·cos(ω(t_i-τ)))² / Σcos²(ω(t_i-τ))
                 + (Σy_i·sin(ω(t_i-τ)))² / Σsin²(ω(t_i-τ)) ]

False Alarm Probability: FAP ≈ 1 - (1 - e^{-P})^M
```

**Returns:**
```sql
STRUCT(
    period           DOUBLE,   -- Detected period
    frequency        DOUBLE,   -- Corresponding frequency
    power            DOUBLE,   -- Normalized power at peak
    false_alarm_prob DOUBLE,   -- FAP (lower = more significant)
    method           VARCHAR   -- "lomb_scargle"
)
```

**Example:**
```sql
-- For irregularly sampled data
SELECT ts_estimate_period_lomb_scargle(
    [1.0, 2.1, 0.9, 2.0, 1.1]::DOUBLE[],
    [0.0, 0.25, 0.5, 0.75, 1.0]::DOUBLE[]  -- irregular times
);
```

**Reference:**
- Lomb, N.R. (1976). "Least-squares frequency analysis of unequally spaced data." *Astrophysics and Space Science*, 39, 447-462.
- Scargle, J.D. (1982). "Studies in astronomical time series analysis II." *The Astrophysical Journal*, 263, 835-853.

---

#### ts_estimate_period_aic

**Description:**
Information criterion-based period selection that fits sinusoidal models at multiple candidate periods and selects the period minimizing the Akaike Information Criterion (AIC), balancing model fit against complexity.

**Signature:**
```sql
ts_estimate_period_aic(values DOUBLE[]) → STRUCT
ts_estimate_period_aic(values DOUBLE[], min_period DOUBLE, max_period DOUBLE) → STRUCT
ts_estimate_period_aic(values DOUBLE[], min_period DOUBLE, max_period DOUBLE, n_candidates INTEGER) → STRUCT
```

**Mathematical Formula:**
```
Model: y_t = μ + Σ_{k=1}^{K} [a_k·cos(2πkt/p) + b_k·sin(2πkt/p)] + ε_t

RSS = Σ(y_t - ŷ_t)²

AIC = n·ln(RSS/n) + 2·(2K + 1)
BIC = n·ln(RSS/n) + (2K + 1)·ln(n)
R² = 1 - RSS / SS_total

Best period: p* = argmin_p AIC(p)
```

**Returns:**
```sql
STRUCT(
    period    DOUBLE,   -- Best period (minimum AIC)
    aic       DOUBLE,   -- AIC of best model
    bic       DOUBLE,   -- BIC of best model
    rss       DOUBLE,   -- Residual sum of squares
    r_squared DOUBLE,   -- Coefficient of determination
    method    VARCHAR   -- "aic"
)
```

**Example:**
```sql
SELECT ts_estimate_period_aic([1,2,3,4,1,2,3,4,1,2,3,4]::DOUBLE[]);
-- Get R² to assess fit quality
SELECT (ts_estimate_period_aic(LIST(value ORDER BY date))).r_squared FROM sales;
```

**Reference:**
- Akaike, H. (1974). "A new look at the statistical model identification." *IEEE Transactions on Automatic Control*, 19(6), 716-723.

---

#### ts_estimate_period_ssa

**Description:**
Singular Spectrum Analysis (SSA) decomposes the time series using eigenvalue decomposition of the trajectory matrix. Periodic components appear as paired eigenvalues, and the period is estimated from eigenvector zero-crossings.

**Signature:**
```sql
ts_estimate_period_ssa(values DOUBLE[]) → STRUCT
ts_estimate_period_ssa(values DOUBLE[], window_size INTEGER) → STRUCT
```

**Mathematical Formula:**
```
Trajectory Matrix: X[i,j] = x[i+j-1]  for i=1..L, j=1..K  (L=window, K=n-L+1)

Covariance: C = XX^T / K

Eigendecomposition: C = UΛU^T

Period from eigenvector u_k: p ≈ 2L / (zero_crossings(u_k))

Variance explained: (λ_1 + λ_2) / Σλ_i
```

**Returns:**
```sql
STRUCT(
    period             DOUBLE,   -- Primary detected period
    variance_explained DOUBLE,   -- By first periodic component pair
    n_eigenvalues      UBIGINT,  -- Number of eigenvalues returned
    method             VARCHAR   -- "ssa"
)
```

**Example:**
```sql
SELECT ts_estimate_period_ssa([1,2,3,4,1,2,3,4,1,2,3,4,1,2,3,4]::DOUBLE[]);
```

**Reference:**
- Golyandina, N., Nekrutkin, V., & Zhigljavsky, A. (2001). *Analysis of Time Series Structure: SSA and Related Techniques*. Chapman & Hall/CRC.

---

#### ts_estimate_period_stl

**Description:**
STL (Seasonal and Trend decomposition using LOESS) based period detection. Multiple candidate periods are tested, and the period maximizing the seasonal strength metric is selected.

**Signature:**
```sql
ts_estimate_period_stl(values DOUBLE[]) → STRUCT
ts_estimate_period_stl(values DOUBLE[], min_period INTEGER, max_period INTEGER) → STRUCT
```

**Mathematical Formula:**
```
STL Decomposition: Y_t = T_t + S_t + R_t  (Trend + Seasonal + Remainder)

Seasonal Strength: F_S = max(0, 1 - Var(R) / Var(S + R))
Trend Strength: F_T = max(0, 1 - Var(R) / Var(T + R))

Best period: p* = argmax_p F_S(p)
```

**Returns:**
```sql
STRUCT(
    period            DOUBLE,   -- Best period (max seasonal strength)
    seasonal_strength DOUBLE,   -- F_S at best period (0-1)
    trend_strength    DOUBLE,   -- F_T at best period (0-1)
    method            VARCHAR   -- "stl"
)
```

**Example:**
```sql
SELECT ts_estimate_period_stl([1,2,3,4,1,2,3,4,1,2,3,4,1,2,3,4]::DOUBLE[]);
-- Get seasonal strength interpretation:
-- 0.0-0.3: Weak, 0.3-0.6: Moderate, 0.6-0.8: Strong, 0.8-1.0: Very strong
```

**Reference:**
- Cleveland, R.B., Cleveland, W.S., McRae, J.E., & Terpenning, I. (1990). "STL: A Seasonal-Trend Decomposition Procedure Based on Loess." *Journal of Official Statistics*, 6(1), 3-73.
- Wang, X., Smith, K., & Hyndman, R. (2006). "Characteristic-based clustering for time series data." *Data Mining and Knowledge Discovery*, 13(3), 335-364.

---

#### ts_estimate_period_matrix_profile

**Description:**
Matrix Profile based period detection finds repeating patterns (motifs) by computing z-normalized Euclidean distances between all subsequences. The most common lag between nearest-neighbor subsequences indicates the period.

**Signature:**
```sql
ts_estimate_period_matrix_profile(values DOUBLE[]) → STRUCT
ts_estimate_period_matrix_profile(values DOUBLE[], subsequence_length INTEGER) → STRUCT
```

**Mathematical Formula:**
```
Z-normalization: z_i = (X_i - μ_i) / σ_i  for each subsequence

Distance: d(i,j) = √(Σ(z_i - z_j)²)

Matrix Profile: MP[i] = min_{j: |i-j| > exclusion} d(i,j)
Profile Index: PI[i] = argmin_{j: |i-j| > exclusion} d(i,j)

Lag histogram: H[k] = count of (|i - PI[i]| = k)
Period: p = argmax_k H[k]
```

**Returns:**
```sql
STRUCT(
    period             DOUBLE,   -- Most common motif lag
    confidence         DOUBLE,   -- Fraction of motifs at this lag
    n_motifs           UBIGINT,  -- Number of motif pairs found
    subsequence_length UBIGINT,  -- Subsequence length used
    method             VARCHAR   -- "matrix_profile"
)
```

**Example:**
```sql
-- Best for detecting repeating patterns regardless of amplitude
SELECT ts_estimate_period_matrix_profile(LIST(value ORDER BY date)) FROM sensor_data;
```

**Reference:**
- Yeh, C.C.M., et al. (2016). "Matrix Profile I: All Pairs Similarity Joins for Time Series." *IEEE ICDM*.
- Yeh, C.C.M., et al. (2017). "Matrix Profile VI: Meaningful Multidimensional Motif Discovery." *IEEE ICDM*.

---

#### ts_estimate_period_sazed

**Description:**
SAZED (Spectral Analysis with Zero-padded Enhanced DFT) uses zero-padding to increase frequency resolution and applies windowing to reduce spectral leakage. SNR estimation provides confidence measurement.

**Signature:**
```sql
ts_estimate_period_sazed(values DOUBLE[]) → STRUCT
ts_estimate_period_sazed(values DOUBLE[], padding_factor INTEGER) → STRUCT
```

**Mathematical Formula:**
```
Mean removal: y = x - μ

Windowing (Hann): w[t] = 0.5·(1 - cos(2πt/(n-1)))

Zero-padding: pad to (n · factor).next_power_of_2()

DFT: Y[k] = Σ_{t=0}^{N-1} y[t]·w[t]·e^{-2πikt/N_padded}

Power: P[k] = |Y[k]|² / N_padded

SNR = P[k_max] / median(P_noise)
```

**Returns:**
```sql
STRUCT(
    period DOUBLE,   -- Primary detected period
    power  DOUBLE,   -- Spectral power at peak
    snr    DOUBLE,   -- Signal-to-noise ratio
    method VARCHAR   -- "sazed"
)
```

**Example:**
```sql
-- Better frequency resolution than standard FFT
SELECT ts_estimate_period_sazed([1,2,3,4,1,2,3,4,1,2,3,4]::DOUBLE[]);
```

**Reference:**
- Ding, H., et al. (2008). "Querying and Mining of Time Series Data: Experimental Comparison of Representations and Distance Measures." *VLDB Endowment*, 1(2), 1542-1552.

---

#### ts_detect_multiple_periods

**Description:**
Iterative multiple period detection using residual subtraction. The dominant period is detected, its sinusoidal component is removed, and the process repeats on the residual to find additional periodic components.

**Signature:**
```sql
ts_detect_multiple_periods(values DOUBLE[]) → STRUCT
ts_detect_multiple_periods(values DOUBLE[], max_periods INTEGER) → STRUCT
ts_detect_multiple_periods(values DOUBLE[], max_periods INTEGER, min_confidence DOUBLE, min_strength DOUBLE) → STRUCT
```

**Mathematical Algorithm:**
```
Initialize: residual = x

For i = 1 to max_periods:
    1. p_i, conf_i = estimate_period_fft(residual)
    2. If conf_i < min_confidence: break
    3. Fit sinusoid: ŝ_i = A_i·sin(2πt/p_i + φ_i)
       where A_i, φ_i from least squares
    4. strength_i = 1 - Var(residual - ŝ_i) / Var(residual)
    5. If strength_i < min_strength: break
    6. residual = residual - ŝ_i
    7. Store (p_i, conf_i, strength_i, A_i, φ_i, i)
```

**Returns:**
```sql
STRUCT(
    period_values     DOUBLE[],   -- Detected periods (in samples)
    confidence_values DOUBLE[],   -- FFT peak confidence for each
    strength_values   DOUBLE[],   -- Variance explained by each
    amplitude_values  DOUBLE[],   -- Sinusoidal amplitude for each
    phase_values      DOUBLE[],   -- Phase (radians) for each
    iteration_values  UBIGINT[],  -- Detection iteration (1-indexed)
    n_periods         UBIGINT,    -- Number of detected periods
    primary_period    DOUBLE,     -- Strongest period (iteration 1)
    method            VARCHAR     -- "multi"
)
```

**Example:**
```sql
-- Detect multiple seasonalities (e.g., daily and weekly)
SELECT ts_detect_multiple_periods(LIST(value ORDER BY date), 3) FROM hourly_data;

-- Access individual periods
SELECT (ts_detect_multiple_periods(values)).period_values AS periods FROM my_data;
```

**Reference:**
- Parthasarathy, S., Mehta, S., & Srinivasan, S. (2006). "Robust Periodicity Detection Algorithms." *ACM CIKM*.

---

### Seasonality Analysis

**ts_analyze_seasonality** (alias: `anofox_fcst_ts_analyze_seasonality`)

Provides detailed seasonality analysis using fdars-core algorithms.

**Signature:**
```sql
ts_analyze_seasonality(values DOUBLE[]) → STRUCT
ts_analyze_seasonality(timestamps TIMESTAMP[], values DOUBLE[]) → STRUCT
```

**Returns:**
```sql
STRUCT(
    detected_periods    INTEGER[],  -- Array of detected seasonal periods
    primary_period      INTEGER,    -- Primary (dominant) seasonal period
    seasonal_strength   DOUBLE,     -- Seasonal strength (0-1)
    trend_strength      DOUBLE      -- Trend strength (0-1)
)
```

**Example:**
```sql
SELECT ts_analyze_seasonality([1,2,3,4,1,2,3,4,1,2,3,4,1,2,3,4]::DOUBLE[]);
-- Returns: {detected_periods: [4], primary_period: 4, seasonal_strength: 0.95, trend_strength: 0.1}
```

---

### Seasonal Strength

**ts_seasonal_strength** (alias: `anofox_fcst_ts_seasonal_strength`)

Calculate seasonal strength using specified method.

**Signature:**
```sql
ts_seasonal_strength(values DOUBLE[], method VARCHAR) → DOUBLE
ts_seasonal_strength(values DOUBLE[], method VARCHAR, period INTEGER) → DOUBLE
```

**Parameters:**
- `values`: Time series values (DOUBLE[])
- `method`: Strength calculation method (VARCHAR)
  - `'variance'` - Variance decomposition method
  - `'spectral'` - Spectral-based measurement
  - `'wavelet'` - Wavelet-based strength measurement
  - `'auto'` - Automatic selection (default)
- `period`: Optional known period (INTEGER)

**Returns:** DOUBLE in range [0, 1]

**Example:**
```sql
SELECT ts_seasonal_strength([1,2,3,4,1,2,3,4]::DOUBLE[], 'spectral');
-- Returns: 0.95

SELECT ts_seasonal_strength([1,2,3,4,1,2,3,4]::DOUBLE[], 'wavelet', 4);
-- Returns: 0.92
```

---

### Windowed Seasonal Strength

**ts_seasonal_strength_windowed** (alias: `anofox_fcst_ts_seasonal_strength_windowed`)

Compute time-varying seasonal strength using sliding windows.

**Signature:**
```sql
ts_seasonal_strength_windowed(values DOUBLE[], window_size INTEGER) → STRUCT
ts_seasonal_strength_windowed(values DOUBLE[], window_size INTEGER, step INTEGER) → STRUCT
```

**Returns:**
```sql
STRUCT(
    window_starts    UBIGINT[],    -- Start indices of each window
    strengths        DOUBLE[],     -- Seasonal strength for each window
    mean_strength    DOUBLE,       -- Mean strength across windows
    min_strength     DOUBLE,       -- Minimum strength
    max_strength     DOUBLE,       -- Maximum strength
    is_stable        BOOLEAN       -- Whether seasonality is stable (std < 0.1)
)
```

**Example:**
```sql
SELECT ts_seasonal_strength_windowed(LIST(value ORDER BY date), 52) AS windowed
FROM weekly_sales GROUP BY product_id;
```

---

### Seasonality Classification

**ts_classify_seasonality** (alias: `anofox_fcst_ts_classify_seasonality`)

Classify the type of seasonal pattern.

**Signature:**
```sql
ts_classify_seasonality(values DOUBLE[]) → STRUCT
ts_classify_seasonality(values DOUBLE[], period INTEGER) → STRUCT
```

**Returns:**
```sql
STRUCT(
    seasonal_type       VARCHAR,      -- 'none', 'weak', 'moderate', 'strong', 'dominant'
    pattern_type        VARCHAR,      -- 'sinusoidal', 'sawtooth', 'pulse', 'complex', 'irregular'
    symmetry            DOUBLE,       -- Symmetry score (-1 to 1, 0 = symmetric)
    sharpness           DOUBLE,       -- Peak sharpness (0 = smooth, 1 = sharp)
    is_multiplicative   BOOLEAN       -- Whether pattern appears multiplicative
)
```

**Example:**
```sql
SELECT ts_classify_seasonality(LIST(value ORDER BY date)) AS classification
FROM sales GROUP BY product_id;
```

---

### Seasonality Change Detection

**ts_detect_seasonality_changes** (alias: `anofox_fcst_ts_detect_seasonality_changes`)

Detect when seasonality begins or ends in a time series.

**Signature:**
```sql
ts_detect_seasonality_changes(values DOUBLE[]) → STRUCT
ts_detect_seasonality_changes(values DOUBLE[], threshold DOUBLE) → STRUCT
```

**Returns:**
```sql
STRUCT(
    change_indices       UBIGINT[],   -- Indices where changes occur
    change_types         VARCHAR[],   -- 'onset' or 'cessation' for each
    strength_before      DOUBLE[],    -- Seasonal strength before each change
    strength_after       DOUBLE[],    -- Seasonal strength after each change
    n_changes            UBIGINT      -- Number of detected changes
)
```

**Example:**
```sql
SELECT ts_detect_seasonality_changes(LIST(value ORDER BY date)) AS changes
FROM product_sales GROUP BY product_id;
```

---

### Instantaneous Period

**ts_instantaneous_period** (alias: `anofox_fcst_ts_instantaneous_period`)

Estimate time-varying period using Hilbert transform for drifting seasonality.

**Signature:**
```sql
ts_instantaneous_period(values DOUBLE[]) → STRUCT
```

**Returns:**
```sql
STRUCT(
    periods              DOUBLE[],    -- Instantaneous period at each point
    mean_period          DOUBLE,      -- Mean instantaneous period
    period_drift         DOUBLE,      -- Linear trend in period (positive = lengthening)
    is_stationary        BOOLEAN      -- Whether period is stationary (drift < threshold)
)
```

**Example:**
```sql
SELECT ts_instantaneous_period(LIST(value ORDER BY date)) AS inst_period
FROM sensor_data GROUP BY sensor_id;
```

---

### Amplitude Modulation Detection

**ts_detect_amplitude_modulation** (alias: `anofox_fcst_ts_detect_amplitude_modulation`)

Detect amplitude modulation in seasonal patterns using wavelet analysis.

**Signature:**
```sql
ts_detect_amplitude_modulation(values DOUBLE[]) → STRUCT
ts_detect_amplitude_modulation(values DOUBLE[], period INTEGER) → STRUCT
```

**Returns:**
```sql
STRUCT(
    has_modulation       BOOLEAN,      -- Whether amplitude modulation is detected
    modulation_period    INTEGER,      -- Period of the modulation (if detected)
    modulation_strength  DOUBLE,       -- Strength of modulation (0-1)
    envelope             DOUBLE[],     -- Extracted amplitude envelope
    is_growing           BOOLEAN       -- Whether amplitude is increasing over time
)
```

**Example:**
```sql
SELECT ts_detect_amplitude_modulation(LIST(value ORDER BY date)) AS modulation
FROM sales GROUP BY product_id;
```

---

## Peak Detection

### Detect Peaks

**ts_detect_peaks** (alias: `anofox_fcst_ts_detect_peaks`)

Detect peaks (local maxima) in time series data with prominence calculation.

**Signature:**
```sql
ts_detect_peaks(values DOUBLE[]) → STRUCT
ts_detect_peaks(values DOUBLE[], min_prominence DOUBLE) → STRUCT
ts_detect_peaks(values DOUBLE[], min_prominence DOUBLE, min_distance INTEGER) → STRUCT
```

**Parameters:**
- `values`: Time series values (DOUBLE[])
- `min_prominence`: Minimum peak prominence threshold (default: 0.0, meaning all peaks)
- `min_distance`: Minimum distance between peaks in observations (default: 1)

**Returns:**
```sql
STRUCT(
    peak_indices     UBIGINT[],    -- Indices of detected peaks (0-based)
    peak_values      DOUBLE[],     -- Values at peak locations
    prominences      DOUBLE[],     -- Prominence of each peak
    n_peaks          UBIGINT       -- Number of peaks detected
)
```

**Example:**
```sql
-- Detect all peaks
SELECT ts_detect_peaks([1.0, 3.0, 2.0, 5.0, 1.0, 4.0, 2.0]::DOUBLE[]);
-- Returns: {peak_indices: [1, 3, 5], peak_values: [3.0, 5.0, 4.0], ...}

-- Detect significant peaks only (prominence > 2.0)
SELECT ts_detect_peaks(LIST(value ORDER BY date), 2.0) AS peaks
FROM sensor_data GROUP BY sensor_id;

-- With minimum distance between peaks
SELECT ts_detect_peaks(LIST(value ORDER BY date), 1.0, 7) AS peaks
FROM daily_data GROUP BY series_id;
```

---

### Peak Timing Analysis

**ts_analyze_peak_timing** (alias: `anofox_fcst_ts_analyze_peak_timing`)

Analyze peak timing variability across seasonal cycles.

**Signature:**
```sql
ts_analyze_peak_timing(values DOUBLE[], period INTEGER) → STRUCT
```

**Parameters:**
- `values`: Time series values (DOUBLE[])
- `period`: Expected seasonal period (INTEGER)

**Returns:**
```sql
STRUCT(
    mean_peak_position    DOUBLE,     -- Mean position of peak within cycle (0 to period-1)
    peak_position_std     DOUBLE,     -- Standard deviation of peak positions
    peak_timing_cv        DOUBLE,     -- Coefficient of variation for peak timing
    n_cycles              UBIGINT,    -- Number of complete cycles analyzed
    is_consistent         BOOLEAN     -- Whether peak timing is consistent (cv < 0.2)
)
```

**Example:**
```sql
-- Analyze weekly peak timing
SELECT ts_analyze_peak_timing(LIST(value ORDER BY date), 7) AS timing
FROM daily_sales GROUP BY store_id;

-- Check if monthly peaks occur at consistent times
SELECT
    product_id,
    (ts_analyze_peak_timing(LIST(value ORDER BY date), 30)).is_consistent AS has_consistent_peaks
FROM monthly_data GROUP BY product_id;
```

---

## Detrending

### Detrend

**ts_detrend** (alias: `anofox_fcst_ts_detrend`)

Remove trend from time series using various methods.

**Signature:**
```sql
ts_detrend(values DOUBLE[], method VARCHAR) → STRUCT
```

**Parameters:**
- `values`: Time series values (DOUBLE[])
- `method`: Detrending method (VARCHAR)
  - `'linear'` - Linear trend removal via least squares
  - `'polynomial'` - Polynomial trend (degree 2) via QR decomposition
  - `'diff'` - First differencing
  - `'diff2'` - Second differencing
  - `'loess'` - LOESS local polynomial regression
  - `'spline'` - P-splines detrending
  - `'auto'` - Automatic selection using AIC

**Returns:**
```sql
STRUCT(
    detrended        DOUBLE[],     -- Detrended values
    trend            DOUBLE[],     -- Extracted trend component
    method_used      VARCHAR,      -- Method that was used
    residual_var     DOUBLE        -- Variance of detrended series
)
```

**Example:**
```sql
-- Linear detrending
SELECT ts_detrend([1,2,3,4,5,6,7,8,9,10]::DOUBLE[], 'linear');

-- Automatic method selection
SELECT (ts_detrend(LIST(value ORDER BY date), 'auto')).detrended AS detrended_values
FROM sales GROUP BY product_id;

-- LOESS detrending for non-linear trends
SELECT ts_detrend(LIST(value ORDER BY date), 'loess') AS result
FROM sensor_data GROUP BY sensor_id;
```

---

## Time Series Decomposition

### Decompose

**ts_decompose** (alias: `anofox_fcst_ts_decompose`)

Additive or multiplicative seasonal decomposition.

**Signature:**
```sql
ts_decompose(values DOUBLE[], type VARCHAR) → STRUCT
ts_decompose(values DOUBLE[], type VARCHAR, period INTEGER) → STRUCT
```

**Parameters:**
- `values`: Time series values (DOUBLE[])
- `type`: Decomposition type (VARCHAR)
  - `'additive'` - data = trend + seasonal + remainder
  - `'multiplicative'` - data = trend × seasonal × remainder
  - `'auto'` - Automatic selection
- `period`: Optional known period (INTEGER)

**Returns:**
```sql
STRUCT(
    trend            DOUBLE[],     -- Trend component
    seasonal         DOUBLE[],     -- Seasonal component
    remainder        DOUBLE[],     -- Residual component
    period           INTEGER,      -- Detected/used period
    type_used        VARCHAR       -- 'additive' or 'multiplicative'
)
```

**Example:**
```sql
-- Additive decomposition
SELECT ts_decompose([1,2,3,4,1,2,3,4,1,2,3,4]::DOUBLE[], 'additive');

-- Auto-detect decomposition type
SELECT ts_decompose(LIST(value ORDER BY date), 'auto') AS decomposition
FROM sales GROUP BY product_id;

-- Multiplicative with known period
SELECT ts_decompose(LIST(value ORDER BY date), 'multiplicative', 12) AS decomposition
FROM monthly_sales GROUP BY product_id;
```

---

### MSTL Decomposition

**ts_mstl_decomposition** (alias: `anofox_fcst_ts_mstl_decomposition`)

Multiple Seasonal-Trend Decomposition using Loess (MSTL).

**Signature:**
```sql
ts_mstl_decomposition(values DOUBLE[]) → STRUCT
```

**Returns:**
```sql
STRUCT(
    trend      DOUBLE[],    -- Trend component
    seasonal   DOUBLE[][],  -- Seasonal components (one per detected period)
    remainder  DOUBLE[],    -- Residual component
    periods    INTEGER[]    -- Detected seasonal periods
)
```

**Example:**
```sql
SELECT ts_mstl_decomposition(
    [1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24]::DOUBLE[]
);
```

**Notes:**
- Automatically detects seasonal periods if not specified
- Decomposition is additive: `value ≈ trend + seasonal[0] + seasonal[1] + ... + remainder`
- Minimum series length: 2 × smallest seasonal period

---

## Changepoint Detection

**ts_detect_changepoints** (alias: `anofox_fcst_ts_detect_changepoints`)

Detects structural breaks in time series.

**Signature:**
```sql
ts_detect_changepoints(values DOUBLE[]) → STRUCT

-- With parameters
ts_detect_changepoints(values DOUBLE[], min_size INTEGER, penalty DOUBLE) → STRUCT
```

**Parameters:**
- `values`: Input time series
- `min_size`: Minimum segment size (default: 2)
- `penalty`: Penalty for adding changepoints (default: auto)

**Returns:**
```sql
STRUCT(
    changepoints    UBIGINT[],  -- Indices of changepoints
    n_changepoints  UBIGINT,    -- Number of changepoints detected
    cost            DOUBLE      -- Total cost of segmentation
)
```

**Example:**
```sql
-- Detect level shift
SELECT ts_detect_changepoints([1,1,1,1,1,10,10,10,10,10]::DOUBLE[]);
-- Returns: {changepoints: [5], n_changepoints: 1, cost: ...}

-- With custom parameters
SELECT ts_detect_changepoints([1,1,1,1,1,10,10,10,10,10]::DOUBLE[], 3, 1.0);
```

---

### BOCPD Changepoint Detection

**ts_detect_changepoints_bocpd** (alias: `anofox_fcst_ts_detect_changepoints_bocpd`)

Detects changepoints using Bayesian Online Changepoint Detection (BOCPD) with a Normal-Gamma conjugate prior.

**Signature:**
```sql
ts_detect_changepoints_bocpd(values DOUBLE[], hazard_lambda DOUBLE, include_probabilities BOOLEAN) → STRUCT
```

**Parameters:**
- `values`: Input time series
- `hazard_lambda`: Hazard rate parameter (expected run length between changepoints)
- `include_probabilities`: Whether to include per-point changepoint probabilities

**Returns:**
```sql
STRUCT(
    is_changepoint           BOOLEAN[],   -- Per-point changepoint flags
    changepoint_probability  DOUBLE[],    -- Per-point changepoint probabilities (if requested)
    changepoint_indices      UBIGINT[]    -- Indices of detected changepoints
)
```

**Example:**
```sql
-- Detect changepoints with BOCPD
SELECT ts_detect_changepoints_bocpd(
    [1,1,1,1,1,10,10,10,10,10]::DOUBLE[],
    100.0,   -- hazard_lambda: expect changepoint every ~100 observations
    true     -- include probabilities
);
-- Returns: {is_changepoint: [false, ..., true, ...], changepoint_probability: [...], changepoint_indices: [5]}
```

---

### Changepoint Detection Aggregate

**ts_detect_changepoints_agg** (alias: `anofox_fcst_ts_detect_changepoints_agg`)

Aggregate function for detecting changepoints in time series grouped by a key. Uses BOCPD algorithm.

**Signature:**
```sql
ts_detect_changepoints_agg(
    timestamp_col TIMESTAMP,
    value_col DOUBLE,
    params MAP(VARCHAR, VARCHAR)
) → LIST<STRUCT>
```

**Parameters in MAP:**
- `hazard_lambda`: Hazard rate parameter (default: 250.0)
- `include_probabilities`: Include per-point probabilities (default: false)

**Returns:**
```sql
LIST<STRUCT(
    timestamp              TIMESTAMP,
    value                  DOUBLE,
    is_changepoint         BOOLEAN,
    changepoint_probability DOUBLE
)>
```

**Example:**
```sql
-- Detect changepoints per product
SELECT
    product_id,
    ts_detect_changepoints_agg(date, value, MAP{}) AS changepoints
FROM sales
GROUP BY product_id;
```

---

## Feature Extraction

### Extract Features

**ts_features** (alias: `anofox_fcst_ts_features`)

Extracts tsfresh-compatible time series features.

**Signatures:**
```sql
-- Scalar version (array input) - convenience function
ts_features_scalar(values DOUBLE[]) → STRUCT

-- Aggregate version (C++ API compatible) - primary function
ts_features(timestamp_col TIMESTAMP, value_col DOUBLE) → STRUCT
ts_features(timestamp_col, value_col, feature_selection LIST(VARCHAR)) → STRUCT
ts_features(timestamp_col, value_col, feature_selection, feature_params LIST(STRUCT)) → STRUCT

-- Alias for aggregate version
ts_features_agg(timestamp_col TIMESTAMP, value_col DOUBLE) → STRUCT
```

**Returns:** A STRUCT containing 117 named feature columns including:

| Feature | Description |
|---------|-------------|
| `abs_energy` | Sum of squared values |
| `absolute_sum_of_changes` | Sum of absolute differences |
| `autocorrelation_lag1` | Autocorrelation at lag 1 |
| `autocorrelation_lag5` | Autocorrelation at lag 5 |
| `benford_correlation` | Correlation with Benford's law |
| `binned_entropy` | Entropy of binned distribution |
| `cid_ce` | Complexity-invariant distance |
| `count_above_mean` | Count of values above mean |
| `count_below_mean` | Count of values below mean |
| `first_value` | First value in series |
| `kurtosis` | Kurtosis |
| `last_value` | Last value in series |
| `length` | Series length |
| `linear_trend_intercept` | Linear trend intercept |
| `linear_trend_r_squared` | R² of linear fit |
| `linear_trend_slope` | Linear trend slope |
| `longest_strike_above_mean` | Longest run above mean |
| `longest_strike_below_mean` | Longest run below mean |
| `maximum` | Maximum value |
| `mean` | Mean value |
| `mean_abs_change` | Mean absolute change |
| `mean_change` | Mean change |
| `mean_second_derivative_central` | Mean second derivative |
| `median` | Median value |
| `minimum` | Minimum value |
| `number_peaks` | Number of peaks |
| `partial_autocorrelation_lag1` | Partial autocorrelation at lag 1 |
| `percentage_above_mean` | Percentage above mean |
| `quantile_0.25` | 25th percentile |
| `quantile_0.75` | 75th percentile |
| `range` | Range (max - min) |
| `root_mean_square` | Root mean square |
| `skewness` | Skewness |
| `standard_deviation` | Standard deviation |
| `sum` | Sum of values |
| `variance` | Variance |
| `zero_crossing_rate` | Zero crossing rate |
| `sample_entropy` | Sample entropy |
| `approximate_entropy` | Approximate entropy |
| `permutation_entropy` | Permutation entropy |
| `lempel_ziv_complexity` | Lempel-Ziv complexity measure |
| `spectral_centroid` | Spectral centroid from FFT |
| `spectral_variance` | Spectral variance from FFT |
| `fft_coefficient_0_real` | FFT coefficient 0 (real part) |
| `fft_coefficient_0_imag` | FFT coefficient 0 (imaginary part) |
| `autocorrelation_lag2-10` | Autocorrelation at lags 2-10 |
| `partial_autocorrelation_lag2-5` | Partial autocorrelation at lags 2-5 |
| `time_reversal_asymmetry_stat_1-3` | Time reversal asymmetry statistics |
| `c3_lag1-3` | C3 nonlinearity measures |
| `ratio_beyond_r_sigma_1-3` | Ratio of values beyond 1-3 sigma |
| `quantile_0.1`, `quantile_0.9` | 10th and 90th percentiles |
| `has_duplicate`, `has_duplicate_max` | Duplicate value indicators |
| `first_location_of_maximum` | Relative position of first max |
| `agg_linear_trend_slope` | Aggregated linear trend slope |
| *...and 80+ more* | See `ts_features_list()` for full list |

**Example:**
```sql
-- Scalar version: extract features from array
SELECT ts_features_scalar([1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0]);

-- Access specific feature using struct field access
SELECT ts_features_scalar([1.0, 2.0, 3.0, 4.0, 5.0]).mean;

-- Aggregate version (C++ API compatible): use directly on columns
SELECT
    product_id,
    ts_features(date, value) AS features
FROM sales
GROUP BY product_id;

-- Access specific feature from aggregate result
SELECT
    product_id,
    (ts_features(date, value)).mean AS avg_value,
    (ts_features(date, value)).linear_trend_slope AS trend
FROM sales
GROUP BY product_id;

-- With feature selection (C++ API compatible)
SELECT
    product_id,
    ts_features(date, value, ['mean', 'variance', 'skewness']) AS features
FROM sales
GROUP BY product_id;
```

---

### List Available Features

**ts_features_list** (alias: `anofox_fcst_ts_features_list`)

Returns available feature metadata as a table. C++ API compatible.

**Signature:**
```sql
ts_features_list() → TABLE(
    column_name        VARCHAR,  -- Default column name ("value")
    feature_name       VARCHAR,  -- Feature identifier
    parameter_suffix   VARCHAR,  -- Parameter suffix for parameterized features
    default_parameters VARCHAR,  -- Default parameters as JSON string
    parameter_keys     VARCHAR   -- Available parameter keys
)
```

**Example:**
```sql
SELECT * FROM ts_features_list();
-- Returns table with feature metadata:
-- | column_name | feature_name    | parameter_suffix | default_parameters | parameter_keys |
-- |-------------|-----------------|------------------|-------------------|----------------|
-- | value       | abs_energy      |                  | {}                |                |
-- | value       | mean            |                  | {}                |                |
-- ...

-- Get just feature names
SELECT feature_name FROM ts_features_list();
```

---

### Feature Extraction Aggregate

**ts_features_agg** (alias: `anofox_fcst_ts_features_agg`)

Aggregate function that extracts features from a time series grouped by a key. Returns a STRUCT with all 117 feature columns. Supports optional feature selection and custom parameters for C++ API compatibility.

**Signatures:**
```sql
-- Basic 2-parameter version
ts_features_agg(timestamp_col TIMESTAMP, value_col DOUBLE) → STRUCT

-- With feature selection (C++ API compatible)
ts_features_agg(timestamp_col TIMESTAMP, value_col DOUBLE,
                feature_selection LIST(VARCHAR)) → STRUCT

-- With feature selection and custom parameters (C++ API compatible)
ts_features_agg(timestamp_col TIMESTAMP, value_col DOUBLE,
                feature_selection LIST(VARCHAR),
                feature_params LIST(STRUCT(feature VARCHAR, params_json VARCHAR))) → STRUCT
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `timestamp_col` | TIMESTAMP | Time index column |
| `value_col` | DOUBLE | Values column |
| `feature_selection` | LIST(VARCHAR) or NULL | Optional list of feature names to extract (NULL = all features) |
| `feature_params` | LIST(STRUCT) or NULL | Optional custom parameters for features |

**Returns:** A STRUCT containing all 117 named feature columns.

**Example:**
```sql
-- Extract features per product (basic)
SELECT
    product_id,
    ts_features_agg(date, value) AS features
FROM sales
GROUP BY product_id;

-- Access specific feature from result
SELECT
    product_id,
    (ts_features_agg(date, value)).mean AS avg_value,
    (ts_features_agg(date, value)).autocorrelation_lag1 AS ac1
FROM sales
GROUP BY product_id;

-- With feature selection (C++ API compatible)
SELECT
    product_id,
    ts_features_agg(date, value, ['mean', 'variance', 'skewness']) AS features
FROM sales
GROUP BY product_id;

-- With NULL parameters (returns all features)
SELECT
    product_id,
    ts_features_agg(date, value, NULL, NULL) AS features
FROM sales
GROUP BY product_id;
```

---

### Feature Configuration

**ts_features_config_from_json** / **ts_features_config_from_csv**

Load feature configuration from external files. Returns a STRUCT with feature names and optional parameter overrides.

**Signature:**
```sql
ts_features_config_from_json(path VARCHAR) → STRUCT(
    feature_names       VARCHAR[],
    overrides           STRUCT(feature VARCHAR, params_json VARCHAR)[]
)
```

**Example:**
```sql
-- Get default configuration (all 117 features)
SELECT ts_features_config_from_json('config.json');
```

---

## Forecasting

The extension provides a comprehensive forecasting system with 32 models ranging from simple baselines to sophisticated state-space methods. Forecasts can be generated using three different API styles depending on your use case.

### API Styles for Forecasting

| API Style | Best For | Example |
|-----------|----------|---------|
| **Table Macros** | Most users; clean SQL interface | `ts_forecast_by('sales', id, date, val, 'ets', 12, MAP{})` |
| **Aggregate Functions** | Custom GROUP BY patterns | `ts_forecast_agg(date, value, 'ets', 12, MAP{})` |
| **Scalar Functions** | Array-based workflows, composition | `ts_forecast([1,2,3,4]::DOUBLE[], 3, 'naive')` |

### Choosing a Forecasting Model

**For beginners:** Start with `Naive` or `SES` to establish baselines, then try `AutoETS` for automatic model selection.

**Model Selection Guide:**

| Data Characteristics | Recommended Models |
|---------------------|-------------------|
| No trend, no seasonality | `Naive`, `SES`, `SESOptimized` |
| Trend, no seasonality | `Holt`, `Theta`, `RandomWalkDrift` |
| Seasonality (single period) | `SeasonalNaive`, `HoltWinters`, `SeasonalES` |
| Multiple seasonalities | `MSTL`, `MFLES`, `TBATS` |
| Intermittent demand (many zeros) | `CrostonClassic`, `CrostonSBA`, `TSB` |
| Unknown characteristics | `AutoETS`, `AutoARIMA`, `AutoTheta` |

### Supported Models (32 Models)

The extension supports all 32 models with **exact case-sensitive naming**.

**Parameter notation:**
- **Bold** = Required parameter
- *Italic* = Optional parameter (has default value)

#### Automatic Selection Models (6)
| Model | Description | Required | Optional |
|-------|-------------|----------|----------|
| `AutoETS` | Automatic ETS model selection | — | *seasonal_period* |
| `AutoARIMA` | Automatic ARIMA model selection | — | *seasonal_period* |
| `AutoTheta` | Automatic Theta method selection | — | *seasonal_period* |
| `AutoMFLES` | Automatic MFLES selection | — | *seasonal_periods[]* |
| `AutoMSTL` | Automatic MSTL selection | — | *seasonal_periods[]* |
| `AutoTBATS` | Automatic TBATS selection | — | *seasonal_periods[]* |

#### Basic Models (6)
| Model | Description | Required | Optional |
|-------|-------------|----------|----------|
| `Naive` | Last value repeated | — | — |
| `SMA` | Simple Moving Average | — | *window* (default: 5) |
| `SeasonalNaive` | Seasonal naive (last season repeated) | **seasonal_period** | — |
| `SES` | Simple Exponential Smoothing | — | *alpha* (default: 0.3) |
| `SESOptimized` | Optimized SES with parameter tuning | — | — |
| `RandomWalkDrift` | Random walk with drift | — | — |

#### Exponential Smoothing Models (4)
| Model | Description | Required | Optional |
|-------|-------------|----------|----------|
| `Holt` | Holt's linear trend method | — | *alpha*, *beta* |
| `HoltWinters` | Holt-Winters seasonal method | **seasonal_period** | *alpha*, *beta*, *gamma* |
| `SeasonalES` | Seasonal Exponential Smoothing | **seasonal_period** | *alpha*, *gamma* |
| `SeasonalESOptimized` | Optimized Seasonal ES | **seasonal_period** | — |

#### Theta Methods (5)
| Model | Description | Required | Optional |
|-------|-------------|----------|----------|
| `Theta` | Standard Theta method | — | *seasonal_period*, *theta* |
| `OptimizedTheta` | Optimized Theta method | — | *seasonal_period* |
| `DynamicTheta` | Dynamic Theta method | — | *seasonal_period*, *theta* |
| `DynamicOptimizedTheta` | Dynamic Optimized Theta | — | *seasonal_period* |
| `AutoTheta` | Automatic Theta selection | — | *seasonal_period* |

#### State Space & ARIMA Models (4)
| Model | Description | Required | Optional |
|-------|-------------|----------|----------|
| `ETS` | Error-Trend-Seasonal model | — | *seasonal_period*, *error*, *trend*, *season* |
| `AutoETS` | Automatic ETS selection | — | *seasonal_period* |
| `ARIMA` | AutoRegressive Integrated Moving Average | **p**, **d**, **q** | *P*, *D*, *Q*, *s* |
| `AutoARIMA` | Automatic ARIMA selection | — | *seasonal_period* |

#### Multiple Seasonality Models (6)
| Model | Description | Required | Optional |
|-------|-------------|----------|----------|
| `MFLES` | Multiple Frequency Locally Estimated Scatterplot Smoothing | **seasonal_periods[]** | *iterations* |
| `AutoMFLES` | Automatic MFLES | — | *seasonal_periods[]* |
| `MSTL` | Multiple Seasonal-Trend decomposition using Loess | **seasonal_periods[]** | *stl_method* |
| `AutoMSTL` | Automatic MSTL | — | *seasonal_periods[]* |
| `TBATS` | Trigonometric, Box-Cox, ARMA, Trend, Seasonal | **seasonal_periods[]** | *use_box_cox* |
| `AutoTBATS` | Automatic TBATS | — | *seasonal_periods[]* |

#### Intermittent Demand Models (6)
| Model | Description | Required | Optional |
|-------|-------------|----------|----------|
| `CrostonClassic` | Classic Croston's method | — | — |
| `CrostonOptimized` | Optimized Croston's method | — | — |
| `CrostonSBA` | Syntetos-Boylan Approximation | — | — |
| `ADIDA` | Aggregate-Disaggregate Intermittent Demand Approach | — | — |
| `IMAPA` | Intermittent Multiple Aggregation Prediction Algorithm | — | — |
| `TSB` | Teunter-Syntetos-Babai method | — | *alpha_d*, *alpha_p* |

> **Note:** Parameters are passed via the `params` MAP argument in table macros and aggregate functions.
> Example: `MAP{'seasonal_period': '7', 'alpha': '0.2'}`

---

### ts_forecast (Scalar)

**ts_forecast** (alias: `anofox_fcst_ts_forecast`)

Generates time series forecasts from an array.

**Signature:**
```sql
-- With default model (auto)
ts_forecast(values DOUBLE[], horizon INTEGER) → STRUCT

-- With specified model
ts_forecast(values DOUBLE[], horizon INTEGER, model VARCHAR) → STRUCT
```

**Parameters:**
- `values`: Historical time series values (DOUBLE[])
- `horizon`: Number of periods to forecast (INTEGER)
- `model`: Forecasting model (VARCHAR, optional, default: 'auto')

**Returns:**
```sql
STRUCT(
    point     DOUBLE[],   -- Point forecasts
    lower     DOUBLE[],   -- Lower prediction interval bounds
    upper     DOUBLE[],   -- Upper prediction interval bounds
    fitted    DOUBLE[],   -- In-sample fitted values
    residuals DOUBLE[],   -- In-sample residuals
    model     VARCHAR,    -- Model name used
    aic       DOUBLE,     -- Akaike Information Criterion
    bic       DOUBLE,     -- Bayesian Information Criterion
    mse       DOUBLE      -- Mean Squared Error
)
```

**Example:**
```sql
-- Simple forecast
SELECT ts_forecast([1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0]::DOUBLE[], 3);

-- With specific model
SELECT ts_forecast([1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0]::DOUBLE[], 3, 'ses');

-- Access point forecasts
SELECT (ts_forecast([1,2,3,4,5,6,7,8,9,10]::DOUBLE[], 3)).point;

-- Use with GROUP BY for multiple series
SELECT
    product_id,
    (ts_forecast(LIST(value ORDER BY date), 7, 'naive')).point AS forecast
FROM sales
GROUP BY product_id;
```

---

### anofox_fcst_ts_forecast (Table Macro)

Generate forecasts for a single series from a table.

**Signature:**
```sql
anofox_fcst_ts_forecast(table_name, date_col, target_col, method, horizon, params) → TABLE
```

**Parameters (all positional):**
- `table_name` - Source table name (VARCHAR)
- `date_col` - Date/timestamp column
- `target_col` - Target value column
- `method` - Forecasting method (VARCHAR)
- `horizon` - Number of periods to forecast (INTEGER)
- `params` - Additional parameters (MAP, typically `MAP{}`)

**Example:**
```sql
SELECT * FROM anofox_fcst_ts_forecast('sales', date, amount, 'naive', 12, MAP{});
```

---

### anofox_fcst_ts_forecast_by / ts_forecast_by (Table Macro)

Generate forecasts for multiple time series grouped by an identifier column. This is the **primary forecasting function** for most use cases.

**Purpose:**
When you have a table with multiple time series (e.g., sales by product, sensor readings by device), this function forecasts each series independently and returns all forecasts in a single result table.

**Signature:**
```sql
ts_forecast_by(table_name, group_col, date_col, target_col, method, horizon, params) → TABLE
```

**Parameters (all positional):**
| Parameter | Type | Description |
|-----------|------|-------------|
| `table_name` | VARCHAR | Source table name (quoted string) |
| `group_col` | IDENTIFIER | Column for grouping series (unquoted) |
| `date_col` | IDENTIFIER | Date/timestamp column (unquoted) |
| `target_col` | IDENTIFIER | Target value column (unquoted) |
| `method` | VARCHAR | Forecasting method name (case-sensitive, see [Supported Models](#supported-models-32-models)) |
| `horizon` | INTEGER | Number of periods to forecast |
| `params` | MAP | Model parameters (use `MAP{}` for defaults) |

**Returns:** A table with columns:
- `group_col` - The series identifier
- `ds` - Forecast timestamp
- `forecast` - Point forecast value
- `lower` - Lower prediction interval bound
- `upper` - Upper prediction interval bound

**Examples:**
```sql
-- Basic forecast: 12 periods ahead using ETS model
SELECT * FROM ts_forecast_by('sales', product_id, date, amount, 'ETS', 12, MAP{});

-- Using Naive method (simple baseline)
SELECT * FROM ts_forecast_by('sales', product_id, date, amount, 'Naive', 7, MAP{});

-- Seasonal model with explicit period
SELECT * FROM ts_forecast_by(
    'weekly_sales', store_id, week, revenue,
    'HoltWinters', 52,
    MAP{'seasonal_period': '52'}
);

-- Filter to specific products
SELECT * FROM ts_forecast_by('sales', product_id, date, amount, 'AutoETS', 30, MAP{})
WHERE product_id IN ('SKU001', 'SKU002', 'SKU003');

-- Join forecasts with actuals for comparison
WITH forecasts AS (
    SELECT * FROM ts_forecast_by('sales', product_id, date, amount, 'ETS', 12, MAP{})
)
SELECT
    f.product_id,
    f.ds,
    f.forecast,
    a.amount AS actual
FROM forecasts f
LEFT JOIN sales a ON f.product_id = a.product_id AND f.ds = a.date;
```

**Setting Model Parameters:**

The `params` MAP allows you to customize model behavior:

```sql
-- SES with custom smoothing parameter
SELECT * FROM ts_forecast_by('sales', id, date, val, 'SES', 12,
    MAP{'alpha': '0.5'}
);

-- Holt-Winters with explicit seasonal period and smoothing
SELECT * FROM ts_forecast_by('sales', id, date, val, 'HoltWinters', 12,
    MAP{'seasonal_period': '7', 'alpha': '0.2', 'beta': '0.1', 'gamma': '0.3'}
);

-- MSTL with multiple seasonal periods (daily data with weekly and yearly patterns)
SELECT * FROM ts_forecast_by('sales', id, date, val, 'MSTL', 30,
    MAP{'seasonal_periods': '[7, 365]'}
);

-- Custom confidence level (95% instead of default 90%)
SELECT * FROM ts_forecast_by('sales', id, date, val, 'ETS', 12,
    MAP{'confidence_level': '0.95'}
);
```

**Notes:**
- The function uses the `frequency` parameter (default: `'1d'`) to generate forecast timestamps
- Each series is forecast independently; errors in one series don't affect others
- For very large datasets, consider filtering to relevant series before forecasting

---

### anofox_fcst_ts_forecast_agg (Aggregate Function)

Aggregate function for generating forecasts.

**Signature:**
```sql
anofox_fcst_ts_forecast_agg(date_col, value_col, method, horizon, params) → STRUCT
```

**Parameters (all positional):**
- `date_col` - Timestamp values (TIMESTAMP)
- `value_col` - Numeric values (DOUBLE)
- `method` - Forecasting method (VARCHAR)
- `horizon` - Number of periods to forecast (INTEGER)
- `params` - Additional parameters (MAP, typically `MAP{}`)

**Returns:**
```sql
STRUCT(
    forecast_step      INTEGER[],    -- Forecast step numbers [1, 2, ..., horizon]
    forecast_timestamp TIMESTAMP[],  -- Forecast timestamps (computed from last observation + step)
    point_forecast     DOUBLE[],     -- Point forecasts
    lower_<N>          DOUBLE[],     -- Lower prediction interval bounds (N = confidence level %)
    upper_<N>          DOUBLE[],     -- Upper prediction interval bounds (N = confidence level %)
    model_name         VARCHAR,      -- Model name used
    insample_fitted    DOUBLE[],     -- In-sample fitted values
    date_col_name      VARCHAR,      -- Date column name (C++ API compatible)
    error_message      VARCHAR       -- Error message if forecast failed (empty on success)
)
```

**Dynamic Column Naming:**
The prediction interval columns use dynamic names based on the confidence level:
- Default (90% confidence): `lower_90`, `upper_90`
- If `confidence_level: '0.95'` in params: `lower_95`, `upper_95`

This provides dynamic column naming based on the configured confidence level.

**Example:**
```sql
-- Forecast by product (with default 90% confidence interval)
SELECT
    product_id,
    anofox_fcst_ts_forecast_agg(ts, value, 'naive', 12, MAP{}) AS forecast
FROM sales
GROUP BY product_id;
-- Result has columns: lower_90, upper_90

-- Access forecast components (note the dynamic column name)
SELECT
    product_id,
    (ts_forecast_agg(ts, value, 'ets', 6, MAP{})).point_forecast AS forecasts,
    (ts_forecast_agg(ts, value, 'ets', 6, MAP{})).lower_90 AS lower_bound
FROM sales
GROUP BY product_id;

-- With custom confidence level (95%)
SELECT
    product_id,
    ts_forecast_agg(ts, value, 'naive', 6, MAP['confidence_level', '0.95']) AS forecast
FROM sales
GROUP BY product_id;
-- Result has columns: lower_95, upper_95
```

---

## Evaluation Metrics

All metrics accept `DOUBLE[]` arrays and return `DOUBLE`. Use with `GROUP BY` via `LIST()` aggregation.

### Mean Absolute Error (MAE)

**ts_mae** (alias: `anofox_fcst_ts_mae`)

**Signature:**
```sql
ts_mae(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE
```

**Formula:** MAE = Σ|y - ŷ| / n

**Example:**
```sql
SELECT ts_mae([1.0, 2.0, 3.0], [1.1, 2.1, 3.1]);
-- Returns: 0.1
```

---

### Mean Squared Error (MSE)

**ts_mse** (alias: `anofox_fcst_ts_mse`)

**Signature:**
```sql
ts_mse(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE
```

**Formula:** MSE = Σ(y - ŷ)² / n

---

### Root Mean Squared Error (RMSE)

**ts_rmse** (alias: `anofox_fcst_ts_rmse`)

**Signature:**
```sql
ts_rmse(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE
```

**Formula:** RMSE = √(MSE)

---

### Mean Absolute Percentage Error (MAPE)

**ts_mape** (alias: `anofox_fcst_ts_mape`)

**Signature:**
```sql
ts_mape(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE
```

**Formula:** MAPE = (100/n) × Σ|y - ŷ| / |y|

> **Warning:** Returns NULL if any actual value is zero.

---

### Symmetric MAPE (sMAPE)

**ts_smape** (alias: `anofox_fcst_ts_smape`)

**Signature:**
```sql
ts_smape(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE
```

**Formula:** sMAPE = (200/n) × Σ|y - ŷ| / (|y| + |ŷ|)

**Range:** [0, 200]

---

### Mean Absolute Scaled Error (MASE)

**ts_mase** (alias: `anofox_fcst_ts_mase`)

Compares forecast accuracy against a baseline (e.g., naive forecast). C++ API compatible.

**Signature:**
```sql
ts_mase(actual DOUBLE[], predicted DOUBLE[], baseline DOUBLE[]) → DOUBLE
```

**Parameters:**
- `actual`: Actual observed values
- `predicted`: Predicted/forecasted values
- `baseline`: Baseline forecast (e.g., naive or seasonal naive)

**Formula:** MASE = MAE(actual, predicted) / MAE(actual, baseline)

**Example:**
```sql
-- Compare model forecast against naive baseline
SELECT ts_mase(
    [100, 110, 120, 130]::DOUBLE[],  -- actual
    [102, 108, 122, 128]::DOUBLE[],  -- model forecast
    [100, 100, 110, 120]::DOUBLE[]   -- naive baseline (lag-1)
);
```

---

### R-squared

**ts_r2** (alias: `anofox_fcst_ts_r2`)

**Signature:**
```sql
ts_r2(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE
```

**Formula:** R² = 1 - (SS_res / SS_tot)

**Range:** (-∞, 1]

---

### Forecast Bias

**ts_bias** (alias: `anofox_fcst_ts_bias`)

**Signature:**
```sql
ts_bias(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE
```

**Formula:** Bias = Σ(ŷ - y) / n

**Interpretation:** Positive = over-forecasting, Negative = under-forecasting

---

### Relative MAE (rMAE)

**ts_rmae** (alias: `anofox_fcst_ts_rmae`)

Compares two model forecasts. C++ API compatible.

**Signature:**
```sql
ts_rmae(actual DOUBLE[], pred1 DOUBLE[], pred2 DOUBLE[]) → DOUBLE
```

**Parameters:**
- `actual`: Actual observed values
- `pred1`: First model's predictions
- `pred2`: Second model's predictions (baseline/benchmark)

**Formula:** rMAE = MAE(actual, pred1) / MAE(actual, pred2)

**Interpretation:**
- rMAE < 1: First model is better
- rMAE = 1: Models are equally accurate
- rMAE > 1: Second model is better

**Example:**
```sql
-- Compare ETS model against naive baseline
SELECT ts_rmae(
    [100, 110, 120]::DOUBLE[],  -- actual
    [102, 108, 122]::DOUBLE[],  -- ETS forecast
    [100, 100, 110]::DOUBLE[]   -- naive forecast
);
-- Returns < 1 if ETS outperforms naive
```

---

### Quantile Loss

**ts_quantile_loss** (alias: `anofox_fcst_ts_quantile_loss`)

**Signature:**
```sql
ts_quantile_loss(actual DOUBLE[], predicted DOUBLE[], quantile DOUBLE) → DOUBLE
```

**Parameters:**
- `quantile`: Quantile level (0 < q < 1)

---

### Mean Quantile Loss

**ts_mqloss** (alias: `anofox_fcst_ts_mqloss`)

**Signature:**
```sql
ts_mqloss(actual DOUBLE[], quantiles DOUBLE[][], levels DOUBLE[]) → DOUBLE
```

**Description:** Computes the mean quantile loss across multiple quantile levels. This is the average pinball loss across all provided quantile forecasts.

**Parameters:**
- `actual`: Array of actual values
- `quantiles`: 2D array where each sub-array is a quantile forecast (one per level)
- `levels`: Array of quantile levels (e.g., [0.1, 0.5, 0.9])

**Example:**
```sql
-- Mean quantile loss across three quantile levels
SELECT ts_mqloss(
    [100.0, 110.0, 105.0],           -- actual values
    [
        [95.0, 100.0, 98.0],         -- 10th percentile forecasts
        [100.0, 108.0, 102.0],       -- 50th percentile forecasts
        [105.0, 115.0, 110.0]        -- 90th percentile forecasts
    ],
    [0.1, 0.5, 0.9]                  -- quantile levels
) AS mqloss;
```

---

### Prediction Interval Coverage

**ts_coverage** (alias: `anofox_fcst_ts_coverage`)

**Signature:**
```sql
ts_coverage(actual DOUBLE[], lower DOUBLE[], upper DOUBLE[]) → DOUBLE
```

**Formula:** Coverage = (Count of actuals within [lower, upper]) / n

**Range:** [0, 1]

**Example:**
```sql
SELECT ts_coverage(
    [10.0, 20.0, 30.0],
    [8.0, 18.0, 28.0],
    [12.0, 22.0, 32.0]
);
-- Returns: 1.0 (all values within bounds)
```

---

## Notes

### Array-Based Design

All scalar functions operate on `DOUBLE[]` arrays. To convert table data to arrays, use the `LIST()` aggregate with `ORDER BY`:

```sql
-- Convert table column to ordered array
SELECT
    product_id,
    ts_stats(LIST(value ORDER BY date)) AS stats
FROM sales
GROUP BY product_id;
```

**Important:** Always use `ORDER BY` in `LIST()` to ensure correct temporal ordering.

### NULL Handling

Most functions handle NULL values gracefully:
- **Statistics functions**: NULLs are typically excluded from calculations
- **Imputation functions**: Specifically designed to fill NULLs (`ts_fill_nulls_*`)
- **Forecasting**: NULLs in input may cause errors; impute first

```sql
-- Recommended: Fill NULLs before forecasting
WITH cleaned AS (
    SELECT * FROM ts_fill_gaps('sales', product_id, date, value, '1d')
),
imputed AS (
    SELECT
        group_col,
        time_col,
        ts_fill_nulls_forward(LIST(value_col ORDER BY time_col)) AS values
    FROM cleaned
    GROUP BY group_col
)
SELECT * FROM ts_forecast_by('imputed', group_col, time_col, values, 'ETS', 12, MAP{});
```

### Minimum Data Requirements

| Function Type | Minimum Length | Recommended |
|--------------|----------------|-------------|
| Basic statistics | n ≥ 2 | n ≥ 10 |
| Seasonality detection | n ≥ 2 × period | n ≥ 4 × period |
| Forecasting (simple models) | n ≥ 3 | n ≥ 20 |
| Forecasting (seasonal models) | n ≥ 2 × period | n ≥ 3 × period |
| Feature extraction | n ≥ 10 | n ≥ 50 |
| Changepoint detection | n ≥ 10 | n ≥ 50 |

### Performance Tips

1. **Use table macros** when possible - they're optimized for batch processing
2. **Filter early**: Apply WHERE clauses before calling forecast functions
3. **Limit horizon**: Forecasts beyond 2-3 seasonal periods have high uncertainty
4. **Batch processing**: Process multiple series in one query rather than separate queries

```sql
-- Good: Single query for all products
SELECT * FROM ts_forecast_by('sales', product_id, date, value, 'ETS', 12, MAP{});

-- Avoid: Separate queries per product
-- SELECT * FROM ts_forecast(...) WHERE product_id = 'A';
-- SELECT * FROM ts_forecast(...) WHERE product_id = 'B';
```

### Backward Compatibility with C++ API

This Rust implementation maintains backward compatibility with the original C++ API:
- **Frequency formats**: Both Polars-style (`'1d'`, `'1h'`) and DuckDB INTERVAL (`'1 day'`) are supported
- **Function names**: All original function names are preserved
- **Parameter order**: Positional parameters maintain the same order

---

**Last Updated:** 2026-01-08
**API Version:** 0.2.4
