# Evaluation Metrics

> Forecast accuracy and error metrics

## Overview

Metrics functions evaluate forecast accuracy by comparing actual values to predictions.

**Use this document to:**
- Compute standard error metrics: MAE, MSE, RMSE, MAPE, sMAPE, Bias
- Calculate scale-independent metrics: MASE (vs baseline), rMAE (relative)
- Evaluate prediction intervals with coverage and quantile loss
- Compare forecast quality across series or models

---

## Quick Start

Compute metrics per group using scalar functions with `GROUP BY`:

```sql
-- MAE per product
SELECT
    product_id,
    ts_mae(LIST(actual ORDER BY date), LIST(forecast ORDER BY date)) AS mae
FROM backtest_results
GROUP BY product_id;

-- Multiple metrics at once
SELECT
    unique_id,
    fold_id,
    ts_mae(LIST(y ORDER BY ds), LIST(forecast ORDER BY ds)) AS mae,
    ts_rmse(LIST(y ORDER BY ds), LIST(forecast ORDER BY ds)) AS rmse,
    ts_mape(LIST(y ORDER BY ds), LIST(forecast ORDER BY ds)) AS mape
FROM cv_results
GROUP BY unique_id, fold_id;
```

---

## Scalar Functions (Recommended)

Use scalar functions with `LIST()` aggregation and `GROUP BY` for best performance. This approach is **~2400x faster** than the deprecated table macros and fully supports parallel execution.

### Pattern

```sql
SELECT
    <group_columns>,
    ts_<metric>(
        LIST(actual_col ORDER BY date_col),
        LIST(forecast_col ORDER BY date_col)
    ) AS <metric_name>
FROM <source>
GROUP BY <group_columns>
```

### Available Scalar Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `ts_mae` | `(LIST, LIST) → DOUBLE` | Mean Absolute Error |
| `ts_mse` | `(LIST, LIST) → DOUBLE` | Mean Squared Error |
| `ts_rmse` | `(LIST, LIST) → DOUBLE` | Root Mean Squared Error |
| `ts_mape` | `(LIST, LIST) → DOUBLE` | Mean Absolute Percentage Error |
| `ts_smape` | `(LIST, LIST) → DOUBLE` | Symmetric MAPE |
| `ts_r2` | `(LIST, LIST) → DOUBLE` | R-squared (coefficient of determination) |
| `ts_bias` | `(LIST, LIST) → DOUBLE` | Bias (mean error) |
| `ts_mase` | `(LIST, LIST, LIST) → DOUBLE` | Mean Absolute Scaled Error (actual, forecast, baseline) |
| `ts_rmae` | `(LIST, LIST, LIST) → DOUBLE` | Relative MAE (actual, pred1, pred2) |
| `ts_coverage` | `(LIST, LIST, LIST) → DOUBLE` | Interval coverage (actual, lower, upper) |
| `ts_quantile_loss` | `(LIST, LIST, DOUBLE) → DOUBLE` | Quantile loss (actual, forecast, quantile) |

---

## Examples

### Basic Metrics by Series

```sql
SELECT
    unique_id,
    ts_mae(LIST(y ORDER BY ds), LIST(forecast ORDER BY ds)) AS mae,
    ts_rmse(LIST(y ORDER BY ds), LIST(forecast ORDER BY ds)) AS rmse
FROM results
GROUP BY unique_id;
```

### Cross-Validation Metrics (by series, fold, model)

```sql
SELECT
    unique_id,
    fold_id,
    model_name,
    ts_mae(LIST(y ORDER BY ds), LIST(forecast ORDER BY ds)) AS mae,
    ts_rmse(LIST(y ORDER BY ds), LIST(forecast ORDER BY ds)) AS rmse,
    ts_mape(LIST(y ORDER BY ds), LIST(forecast ORDER BY ds)) AS mape
FROM cv_forecasts
GROUP BY unique_id, fold_id, model_name;
```

### Global Metric (no grouping)

```sql
SELECT
    ts_mae(LIST(y ORDER BY ds), LIST(forecast ORDER BY ds)) AS mae
FROM results;
```

### MASE with Baseline

```sql
SELECT
    unique_id,
    ts_mase(
        LIST(y ORDER BY ds),
        LIST(forecast ORDER BY ds),
        LIST(naive_forecast ORDER BY ds)
    ) AS mase
FROM results
GROUP BY unique_id;
```

### Relative MAE (Model Comparison)

```sql
SELECT
    unique_id,
    ts_rmae(
        LIST(y ORDER BY ds),
        LIST(ets_forecast ORDER BY ds),
        LIST(naive_forecast ORDER BY ds)
    ) AS rmae
FROM results
GROUP BY unique_id;
-- rmae < 1 means ETS outperforms naive
```

### Prediction Interval Coverage

```sql
SELECT
    unique_id,
    ts_coverage(
        LIST(y ORDER BY ds),
        LIST(yhat_lower ORDER BY ds),
        LIST(yhat_upper ORDER BY ds)
    ) AS coverage_90
FROM results
GROUP BY unique_id;
```

### Quantile Loss

```sql
SELECT
    unique_id,
    ts_quantile_loss(
        LIST(y ORDER BY ds),
        LIST(forecast_p50 ORDER BY ds),
        0.5
    ) AS quantile_loss_50
FROM results
GROUP BY unique_id;
```

---

## Cross-Validation Workflow Example

```sql
-- Step 1: Generate CV folds
CREATE TABLE cv_folds AS
SELECT * FROM ts_cv_folds_by('data', unique_id, ds, y, 3, 12, MAP{});

-- Step 2: Generate forecasts
CREATE TABLE cv_forecasts AS
SELECT * FROM ts_cv_forecast_by('cv_folds', unique_id, ds, y, 'Naive', MAP{});

-- Step 3: Compute metrics per series/fold/model
SELECT
    unique_id,
    fold_id,
    model_name,
    ts_mae(LIST(y ORDER BY ds), LIST(forecast ORDER BY ds)) AS mae,
    ts_rmse(LIST(y ORDER BY ds), LIST(forecast ORDER BY ds)) AS rmse
FROM cv_forecasts
GROUP BY unique_id, fold_id, model_name;
```

---

## Deprecated: Table Macros

> **Warning**: The `_by` table macros (`ts_mae_by`, `ts_rmse_by`, etc.) are deprecated.
> They are ~2400x slower than scalar functions and have threading issues requiring
> `SET threads=1` for correct results. Use scalar functions with `GROUP BY` instead.

The following macros are deprecated and will be removed in a future version:
- `ts_mae_by`, `ts_mse_by`, `ts_rmse_by`, `ts_mape_by`, `ts_smape_by`, `ts_r2_by`, `ts_bias_by`
- `ts_mase_by`, `ts_rmae_by`, `ts_coverage_by`, `ts_quantile_loss_by`

**Migration example:**

```sql
-- DEPRECATED (slow, threading issues)
SELECT * FROM ts_mae_by(
    (SELECT unique_id, ds, y, forecast FROM results),
    'ds', 'y', 'forecast'
);

-- RECOMMENDED (fast, parallel-safe)
SELECT
    unique_id,
    ts_mae(LIST(y ORDER BY ds), LIST(forecast ORDER BY ds)) AS mae
FROM results
GROUP BY unique_id;
```

---

*See also: [Cross-Validation](08-cross-validation.md) | [Conformal Prediction](11-conformal-prediction.md)*
