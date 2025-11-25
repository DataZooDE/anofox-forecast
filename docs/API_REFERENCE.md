# Anofox Forecast Extension - API Reference

**Version:** 1.0.0  
**DuckDB Version:** ≥ v1.4.2  
**Forecasting Engine:** anofox-time

---

## Overview

The Anofox Forecast extension provides comprehensive time series forecasting capabilities directly within DuckDB. All forecasting computations are performed by the **anofox-time** library, which implements efficient time series algorithms in C++.

### Key Features

- **31 Forecasting Models**: From simple naive methods to advanced AutoML models
- **Complete Workflow**: EDA, data preparation, forecasting, and evaluation
- **76+ Time Series Features**: tsfresh-compatible feature extraction
- **Native Parallelization**: Automatic GROUP BY parallelization across CPU cores
- **Multiple Function Types**: Table functions, aggregates, window functions, scalar functions

### Function Naming Conventions

Functions follow consistent naming patterns:
- `TS_*` prefix for time series functions
- `TS_FORECAST*` for forecasting operations
- `TS_*_BY` suffix for multi-series operations with GROUP BY
- `TS_*_AGG` suffix for aggregate functions (internal/low-level)

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

1. [Forecasting Functions](#forecasting-functions)
   - [TS_FORECAST](#ts_forecast)
   - [TS_FORECAST_BY](#ts_forecast_by)
   - [TS_FORECAST_AGG](#ts_forecast_agg)
2. [Evaluation Metrics](#evaluation-metrics)
   - [TS_MAE](#ts_mae)
   - [TS_MSE](#ts_mse)
   - [TS_RMSE](#ts_rmse)
   - [TS_MAPE](#ts_mape)
   - [TS_SMAPE](#ts_smape)
   - [TS_MASE](#ts_mase)
   - [TS_R2](#ts_r2)
   - [TS_BIAS](#ts_bias)
   - [TS_RMAE](#ts_rmae)
   - [TS_QUANTILE_LOSS](#ts_quantile_loss)
   - [TS_MQLOSS](#ts_mqloss)
   - [TS_COVERAGE](#ts_coverage)
3. [EDA Macros](#eda-macros)
   - [TS_STATS](#ts_stats)
   - [TS_QUALITY_REPORT](#ts_quality_report)
   - [TS_DATASET_SUMMARY](#ts_dataset_summary)
   - [TS_GET_PROBLEMATIC](#ts_get_problematic)
   - [TS_DETECT_SEASONALITY_ALL](#ts_detect_seasonality_all)
4. [Data Quality Macros](#data-quality-macros)
   - [TS_DATA_QUALITY_HEALTH_CARD](#ts_data_quality_health_card)
   - [TS_DATA_QUALITY_SUMMARY](#ts_data_quality_summary)
5. [Data Preparation Macros](#data-preparation-macros)
   - [Gap Filling](#gap-filling)
   - [Series Filtering](#series-filtering)
   - [Edge Cleaning](#edge-cleaning)
   - [Missing Value Imputation](#missing-value-imputation)
6. [Seasonality Functions](#seasonality-functions)
   - [TS_DETECT_SEASONALITY](#ts_detect_seasonality)
   - [TS_ANALYZE_SEASONALITY](#ts_analyze_seasonality)
7. [Changepoint Detection](#changepoint-detection)
   - [TS_DETECT_CHANGEPOINTS](#ts_detect_changepoints)
   - [TS_DETECT_CHANGEPOINTS_BY](#ts_detect_changepoints_by)
   - [TS_DETECT_CHANGEPOINTS_AGG](#ts_detect_changepoints_agg)
8. [Time Series Features](#time-series-features)
   - [ts_features](#ts_features)
   - [ts_features_list](#ts_features_list)
   - [ts_features_config_from_json](#ts_features_config_from_json)
   - [ts_features_config_from_csv](#ts_features_config_from_csv)
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
12. [Version History](#version-history)
13. [Notes](#notes)
14. [Support](#support)

---

## Forecasting Functions

### TS_FORECAST

**Single Time Series Forecasting**

Generate forecasts for a single time series with automatic parameter validation.

**Signature:**
```sql
TS_FORECAST(
    table_name    VARCHAR,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE,
    method        VARCHAR,
    horizon       INTEGER,
    params        MAP
) → TABLE
```

**Parameters:**
- `table_name`: Name of the input table
- `date_col`: Date/timestamp column name
- `value_col`: Value column name to forecast
- `method`: Model name (see [Supported Models](#supported-models))
- `horizon`: Number of future periods to forecast (must be > 0)
- `params`: Configuration MAP with model-specific parameters

**Returns:**
```sql
TABLE(
    forecast_step      INTEGER,
    date               DATE | TIMESTAMP | INTEGER,  -- Type matches input
    point_forecast     DOUBLE,
    lower              DOUBLE,  -- Lower bound (confidence_level)
    upper              DOUBLE,  -- Upper bound (confidence_level)
    model_name         VARCHAR,
    insample_fitted    DOUBLE[],  -- Empty unless return_insample=true
    confidence_level   DOUBLE
)
```

**Example:**
```sql
SELECT * FROM TS_FORECAST(
    'sales',
    date,
    amount,
    'AutoETS',
    28,
    MAP{'seasonal_period': 7, 'confidence_level': 0.95}
);
```

**Behavioral Notes:**
- Timestamp generation based on training data interval (configurable via `generate_timestamps`)
- Prediction intervals computed at specified confidence level (default 0.90)
- Optional in-sample fitted values via `return_insample: true`
- Date column type preserved from input

---

### TS_FORECAST_BY

**Multiple Time Series Forecasting with GROUP BY**

Generate forecasts for multiple time series with native DuckDB GROUP BY parallelization.

**Signature:**
```sql
TS_FORECAST_BY(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE,
    method        VARCHAR,
    horizon       INTEGER,
    params        MAP
) → TABLE
```

**Parameters:**
- `table_name`: Name of the input table
- `group_col`: Grouping column name (any type, preserved in output)
- `date_col`: Date/timestamp column name
- `value_col`: Value column name to forecast
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
    lower              DOUBLE,
    upper              DOUBLE,
    model_name         VARCHAR,
    insample_fitted    DOUBLE[],
    confidence_level   DOUBLE
)
```

**Example:**
```sql
SELECT 
    product_id,
    forecast_step,
    point_forecast
FROM TS_FORECAST_BY(
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

### TS_FORECAST_AGG

**Aggregate Function for Custom GROUP BY**

Low-level aggregate function for forecasting with 2+ group columns or custom aggregation patterns.

**Signature:**
```sql
TS_FORECAST_AGG(
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
    forecast_step          INTEGER[],
    forecast_timestamp     TIMESTAMP[],
    point_forecast         DOUBLE[],
    lower                  DOUBLE[],  -- Dynamic name based on confidence_level
    upper                  DOUBLE[],  -- Dynamic name based on confidence_level
    model_name             VARCHAR,
    insample_fitted        DOUBLE[],
    confidence_level       DOUBLE,
    date_col_name          VARCHAR
)
```

**Example:**
```sql
WITH fc AS (
    SELECT 
        product_id,
        location_id,
        TS_FORECAST_AGG(date, amount, 'AutoETS', 28, MAP{'seasonal_period': 7}) AS result
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

**Use Case:** When you need multiple grouping columns or custom aggregation patterns beyond single `group_col`.

---

## Evaluation Metrics

All metrics accept `DOUBLE[]` arrays and return `DOUBLE`. Use with `GROUP BY` via `LIST()` aggregation.

### TS_MAE

**Mean Absolute Error**

**Signature:**
```sql
TS_MAE(
    actual      DOUBLE[],
    predicted   DOUBLE[]
) → DOUBLE
```

**Formula:** MAE = Σ|y - ŷ| / n

**Example:**
```sql
SELECT 
    product_id,
    TS_MAE(LIST(actual), LIST(predicted)) AS mae
FROM results
GROUP BY product_id;
```

---

### TS_MSE

**Mean Squared Error**

**Signature:**
```sql
TS_MSE(
    actual      DOUBLE[],
    predicted   DOUBLE[]
) → DOUBLE
```

**Formula:** MSE = Σ(y - ŷ)² / n

---

### TS_RMSE

**Root Mean Squared Error**

**Signature:**
```sql
TS_RMSE(
    actual      DOUBLE[],
    predicted   DOUBLE[]
) → DOUBLE
```

**Formula:** RMSE = √(MSE)

---

### TS_MAPE

**Mean Absolute Percentage Error**

**Signature:**
```sql
TS_MAPE(
    actual      DOUBLE[],
    predicted   DOUBLE[]
) → DOUBLE
```

**Formula:** MAPE = (100/n) × Σ|y - ŷ| / |y|

**Note:** Returns NULL if any actual value is zero.

---

### TS_SMAPE

**Symmetric Mean Absolute Percentage Error**

**Signature:**
```sql
TS_SMAPE(
    actual      DOUBLE[],
    predicted   DOUBLE[]
) → DOUBLE
```

**Formula:** SMAPE = (200/n) × Σ|y - ŷ| / (|y| + |ŷ|)

**Range:** [0, 200]

**Note:** Handles zero values better than MAPE.

---

### TS_MASE

**Mean Absolute Scaled Error**

**Signature:**
```sql
TS_MASE(
    actual      DOUBLE[],
    predicted   DOUBLE[],
    baseline    DOUBLE[]
) → DOUBLE
```

**Formula:** MASE = MAE / (MAE of baseline method)

**Use Case:** Compare forecast accuracy relative to a baseline (e.g., naive forecast).

---

### TS_R2

**R-squared (Coefficient of Determination)**

**Signature:**
```sql
TS_R2(
    actual      DOUBLE[],
    predicted   DOUBLE[]
) → DOUBLE
```

**Formula:** R² = 1 - (SS_res / SS_tot)

**Range:** (-∞, 1]

---

### TS_BIAS

**Forecast Bias**

**Signature:**
```sql
TS_BIAS(
    actual      DOUBLE[],
    predicted   DOUBLE[]
) → DOUBLE
```

**Formula:** Bias = Σ(ŷ - y) / n

**Interpretation:** Positive = over-forecasting, Negative = under-forecasting

---

### TS_RMAE

**Relative Mean Absolute Error**

**Signature:**
```sql
TS_RMAE(
    actual      DOUBLE[],
    pred1        DOUBLE[],
    pred2        DOUBLE[]
) → DOUBLE
```

**Formula:** RMAE = MAE(pred1) / MAE(pred2)

**Use Case:** Compare relative performance of two forecasting methods.

---

### TS_QUANTILE_LOSS

**Quantile Loss (Pinball Loss)**

**Signature:**
```sql
TS_QUANTILE_LOSS(
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

### TS_MQLOSS

**Mean Quantile Loss**

**Signature:**
```sql
TS_MQLOSS(
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

### TS_COVERAGE

**Prediction Interval Coverage**

**Signature:**
```sql
TS_COVERAGE(
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
    TS_COVERAGE(LIST(actual), LIST(lower), LIST(upper)) * 100 AS coverage_pct
FROM results
GROUP BY product_id;
-- Coverage should be close to confidence_level × 100
```

---

## EDA Macros

SQL macros for exploratory data analysis and quality assessment.

### TS_STATS

**Per-Series Comprehensive Statistics**

**Signature:**
```sql
TS_STATS(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE
) → TABLE
```

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
    n_zeros_end          BIGINT
)
```

**Example:**
```sql
CREATE TABLE sales_stats AS
SELECT * FROM TS_STATS('sales_raw', product_id, date, amount);
```

---

### TS_QUALITY_REPORT

**Data Quality Assessment**

**Signature:**
```sql
TS_QUALITY_REPORT(
    stats_table    VARCHAR,
    min_length     INTEGER
) → TABLE
```

**Parameters:**
- `stats_table`: Table produced by `TS_STATS`
- `min_length`: Minimum acceptable series length

**Returns:** Quality assessment with configurable minimum length threshold.

**Checks:**
- Gap analysis
- Missing values
- Constant series
- Short series
- End date alignment

---

### TS_DATASET_SUMMARY

**Overall Dataset Statistics**

**Signature:**
```sql
TS_DATASET_SUMMARY(
    stats_table    VARCHAR
) → TABLE
```

**Returns:** Aggregate statistics across all series.

---

### TS_GET_PROBLEMATIC

**Identify Low-Quality Series**

**Signature:**
```sql
TS_GET_PROBLEMATIC(
    stats_table         VARCHAR,
    quality_threshold   DOUBLE
) → TABLE
```

**Returns:** Series below quality threshold.

---

### TS_DETECT_SEASONALITY_ALL

**Detect Seasonality for All Series**

**Signature:**
```sql
TS_DETECT_SEASONALITY_ALL(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE
) → TABLE
```

**Returns:**
```sql
TABLE(
    series_id          ANY,
    detected_periods  INTEGER[],
    primary_period     INTEGER,
    is_seasonal        BOOLEAN
)
```

**Example:**
```sql
SELECT * FROM TS_DETECT_SEASONALITY_ALL('sales', product_id, date, amount)
WHERE is_seasonal = true;
```

---

## Data Quality Macros

### TS_DATA_QUALITY_HEALTH_CARD

**Comprehensive Data Quality Assessment**

**Signature:**
```sql
TS_DATA_QUALITY_HEALTH_CARD(
    table_name      VARCHAR,
    unique_id_col   ANY,
    date_col        DATE | TIMESTAMP | INTEGER,
    value_col       DOUBLE,
    n_short         INTEGER
) → TABLE
```

**Parameters:**
- `n_short`: Optional threshold for short series detection (default: 30)

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

**Dimensions:**
- **Structural**: Key uniqueness, ID cardinality
- **Temporal**: Series length, timestamp gaps, alignment, frequency inference
- **Magnitude**: Missing values, value bounds, static values
- **Behavioural**: Intermittency, seasonality check, trend detection

**Example:**
```sql
SELECT * FROM TS_DATA_QUALITY_HEALTH_CARD('sales', product_id, date, amount, 30)
WHERE dimension = 'Temporal' AND metric = 'timestamp_gaps';
```

---

### TS_DATA_QUALITY_SUMMARY

**Aggregated Summary by Dimension**

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

---

## Data Preparation Macros

SQL macros for data cleaning and transformation. Date type support varies by function.

### Gap Filling

#### TS_FILL_GAPS

**Fill Missing Timestamps**

**Signature:**
```sql
TS_FILL_GAPS(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP,
    value_col     DOUBLE
) → TABLE
```

**Note:** DATE/TIMESTAMP only. For INTEGER dates, use `TS_FILL_GAPS_INT`.

**Behavior:** Fills missing timestamps in series with NULL values.

---

#### TS_FILL_FORWARD

**Extend Series to Target Date**

**Signature:**
```sql
TS_FILL_FORWARD(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP,
    value_col     DOUBLE,
    target_date   DATE | TIMESTAMP
) → TABLE
```

**Note:** DATE/TIMESTAMP only. For INTEGER dates, use `TS_FILL_FORWARD_INT`.

**Behavior:** Extends series to target date, filling gaps with NULL.

---

### Series Filtering

#### TS_DROP_CONSTANT

**Remove Constant Series**

**Signature:**
```sql
TS_DROP_CONSTANT(
    table_name    VARCHAR,
    group_col     ANY,
    value_col     DOUBLE
) → TABLE
```

**Behavior:** Removes series with constant values (no variation).

---

#### TS_DROP_SHORT

**Remove Short Series**

**Signature:**
```sql
TS_DROP_SHORT(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    min_length    INTEGER
) → TABLE
```

**Behavior:** Removes series below minimum length.

---

#### TS_DROP_GAPPY

**Remove Series with Excessive Gaps**

**Signature:**
```sql
TS_DROP_GAPPY(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP,
    max_gap_pct   DOUBLE
) → TABLE
```

**Note:** DATE/TIMESTAMP only. For INTEGER dates, use `TS_DROP_GAPPY_INT`.

**Parameters:**
- `max_gap_pct`: Maximum acceptable gap percentage (0-100)

---

### Edge Cleaning

#### TS_DROP_LEADING_ZEROS

**Remove Leading Zeros**

**Signature:**
```sql
TS_DROP_LEADING_ZEROS(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE
) → TABLE
```

---

#### TS_DROP_TRAILING_ZEROS

**Remove Trailing Zeros**

**Signature:**
```sql
TS_DROP_TRAILING_ZEROS(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE
) → TABLE
```

---

#### TS_DROP_EDGE_ZEROS

**Remove Both Leading and Trailing Zeros**

**Signature:**
```sql
TS_DROP_EDGE_ZEROS(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE
) → TABLE
```

---

### Missing Value Imputation

#### TS_FILL_NULLS_CONST

**Fill with Constant Value**

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

---

#### TS_FILL_NULLS_FORWARD

**Forward Fill (Last Observation Carried Forward)**

**Signature:**
```sql
TS_FILL_NULLS_FORWARD(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE
) → TABLE
```

**Behavior:** Uses `LAST_VALUE(... IGNORE NULLS)` window function.

---

#### TS_FILL_NULLS_BACKWARD

**Backward Fill**

**Signature:**
```sql
TS_FILL_NULLS_BACKWARD(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE
) → TABLE
```

**Behavior:** Uses `FIRST_VALUE(... IGNORE NULLS)` window function.

---

#### TS_FILL_NULLS_MEAN

**Fill with Series Mean**

**Signature:**
```sql
TS_FILL_NULLS_MEAN(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP | INTEGER,
    value_col     DOUBLE
) → TABLE
```

**Behavior:** Computes mean per series and fills NULLs.

---

## Seasonality Functions

### TS_DETECT_SEASONALITY

**Simple Seasonality Detection**

**Signature:**
```sql
TS_DETECT_SEASONALITY(
    values    DOUBLE[]
) → INTEGER[]
```

**Returns:** Array of detected seasonal periods sorted by strength.

**Example:**
```sql
SELECT 
    product_id,
    TS_DETECT_SEASONALITY(LIST(value ORDER BY date)) AS periods
FROM sales
GROUP BY product_id;
-- Returns: [7, 30] (weekly and monthly patterns)
```

**Algorithm:** Uses autocorrelation-based detection with minimum period of 4 and threshold of 0.9.

---

### TS_ANALYZE_SEASONALITY

**Detailed Seasonality Analysis**

**Signature:**
```sql
TS_ANALYZE_SEASONALITY(
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
    TS_ANALYZE_SEASONALITY(
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

### TS_DETECT_CHANGEPOINTS

**Single Series Changepoint Detection**

**Signature:**
```sql
TS_DETECT_CHANGEPOINTS(
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

### TS_DETECT_CHANGEPOINTS_BY

**Multiple Series Changepoint Detection**

**Signature:**
```sql
TS_DETECT_CHANGEPOINTS_BY(
    table_name    VARCHAR,
    group_col     ANY,
    date_col      DATE | TIMESTAMP,
    value_col     DOUBLE,
    params        MAP
) → TABLE
```

**Returns:** Same as `TS_DETECT_CHANGEPOINTS`, plus `group_col` column.

**Behavioral Notes:**
- Full parallelization on GROUP BY operations
- Detects level shifts, trend changes, variance shifts, regime changes
- Independent detection per series

---

### TS_DETECT_CHANGEPOINTS_AGG

**Aggregate Function for Changepoint Detection**

**Signature:**
```sql
TS_DETECT_CHANGEPOINTS_AGG(
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

### ts_features

**Extract Time Series Features (tsfresh-compatible)**

**Signature:**
```sql
ts_features(
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
    ts_features(
        date,
        sales,
        ['mean', 'variance', 'autocorrelation__lag_1'],
        [{'feature': 'ratio_beyond_r_sigma', 'params': {'r': 1.0}}]
    ) AS feats
FROM sales
GROUP BY product_id;
```

---

### ts_features_list

**List Available Features**

**Signature:**
```sql
ts_features_list() → TABLE
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

### ts_features_config_from_json

**Load Feature Configuration from JSON**

**Signature:**
```sql
ts_features_config_from_json(
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

### ts_features_config_from_csv

**Load Feature Configuration from CSV**

**Signature:**
```sql
ts_features_config_from_csv(
    path    VARCHAR
) → STRUCT
```

**Returns:** Same as `ts_features_config_from_json`.

**File Format:** CSV with header row containing `feature` and parameter columns.

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

**Note:** ARIMA models require Eigen3 library.

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
| Data Preparation | 12 | Table macros |
| Seasonality | 2 | Scalar functions |
| Changepoint Detection | 3 | Table macros (2), Aggregate (1) |
| Time Series Features | 4 | Aggregate (1), Table function (1), Scalar (2) |
| **Total** | **43** | |

### Function Type Breakdown

| Type | Count | Examples |
|------|-------|----------|
| Table Macros | 23 | `TS_FORECAST`, `TS_STATS`, `TS_FILL_GAPS` |
| Aggregate Functions | 5 | `TS_FORECAST_AGG`, `ts_features`, `TS_DETECT_CHANGEPOINTS_AGG` |
| Scalar Functions | 14 | `TS_MAE`, `TS_DETECT_SEASONALITY`, `TS_ANALYZE_SEASONALITY` |
| Table Functions | 1 | `ts_features_list` |

### GROUP BY Support

| Function Category | GROUP BY Support | Notes |
|-------------------|------------------|-------|
| Forecasting | ✅ | `TS_FORECAST_BY` and `TS_FORECAST_AGG` |
| Evaluation Metrics | ✅ | Use with `LIST()` aggregation |
| EDA Macros | ✅ | All macros support GROUP BY via `group_col` |
| Data Quality | ✅ | All macros support GROUP BY |
| Data Preparation | ✅ | All macros support GROUP BY |
| Seasonality | ✅ | Use with `LIST()` aggregation |
| Changepoint Detection | ✅ | `TS_DETECT_CHANGEPOINTS_BY` and `TS_DETECT_CHANGEPOINTS_AGG` |
| Time Series Features | ✅ | Aggregate function supports GROUP BY |

### Window Function Support

| Function Category | Window Support | Notes |
|-------------------|----------------|-------|
| Evaluation Metrics | ❌ | Scalar functions only |
| Seasonality | ❌ | Scalar functions only |
| Time Series Features | ✅ | `ts_features` supports `OVER` clauses |

---

## Version History

### v1.0.0 (Current)

**Initial Release:**
- 31 forecasting models
- 12 evaluation metrics
- 5 EDA macros
- 12 data preparation macros
- 2 data quality macros
- 2 seasonality functions
- 3 changepoint detection functions
- 4 time series feature functions

**Total: 43 functions**

---

## Notes

1. **All forecasting calculations** are performed by the **anofox-time** library implemented in C++.

2. **Positional parameters only**: Functions do NOT support named parameters (`:=` syntax). Parameters must be provided in the order specified.

3. **Date type support**: Most functions support DATE, TIMESTAMP, and INTEGER date types. Some gap-filling functions are DATE/TIMESTAMP only (see function documentation).

4. **NULL handling**:
   - Missing values in input arrays will cause errors in metrics functions
   - Data preparation macros handle NULLs explicitly
   - Window functions handle NULL y values specially (fit-predict)

5. **Performance**:
   - Table macros: O(n) for small series, optimized for large datasets
   - Aggregates: Optimized for GROUP BY parallelism
   - Window functions: Cached computation when frame doesn't change
   - Automatic parallelization across CPU cores for multi-series operations

6. **Minimum sample sizes**:
   - General rule: n ≥ 2 × seasonal_period for seasonal models
   - For inference: n > p + 1 to have sufficient degrees of freedom
   - Some models require minimum lengths (e.g., ARIMA needs sufficient data for differencing)

7. **Memory efficiency**:
   - Columnar storage and streaming operations
   - ~1GB for 1M rows, 1K series
   - Efficient for millions of rows and thousands of series

---

## Support

- **Documentation**: [guides/](../guides/)
- **Issues**: [GitHub Issues](https://github.com/DataZooDE/anofox-forecast/issues)
- **Email**: sm@data-zoo.de

---

**Last Updated:** 2025-01-25  
**API Version:** 1.0.0

