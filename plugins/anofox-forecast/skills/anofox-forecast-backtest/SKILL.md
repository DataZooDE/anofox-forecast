---
name: anofox-forecast-backtest
description: >
  Backtesting, cross-validation, evaluation metrics, and conformal
  prediction intervals for the anofox_forecast DuckDB extension.
  Use when evaluating forecast accuracy, comparing models with
  time-series-aware CV, computing metrics (MAE / RMSE / MAPE /
  MASE / coverage), or attaching distribution-free prediction
  intervals to forecasts.
version: 0.15.3
user-invocable: false
---

# Anofox Forecast — Backtesting, CV, Metrics & Conformal Cheat Sheet

**Extension:** `anofox_forecast` v0.15.3 | **DuckDB:** v1.4.5 LTS / v1.5.4+ | **Dual naming:** `ts_*` and `anofox_fcst_ts_*`

Time-series-aware cross-validation, error metrics, and distribution-free intervals.

## Critical gotchas

- **`ts_backtest_auto_by` was REMOVED.** Use the two-step CV workflow (`ts_cv_folds_by` → `ts_cv_forecast_by`) instead. Older docs and tests may still reference the retired one-liner.
- **Metric `_by` table macros (`ts_mae_by`, `ts_rmse_by`, …) are deprecated.** They're ~2400× slower than the scalar + `GROUP BY` pattern and don't parallelise. Use scalars.
- **Always `ORDER BY` inside `LIST()`** for temporal correctness: `LIST(y ORDER BY ds)`, not `LIST(y)`.
- **`ts_cv_forecast_by` output renames the target column to `y`** (canonical). Don't try to access the original name.
- **Folds must be pre-computed before forecasting.** Passing raw data to `ts_cv_forecast_by` throws a clear error.

## The CV two-step workflow (standard)

```sql
-- Step 1: Create fold table (train/test rows with actual dates)
CREATE OR REPLACE TABLE cv_folds AS
SELECT * FROM ts_cv_folds_by('data', unique_id, ds, y,
    3,                  -- n_folds
    12,                 -- horizon per fold
    MAP{});             -- optional params

-- Step 2: Forecast per fold's train set, predict its test set
CREATE OR REPLACE TABLE cv_forecasts AS
SELECT * FROM ts_cv_forecast_by('cv_folds', unique_id, ds, y,
    'AutoETS',
    MAP{'seasonal_period': '12'});

-- Step 3: Compute per-series / per-fold metrics
SELECT unique_id, fold_id,
    ts_rmse(LIST(y ORDER BY ds), LIST(yhat ORDER BY ds)) AS rmse,
    ts_mae(LIST(y ORDER BY ds), LIST(yhat ORDER BY ds))  AS mae
FROM cv_forecasts
GROUP BY unique_id, fold_id;
```

## `ts_cv_folds_by`

```sql
ts_cv_folds_by(source VARCHAR, group_col COLUMN, date_col COLUMN, target_col COLUMN,
               n_folds BIGINT, horizon BIGINT, params MAP/STRUCT) → TABLE
```

Input must be pre-cleaned (no gaps, consistent frequency). Uses position-based indexing so works with all frequencies (including calendar-based monthly / quarterly / yearly).

Params:

| Key | Default | Description |
|---|---|---|
| `gap` | 0 | Periods between train end and test start |
| `embargo` | 0 | Periods excluded from training after previous test |
| `window_type` | `'expanding'` | `'expanding'`, `'fixed'`, or `'sliding'` |
| `min_train_size` | 1 | Min training size (fixed / sliding only) |
| `initial_train_size` | auto | Periods before first fold |
| `skip_length` | horizon | Periods between folds (1 = dense overlap) |
| `clip_horizon` | false | Allow partial test windows near series end |

Output: 5 columns — `<group_col>`, `<date_col>`, `<target_col>`, `fold_id BIGINT`, `split VARCHAR` (`'train'` / `'test'`). Features NOT passed through — use `ts_cv_hydrate_by` to join extra columns.

## `ts_cv_split_by` / `ts_cv_split_folds_by` / `ts_cv_split_index_by`

Alternate fold-creation entry points for custom cutoffs, pre-computed fold boundaries, or memory-efficient index-only splits. See `docs/api/08-cross-validation.md` for their param signatures.

## `ts_cv_forecast_by`

```sql
ts_cv_forecast_by(cv_folds_table VARCHAR, group_col COLUMN, date_col COLUMN, target_col COLUMN,
                  method VARCHAR, params MAP/STRUCT) → TABLE
```

Fits `method` on each fold's train partition, forecasts the test partition.

Output columns: `fold_id`, `<group_col>`, `<date_col>`, `y` (renamed from target_col), `split`, `yhat`, `yhat_lower`, `yhat_upper`, `model_name`.

Params: same shape as `ts_forecast_by`. All model strings work (`'AutoETS'`, `'Laplace'`, `'HoltWinters'`, etc.). See `anofox-forecast-models` for the full catalogue.

## `ts_cv_hydrate_by` — attach features to fold rows

If your model needs exogenous columns (features, calendar flags, promotions), the folds table lost them. Join them back:

```sql
CREATE OR REPLACE TABLE cv_folds_with_features AS
SELECT * FROM ts_cv_hydrate_by('cv_folds', unique_id, ds, y, 'features', ['promo', 'holiday']);
```

## `ts_check_leakage`

Sanity-check that no test-window rows leaked into training:

```sql
SELECT * FROM ts_check_leakage('cv_folds', unique_id, ds, fold_id, split);
```

## Metrics — use scalar functions

Pattern: `SELECT group, ts_<metric>(LIST(actual ORDER BY ds), LIST(pred ORDER BY ds)) FROM ... GROUP BY group;`

| Function | Signature | Description |
|---|---|---|
| `ts_mae` | `(DOUBLE[], DOUBLE[]) → DOUBLE` | Mean Absolute Error |
| `ts_mse` | `(DOUBLE[], DOUBLE[]) → DOUBLE` | Mean Squared Error |
| `ts_rmse` | `(DOUBLE[], DOUBLE[]) → DOUBLE` | Root MSE |
| `ts_mape` | `(DOUBLE[], DOUBLE[]) → DOUBLE` | Mean Absolute % Error |
| `ts_smape` | `(DOUBLE[], DOUBLE[]) → DOUBLE` | Symmetric MAPE |
| `ts_r2` | `(DOUBLE[], DOUBLE[]) → DOUBLE` | R² |
| `ts_bias` | `(DOUBLE[], DOUBLE[]) → DOUBLE` | Bias (mean error) |
| `ts_mase` | `(actual, forecast, baseline) → DOUBLE` | Scaled by seasonal naive |
| `ts_rmae` | `(actual, pred1, pred2) → DOUBLE` | Relative MAE. < 1 = pred1 better |
| `ts_coverage` | `(actual, lower, upper) → DOUBLE` | Interval coverage rate |
| `ts_quantile_loss` | `(actual, forecast, q) → DOUBLE` | Quantile loss at level q |
| `ts_mqloss` | `(actual, quantiles[], levels[]) → DOUBLE` | Multi-quantile CRPS-adjacent |

### Model comparison across CV folds

```sql
-- Fit two models
CREATE OR REPLACE TABLE cv_naive   AS
SELECT * FROM ts_cv_forecast_by('cv_folds', id, ds, y, 'Naive',   MAP{});
CREATE OR REPLACE TABLE cv_autoets AS
SELECT * FROM ts_cv_forecast_by('cv_folds', id, ds, y, 'AutoETS', MAP{'seasonal_period': '7'});

-- Compare
SELECT 'Naive' AS model,
    ts_mae(LIST(y ORDER BY ds), LIST(yhat ORDER BY ds)) AS mae
FROM cv_naive
UNION ALL
SELECT 'AutoETS',
    ts_mae(LIST(y ORDER BY ds), LIST(yhat ORDER BY ds))
FROM cv_autoets
ORDER BY mae;
```

## Conformal prediction — distribution-free intervals

Attach a coverage-guaranteed prediction interval to forecasts, calibrated from backtest residuals.

### Three flavours

| Approach | Functions | Use when |
|---|---|---|
| **One-step** | `ts_conformal_by` | You already have backtest results, want intervals now |
| **Modular** | `ts_conformal_calibrate` + `ts_conformal_apply_by` | Reuse calibration across multiple forecast rounds |
| **Array-based** | `ts_conformal_predict`, `ts_conformal_quantile`, etc. | Custom pipelines over `LIST(...)` arrays |

### `ts_conformal_by` (one-step)

```sql
ts_conformal_by(backtest_results VARCHAR, group_col COLUMN, actual_col COLUMN,
                forecast_col COLUMN, point_forecast_col COLUMN, params STRUCT) → TABLE
```

Params:

| Key | Default | Description |
|---|---|---|
| `alpha` | 0.1 | Miscoverage rate (0.1 = 90 %, 0.05 = 95 %) |
| `method` | `'split'` | `'split'` (symmetric) or `'asymmetric'` (skewed residuals) |

```sql
CREATE OR REPLACE TABLE conformal_bounds AS
SELECT * FROM ts_conformal_by('backtest', unique_id, y, yhat, yhat, {alpha: 0.1});
```

### Modular (`ts_conformal_calibrate` → `ts_conformal_apply_by`)

```sql
-- Compute calibration once
CREATE OR REPLACE TABLE calib AS
SELECT * FROM ts_conformal_calibrate('backtest', y, yhat, {alpha: 0.1});

-- Apply to any future forecast
SELECT * FROM ts_conformal_apply_by('new_forecasts', unique_id, yhat,
    (SELECT conformity_score FROM calib));
```

### Array-based helpers

- `ts_conformal_predict`, `ts_conformal_predict_asymmetric`, `ts_conformal_predict_per_step`
- `ts_conformal_quantile`, `ts_conformal_intervals`
- `ts_conformal_learn`, `ts_conformal_apply`
- `ts_conformal_coverage`, `ts_conformal_evaluate`

Use these when composing custom pipelines over `LIST(residual)` arrays.

## `ts_estimate_backtest_memory`

Pre-flight a CV run's memory footprint:

```sql
SELECT * FROM ts_estimate_backtest_memory(n_series, avg_length, n_folds, horizon);
```

## Full backtest → conformal pipeline

```sql
-- 1. CV folds
CREATE OR REPLACE TABLE folds AS
SELECT * FROM ts_cv_folds_by('clean', product_id, ds, y, 5, 14, MAP{});

-- 2. Model forecast per fold
CREATE OR REPLACE TABLE bt AS
SELECT * FROM ts_cv_forecast_by('folds', product_id, ds, y, 'AutoETS',
    MAP{'seasonal_period': '7'});

-- 3. Per-series metrics
SELECT product_id,
    ts_mae(LIST(y ORDER BY ds), LIST(yhat ORDER BY ds)) AS mae,
    ts_rmse(LIST(y ORDER BY ds), LIST(yhat ORDER BY ds)) AS rmse,
    ts_coverage(LIST(y ORDER BY ds), LIST(yhat_lower ORDER BY ds), LIST(yhat_upper ORDER BY ds)) AS cov
FROM bt GROUP BY product_id;

-- 4. Conformalise → guaranteed 90 % coverage
CREATE OR REPLACE TABLE calib AS
SELECT * FROM ts_conformal_calibrate('bt', y, yhat, {alpha: 0.1});

-- 5. Apply calibration to new forecasts
CREATE OR REPLACE TABLE new_fcst AS
SELECT * FROM ts_forecast_by('clean', product_id, ds, y, 'AutoETS', 14, '1d',
    MAP{'seasonal_period': '7'});

SELECT * FROM ts_conformal_apply_by('new_fcst', product_id, yhat,
    (SELECT conformity_score FROM calib));
```

See also: `anofox-forecast-data-prep` (clean input required by `ts_cv_folds_by`), `anofox-forecast-detection` (detect `seasonal_period` before backtesting), `anofox-forecast-models` (any model string usable in `ts_cv_forecast_by`).

Reference docs:
- `docs/api/08-cross-validation.md`
- `docs/api/09-evaluation-metrics.md`
- `docs/api/11-conformal-prediction.md`
