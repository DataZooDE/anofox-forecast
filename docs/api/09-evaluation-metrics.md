# Evaluation Metrics

> Forecast accuracy and error metrics

## Overview

Metrics functions evaluate forecast accuracy by comparing actual values to predictions.

**Use this document to:**
- Compute standard error metrics: MAE, MSE, RMSE, MAPE, sMAPE, Bias
- Calculate scale-independent metrics: MASE (vs baseline), rMAE (relative)
- Evaluate prediction intervals with coverage and quantile loss
- Compare forecast quality across series or models
- Aggregate metrics with flexible grouping

---

## Quick Start

Compute metrics with flexible grouping using table macros:

```sql
-- Per-row metrics (each row gets its metric value)
SELECT * FROM ts_rmse_by('forecast_results', ds, y, forecast);

-- Aggregate to get metrics per group
SELECT id, AVG(rmse) AS rmse
FROM ts_rmse_by('forecast_results', ds, y, forecast)
GROUP BY id;

-- For CV results: metrics per series/fold/model
SELECT unique_id, fold_id, model_name, AVG(rmse) AS rmse
FROM ts_rmse_by('cv_results', ds, y, forecast)
GROUP BY unique_id, fold_id, model_name;
```

---

## Table Macros

### ts_mae_by

Compute Mean Absolute Error. Returns all source columns plus the `mae` metric.

**Signature:**
```sql
ts_mae_by(source, date_col, actual_col, forecast_col) → TABLE
```

**Usage:**
The macro computes MAE for each combination of values in the source table using GROUP BY ALL.
Wrap with SELECT and GROUP BY to aggregate to your desired grouping level.

**Examples:**
```sql
-- Per-row MAE (each unique combination of all columns gets its MAE)
SELECT * FROM ts_mae_by('results', ds, y, forecast);

-- Aggregate to get MAE per series
SELECT unique_id, AVG(mae) AS mae
FROM ts_mae_by('results', ds, y, forecast)
GROUP BY unique_id;

-- Aggregate to get overall MAE
SELECT AVG(mae) AS total_mae
FROM ts_mae_by('results', ds, y, forecast);
```

---

### ts_mse_by, ts_rmse_by, ts_mape_by, ts_smape_by, ts_r2_by, ts_bias_by

Same signature as `ts_mae_by`, returns respective metric.

```sql
ts_mse_by(source, date_col, actual_col, forecast_col) → TABLE (mse)
ts_rmse_by(source, date_col, actual_col, forecast_col) → TABLE (rmse)
ts_mape_by(source, date_col, actual_col, forecast_col) → TABLE (mape)
ts_smape_by(source, date_col, actual_col, forecast_col) → TABLE (smape)
ts_r2_by(source, date_col, actual_col, forecast_col) → TABLE (r2)
ts_bias_by(source, date_col, actual_col, forecast_col) → TABLE (bias)
```

---

### ts_mase_by

Compute Mean Absolute Scaled Error (requires baseline).

**Signature:**
```sql
ts_mase_by(source, date_col, actual_col, forecast_col, baseline_col) → TABLE (mase)
```

**Example:**
```sql
SELECT unique_id, AVG(mase) AS mase
FROM ts_mase_by('results', date, actual, forecast, naive_forecast)
GROUP BY unique_id;
```

---

### ts_rmae_by

Compute Relative MAE (compares two models).

**Signature:**
```sql
ts_rmae_by(source, date_col, actual_col, pred1_col, pred2_col) → TABLE (rmae)
```

**Example:**
```sql
SELECT unique_id, AVG(rmae) AS rmae
FROM ts_rmae_by('results', date, actual, ets_forecast, naive_forecast)
GROUP BY unique_id;
-- rmae < 1 means ETS outperforms naive
```

---

### ts_coverage_by

Compute prediction interval coverage.

**Signature:**
```sql
ts_coverage_by(source, date_col, actual_col, lower_col, upper_col) → TABLE (coverage)
```

**Example:**
```sql
SELECT unique_id, AVG(coverage) AS coverage
FROM ts_coverage_by('results', date, actual, lower_90, upper_90)
GROUP BY unique_id;
```

---

### ts_quantile_loss_by

Compute quantile loss (pinball loss).

**Signature:**
```sql
ts_quantile_loss_by(source, date_col, actual_col, forecast_col, quantile) → TABLE (quantile_loss)
```

**Example:**
```sql
SELECT unique_id, AVG(quantile_loss) AS ql
FROM ts_quantile_loss_by('results', date, actual, forecast_p50, 0.5)
GROUP BY unique_id;
```

---

*See also: [Cross-Validation](08-cross-validation.md) | [Conformal Prediction](11-conformal-prediction.md)*
