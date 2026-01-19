# Evaluation Metrics

> Forecast accuracy and error metrics

## Overview

Metrics functions evaluate forecast accuracy by comparing actual values to predictions. Available as both scalar functions and table macros.

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

## Scalar Functions

> Advanced usage with `LIST()` aggregation.

### Error Metrics

### ts_mae

Mean Absolute Error.

**Signature:**
```sql
ts_mae(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE
```

**Formula:** MAE = Σ|y - ŷ| / n

**Example:**
```sql
SELECT ts_mae([1.0, 2.0, 3.0], [1.1, 2.1, 3.1]);
-- Returns: 0.1
```

---

### ts_mse

Mean Squared Error.

**Signature:**
```sql
ts_mse(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE
```

**Formula:** MSE = Σ(y - ŷ)² / n

---

### ts_rmse

Root Mean Squared Error.

**Signature:**
```sql
ts_rmse(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE
```

**Formula:** RMSE = √(MSE)

---

### ts_mape

Mean Absolute Percentage Error.

**Signature:**
```sql
ts_mape(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE
```

**Formula:** MAPE = (100/n) × Σ|y - ŷ| / |y|

> **Warning:** Returns NULL if any actual value is zero.

---

### ts_smape

Symmetric Mean Absolute Percentage Error.

**Signature:**
```sql
ts_smape(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE
```

**Formula:** sMAPE = (200/n) × Σ|y - ŷ| / (|y| + |ŷ|)

**Range:** [0, 200]

---

## Comparative Metrics

### ts_mase

Mean Absolute Scaled Error - compares forecast accuracy against a baseline.

**Signature:**
```sql
ts_mase(actual DOUBLE[], predicted DOUBLE[], baseline DOUBLE[]) → DOUBLE
```

**Parameters:**
- `actual`: Actual observed values
- `predicted`: Predicted/forecasted values
- `baseline`: Baseline forecast (e.g., naive or seasonal naive)

**Formula:** MASE = MAE(actual, predicted) / MAE(actual, baseline)

**Example:**
```sql
SELECT ts_mase(
    [100, 110, 120, 130]::DOUBLE[],  -- actual
    [102, 108, 122, 128]::DOUBLE[],  -- model forecast
    [100, 100, 110, 120]::DOUBLE[]   -- naive baseline
);
```

---

### ts_rmae

Relative MAE - compares two model forecasts.

**Signature:**
```sql
ts_rmae(actual DOUBLE[], pred1 DOUBLE[], pred2 DOUBLE[]) → DOUBLE
```

**Formula:** rMAE = MAE(actual, pred1) / MAE(actual, pred2)

**Interpretation:**
- rMAE < 1: First model is better
- rMAE = 1: Models are equally accurate
- rMAE > 1: Second model is better

**Example:**
```sql
SELECT ts_rmae(
    [100, 110, 120]::DOUBLE[],  -- actual
    [102, 108, 122]::DOUBLE[],  -- ETS forecast
    [100, 100, 110]::DOUBLE[]   -- naive forecast
);
-- Returns < 1 if ETS outperforms naive
```

---

## Fit Statistics

### ts_r2

R-squared (Coefficient of Determination).

**Signature:**
```sql
ts_r2(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE
```

**Formula:** R² = 1 - (SS_res / SS_tot)

**Range:** (-∞, 1]

---

### ts_bias

Forecast Bias.

**Signature:**
```sql
ts_bias(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE
```

**Formula:** Bias = Σ(ŷ - y) / n

**Interpretation:**
- Positive = over-forecasting
- Negative = under-forecasting

---

## Quantile Metrics

### ts_quantile_loss

Pinball loss for a single quantile.

**Signature:**
```sql
ts_quantile_loss(actual DOUBLE[], predicted DOUBLE[], quantile DOUBLE) → DOUBLE
```

**Parameters:**
- `quantile`: Quantile level (0 < q < 1)

---

### ts_mqloss

Mean Quantile Loss across multiple quantile levels.

**Signature:**
```sql
ts_mqloss(actual DOUBLE[], quantiles DOUBLE[][], levels DOUBLE[]) → DOUBLE
```

**Parameters:**
- `actual`: Array of actual values
- `quantiles`: 2D array where each sub-array is a quantile forecast
- `levels`: Array of quantile levels (e.g., [0.1, 0.5, 0.9])

**Example:**
```sql
SELECT ts_mqloss(
    [100.0, 110.0, 105.0],
    [
        [95.0, 100.0, 98.0],     -- 10th percentile
        [100.0, 108.0, 102.0],   -- 50th percentile
        [105.0, 115.0, 110.0]    -- 90th percentile
    ],
    [0.1, 0.5, 0.9]
) AS mqloss;
```

---

## Interval Metrics

### ts_coverage

Prediction Interval Coverage.

**Signature:**
```sql
ts_coverage(actual DOUBLE[], lower DOUBLE[], upper DOUBLE[]) → DOUBLE
```

**Formula:** Coverage = (Count of actuals within [lower, upper]) / n

**Range:** [0, 1]

**Example:**
```sql
SELECT ts_coverage(
    [10.0, 20.0, 30.0],
    [8.0, 18.0, 28.0],
    [12.0, 22.0, 32.0]
);
-- Returns: 1.0 (all values within bounds)
```

---

## Using Scalar Functions with Tables

For custom aggregation patterns, use scalar functions with `LIST()`:

```sql
-- Compute metrics per series using scalar functions
SELECT
    product_id,
    ts_mae(LIST(actual ORDER BY date), LIST(forecast ORDER BY date)) AS mae,
    ts_rmse(LIST(actual ORDER BY date), LIST(forecast ORDER BY date)) AS rmse
FROM backtest_results
GROUP BY product_id;

-- Or use table macros (simpler syntax)
SELECT * FROM ts_mae_by('backtest_results', product_id, date, actual, forecast);
SELECT * FROM ts_rmse_by('backtest_results', product_id, date, actual, forecast);

-- Overall metrics (no grouping)
SELECT
    ts_mae(LIST(actual), LIST(forecast)) AS overall_mae,
    ts_coverage(LIST(actual), LIST(lower_90), LIST(upper_90)) AS coverage
FROM backtest_results;
```

---

*See also: [Cross-Validation](06-cross-validation.md) | [Conformal Prediction](11-conformal-prediction.md)*
