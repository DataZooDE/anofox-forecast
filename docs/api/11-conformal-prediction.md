# Conformal Prediction

> Distribution-free prediction intervals with guaranteed coverage

## Overview

Conformal prediction provides **distribution-free prediction intervals** with guaranteed coverage probability. Unlike parametric methods, conformal prediction makes minimal assumptions about the underlying distribution and provides valid coverage even for finite samples.

**Use this document to:**
- Create prediction intervals with guaranteed coverage (e.g., 90%, 95%)
- Calibrate conformity scores from backtest residuals
- Apply calibrated intervals to new forecasts
- Choose between one-step, modular, or array-based approaches
- Build production pipelines that separate calibration from application

### How It Works

Conformal prediction works in two phases:
1. **Calibration**: Compute a conformity score from calibration residuals
2. **Prediction**: Apply the conformity score to new forecasts

The resulting intervals cover the true value with probability at least `1 - α`, where `α` is the miscoverage rate.

---

## Choose Your Approach

The API offers three levels of abstraction. Choose based on your needs:

| Approach | Functions | Use When |
|----------|-----------|----------|
| **One-Step** | `ts_conformal_by` | You have backtest results and want intervals immediately |
| **Modular** | `ts_conformal_calibrate` + `ts_conformal_apply_by` | You need to reuse calibration across multiple forecasts |
| **Array-Based** | `ts_conformal_predict`, `ts_conformal_quantile`, etc. | You're building custom pipelines or working with array data |

### One-Step Approach

Best for most users. Calibrates and applies intervals in a single call.

```sql
SELECT * FROM ts_conformal_by(
    'backtest_results',
    product_id,
    actual,
    forecast,
    forecast,
    {'alpha': 0.1}  -- 90% coverage
);
```

### Modular Approach

Use when you need to:
- Apply the same calibration to multiple forecast tables
- Store calibration scores for production use
- Separate calibration from application for debugging

```sql
-- Step 1: Calibrate once
CREATE TABLE calibration AS
SELECT * FROM ts_conformal_calibrate(
    'backtest', actual, forecast, {'alpha': 0.1}
);

-- Step 2: Apply to any forecast table
SELECT * FROM ts_conformal_apply_by(
    'future_forecasts',
    product_id,
    forecast,
    (SELECT conformity_score FROM calibration)
);
```

### Array-Based Functions

Use when you need:
- Fine-grained control over the conformal prediction process
- Integration with custom forecasting pipelines
- Array inputs/outputs for programmatic use

```sql
-- Direct array-based prediction
SELECT (ts_conformal_predict(
    residuals_array,
    forecasts_array,
    0.1
)).*;
```

---

## Common Scenarios

### Scenario 1: Quick Uncertainty Quantification

**Goal:** You have backtest results and need prediction intervals quickly.

**Solution:** Use `ts_conformal_by` for a one-step solution.

```sql
-- Already have backtest results with actual vs forecast
SELECT * FROM ts_conformal_by(
    'my_backtest_results',
    product_id,
    actual,
    forecast,
    forecast,
    {'alpha': 0.1}
);
```

### Scenario 2: Production Forecasting System

**Goal:** Calibrate once, apply to daily/weekly forecast batches.

**Solution:** Use modular approach—store calibration, apply repeatedly.

```sql
-- Run once: calibrate from historical backtests
CREATE TABLE calibration AS
SELECT * FROM ts_conformal_calibrate(
    'historical_backtest', actual, forecast, {'alpha': 0.1}
);

-- Run daily: apply to new forecasts
INSERT INTO forecast_with_intervals
SELECT * FROM ts_conformal_apply_by(
    'todays_forecasts',
    product_id,
    forecast,
    (SELECT conformity_score FROM calibration)
);
```

### Scenario 3: Skewed Forecast Errors

**Goal:** Your forecasts consistently under-predict (e.g., demand forecasting).

**Solution:** Use asymmetric intervals for tighter, more accurate bounds.

```sql
SELECT * FROM ts_conformal_by(
    'backtest_results',
    product_id,
    actual,
    forecast,
    forecast,
    {'alpha': 0.1, 'method': 'asymmetric'}
);
```

### Scenario 4: Model Comparison

**Goal:** Compare which model produces sharper intervals while maintaining coverage.

**Solution:** Use evaluation functions to compare interval quality.

```sql
-- Generate intervals for both models
CREATE TABLE model_a_intervals AS ...;
CREATE TABLE model_b_intervals AS ...;

-- Compare interval widths
SELECT 'Model A' AS model, * FROM ts_interval_width_by(
    'model_a_intervals', product_id, lower, upper
)
UNION ALL
SELECT 'Model B' AS model, * FROM ts_interval_width_by(
    'model_b_intervals', product_id, lower, upper
);
```

### Scenario 5: Custom Array-Based Pipeline

**Goal:** Working with pre-aggregated array data in a custom pipeline.

**Solution:** Use scalar functions directly.

```sql
-- Data already in array form
WITH series_data AS (
    SELECT
        product_id,
        list(actual ORDER BY date) AS actuals,
        list(forecast ORDER BY date) AS forecasts,
        list(actual - forecast ORDER BY date) AS residuals
    FROM backtest_results
    GROUP BY product_id
)
SELECT
    product_id,
    (ts_conformal_predict(residuals, forecasts, 0.1)).*
FROM series_data;
```

---

## Complete Workflow Example

A full workflow from raw data to conformal intervals:

```sql
-- Step 0: Detect seasonality (e.g., weekly = 7)
SELECT * FROM ts_detect_periods_by('sales', product_id, date, value, MAP{});

-- Step 1: Generate backtest results with AutoETS
CREATE TABLE backtest AS
SELECT * FROM ts_backtest_auto_by(
    'sales', product_id, date, value, 7, 5, '1d',
    {'method': 'AutoETS', 'seasonal_period': 7}
);

-- Step 2: Calibrate conformity score
CREATE TABLE calibration AS
SELECT * FROM ts_conformal_calibrate(
    'backtest', actual, forecast, {'alpha': 0.1}
);

-- Step 3: Generate future forecasts
CREATE TABLE future AS
SELECT * FROM ts_forecast_by(
    'sales', product_id, date, value, 'AutoETS', 14,
    {'seasonal_period': 7}
);

-- Step 4: Apply conformal intervals
SELECT * FROM ts_conformal_apply_by(
    'future',
    product_id,
    forecast,
    (SELECT conformity_score FROM calibration)
);
```

---

## Table Macros

> **When to use:** Table macros operate on tables directly and handle grouping automatically. Use these for standard forecasting workflows with tabular data.

### ts_conformal_by

High-level macro for conformal prediction on grouped backtest results.

**Signature:**
```sql
ts_conformal_by(backtest_results, group_col, actual_col, forecast_col, point_forecast_col, params)
```

**Parameters:**
| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `backtest_results` | VARCHAR | Yes | — | Table with backtest results |
| `group_col` | COLUMN | Yes | — | Series identifier column |
| `actual_col` | COLUMN | Yes | — | Actual values column |
| `forecast_col` | COLUMN | Yes | — | Forecast values column |
| `point_forecast_col` | COLUMN | Yes | — | Point forecast column |
| `params` | STRUCT | Yes | — | Configuration parameters |

**Params Options:**
| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `alpha` | DOUBLE | `0.1` | Miscoverage rate (0.1 = 90% coverage, 0.05 = 95%) |
| `method` | VARCHAR | `'split'` | `'split'` (symmetric) or `'asymmetric'` |

**Examples:**
```sql
-- Symmetric intervals (default)
SELECT * FROM ts_conformal_by(
    'backtest_results',
    product_id,
    actual,
    forecast,
    point_forecast,
    {'alpha': 0.1, 'method': 'split'}
);

-- Asymmetric intervals (for skewed residuals)
SELECT * FROM ts_conformal_by(
    'backtest_results',
    product_id,
    actual,
    forecast,
    point_forecast,
    {'alpha': 0.1, 'method': 'asymmetric'}
);
```

**When to use asymmetric:** Use `'asymmetric'` when residuals are not symmetric (e.g., demand forecasts often under-predict more than over-predict). This creates separate upper and lower quantiles for tighter, more accurate intervals.

---

### ts_conformal_calibrate

Calibrates a conformity score from backtest residuals.

**Signature:**
```sql
ts_conformal_calibrate(backtest_results, actual_col, forecast_col, params)
```

**Parameters:**
| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `backtest_results` | VARCHAR | Yes | — | Table with backtest results |
| `actual_col` | COLUMN | Yes | — | Actual values column |
| `forecast_col` | COLUMN | Yes | — | Forecast values column |
| `params` | STRUCT | Yes | — | Configuration parameters |

**Params Options:**
| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `alpha` | DOUBLE | `0.1` | Miscoverage rate (0.1 = 90% coverage) |

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `conformity_score` | DOUBLE | Calibrated score for interval construction |
| `coverage` | DOUBLE | Empirical coverage from calibration |
| `n_residuals` | INTEGER | Number of residuals used |

**Example:**
```sql
SELECT * FROM ts_conformal_calibrate(
    'backtest_results',
    actual,
    forecast,
    {'alpha': 0.05}
);
```

---

### ts_conformal_apply_by

Applies a pre-computed conformity score to forecast results.

**Signature:**
```sql
ts_conformal_apply_by(forecast_results, group_col, forecast_col, conformity_score)
```

**Parameters:**
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `forecast_results` | VARCHAR | Yes | Table with forecast results |
| `group_col` | COLUMN | Yes | Series identifier column |
| `forecast_col` | COLUMN | Yes | Point forecast column |
| `conformity_score` | DOUBLE | Yes | Score from ts_conformal_calibrate |

**Example:**
```sql
WITH score AS (
    SELECT conformity_score FROM ts_conformal_calibrate(
        'backtest', actual, forecast, {'alpha': 0.1}
    )
)
SELECT * FROM ts_conformal_apply_by(
    'future_forecasts',
    product_id,
    forecast,
    (SELECT conformity_score FROM score)
);
```

---

### ts_interval_width_by

Computes mean interval width for grouped forecast results.

**Signature:**
```sql
ts_interval_width_by(results, group_col, lower_col, upper_col)
```

**Returns:** `group_col`, `mean_width`, `n_intervals`

**Example:**
```sql
SELECT * FROM ts_interval_width_by(
    'forecast_results',
    product_id,
    lower,
    upper
);
```

---

## Scalar Functions

Scalar functions operate on arrays and return scalar values or structs. They are the building blocks used internally by the table macros.

### When to Use Scalar Functions

| Scenario | Recommended Functions |
|----------|----------------------|
| **Custom pipeline with array data** | `ts_conformal_predict`, `ts_conformal_predict_asymmetric` |
| **Need just the quantile score** | `ts_conformal_quantile` |
| **Apply pre-computed score to arrays** | `ts_conformal_intervals` |
| **Evaluate interval quality** | `ts_conformal_coverage`, `ts_conformal_evaluate`, `ts_mean_interval_width` |
| **Multi-level calibration profiles** | `ts_conformal_learn` |

---

### Calibration Functions

These functions compute conformity scores from residuals.

#### ts_conformal_quantile

Computes the empirical quantile of absolute residuals for split conformal prediction.

**Signature:**
```sql
ts_conformal_quantile(residuals DOUBLE[], alpha DOUBLE) → DOUBLE
```

**Parameters:**
- `residuals`: Residuals (actual - predicted) from calibration set
- `alpha`: Miscoverage rate (0 < alpha < 1). Use 0.1 for 90% coverage.

**Example:**
```sql
SELECT ts_conformal_quantile(
    [1.0, -0.5, 2.0, -1.5, 0.8],  -- residuals from backtest
    0.1                           -- 90% coverage
) AS conformity_score;
```

---

### Prediction Functions

These functions apply conformity scores to generate prediction intervals.

#### ts_conformal_intervals

Applies a pre-computed conformity score to create symmetric prediction intervals.

**Signature:**
```sql
ts_conformal_intervals(forecasts DOUBLE[], conformity_score DOUBLE)
→ STRUCT(lower DOUBLE[], upper DOUBLE[])
```

**Example:**
```sql
SELECT
    (ts_conformal_intervals([100.0, 110.0, 120.0], 5.0)).lower AS lower,
    (ts_conformal_intervals([100.0, 110.0, 120.0], 5.0)).upper AS upper;
-- Returns: lower=[95,105,115], upper=[105,115,125]
```

---

#### ts_conformal_predict

Full split conformal prediction: computes conformity score and applies to forecasts in one call.

**Signature:**
```sql
ts_conformal_predict(residuals DOUBLE[], forecasts DOUBLE[], alpha DOUBLE)
→ STRUCT(
    point DOUBLE[],
    lower DOUBLE[],
    upper DOUBLE[],
    coverage DOUBLE,
    conformity_score DOUBLE,
    method VARCHAR
)
```

**Example:**
```sql
WITH backtest_residuals AS (
    SELECT [1.2, -0.8, 1.5, -1.0, 0.5]::DOUBLE[] AS residuals
),
future_forecasts AS (
    SELECT [100.0, 102.0, 104.0]::DOUBLE[] AS forecasts
)
SELECT
    (ts_conformal_predict(residuals, forecasts, 0.1)).*
FROM backtest_residuals, future_forecasts;
```

---

#### ts_conformal_predict_asymmetric

Asymmetric conformal prediction with separate upper and lower quantiles.

**Signature:**
```sql
ts_conformal_predict_asymmetric(residuals DOUBLE[], forecasts DOUBLE[], alpha DOUBLE)
→ STRUCT(point DOUBLE[], lower DOUBLE[], upper DOUBLE[], coverage DOUBLE,
         conformity_score DOUBLE, method VARCHAR)
```

**Use Case:** When residuals are not symmetric (e.g., skewed demand forecasts).

**Example:**
```sql
SELECT
    (ts_conformal_predict_asymmetric(
        [-0.5, -0.3, 0.2, 1.0, 2.5],  -- positively skewed residuals
        [100.0, 110.0],               -- forecasts
        0.1                           -- 90% coverage
    )).*;
```

---

### Evaluation Functions

These functions assess the quality of prediction intervals.

#### ts_mean_interval_width

Computes the mean width of prediction intervals. Narrower intervals are better (more precise) as long as coverage is maintained.

**Signature:**
```sql
ts_mean_interval_width(lower DOUBLE[], upper DOUBLE[]) → DOUBLE
```

**Example:**
```sql
SELECT
    ts_mean_interval_width([95.0, 105.0], [105.0, 115.0]) AS model_a_width,
    ts_mean_interval_width([90.0, 100.0], [110.0, 120.0]) AS model_b_width;
-- model_a_width=10.0, model_b_width=20.0 (model A has sharper intervals)
```

---

#### ts_conformal_coverage

Computes the empirical coverage of prediction intervals. Coverage should be close to `1 - alpha` (e.g., 90% for alpha=0.1).

**Signature:**
```sql
ts_conformal_coverage(actuals DOUBLE[], lower DOUBLE[], upper DOUBLE[]) → DOUBLE
```

**Returns:** Fraction of actuals within [lower, upper] bounds.

**Example:**
```sql
SELECT ts_conformal_coverage(
    [100.0, 110.0, 120.0],  -- actual values
    [95.0, 105.0, 115.0],   -- lower bounds
    [105.0, 115.0, 125.0]   -- upper bounds
) AS coverage;
-- Returns: 1.0 (all values within bounds)
```

---

#### ts_conformal_evaluate

Comprehensive evaluation of conformal prediction intervals. Returns multiple metrics in one call.

**Signature:**
```sql
ts_conformal_evaluate(actuals DOUBLE[], lower DOUBLE[], upper DOUBLE[], alpha DOUBLE)
→ STRUCT(coverage DOUBLE, violation_rate DOUBLE, mean_width DOUBLE,
         winkler_score DOUBLE, n_observations INTEGER)
```

**Returns:**
- `coverage`: Empirical coverage rate
- `violation_rate`: Fraction of values outside intervals
- `mean_width`: Average interval width
- `winkler_score`: Winkler score (penalizes width and violations)
- `n_observations`: Number of observations

**Example:**
```sql
SELECT (ts_conformal_evaluate(
    [100.0, 110.0, 120.0],
    [95.0, 105.0, 115.0],
    [105.0, 115.0, 125.0],
    0.1
)).*;
```

---

### Advanced Calibration

#### ts_conformal_learn

Learn a calibration profile from residuals. Use this when you need multiple coverage levels or advanced calibration strategies.

**Signature:**
```sql
ts_conformal_learn(residuals DOUBLE[], alphas DOUBLE[], method VARCHAR, strategy VARCHAR)
→ STRUCT(
    method VARCHAR,
    strategy VARCHAR,
    alphas DOUBLE[],
    state_vector DOUBLE[],
    scores_lower DOUBLE[],
    scores_upper DOUBLE[]
)
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `residuals` | DOUBLE[] | Calibration residuals (actual - predicted) |
| `alphas` | DOUBLE[] | Array of miscoverage rates to calibrate |
| `method` | VARCHAR | `'symmetric'` or `'asymmetric'` |
| `strategy` | VARCHAR | `'naive'`, `'cv'`, or `'adaptive'` |

**Methods:**
| Method | Description |
|--------|-------------|
| `'symmetric'` | Use absolute residuals for symmetric intervals |
| `'asymmetric'` | Separate upper/lower quantiles for asymmetric intervals |

**Strategies:**
| Strategy | Description |
|----------|-------------|
| `'naive'` | Simple empirical quantile (default) |
| `'cv'` | Cross-validation based calibration |
| `'adaptive'` | Adaptive conformal inference |

**Example:**
```sql
-- Learn calibration profile for multiple coverage levels
SELECT ts_conformal_learn(
    [1.2, -0.8, 1.5, -1.0, 0.5, -0.3, 0.9, -1.2]::DOUBLE[],  -- residuals
    [0.1, 0.05, 0.2]::DOUBLE[],                               -- 90%, 95%, 80% coverage
    'symmetric',
    'naive'
) AS profile;
```

---

*See also: [Cross-Validation](08-cross-validation.md) | [Evaluation Metrics](09-evaluation-metrics.md)*
