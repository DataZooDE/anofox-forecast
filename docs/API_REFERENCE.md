# Anofox Forecast Extension - API Reference

**Version:** 0.2.4
**DuckDB Version:** >= v1.4.3
**Forecasting Engine:** anofox-fcst-core (Rust)

---

## Overview

The Anofox Forecast extension provides time series forecasting capabilities directly within DuckDB. All computations are performed by the **anofox-fcst-core** library, implemented in Rust.

### API Variants

The extension provides **three API styles**:

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
   - [Seasonality Detection](#seasonality-detection)
   - [Seasonality Analysis](#seasonality-analysis)
5. [Time Series Decomposition](#time-series-decomposition)
   - [MSTL Decomposition](#mstl-decomposition)
6. [Changepoint Detection](#changepoint-detection)
   - [Changepoint Detection Aggregate](#changepoint-detection-aggregate)
7. [Feature Extraction](#feature-extraction)
   - [Extract Features](#extract-features)
   - [List Available Features](#list-available-features)
   - [Feature Extraction Aggregate](#feature-extraction-aggregate)
   - [Feature Configuration](#feature-configuration)
8. [Forecasting](#forecasting)
   - [ts_forecast (Scalar)](#ts_forecast-scalar)
   - [ts_forecast (Table Macro)](#anofox_fcst_ts_forecast--ts_forecast-table-macro)
   - [ts_forecast_by (Table Macro)](#anofox_fcst_ts_forecast_by--ts_forecast_by-table-macro)
   - [ts_forecast_agg (Aggregate)](#anofox_fcst_ts_forecast_agg--ts_forecast_agg-aggregate-function)
9. [Evaluation Metrics](#evaluation-metrics)
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

Table macros provide a high-level API for working directly with tables. Column names are passed as identifiers (unquoted).

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

### Seasonality Detection

**ts_detect_seasonality** (alias: `anofox_fcst_ts_detect_seasonality`)

Detects seasonal periods using autocorrelation analysis.

**Signature:**
```sql
ts_detect_seasonality(values DOUBLE[]) → INTEGER[]
```

**Returns:** Array of detected seasonal periods, sorted by strength.

**Example:**
```sql
-- Detect weekly and monthly patterns
SELECT ts_detect_seasonality(LIST(value ORDER BY date)) AS periods
FROM sales
GROUP BY product_id;
-- Returns: [7, 30] for weekly and monthly patterns

-- Simple example with repeating pattern
SELECT ts_detect_seasonality([1,2,3,4,1,2,3,4,1,2,3,4,1,2,3,4,1,2,3,4]::DOUBLE[]);
-- Returns: [4] (period of 4)
```

---

### Seasonality Analysis

**ts_analyze_seasonality** (alias: `anofox_fcst_ts_analyze_seasonality`)

Provides detailed seasonality analysis. C++ API compatible with optional timestamps parameter.

**Signature:**
```sql
-- Single-argument form (convenience)
ts_analyze_seasonality(values DOUBLE[]) → STRUCT

-- Two-argument form (C++ API compatible)
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

## Time Series Decomposition

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

The extension provides multiple ways to generate forecasts:
1. **Scalar functions** - operate on arrays, use with `LIST()` and `GROUP BY`
2. **Table macros** - operate on tables directly with positional parameters
3. **Aggregate functions** - use with custom `GROUP BY` patterns

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

### anofox_fcst_ts_forecast_by (Table Macro)

Generate forecasts for multiple series grouped by a column.

**Signature:**
```sql
anofox_fcst_ts_forecast_by(table_name, group_col, date_col, target_col, method, horizon, params) → TABLE
```

**Parameters (all positional):**
- `table_name` - Source table name (VARCHAR)
- `group_col` - Column for grouping series
- `date_col` - Date/timestamp column
- `target_col` - Target value column
- `method` - Forecasting method (VARCHAR)
- `horizon` - Number of periods to forecast (INTEGER)
- `params` - Additional parameters (MAP, typically `MAP{}`)

**Example:**
```sql
SELECT * FROM anofox_fcst_ts_forecast_by('sales', product_id, date, amount, 'ets', 12, MAP{});
```

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

1. **Array-based design**: All functions operate on DOUBLE[] arrays. Use `LIST(column ORDER BY date)` to convert table data to arrays.

2. **NULL handling**: Most functions handle NULLs gracefully. Use imputation functions to fill NULLs before analysis if needed.

3. **Performance**: Scalar functions are optimized for use with DuckDB's vectorized execution engine.

4. **Minimum data requirements**:
   - General rule: n ≥ 2 for basic statistics
   - Seasonality: n ≥ 2 × seasonal_period
   - Forecasting: n ≥ 10 recommended

5. **Ordering**: Time series order matters. Always use `ORDER BY date` in `LIST()` aggregations.

---

**Last Updated:** 2026-01-03
**API Version:** 0.2.4
