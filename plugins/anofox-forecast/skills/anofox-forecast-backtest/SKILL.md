---
name: anofox-forecast-backtest
description: >
  Backtesting, cross-validation, evaluation metrics, and conformal
  prediction intervals for the anofox_forecast DuckDB extension.
  Use when evaluating forecast accuracy, comparing models with
  time-series-aware CV, computing metrics (MAE / RMSE / MAPE /
  MASE / coverage), or attaching distribution-free prediction
  intervals to forecasts.
version: 2026.09.26
user-invocable: false
---

# Anofox Forecast — Backtesting, CV, Metrics & Conformal Cheat Sheet

**Extension:** `anofox_forecast` v2026.09.26 (Rust crate `anofox-forecast` v0.16.2) | **DuckDB:** v1.4.5 LTS / v1.5.4+ | **Dual naming:** `ts_*` and `anofox_fcst_ts_*`

Time-series-aware cross-validation, error metrics, and distribution-free intervals.

## Critical gotchas

- **`ts_backtest_auto_by` was REMOVED**, along with the orphaned internal `_ts_backtest_native` table function that backed it (Phase 12 D-07). Use the two-step CV workflow (`ts_cv_folds_by` → `ts_cv_forecast_by`) instead.
- **Metric `_by` table macros are deprecated.** They're ~2400× slower than the scalar + `GROUP BY` pattern and don't parallelise. Use scalars. The full deprecated family: `ts_mae_by`, `ts_rmse_by`, `ts_mse_by`, `ts_mape_by`, `ts_smape_by`, `ts_bias_by`, `ts_mase_by`, `ts_rmae_by`, `ts_r2_by`, `ts_coverage_by`, `ts_quantile_loss_by`, `ts_interval_width_by`. Each maps to a scalar (`ts_mae`, `ts_rmse`, … below) called under `GROUP BY`. Two helpers are exceptions: `ts_interval_width_by` is a table macro `(results, group_col, lower_col, upper_col)` and `ts_mean_interval_width(DOUBLE[] lower, DOUBLE[] upper) → DOUBLE` is a **scalar** — still prefer the scalar + `GROUP BY` pattern over the `_by` table macro.
- **Always `ORDER BY` inside `LIST()`** for temporal correctness: `LIST(y ORDER BY ds)`, not `LIST(y)`.
- **`ts_cv_forecast_by` output renames the target column to `y`** (canonical). Don't try to access the original name.
- **Folds must be pre-computed before forecasting.** Passing raw data to `ts_cv_forecast_by` throws a clear error.
- **Metrics use pairwise-complete NULL handling (v0.10.0, Phase 12).** `ts_mae`/`ts_mse`/`ts_rmse`/`ts_mape`/`ts_smape`/`ts_r2`/`ts_bias`/`ts_rmae`/`ts_quantile_loss`/`ts_mqloss`/`ts_coverage` drop a pair (or row, for 3+ input metrics) the moment ANY input is NULL at that position, computed in Rust — not spliced independently per array in C++. No complete pair/row → `NULL`, not an error. `ts_mape` additionally returns `NULL` for the whole call if any pairwise-complete `actual` is exactly `0.0` (D-07) — use `ts_smape`/`ts_mase` for intermittent demand. `ts_mase`'s `insample` argument has its OWN policy (never pairwise-complete-filtered — see below), not this one.

## The CV two-step workflow (standard)

```sql
-- Setup: data(unique_id, ds, y), 2 series x 130 daily rows (shared by every example
-- below in this file).
CREATE OR REPLACE TABLE data AS
SELECT
    unique_id,
    (DATE '2020-01-01' + INTERVAL (i) DAY) AS ds,
    100.0 + unique_id * 10.0 + 0.3 * i + 5.0 * sin(2 * pi() * i / 7.0) AS y
FROM range(0, 130) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS unique_id);
```

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
-- Setup: features(unique_id, ds, promo, holiday), the raw source table that carries
-- the exogenous columns the folds table itself lost.
CREATE OR REPLACE TABLE features AS
SELECT unique_id, ds,
       (CAST(ds AS DATE) - DATE '2020-01-01') % 14 = 0 AS promo,
       (CAST(ds AS DATE) - DATE '2020-01-01') % 30 = 0 AS holiday
FROM data;

-- Signature is (cv_folds, source, group_col, date_col, unknown_features, params := MAP{})
CREATE OR REPLACE TABLE cv_folds_with_features AS
SELECT * FROM ts_cv_hydrate_by('cv_folds', 'features', unique_id, ds, ['promo', 'holiday']);
```

## `ts_check_leakage`

Sanity-check that no test-window rows leaked into training. Signature is
`(source, is_test_col, params)` — `source` is a table name (not a subquery
string), and `is_test_col` is a boolean column on that table, not a list of
positional group/date/fold columns:

```sql
CREATE OR REPLACE TABLE cv_folds_flagged AS
SELECT *, (split = 'test') AS is_test FROM cv_folds;

SELECT * FROM ts_check_leakage('cv_folds_flagged', is_test, MAP{});
```

## Metrics — use scalar functions

Pattern: `SELECT group, ts_<metric>(LIST(actual ORDER BY ds), LIST(pred ORDER BY ds)) FROM ... GROUP BY group;`

| Function | Signature | Description |
|---|---|---|
| `ts_mae` | `(DOUBLE[], DOUBLE[]) → DOUBLE` | Mean Absolute Error |
| `ts_mse` | `(DOUBLE[], DOUBLE[]) → DOUBLE` | Mean Squared Error |
| `ts_rmse` | `(DOUBLE[], DOUBLE[]) → DOUBLE` | Root MSE |
| `ts_mape` | `(DOUBLE[], DOUBLE[]) → DOUBLE` | Mean Absolute % Error — returns `NULL` if ANY pairwise-complete `actual` is exactly `0.0` (D-07; MAPE is undefined there) — use `ts_smape`/`ts_mase` for intermittent demand |
| `ts_smape` | `(DOUBLE[], DOUBLE[]) → DOUBLE` | Symmetric MAPE |
| `ts_r2` | `(DOUBLE[], DOUBLE[]) → DOUBLE` | R² |
| `ts_bias` | `(DOUBLE[], DOUBLE[]) → DOUBLE` | Bias (mean error) |
| `ts_mase` | `(actual, forecast, insample, seasonal_period) → DOUBLE` | **Hyndman-Koehler MASE** (D-08): `MAE(actual,forecast) / mean(\|insample_t - insample_{t-seasonal_period}\|)`. `seasonal_period` is a REQUIRED 4th positional arg (no `:=` on scalar functions). `insample` lag pairs touching a missing value are skipped, never spliced. Scale numerically zero → `NULL`. The pre-v0.10.0 3-arg `(actual, forecast, baseline)` form — a relative MAE, not MASE — is REMOVED; it errors naming `ts_rmae`. `ts_mase_by(source, date_col, actual_col, forecast_col, seasonal_period := 1)` needs `source` to include each group's HISTORY rows (forecast NULL, dated before the first forecast) to supply the in-sample scale — the old baseline-column 5th argument is also removed, naming `ts_rmae_by`. |
| `ts_rmae` | `(actual, pred1, pred2) → DOUBLE` | Relative MAE. < 1 = pred1 better |
| `ts_coverage` | `(actual, lower, upper) → DOUBLE` | Interval coverage rate |
| `ts_quantile_loss` | `(actual, forecast, q) → DOUBLE` | Quantile loss at level q |
| `ts_mqloss` | `(actual, quantiles[], levels[]) → DOUBLE` | Multi-quantile CRPS-adjacent |

### Model comparison across CV folds

```sql
-- Fit two models
CREATE OR REPLACE TABLE cv_naive   AS
SELECT * FROM ts_cv_forecast_by('cv_folds', unique_id, ds, y, 'Naive',   MAP{});
CREATE OR REPLACE TABLE cv_autoets AS
SELECT * FROM ts_cv_forecast_by('cv_folds', unique_id, ds, y, 'AutoETS', MAP{'seasonal_period': '7'});

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

Attach a distribution-free prediction interval to forecasts, calibrated from backtest
residuals, horizon-ordered and calibrated per group by default (Phase 16). Coverage is
guaranteed under exchangeability AND a finite-sample minimum calibration-set size for the
requested alpha — see `docs/api/11-conformal-prediction.md#coverage-guarantee-and-its-limits`.

### Three flavours

| Approach | Functions | Use when |
|---|---|---|
| **One-step** | `ts_conformal_by` | You already have backtest results, want intervals now |
| **Modular** | `ts_conformal_calibrate` + `ts_conformal_apply_by` | Reuse calibration across multiple forecast rounds |
| **Array-based** | `ts_conformal_predict`, `ts_conformal_quantile`, etc. | Custom pipelines over `LIST(...)` arrays |

### `ts_conformal_by` (one-step)

```sql
ts_conformal_by(backtest_results VARCHAR, group_col COLUMN, date_col COLUMN,
                actual_col COLUMN, forecast_col COLUMN, point_forecast_col COLUMN,
                params := MAP{}, difficulty_col := NULL) → TABLE
```

Returns one row per `(group_col, date_col)`. `date_col` is required and orders/pairs every
row — forecasts are never value-sorted. Params:

| Key | Default | Description |
|---|---|---|
| `alpha` | 0.1 | Miscoverage rate (0.1 = 90 %, 0.05 = 95 %) |
| `method` | `'symmetric'` | `'symmetric'` (alias `'split'`), `'asymmetric'` (skewed residuals), or `'adaptive'` (needs `difficulty_col`) |

`coverage` on the output is the EMPIRICAL fraction of calibration residuals covered, not
simply `1 - alpha`; a separate `nominal_level` column reports the requested level.
`calibration_insufficient = true` (with `NULL` bounds/score) when the calibration set is
below the alpha's minimum size (9 for alpha=0.1 symmetric, 19 asymmetric) — never clamped.

```sql
CREATE OR REPLACE TABLE conformal_bounds AS
SELECT * FROM ts_conformal_by('backtest', unique_id, ds, y, yhat, yhat, params := {'alpha': 0.1});
```

### Modular (`ts_conformal_calibrate` → `ts_conformal_apply_by`)

```sql
-- Compute calibration once -- one row PER GROUP by default (D-03); pool via
-- params := {'pooled': true}
CREATE OR REPLACE TABLE calib AS
SELECT * FROM ts_conformal_calibrate('backtest', unique_id, y, yhat, params := {'alpha': 0.1});

-- Apply to any future forecast -- `calib` is a calibration TABLE name (D-14), not a bare
-- score; each forecast group matches its own calibration row (falling back to a pooled
-- row with group_col IS NULL, if present)
SELECT * FROM ts_conformal_apply_by('new_forecasts', unique_id, ds, yhat, 'calib');
```

### Array-based helpers

- `ts_conformal_predict`, `ts_conformal_predict_asymmetric`, `ts_conformal_predict_per_step`
- `ts_conformal_quantile`, `ts_conformal_intervals`
- `ts_conformal_learn`, `ts_conformal_apply` — strategies are `'split'`/`'crossval'` only;
  `'jackknife+'` (and every alias) was removed in Phase 16 — it computed the identical
  quantile to `'split'`, with no actual leave-one-out refits behind the name
- `ts_conformal_coverage`, `ts_conformal_evaluate`

Use these when composing custom pipelines over `LIST(residual)` arrays.

**NULL policy (gotcha):** residual pools drop `NULL`/`NaN` entries; forecast positions
(`ts_conformal_intervals`/`predict(_asymmetric)`/`apply`/`predict_per_step`, both bootstrap
functions) are position-preserving (`NULL` in → `NULL` at that position out, never a splice);
`ts_conformal_predict_per_step` errors in Rust if the flat residual count isn't an exact
multiple of `horizon`, or if `point_forecasts` length != `horizon` (`try(...)` to downgrade
to `NULL`) — see `docs/api/11-conformal-prediction.md#null-handling`.

## Bootstrap prediction intervals

Residual-bootstrap alternative to conformal — resample forecast residuals to
build empirical intervals / quantiles around point forecasts. Both are scalar
functions over `DOUBLE[]` arrays with a fixed seed for reproducibility. A right-skewed
residual correctly puts the long tail on the UPPER side (Phase 16 fix, D-10).

### `ts_bootstrap_intervals` — coverage-band intervals

```sql
ts_bootstrap_intervals(residuals DOUBLE[], forecasts DOUBLE[],
                       n_paths INTEGER, coverage DOUBLE, seed BIGINT)
    → STRUCT(point DOUBLE[], lower DOUBLE[], upper DOUBLE[], coverage DOUBLE)
```

`n_paths` must be `>= 10` and `n_paths * len(forecasts) <= 10,000,000`; `coverage` must be
in `(0, 1)` — each is a caller error naming the value, never silently adjusted.

```sql
SELECT ts_bootstrap_intervals(
    [0.2, -0.3, 0.1, -0.2, 0.15, -0.1, 0.05, -0.05],  -- residuals
    [10.0, 11, 10.5, 12, 11.5, 13, 12.5, 14],         -- point forecasts (7+ steps)
    1000, 0.90, 42) AS r;
```

### `ts_bootstrap_quantiles` — arbitrary quantile levels

```sql
ts_bootstrap_quantiles(residuals DOUBLE[], forecasts DOUBLE[],
                       n_paths INTEGER, quantiles DOUBLE[], seed BIGINT)
    → STRUCT(point DOUBLE[], quantiles DOUBLE[], "values" DOUBLE[])
```

`values` is flattened `quantiles × horizon` (one block of horizon values per
requested quantile level).

```sql
SELECT ts_bootstrap_quantiles(
    [0.2, -0.3, 0.1, -0.2, 0.15, -0.1, 0.05, -0.05],
    [10.0, 11, 10.5, 12, 11.5, 13, 12.5, 14],
    1000, [0.1, 0.5, 0.9], 42) AS r;
```

## `ts_estimate_backtest_memory`

Pre-flight a CV run's memory footprint (scalar function: `(n_series, n_dates, folds, horizon) → DOUBLE`, estimated bytes):

```sql
SELECT ts_estimate_backtest_memory(1000, 365, 5, 14) AS estimated_bytes;
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

-- 4. Conformalise per product -> targets 90% nominal coverage (empirical coverage and
--    calibration_insufficient are reported alongside, see docs/api/11-conformal-prediction.md)
CREATE OR REPLACE TABLE calib AS
SELECT * FROM ts_conformal_calibrate('bt', product_id, y, yhat, params := {'alpha': 0.1});

-- 5. Apply calibration (a TABLE, not a bare score) to new forecasts
CREATE OR REPLACE TABLE new_fcst AS
SELECT * FROM ts_forecast_by('clean', product_id, ds, y, 'AutoETS', 14, '1d',
    MAP{'seasonal_period': '7'});

SELECT * FROM ts_conformal_apply_by('new_fcst', product_id, ds, yhat, 'calib');
```

See also: `anofox-forecast-data-prep` (clean input required by `ts_cv_folds_by`), `anofox-forecast-detection` (detect `seasonal_period` before backtesting), `anofox-forecast-models` (any model string usable in `ts_cv_forecast_by`).

Reference docs:
- `docs/api/08-cross-validation.md`
- `docs/api/09-evaluation-metrics.md`
- `docs/api/11-conformal-prediction.md`
