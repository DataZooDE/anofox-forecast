# Complete API Reference

## Overview

Comprehensive technical reference for all functions, macros, and parameters in the Anofox Forecast extension.

**API Summary**:

- 2 core forecasting functions (`TS_FORECAST`, `TS_FORECAST_BY`)
- 31 forecasting models
- 12 evaluation metrics
- 5 EDA macros
- 12 data preparation macros
- 4 seasonality/changepoint detection functions

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

**Output Schema**:

```sql
SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, 
                          {'seasonal_period': 7, 'confidence_level': 0.95});
```

**Behavioral Notes**:

- `date_col` type preserved from input (INTEGER, DATE, or TIMESTAMP)
- `insample_fitted` array length equals training data size (empty by default, populate via `return_insample: true`)
- Prediction intervals computed at `confidence_level` (default 0.90)

**Example**:

<!-- include: test/sql/docs_examples/90_api_reference_example_03.sql -->

### TS_FORECAST_BY

Generate forecasts for multiple time series with GROUP BY.

**Signature**:

<!-- include: test/sql/docs_examples/90_api_reference_forecast_04.sql -->

**Output Columns**: Same as TS_FORECAST, plus:

- `group_col: ANY` - Grouping column value

**Example**:

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

### TS_FEATURES

Extract the complete tsfresh-compatible feature vector directly inside DuckDB.

**Signature**:

```sql
ts_features(
    ts_column TIMESTAMP|DATE|BIGINT,
    value_column DOUBLE,
    feature_selection LIST(VARCHAR) | STRUCT(
        feature_names LIST(VARCHAR),
        overrides LIST(STRUCT(feature VARCHAR, params MAP(VARCHAR, ANY)))
    ) DEFAULT NULL,
    feature_params LIST(STRUCT(feature VARCHAR, params MAP(VARCHAR, ANY))) DEFAULT NULL
) -> STRUCT(...)
```

**Highlights**:

- Mirrors the 76 feature calculators and default parameter grids from upstream
  [tsfresh](https://tsfresh.readthedocs.io/en/latest/text/list_of_features.html)
- Safe for both `GROUP BY` and analytic windows (`OVER (PARTITION BY ... ORDER BY ... ROWS BETWEEN ...)`)
- Optional `feature_names` lets you restrict output to a LIST(VARCHAR) of columns (omit or pass `NULL` for defaults)
- Optional `feature_params` accepts a LIST(STRUCT(feature, params)) or MAP(VARCHAR, ANY) to override parameter grids
  per feature
- Use `ts_features_config_from_json(path)` or `ts_features_config_from_csv(path)` to materialize both `feature_names`
  and `feature_params` from external files; pass the resulting STRUCT as the third argument to `ts_features`
- Overrides replace (not merge) the default parameter combinations for that feature, making ad hoc ratios or
  thresholds trivial
- Default output emits exactly one column per feature; specify `feature_params` if you need additional parameterized
  variants
- Use `ts_features_list()` to inspect column names, default parameter values, and the supported parameter keys
- Timestamp ordering inferred from INTEGER/DATE/TIMESTAMP inputs; values are always DOUBLE
- Output columns follow `feature__param_key_value` naming for stable downstream references

**Example**:

```sql
-- ts_features example: compute feature vector per product
LOAD anofox_forecast;

CREATE OR REPLACE TABLE demand_series AS
SELECT 
    product_id,
    (TIMESTAMP '2024-01-01' + INTERVAL day DAY) AS ts,
    (100 + product_id * 10 + day)::DOUBLE AS demand
FROM generate_series(0, 6) t(day)
CROSS JOIN (SELECT 1 AS product_id UNION ALL SELECT 2) p;

SELECT column_name, feature_name, default_parameters, parameter_keys
FROM ts_features_list()
ORDER BY column_name
LIMIT 5;

WITH feature_vec AS (
    SELECT 
        product_id,
        ts_features(
            ts,
            demand,
            ['mean', 'variance', 'autocorrelation__lag_1', 'ratio_beyond_r_sigma'],
            [{'feature': 'ratio_beyond_r_sigma', 'params': {'r': 1.0}}]
        ) AS feats
    FROM demand_series
    GROUP BY product_id
)
SELECT 
    product_id,
    (feats).mean AS avg_demand,
    (feats).variance AS demand_variance,
    (feats).autocorrelation__lag_1 AS lag1_autocorr,
    (feats).ratio_beyond_r_sigma__r_1 AS outlier_share
FROM feature_vec
ORDER BY product_id;

-- Load overrides from a JSON file and pass the config struct directly
SELECT 
    product_id,
    (ts_features(
        ts,
        demand,
        ts_features_config_from_json('benchmark/timeseries_features/data/features_overrides.json')
    )).autocorrelation__lag_2 AS lag2_autocorr
FROM demand_series
GROUP BY product_id
ORDER BY product_id;

```

### TS_FEATURES_LIST

Return metadata for every available tsfresh feature (one row per feature by default).

**Signature**:

```sql
ts_features_list() -> TABLE(
    column_name VARCHAR,
    feature_name VARCHAR,
    parameter_suffix VARCHAR,
    default_parameters VARCHAR,
    parameter_keys VARCHAR
)
```

Use this helper to discover valid column names before passing them to `ts_features(..., feature_names)` or to inspect
default parameter sets and supported keys.

### TS_FEATURES_CONFIG_FROM_JSON / TS_FEATURES_CONFIG_FROM_CSV

Load reusable override definitions from external files. Each helper returns a STRUCT with two fields—`feature_names`
(`LIST(VARCHAR)`) and `overrides` (`LIST(STRUCT(feature VARCHAR, params_json VARCHAR))`)—which can be supplied as the
third argument to `ts_features`. The `params_json` field stores the parameter map as a JSON string, which `ts_features`
parses at bind time to reconstruct typed overrides.

**Signatures**:

```sql
ts_features_config_from_json(path VARCHAR) -> STRUCT(feature_names LIST(VARCHAR), overrides LIST(STRUCT(feature VARCHAR, params_json VARCHAR)))
ts_features_config_from_csv(path VARCHAR)  -> STRUCT(feature_names LIST(VARCHAR), overrides LIST(STRUCT(feature VARCHAR, params_json VARCHAR)))
```

- JSON files should contain an array of objects with `feature` plus optional `params` objects (see
  `benchmark/timeseries_features/data/features_overrides.json`)
- CSV files should contain a header row with `feature` and any parameter columns (e.g., `r`, `lag`, etc.) as shown in
  `benchmark/timeseries_features/data/features_overrides.csv`
- Results are cached per-path for the session and evaluated at bind time, guaranteeing that `ts_features` receives
  constant literal arguments
- `ts_features_config_template()` exposes the same defaults as a table (one row per feature with a JSON payload) and is
  used to generate the checked-in templates `benchmark/timeseries_features/data/all_features_overrides.{json,csv}`

---

## Available Models (32 Total)

### Automatic Selection (Recommended)

| Model | Best For | Parameters |
|-------|----------|------------|
| **AutoETS** | General purpose | `seasonal_period` |
| **AutoARIMA** | Complex patterns | `seasonal_period` |
| **AutoTheta** | Theta family selection | `seasonal_period`, `model` |
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
| **AutoTheta** | Auto model selection (STM/OTM/DSTM/DOTM) | `seasonal_period`, `model`, `decomposition_type` |
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

#### ARIMA Parameters

<!-- include: test/sql/docs_examples/90_api_reference_example_07.sql -->

#### Multiple Seasonality Parameters

<!-- include: test/sql/docs_examples/90_api_reference_seasonality_08.sql -->

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

<!-- include: test/sql/docs_examples/90_api_reference_evaluate_09.sql -->

---

## EDA Functions (5 Macros)

### TS_STATS

Generate comprehensive per-series statistics.

**Signature**:

<!-- include: test/sql/docs_examples/90_api_reference_statistics_10.sql -->

**Output** (23 features):

- Basic: length, start_date, end_date, mean, std, min, max, median
- Quality: n_null, n_nan, n_zeros, n_gaps, n_unique_values, is_constant
- Advanced: trend_corr, cv, intermittency, quality_score
- Raw: values, dates (arrays)

### TS_QUALITY_REPORT

Comprehensive data quality checks.

**Signature**:

<!-- include: test/sql/docs_examples/90_api_reference_data_quality_11.sql -->

**Checks**:

- Gap analysis
- Missing values
- Constant series
- Short series
- End date alignment

### TS_STATS_SUMMARY

Overall dataset statistics.

**Signature**:

<!-- include: test/sql/docs_examples/90_api_reference_statistics_12.sql -->

###

Identify low-quality series.

**Signature**:

```sql
(table_name, group_col, date_col, value_col) → TABLE
```

###

Detect seasonality for all series.

**Signature**:

<!-- include: test/sql/docs_examples/90_api_reference_data_quality_14.sql -->

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
TS_FILL_FORWARD(table_name, group_col, date_col, value_col, target_date) → TABLE
```

**TS_FILL_FORWARD**: Extend series to target date

<!-- include: test/sql/docs_examples/90_api_reference_fill_gaps_16.sql -->

### Series Filtering

**TS_DROP_CONSTANT**: Remove constant series

```sql
TS_DROP_SHORT(table_name, group_col, date_col, min_length) → TABLE
```

**TS_DROP_SHORT**: Remove short series

```sql
TS_DROP_GAPPY(table_name, group_col, date_col, max_gap_pct) → TABLE
```

**TS_DROP_GAPPY**: Remove series with excessive gaps

```sql
TS_DROP_LEADING_ZEROS(table_name, group_col, date_col, value_col) → TABLE
```

### Edge Cleaning

**TS_DROP_LEADING_ZEROS**: Remove leading zeros

```sql
TS_DROP_TRAILING_ZEROS(table_name, group_col, date_col, value_col) → TABLE
```

**TS_DROP_TRAILING_ZEROS**: Remove trailing zeros

```sql
TS_DROP_EDGE_ZEROS(table_name, group_col, date_col, value_col) → TABLE
```

**TS_DROP_EDGE_ZEROS**: Remove both leading and trailing zeros

```sql
TS_FILL_NULLS_CONST(table_name, group_col, date_col, value_col, fill_value) → TABLE
```

### Missing Value Imputation

**TS_FILL_NULLS_CONST**: Fill with constant

```sql
TS_FILL_NULLS_FORWARD(table_name, group_col, date_col, value_col) → TABLE
```

**TS_FILL_NULLS_FORWARD**: Forward fill (LOCF)

```sql
TS_FILL_NULLS_BACKWARD(table_name, group_col, date_col, value_col) → TABLE
```

**TS_FILL_NULLS_BACKWARD**: Backward fill

```sql
TS_FILL_NULLS_MEAN(table_name, group_col, date_col, value_col) → TABLE
```

**TS_FILL_NULLS_MEAN**: Fill with series mean

<!-- include: test/sql/docs_examples/90_api_reference_example_26.sql -->

---

## Seasonality Functions

### TS_DETECT_SEASONALITY

Detect seasonal periods in a single series.

**Signature**:

```sql
SELECT TS_DETECT_SEASONALITY(LIST(sales ORDER BY date)) AS periods
FROM sales_data;
-- Returns: [7, 30] (weekly and monthly patterns)
```

**Example**:

```sql
TS_ANALYZE_SEASONALITY(
    timestamps: TIMESTAMP[],
    values: DOUBLE[]
) → STRUCT
```

### TS_ANALYZE_SEASONALITY

Detailed seasonality analysis with decomposition.

**Signature**:

```sql
TS_DETECT_CHANGEPOINTS(
    table_name: VARCHAR,
    date_col: DATE | TIMESTAMP,
    value_col: DOUBLE,
    params: MAP
) → TABLE
```

**Returns**: trend_strength, seasonal_strength, periods, etc.

---

## Changepoint Detection

### TS_DETECT_CHANGEPOINTS

Detect regime changes in a single series.

**Signature**:

```sql
{
    'hazard_lambda': DOUBLE,         -- Detection sensitivity (default: 250)
    'include_probabilities': BOOL    -- Return probabilities (default: false)
}
```

**Parameters**:

```sql
TS_DETECT_CHANGEPOINTS_BY(
    table_name: VARCHAR,
    group_col: VARCHAR,
    date_col: DATE | TIMESTAMP,
    value_col: DOUBLE,
    params: MAP
) → TABLE
```

**Output**:

- `date_col: TIMESTAMP`
- `value_col: DOUBLE`
- `is_changepoint: BOOLEAN`
- `changepoint_probability: DOUBLE`

### TS_DETECT_CHANGEPOINTS_BY

Changepoint detection with GROUP BY.

**Signature**:

<!-- include: test/sql/docs_examples/90_api_reference_seasonality_32.sql -->

---

## Low-Level Functions

### TS_FORECAST_AGG

Aggregate function for custom GROUP BY.

**Signature**:

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

**Usage** (for 2+ group columns):

<!-- include: test/sql/docs_examples/90_api_reference_example_34.sql -->

### TS_DETECT_CHANGEPOINTS_AGG

Aggregate function for custom changepoint detection.

**Signature**:

<!-- include: test/sql/docs_examples/90_api_reference_seasonality_35.sql -->

---

## Parameter Quick Reference

### Forecasting Parameters

```sql
{
    'hazard_lambda': DOUBLE,         -- Default: 250 (lower = more sensitive)
    'include_probabilities': BOOL    -- Default: false (faster)
}
```

### Changepoint Parameters

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

---

## Return Types

### Forecast Output

```sql
STRUCT {
    timestamp: TIMESTAMP,
    value: DOUBLE,
    is_changepoint: BOOLEAN,
    changepoint_probability: DOUBLE
}
```

### Changepoint Output

```sql
-- Solution: Add seasonal_period
{'seasonal_period': 7}
```

---

## Error Handling

### Common Errors

#### "Model requires `seasonal_period` parameter"

```sql
-- Need at least 2 * seasonal_period observations
-- Solution: Use non-seasonal model or get more data
```

#### "Series too short for seasonal model"

<!-- include: test/sql/docs_examples/90_api_reference_example_41.sql -->

#### "Constant series detected"

```sql
-- Solution: Use valid range
{'confidence_level': 0.95}  -- ✅
{'confidence_level': 95}    -- ❌
```

#### "`confidence_level` must be between 0 and 1"

<!-- include: test/sql/docs_examples/90_api_reference_seasonality_43.sql -->

---

## Performance Considerations

### Optimization Tips

1. **Use GROUP BY efficiently**:

<!-- include: test/sql/docs_examples/90_api_reference_example_44.sql -->

1. **Materialize intermediate results**:

<!-- include: test/sql/docs_examples/90_api_reference_multi_series_45.sql -->

1. **Disable features you don't need**:

```sql
-- AutoETS: Slower but accurate
-- SeasonalNaive: Fast for simple patterns
```

1. **Use appropriate models**:

<!-- include: test/sql/docs_examples/90_api_reference_example_47.sql -->

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
