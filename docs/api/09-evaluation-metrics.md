# Evaluation Metrics

> Forecast accuracy and error metrics

## Overview

Metrics functions evaluate forecast accuracy by comparing actual values to predictions.

**Use this document to:**
- Compute standard error metrics: MAE, MSE, RMSE, MAPE, sMAPE, Bias
- Calculate scale-independent metrics: `ts_mase` (Hyndman-Koehler MASE, scaled by the
  in-sample seasonal-naive MAE), rMAE (relative MAE against a baseline model)
- Evaluate prediction intervals with coverage and quantile loss
- Compare forecast quality across series or models

**NULL handling (v2026.10.16, Phase 12/13, RUST-01/RUST-02, D-07/D-08):** paired metrics
(`ts_mae`, `ts_mse`, `ts_rmse`, `ts_smape`, `ts_r2`, `ts_bias`, `ts_rmae`,
`ts_quantile_loss`, `ts_mqloss`, `ts_coverage`) compute on **pairwise-complete
observations**, computed in Rust from a validity bitmask — a pair (or, for
three-/four-input metrics like `ts_rmae`/`ts_coverage`/`ts_mqloss`, a row) is
dropped the moment ANY of its inputs is NULL at that position; `ts_mae([1,NULL,3,4],
[1,2,NULL,4])` is `0.0` (not the old spliced `0.333`). If no complete pair/row remains,
the result is `NULL`, not an error. Mismatched input-array lengths still return `NULL`.
`ts_mape` follows the SAME pairwise-complete policy for its `(actual, forecast)` pairs,
but ADDITIONALLY returns `NULL` for the whole call if any pairwise-complete `actual` is
exactly `0.0` (D-07) — see the function table below. `ts_mase` has its own two-part policy
(D-08): the `(actual, forecast)` evaluation pair is pairwise-complete exactly like every
other metric, but the `insample` argument is NEVER pairwise-complete-filtered — see
below. `ts_mqloss`'s `levels` argument is a *parameter* list, not data — a NULL quantile
level is a SQL error naming its position, not a missing-data case. Constant/literal list
arguments (e.g. `ts_mae(a, [0,0,0])` broadcast across every row of a table) now give the
correct result on every row, not just the first row of a chunk. The `ts_*_by` table-macro
surface (`_ts_metrics_native`) is pairwise-complete identically to the scalar functions.

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
-- Setup: cv_results(unique_id, fold_id, ds, y, forecast), 2 folds x 20 rows.
CREATE TABLE cv_results AS
SELECT
    'A' AS unique_id,
    fold_id,
    DATE '2024-01-01' + INTERVAL (i) DAY AS ds,
    100.0 + i AS y,
    100.0 + i + 1 AS forecast
FROM range(0, 20) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS fold_id);

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

```text
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
| `ts_mape` | `(LIST, LIST) → DOUBLE` | Mean Absolute Percentage Error — returns `NULL` if any pairwise-complete `actual` is exactly `0.0` (MAPE is undefined there); use `ts_smape` or `ts_mase` for intermittent-demand series where exact zeros are expected |
| `ts_smape` | `(LIST, LIST) → DOUBLE` | Symmetric MAPE |
| `ts_r2` | `(LIST, LIST) → DOUBLE` | R-squared (coefficient of determination) |
| `ts_bias` | `(LIST, LIST) → DOUBLE` | Bias (mean error) |
| `ts_mase` | `(LIST, LIST, LIST, BIGINT) → DOUBLE` | Hyndman-Koehler MASE: `MAE(actual, forecast) / mean(\|insample_t - insample_{t-seasonal_period}\|)` — the out-of-sample forecast MAE scaled by the IN-SAMPLE seasonal-naive MAE at lag `seasonal_period`. `seasonal_period` is a required 4th positional argument (DuckDB scalar functions don't accept `:=` named arguments) because a defaulted value would silently reinterpret an old 3-list call's `baseline` array as `insample`. Returns `NULL` if the in-sample scale is (numerically) zero — MASE is undefined there. The pre-v2026.10.16 3-argument `ts_mase(actual, forecast, baseline)` form, which computed a relative MAE and not MASE, is REMOVED (`ts_mase(a, f, b)` now raises an error naming `ts_rmae`) |
| `ts_rmae` | `(LIST, LIST, LIST) → DOUBLE` | Relative MAE (actual, pred1, pred2) |
| `ts_coverage` | `(LIST, LIST, LIST) → DOUBLE` | Interval coverage (actual, lower, upper) |
| `ts_quantile_loss` | `(LIST, LIST, DOUBLE) → DOUBLE` | Quantile loss (actual, forecast, quantile) |

---

## Examples

Self-contained setup shared by the examples below (200 daily rows per series):

```sql
CREATE TABLE results AS
SELECT
    'A' AS unique_id,
    DATE '2024-01-01' + INTERVAL (i) DAY AS ds,
    100.0 + i AS y,
    100.0 + i + 1 AS forecast,
    100.0 + i + 1 AS ets_forecast,
    100.0 + i + 2 AS naive_forecast,
    100.0 + i - 1.5 AS yhat_lower,
    100.0 + i + 1.5 AS yhat_upper,
    100.0 + i + 0.8 AS forecast_p50
FROM range(0, 200) t(i);
```

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
-- Setup: cv_forecasts_by_model(unique_id, fold_id, model_name, ds, y, forecast),
-- 2 folds x 2 models x 20 rows (illustrative shape, named distinctly from the real
-- `ts_cv_forecast_by` output further down -- see the Cross-Validation Workflow
-- Example below, whose forecast column is named `yhat`).
CREATE TABLE cv_forecasts_by_model AS
SELECT
    'A' AS unique_id, fold_id, model_name,
    DATE '2024-01-01' + INTERVAL (i) DAY AS ds,
    100.0 + i AS y,
    100.0 + i + 1 AS forecast
FROM range(0, 20) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS fold_id)
CROSS JOIN (SELECT unnest(['AutoETS', 'Naive']) AS model_name);

SELECT
    unique_id,
    fold_id,
    model_name,
    ts_mae(LIST(y ORDER BY ds), LIST(forecast ORDER BY ds)) AS mae,
    ts_rmse(LIST(y ORDER BY ds), LIST(forecast ORDER BY ds)) AS rmse,
    ts_mape(LIST(y ORDER BY ds), LIST(forecast ORDER BY ds)) AS mape
FROM cv_forecasts_by_model
GROUP BY unique_id, fold_id, model_name;
```

### Global Metric (no grouping)

```sql
SELECT
    ts_mae(LIST(y ORDER BY ds), LIST(forecast ORDER BY ds)) AS mae
FROM results;
```

### MASE (Hyndman-Koehler)

`ts_mase(actual, forecast, insample, seasonal_period)` is the Hyndman & Koehler
(2006) MASE: the out-of-sample forecast MAE, scaled by the IN-SAMPLE
seasonal-naive MAE at lag `seasonal_period`. Uses the `results` table defined
under [Examples](#examples) above (200 daily rows per series).

```sql
SELECT
    unique_id,
    ts_mase(
        LIST(y ORDER BY ds) FILTER (WHERE ds >= '2024-06-01'),
        LIST(forecast ORDER BY ds) FILTER (WHERE ds >= '2024-06-01'),
        LIST(y ORDER BY ds) FILTER (WHERE ds < '2024-06-01'),
        12
    ) AS mase
FROM results
GROUP BY unique_id;
```

- `actual`/`forecast` are the evaluation pair (pairwise-complete, same policy as
  every other metric).
- `insample` is the training-period values the model was fit on — NOT
  pairwise-complete-filtered with `actual`/`forecast`. The scale is
  `mean(|insample_t - insample_{t-seasonal_period}|)`, computed only over lag
  pairs where BOTH sides are present; a lag pair touching a missing `insample`
  value is skipped, never spliced out and re-paired (position-preserving).
- `seasonal_period` is a REQUIRED 4th positional argument (not `:=`-defaultable
  here — DuckDB scalar functions don't accept named arguments at all; the
  `ts_mase_by` table macro below CAN default it because its argument type
  distinguishes the old form).
- Returns `NULL` when the in-sample scale is (numerically) zero — MASE is
  undefined there (R's `forecast::accuracy()` returns `Inf`; this product
  returns `NULL` instead).

**Removed in v2026.10.16:** the pre-v2026.10.16 3-argument form
`ts_mase(actual, forecast, baseline)` computed `MAE(actual, forecast) /
MAE(actual, baseline)` — a relative MAE against an arbitrary baseline array,
not MASE at all. Calling it now raises an error naming the replacement:

```sql
SELECT ts_mase([1.0, 2.0, 3.0], [1.1, 2.1, 3.1], [1.5, 2.5, 3.5]);
-- Error: ts_mase(actual, forecast, baseline) was removed in v2026.10.16: it computed
-- a relative MAE, not MASE. Use ts_rmae(actual, forecast, baseline) for that
-- result, or ts_mase(actual, forecast, insample, seasonal_period) for
-- Hyndman-Koehler MASE.
```

Use `ts_rmae` (below) for the old relative-MAE comparison against a baseline model.

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
-- Setup: data(unique_id, ds, y), 2 series x 130 daily rows.
CREATE TABLE data AS
SELECT
    unique_id,
    (DATE '2020-01-01' + INTERVAL (i) DAY) AS ds,
    100.0 + unique_id * 10.0 + 0.3 * i + 5.0 * sin(2 * pi() * i / 7.0) AS y
FROM range(0, 130) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS unique_id);

-- Step 1: Generate CV folds
CREATE TABLE cv_folds AS
SELECT * FROM ts_cv_folds_by('data', unique_id, ds, y, 3, 12, MAP{});

-- Step 2: Generate forecasts
CREATE TABLE cv_forecasts AS
SELECT * FROM ts_cv_forecast_by('cv_folds', unique_id, ds, y, 'Naive', MAP{});

-- Step 3: Compute metrics per series/fold/model (ts_cv_forecast_by's forecast column
-- is named yhat, not forecast)
SELECT
    unique_id,
    fold_id,
    model_name,
    ts_mae(LIST(y ORDER BY ds), LIST(yhat ORDER BY ds)) AS mae,
    ts_rmse(LIST(y ORDER BY ds), LIST(yhat ORDER BY ds)) AS rmse
FROM cv_forecasts
GROUP BY unique_id, fold_id, model_name;
```

---

## Deprecated: Table Macros

> **Warning**: The `_by` table macros (`ts_mae_by`, `ts_rmse_by`, etc.) are deprecated.
> They are ~2400x slower than scalar functions. Use scalar functions with `GROUP BY` instead.

Their results do not depend on the row order of `source` or on `source` being a
UNION ALL (#285); no pre-sorting and no `SET threads = 1` are needed.

The following macros are deprecated and will be removed in a future version:
- `ts_mae_by`, `ts_mse_by`, `ts_rmse_by`, `ts_mape_by`, `ts_smape_by`, `ts_r2_by`, `ts_bias_by`
- `ts_mase_by`, `ts_rmae_by`, `ts_coverage_by`, `ts_quantile_loss_by`

**Migration example:**

```sql
-- DEPRECATED (slow)
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

**`ts_mase_by` (D-08, v2026.10.16):** signature is now
`ts_mase_by(source, date_col, actual_col, forecast_col, seasonal_period := 1)`
— the old 5th-argument baseline-column form was removed (see above). `source`
must include each group's HISTORY rows (dated before that group's first
forecast, with `forecast` NULL) alongside its evaluation rows (`forecast`
non-NULL) — the history rows supply the in-sample seasonal-naive scale:

```sql
CREATE TABLE results_with_history AS
SELECT
    unique_id, ds, y,
    CASE WHEN ds >= '2024-06-01' THEN forecast ELSE NULL END AS forecast
FROM results;

SELECT * FROM ts_mase_by(
    (SELECT unique_id, ds, y, forecast FROM results_with_history),
    'ds', 'y', 'forecast', seasonal_period := 12
);
```

A group with no history rows (no dates before its first forecast) or no
forecast row at all returns `NULL` for that group.

---

*See also: [Cross-Validation](08-cross-validation.md) | [Conformal Prediction](11-conformal-prediction.md)*
