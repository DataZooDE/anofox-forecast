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
SELECT * FROM ts_mae_by('backtest_results', product_id, date, actual, forecast);

-- Multiple metrics at once
SELECT
    m.id,
    m.mae,
    r.rmse,
    c.coverage
FROM ts_mae_by('results', id, date, actual, forecast) m
JOIN ts_rmse_by('results', id, date, actual, forecast) r ON m.id = r.id
JOIN ts_coverage_by('results', id, date, actual, lower_90, upper_90) c ON m.id = c.id;
```

---

## Table Macros

### ts_mae_by

Compute Mean Absolute Error per group.

**Signature:**
```sql
ts_mae_by(source, group_col, date_col, actual_col, forecast_col) → TABLE(id, mae)
```

**Example:**
```sql
SELECT * FROM ts_mae_by('backtest_results', product_id, date, actual, forecast);
```

---

### ts_mse_by, ts_rmse_by, ts_mape_by, ts_smape_by, ts_r2_by, ts_bias_by

Same signature as `ts_mae_by`, returns respective metric.

```sql
ts_mse_by(source, group_col, date_col, actual_col, forecast_col) → TABLE(id, mse)
ts_rmse_by(source, group_col, date_col, actual_col, forecast_col) → TABLE(id, rmse)
ts_mape_by(source, group_col, date_col, actual_col, forecast_col) → TABLE(id, mape)
ts_smape_by(source, group_col, date_col, actual_col, forecast_col) → TABLE(id, smape)
ts_r2_by(source, group_col, date_col, actual_col, forecast_col) → TABLE(id, r2)
ts_bias_by(source, group_col, date_col, actual_col, forecast_col) → TABLE(id, bias)
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

*See also: [Cross-Validation](08-cross-validation.md) | [Conformal Prediction](11-conformal-prediction.md)*
