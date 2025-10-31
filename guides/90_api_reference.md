# Complete API Reference

## Overview

Comprehensive reference for all functions, macros, and parameters in anofox-forecast.

**Total API Elements**: 60+ functions and macros

---

## Forecasting Functions

### TS_FORECAST

Generate forecasts for a single time series.

**Signature**:
```sql
TS_FORECAST(
    table_name: VARCHAR,
    date_col: DATE | TIMESTAMP,
    value_col: DOUBLE,
    method: VARCHAR,
    horizon: INT,
    params: MAP<VARCHAR, ANY>
) → TABLE
```

**Output Columns**:
- `forecast_step: INT` - Step ahead (1, 2, 3, ...)
- `date_col: TIMESTAMP` - Forecast timestamp
- `point_forecast: DOUBLE` - Point prediction
- `lower: DOUBLE` - Lower confidence bound
- `upper: DOUBLE` - Upper confidence bound
- `model_name: VARCHAR` - Model identifier
- `insample_fitted: DOUBLE[]` - Fitted values (if requested)
- `confidence_level: DOUBLE` - Confidence level used

**Example**:
```sql
SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, 
                          {'seasonal_period': 7, 'confidence_level': 0.95});
```

### TS_FORECAST_BY

Generate forecasts for multiple time series with GROUP BY.

**Signature**:
```sql
TS_FORECAST_BY(
    table_name: VARCHAR,
    group_col: VARCHAR,
    date_col: DATE | TIMESTAMP,
    value_col: DOUBLE,
    method: VARCHAR,
    horizon: INT,
    params: MAP<VARCHAR, ANY>
) → TABLE
```

**Output Columns**: Same as TS_FORECAST, plus:
- `group_col: ANY` - Grouping column value

**Example**:
```sql
SELECT * FROM TS_FORECAST_BY('sales', product_id, date, amount, 'AutoETS', 28, 
                             {'seasonal_period': 7});
```

---

## Available Models (31 Total)

### Automatic Selection (Recommended)

| Model | Best For | Parameters |
|-------|----------|------------|
| **AutoETS** | General purpose | `seasonal_period` |
| **AutoARIMA** | Complex patterns | `seasonal_period` |
| **AutoMFLES** | Multiple seasonality | `seasonal_periods: [7, 365]` |
| **AutoMSTL** | Multiple seasonality | `seasonal_periods: [7, 30]` |
| **AutoTBATS** | Complex seasonality | `seasonal_periods: [24, 168]` |

### Exponential Smoothing

| Model | Description | Parameters |
|-------|-------------|------------|
| **Naive** | Last value repeated | None |
| **SES** | Simple exponential smoothing | `alpha` |
| **SESOptimized** | Auto-optimized SES | None |
| **Holt** | Trend | `alpha`, `beta` |
| **HoltWinters** | Trend + seasonality | `seasonal_period`, `alpha`, `beta`, `gamma` |
| **SeasonalNaive** | Seasonal patterns | `seasonal_period` |
| **SeasonalES** | Seasonal exponential smoothing | `seasonal_period`, `alpha`, `gamma` |
| **SeasonalESOptimized** | Auto-optimized | `seasonal_period` |

### State Space Models

| Model | Description | Parameters |
|-------|-------------|------------|
| **ETS** | Error-Trend-Seasonal | `seasonal_period`, `error_type`, `trend_type`, `season_type` |
| **AutoETS** | Automatic ETS selection | `seasonal_period` |

### ARIMA Family

| Model | Description | Parameters |
|-------|-------------|------------|
| **ARIMA** | Manual ARIMA | `p`, `d`, `q`, `P`, `D`, `Q`, `s` |
| **AutoARIMA** | Automatic ARIMA selection | `seasonal_period` |

### Theta Methods

| Model | Description | Parameters |
|-------|-------------|------------|
| **Theta** | Theta decomposition | `seasonal_period`, `theta` |
| **OptimizedTheta** | Auto-optimized | `seasonal_period` |
| **DynamicTheta** | Adaptive | `seasonal_period`, `theta` |
| **DynamicOptimizedTheta** | Auto adaptive | `seasonal_period` |

### Multiple Seasonality

| Model | Description | Parameters |
|-------|-------------|------------|
| **MFLES** | Multiple FLES | `seasonal_periods`, `n_iterations` |
| **AutoMFLES** | Auto MFLES | `seasonal_periods` |
| **MSTL** | Multiple STL decomposition | `seasonal_periods`, `trend_method`, `seasonal_method` |
| **AutoMSTL** | Auto MSTL | `seasonal_periods` |
| **TBATS** | Trigonometric, Box-Cox, ARMA, Trend, Seasonal | `seasonal_periods`, `use_box_cox` |
| **AutoTBATS** | Auto TBATS | `seasonal_periods` |

### Intermittent Demand

| Model | Description | Parameters |
|-------|-------------|------------|
| **CrostonClassic** | Croston's method | None |
| **CrostonOptimized** | Optimized Croston | None |
| **CrostonSBA** | Syntetos-Boylan approximation | None |
| **ADIDA** | Aggregate-Disaggregate | None |
| **IMAPA** | Intermittent Moving Average | None |
| **TSB** | Teunter-Syntetos-Babai | `alpha_d`, `alpha_p` |

### Simple Methods

| Model | Description | Parameters |
|-------|-------------|------------|
| **SMA** | Simple moving average | `window` |
| **RandomWalkDrift** | Random walk with drift | None |
| **SeasonalWindowAverage** | Seasonal average | `seasonal_period`, `window` |

---

## Parameters Reference

### Common Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `seasonal_period` | INT | Required* | Seasonal cycle length |
| `confidence_level` | DOUBLE | 0.90 | CI level (0.0-1.0) |
| `return_insample` | BOOLEAN | false | Return fitted values |

*Required for seasonal models

### Model-Specific Parameters

#### ETS Parameters
```sql
{
    'seasonal_period': 7,
    'error_type': 0 | 1,      -- 0=additive, 1=multiplicative
    'trend_type': 0 | 1 | 2,  -- 0=none, 1=additive, 2=damped
    'season_type': 0 | 1 | 2, -- 0=none, 1=additive, 2=multiplicative
    'alpha': 0.0-1.0,         -- Level smoothing (optional, auto-optimized)
    'beta': 0.0-1.0,          -- Trend smoothing (optional)
    'gamma': 0.0-1.0,         -- Seasonal smoothing (optional)
    'phi': 0.0-1.0            -- Damping (optional)
}
```

#### ARIMA Parameters
```sql
{
    'p': INT,                  -- AR order (0-5 typical)
    'd': INT,                  -- Differencing (0-2 typical)
    'q': INT,                  -- MA order (0-5 typical)
    'P': INT,                  -- Seasonal AR (0-2)
    'D': INT,                  -- Seasonal differencing (0-1)
    'Q': INT,                  -- Seasonal MA (0-2)
    's': INT,                  -- Seasonal period
    'include_intercept': BOOL  -- Include constant term
}
```

#### Multiple Seasonality Parameters
```sql
{
    'seasonal_periods': [7, 30, 365],  -- Array of periods
    'n_iterations': 100,                -- MFLES iterations
    'use_box_cox': true,                -- TBATS Box-Cox
    'box_cox_lambda': 0.5               -- TBATS lambda
}
```

---

## Evaluation Metrics (12 Total)

### Point Forecast Metrics

| Function | Formula | Use Case | Range |
|----------|---------|----------|-------|
| **TS_MAE** | Mean(\|Actual - Forecast\|) | Average error | [0, ∞) |
| **TS_MSE** | Mean((Actual - Forecast)²) | Squared error | [0, ∞) |
| **TS_RMSE** | √MSE | Error in original units | [0, ∞) |
| **TS_MAPE** | Mean(\|Actual - Forecast\|/Actual) × 100 | % error | [0, ∞) |
| **TS_SMAPE** | Symmetric MAPE | % error (handles zeros) | [0, 200] |
| **TS_R2** | 1 - Σ(Actual-Forecast)²/Σ(Actual-Mean)² | Variance explained | (-∞, 1] |
| **TS_BIAS** | Mean(Forecast - Actual) | Systematic error | (-∞, ∞) |

### Comparative Metrics

| Function | Use Case |
|----------|----------|
| **TS_MASE** | Compare to baseline (scaled error) |
| **TS_RMAE** | Relative performance of two models |

### Distribution Metrics

| Function | Use Case |
|----------|----------|
| **TS_QUANTILE_LOSS** | Quantile forecast accuracy |
| **TS_MQLOSS** | Multi-quantile performance |
| **TS_COVERAGE** | Interval calibration |

**Example Usage**:
```sql
SELECT 
    TS_MAE(LIST(actual), LIST(forecast)) AS mae,
    TS_RMSE(LIST(actual), LIST(forecast)) AS rmse,
    TS_MAPE(LIST(actual), LIST(forecast)) AS mape,
    TS_R2(LIST(actual), LIST(forecast)) AS r_squared,
    TS_COVERAGE(LIST(actual), LIST(lower), LIST(upper)) AS coverage
FROM results;
```

---

## EDA Functions (5 Macros)

### TS_STATS

Generate comprehensive per-series statistics.

**Signature**:
```sql
TS_STATS(table_name, group_col, date_col, value_col) → TABLE
```

**Output** (23 features):
- Basic: length, start_date, end_date, mean, std, min, max, median
- Quality: n_null, n_nan, n_zeros, n_gaps, n_unique_values, is_constant
- Advanced: trend_corr, cv, intermittency, quality_score
- Raw: values, dates (arrays)

### TS_QUALITY_REPORT

Comprehensive data quality checks.

**Signature**:
```sql
TS_QUALITY_REPORT(stats_table, min_length) → TABLE
```

**Checks**:
- Gap analysis
- Missing values
- Constant series
- Short series
- End date alignment

### TS_DATASET_SUMMARY

Overall dataset statistics.

**Signature**:
```sql
TS_DATASET_SUMMARY(stats_table) → TABLE
```

### TS_GET_PROBLEMATIC

Identify low-quality series.

**Signature**:
```sql
TS_GET_PROBLEMATIC(stats_table, quality_threshold) → TABLE
```

### TS_DETECT_SEASONALITY_ALL

Detect seasonality for all series.

**Signature**:
```sql
TS_DETECT_SEASONALITY_ALL(table_name, group_col, date_col, value_col) → TABLE
```

**Output**:
- `series_id`
- `detected_periods: INT[]` - All detected periods
- `primary_period: INT` - Main seasonal period
- `is_seasonal: BOOLEAN`

---

## Data Preparation Functions (12 Macros)

### Gap Filling

**TS_FILL_GAPS**: Fill missing time points
```sql
TS_FILL_GAPS(table_name, group_col, date_col, value_col) → TABLE
```

**TS_FILL_FORWARD**: Extend series to target date
```sql
TS_FILL_FORWARD(table_name, group_col, date_col, value_col, target_date) → TABLE
```

### Series Filtering

**TS_DROP_CONSTANT**: Remove constant series
```sql
TS_DROP_CONSTANT(table_name, group_col, value_col) → TABLE
```

**TS_DROP_SHORT**: Remove short series
```sql
TS_DROP_SHORT(table_name, group_col, date_col, min_length) → TABLE
```

**TS_DROP_GAPPY**: Remove series with excessive gaps
```sql
TS_DROP_GAPPY(table_name, group_col, date_col, max_gap_pct) → TABLE
```

### Edge Cleaning

**TS_DROP_LEADING_ZEROS**: Remove leading zeros
```sql
TS_DROP_LEADING_ZEROS(table_name, group_col, date_col, value_col) → TABLE
```

**TS_DROP_TRAILING_ZEROS**: Remove trailing zeros
```sql
TS_DROP_TRAILING_ZEROS(table_name, group_col, date_col, value_col) → TABLE
```

**TS_DROP_EDGE_ZEROS**: Remove both leading and trailing zeros
```sql
TS_DROP_EDGE_ZEROS(table_name, group_col, date_col, value_col) → TABLE
```

### Missing Value Imputation

**TS_FILL_NULLS_CONST**: Fill with constant
```sql
TS_FILL_NULLS_CONST(table_name, group_col, date_col, value_col, fill_value) → TABLE
```

**TS_FILL_NULLS_FORWARD**: Forward fill (LOCF)
```sql
TS_FILL_NULLS_FORWARD(table_name, group_col, date_col, value_col) → TABLE
```

**TS_FILL_NULLS_BACKWARD**: Backward fill
```sql
TS_FILL_NULLS_BACKWARD(table_name, group_col, date_col, value_col) → TABLE
```

**TS_FILL_NULLS_MEAN**: Fill with series mean
```sql
TS_FILL_NULLS_MEAN(table_name, group_col, date_col, value_col) → TABLE
```

---

## Seasonality Functions

### TS_DETECT_SEASONALITY

Detect seasonal periods in a single series.

**Signature**:
```sql
TS_DETECT_SEASONALITY(values: DOUBLE[]) → INT[]
```

**Example**:
```sql
SELECT TS_DETECT_SEASONALITY(LIST(sales ORDER BY date)) AS periods
FROM sales_data;
-- Returns: [7, 30] (weekly and monthly patterns)
```

### TS_ANALYZE_SEASONALITY

Detailed seasonality analysis with decomposition.

**Signature**:
```sql
TS_ANALYZE_SEASONALITY(
    timestamps: TIMESTAMP[],
    values: DOUBLE[]
) → STRUCT
```

**Returns**: trend_strength, seasonal_strength, periods, etc.

---

## Changepoint Detection

### TS_DETECT_CHANGEPOINTS

Detect regime changes in a single series.

**Signature**:
```sql
TS_DETECT_CHANGEPOINTS(
    table_name: VARCHAR,
    date_col: DATE | TIMESTAMP,
    value_col: DOUBLE,
    params: MAP
) → TABLE
```

**Parameters**:
```sql
{
    'hazard_lambda': DOUBLE,         -- Detection sensitivity (default: 250)
    'include_probabilities': BOOL    -- Return probabilities (default: false)
}
```

**Output**:
- `date_col: TIMESTAMP`
- `value_col: DOUBLE`
- `is_changepoint: BOOLEAN`
- `changepoint_probability: DOUBLE`

### TS_DETECT_CHANGEPOINTS_BY

Changepoint detection with GROUP BY.

**Signature**:
```sql
TS_DETECT_CHANGEPOINTS_BY(
    table_name: VARCHAR,
    group_col: VARCHAR,
    date_col: DATE | TIMESTAMP,
    value_col: DOUBLE,
    params: MAP
) → TABLE
```

---

## Low-Level Functions

### TS_FORECAST_AGG

Aggregate function for custom GROUP BY.

**Signature**:
```sql
TS_FORECAST_AGG(
    date_col: TIMESTAMP,
    value_col: DOUBLE,
    method: VARCHAR,
    horizon: INT,
    params: MAP
) → STRUCT
```

**Usage** (for 2+ group columns):
```sql
WITH fc AS (
    SELECT 
        product_id,
        location_id,
        TS_FORECAST_AGG(date, amount, 'AutoETS', 28, {'seasonal_period': 7}) AS result
    FROM sales
    GROUP BY product_id, location_id
)
SELECT 
    product_id,
    location_id,
    UNNEST(result.forecast_step) AS forecast_step,
    UNNEST(result.point_forecast) AS point_forecast
FROM fc;
```

### TS_DETECT_CHANGEPOINTS_AGG

Aggregate function for custom changepoint detection.

**Signature**:
```sql
TS_DETECT_CHANGEPOINTS_AGG(
    date_col: TIMESTAMP,
    value_col: DOUBLE,
    params: MAP
) → STRUCT
```

---

## Parameter Quick Reference

### Forecasting Parameters

```sql
{
    -- Universal
    'confidence_level': 0.80 | 0.90 | 0.95 | 0.99,  -- Default: 0.90
    'return_insample': true | false,                 -- Default: false
    'seasonal_period': INT,                          -- Required for seasonal models
    'seasonal_periods': [INT, ...],                  -- Multiple seasonality
    
    -- ETS
    'error_type': 0 | 1,      -- 0=additive, 1=multiplicative
    'trend_type': 0 | 1 | 2,  -- 0=none, 1=additive, 2=damped
    'season_type': 0 | 1 | 2, -- 0=none, 1=additive, 2=multiplicative
    'alpha': 0.0-1.0,         -- Level smoothing
    'beta': 0.0-1.0,          -- Trend smoothing
    'gamma': 0.0-1.0,         -- Seasonal smoothing
    'phi': 0.0-1.0,           -- Damping parameter
    
    -- ARIMA
    'p': INT,                 -- AR order
    'd': INT,                 -- Differencing
    'q': INT,                 -- MA order
    'P': INT,                 -- Seasonal AR
    'D': INT,                 -- Seasonal differencing
    'Q': INT,                 -- Seasonal MA
    's': INT,                 -- Seasonal period
    'include_intercept': BOOL,
    
    -- Theta
    'theta': DOUBLE,          -- Theta parameter (default: 2.0)
    
    -- TBATS
    'use_box_cox': BOOL,
    'box_cox_lambda': DOUBLE,
    'use_trend': BOOL,
    'use_damped_trend': BOOL,
    
    -- MFLES/MSTL
    'n_iterations': INT,
    'lr_trend': DOUBLE,
    'lr_season': DOUBLE,
    'lr_level': DOUBLE,
    'trend_method': INT,
    'seasonal_method': INT
}
```

### Changepoint Parameters

```sql
{
    'hazard_lambda': DOUBLE,         -- Default: 250 (lower = more sensitive)
    'include_probabilities': BOOL    -- Default: false (faster)
}
```

---

## Return Types

### Forecast Output

```sql
STRUCT {
    forecast_step: LIST<INT>,
    forecast_timestamp: LIST<TIMESTAMP>,
    point_forecast: LIST<DOUBLE>,
    lower: LIST<DOUBLE>,
    upper: LIST<DOUBLE>,
    model_name: VARCHAR,
    insample_fitted: LIST<DOUBLE>,
    confidence_level: DOUBLE
}
```

### Changepoint Output

```sql
STRUCT {
    timestamp: TIMESTAMP,
    value: DOUBLE,
    is_changepoint: BOOLEAN,
    changepoint_probability: DOUBLE
}
```

---

## Error Handling

### Common Errors

**"Model requires 'seasonal_period' parameter"**
```sql
-- Solution: Add seasonal_period
{'seasonal_period': 7}
```

**"Series too short for seasonal model"**
```sql
-- Need at least 2 * seasonal_period observations
-- Solution: Use non-seasonal model or get more data
```

**"Constant series detected"**
```sql
-- Solution: Drop constant series
SELECT * FROM TS_DROP_CONSTANT('sales', product_id, amount);
```

**"confidence_level must be between 0 and 1"**
```sql
-- Solution: Use valid range
{'confidence_level': 0.95}  -- ✅
{'confidence_level': 95}    -- ❌
```

---

## Performance Considerations

### Optimization Tips

1. **Use GROUP BY efficiently**:
```sql
-- Good: Single TS_FORECAST_BY call
SELECT * FROM TS_FORECAST_BY('sales', product_id, ...);

-- Avoid: Multiple individual calls
```

2. **Materialize intermediate results**:
```sql
-- For complex pipelines
CREATE TABLE sales_prep AS SELECT * FROM TS_FILL_GAPS(...);
CREATE TABLE forecasts AS SELECT * FROM TS_FORECAST_BY('sales_prep', ...);
```

3. **Disable features you don't need**:
```sql
-- Don't request fitted values unless needed
{'return_insample': false}  -- Faster
```

4. **Use appropriate models**:
```sql
-- AutoETS: Slower but accurate
-- SeasonalNaive: Fast for simple patterns
```

### Memory Usage

| Dataset | Memory | Notes |
|---------|--------|-------|
| 1K series × 365 days | ~50 MB | Efficient columnar storage |
| 10K series × 365 days | ~500 MB | Parallel processing |
| 100K series × 365 days | ~5 GB | May need partitioning |

---

## Summary

**Total API**:
- 31 forecasting models
- 12 evaluation metrics
- 5 EDA macros
- 12 data preparation macros
- 4 seasonality/changepoint functions

**60+ functions** for complete time series analysis!

**Next**: See [guides/](.) for detailed usage examples

---

**Quick Links**:
- [Basic Forecasting](30_basic_forecasting.md) - Get started
- [Model Selection](40_model_selection.md) - Choose the right model
- [Parameters Guide](12_parameters.md) - Detailed parameter reference

