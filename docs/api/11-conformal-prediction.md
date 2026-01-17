# Conformal Prediction

> Distribution-free prediction intervals with guaranteed coverage

## Overview

Conformal prediction provides **distribution-free prediction intervals** with guaranteed coverage probability. Unlike parametric methods, conformal prediction makes minimal assumptions about the underlying distribution and provides valid coverage even for finite samples.

## How It Works

Conformal prediction works in two phases:
1. **Calibration**: Compute a conformity score from calibration residuals
2. **Prediction**: Apply the conformity score to new forecasts

The resulting intervals cover the true value with probability at least `1 - α`, where `α` is the miscoverage rate.

---

## Scalar Functions

### ts_conformal_quantile

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

### ts_conformal_intervals

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

### ts_conformal_predict

Full split conformal prediction: computes conformity score and applies to forecasts.

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

### ts_conformal_predict_asymmetric

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

### ts_mean_interval_width

Computes the mean width of prediction intervals.

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

### ts_conformal_coverage

Computes the empirical coverage of prediction intervals.

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

### ts_conformal_evaluate

Comprehensive evaluation of conformal prediction intervals.

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

### ts_conformal_learn

Learn a calibration profile from residuals for advanced conformal prediction workflows.

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

## Table Macros

### ts_conformal

High-level macro for conformal prediction on grouped backtest results.

**Signature:**
```sql
ts_conformal(backtest_results, group_col, actual_col, forecast_col, point_forecast_col, params)
```

**Params:**
- `alpha` (DOUBLE): Miscoverage rate (default: 0.1)
- `method` (VARCHAR): 'split' or 'asymmetric' (default: 'split')

**Example:**
```sql
SELECT * FROM ts_conformal(
    'backtest_results',
    product_id,
    actual,
    forecast,
    point_forecast,
    MAP{'alpha': 0.1, 'method': 'split'}
);
```

---

### ts_conformal_calibrate

Calibrates a conformity score from backtest residuals.

**Signature:**
```sql
ts_conformal_calibrate(backtest_results, actual_col, forecast_col, params)
```

**Returns:** `conformity_score`, `coverage`, `n_residuals`

**Example:**
```sql
SELECT * FROM ts_conformal_calibrate(
    'backtest_results',
    actual,
    forecast,
    MAP{'alpha': 0.05}
);
```

---

### ts_conformal_apply

Applies a pre-computed conformity score to forecast results.

**Signature:**
```sql
ts_conformal_apply(forecast_results, group_col, forecast_col, conformity_score)
```

**Example:**
```sql
WITH score AS (
    SELECT conformity_score FROM ts_conformal_calibrate(
        'backtest', actual, forecast, MAP{'alpha': 0.1}
    )
)
SELECT * FROM ts_conformal_apply(
    'future_forecasts',
    product_id,
    forecast,
    (SELECT conformity_score FROM score)
);
```

---

### ts_interval_width

Computes mean interval width for grouped forecast results.

**Signature:**
```sql
ts_interval_width(results, group_col, lower_col, upper_col)
```

**Returns:** `group_col`, `mean_width`, `n_intervals`

**Example:**
```sql
SELECT * FROM ts_interval_width(
    'forecast_results',
    product_id,
    lower,
    upper
);
```

---

## Complete Workflow

```sql
-- Step 1: Generate backtest results
CREATE TABLE backtest AS
SELECT * FROM ts_backtest_auto(
    'sales', product_id, date, value, 7, 5, '1d', MAP{}
);

-- Step 2: Calibrate conformity score
CREATE TABLE calibration AS
SELECT * FROM ts_conformal_calibrate(
    'backtest', actual, forecast, MAP{'alpha': 0.1}
);

-- Step 3: Generate future forecasts
CREATE TABLE future AS
SELECT * FROM ts_forecast_by(
    'sales', product_id, date, value, 'AutoETS', 14, MAP{}
);

-- Step 4: Apply conformal intervals
SELECT * FROM ts_conformal_apply(
    'future',
    product_id,
    forecast,
    (SELECT conformity_score FROM calibration)
);

-- Or use one-step approach
SELECT * FROM ts_conformal(
    'backtest', product_id, actual, forecast, forecast,
    MAP{'alpha': 0.1}
);
```

---

*See also: [Cross-Validation](06-cross-validation.md) | [Evaluation Metrics](07-evaluation-metrics.md)*
