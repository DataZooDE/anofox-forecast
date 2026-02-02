# Evaluation Metrics

> Forecast accuracy and error metrics

## Overview

Metrics functions evaluate forecast accuracy by comparing actual values to predictions.

**Use this document to:**
- Compute standard error metrics: MAE, MSE, RMSE, MAPE, sMAPE, Bias
- Calculate scale-independent metrics: MASE (vs baseline), rMAE (relative)
- Evaluate prediction intervals with coverage and quantile loss
- Compare forecast quality across series or models
- Aggregate metrics per group using table macros

---

## Quick Start

Compute metrics per group using table macros:

```sql
-- MAE per product
SELECT * FROM ts_mae_by(
    (SELECT product_id, date, actual, forecast FROM backtest_results),
    'date', 'actual', 'forecast'
);

-- Multiple metrics at once
SELECT
    m.id,
    m.mae,
    r.rmse
FROM ts_mae_by(
    (SELECT id, date, actual, forecast FROM results),
    'date', 'actual', 'forecast'
) m
JOIN ts_rmse_by(
    (SELECT id, date, actual, forecast FROM results),
    'date', 'actual', 'forecast'
) r USING (id);
```

---

## Table Macros (GROUP BY ALL Pattern)

The `_by` macros use a flexible grouping pattern. User controls grouping by selecting columns in the source subquery - all columns except date/actual/forecast become grouping columns.

### ts_mae_by

Compute Mean Absolute Error grouped by selected columns.

**Signature:**
```sql
ts_mae_by(source, date_col, actual_col, forecast_col) → TABLE(...group_cols, mae)
```

**Parameters:**
- `source` - Table expression (subquery or view) with grouping + metric columns
- `date_col` - VARCHAR column name for date/time ordering
- `actual_col` - VARCHAR column name for actual values
- `forecast_col` - VARCHAR column name for predicted values

**Examples:**
```sql
-- Group by series
SELECT * FROM ts_mae_by(
    (SELECT unique_id, ds, y, forecast FROM results),
    'ds', 'y', 'forecast'
);

-- Group by series, fold, and model
SELECT * FROM ts_mae_by(
    (SELECT unique_id, fold_id, model_name, ds, y, forecast FROM cv_results),
    'ds', 'y', 'forecast'
);

-- Global metric (no grouping)
SELECT * FROM ts_mae_by(
    (SELECT ds, y, forecast FROM results),
    'ds', 'y', 'forecast'
);
```

---

### ts_mse_by, ts_rmse_by, ts_mape_by, ts_smape_by, ts_r2_by, ts_bias_by

Same signature as `ts_mae_by`, returns respective metric.

```sql
ts_mse_by(source, date_col, actual_col, forecast_col) → TABLE(...group_cols, mse)
ts_rmse_by(source, date_col, actual_col, forecast_col) → TABLE(...group_cols, rmse)
ts_mape_by(source, date_col, actual_col, forecast_col) → TABLE(...group_cols, mape)
ts_smape_by(source, date_col, actual_col, forecast_col) → TABLE(...group_cols, smape)
ts_r2_by(source, date_col, actual_col, forecast_col) → TABLE(...group_cols, r2)
ts_bias_by(source, date_col, actual_col, forecast_col) → TABLE(...group_cols, bias)
```

---

### ts_mase_by

Compute Mean Absolute Scaled Error per group (requires baseline).

**Signature:**
```sql
ts_mase_by(source, group_col, date_col, actual_col, forecast_col, baseline_col) → TABLE(id, mase)
```

**Example:**
```sql
SELECT * FROM ts_mase_by('results', product_id, date, actual, forecast, naive_forecast);
```

---

### ts_rmae_by

Compute Relative MAE per group (compares two models).

**Signature:**
```sql
ts_rmae_by(source, group_col, date_col, actual_col, pred1_col, pred2_col) → TABLE(id, rmae)
```

**Example:**
```sql
SELECT * FROM ts_rmae_by('results', product_id, date, actual, ets_forecast, naive_forecast);
-- rmae < 1 means ETS outperforms naive
```

---

### ts_coverage_by

Compute prediction interval coverage per group.

**Signature:**
```sql
ts_coverage_by(source, group_col, date_col, actual_col, lower_col, upper_col) → TABLE(id, coverage)
```

**Example:**
```sql
SELECT * FROM ts_coverage_by('results', product_id, date, actual, lower_90, upper_90);
```

---

### ts_quantile_loss_by

Compute quantile loss per group.

**Signature:**
```sql
ts_quantile_loss_by(source, group_col, date_col, actual_col, forecast_col, quantile) → TABLE(id, quantile_loss)
```

**Example:**
```sql
SELECT * FROM ts_quantile_loss_by('results', product_id, date, actual, forecast_p50, 0.5);
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
SELECT * FROM ts_rmse_by(
    (SELECT unique_id, fold_id, model_name, ds, y, forecast FROM cv_forecasts),
    'ds', 'y', 'forecast'
);
```

---

*See also: [Cross-Validation](08-cross-validation.md) | [Conformal Prediction](11-conformal-prediction.md)*
