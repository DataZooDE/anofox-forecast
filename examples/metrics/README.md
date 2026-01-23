# Metrics Examples

> **Measure forecast accuracy - the foundation of model selection.**

This folder contains runnable SQL examples demonstrating forecast accuracy metrics with the anofox-forecast extension.

## Functions

| Function | Full Name | Best For |
|----------|-----------|----------|
| `ts_mae_by` | Mean Absolute Error | General purpose |
| `ts_mse_by` | Mean Squared Error | Variance analysis |
| `ts_rmse_by` | Root Mean Squared Error | Penalize large errors |
| `ts_mape_by` | Mean Absolute Percentage Error | Scale-independent |
| `ts_smape_by` | Symmetric MAPE | Bounded percentage |
| `ts_r2_by` | R-squared | Model fit quality |
| `ts_bias_by` | Bias (Mean Error) | Detect systematic errors |
| `ts_mase_by` | Mean Absolute Scaled Error | Cross-series comparison |
| `ts_coverage_by` | Interval Coverage | Calibration check |
| `ts_interval_width_by` | Interval Width | Interval sharpness |
| `ts_quantile_loss_by` | Quantile Loss | Probabilistic forecasts |

## Example Files

| File | Description | Data Source |
|------|-------------|-------------|
| [`synthetic_metrics_examples.sql`](synthetic_metrics_examples.sql) | Multi-series metrics examples | Synthetic |

## Quick Start

```bash
# Run synthetic examples
./build/release/duckdb < examples/metrics/synthetic_metrics_examples.sql
```

---

## Usage

### Point Forecast Metrics

```sql
-- Mean Absolute Error per series
SELECT * FROM ts_mae_by('backtest_results', product_id, actual, predicted);

-- Root Mean Squared Error per series
SELECT * FROM ts_rmse_by('backtest_results', product_id, actual, predicted);

-- Mean Absolute Percentage Error per series
SELECT * FROM ts_mape_by('backtest_results', product_id, actual, predicted);

-- Symmetric MAPE per series
SELECT * FROM ts_smape_by('backtest_results', product_id, actual, predicted);

-- R-squared per series
SELECT * FROM ts_r2_by('backtest_results', product_id, actual, predicted);

-- Bias (Mean Error) per series
SELECT * FROM ts_bias_by('backtest_results', product_id, actual, predicted);
```

### Interval Metrics

```sql
-- Coverage (% of actuals within prediction interval)
SELECT * FROM ts_coverage_by('forecast_intervals', product_id, actual, lower_90, upper_90);

-- Mean interval width
SELECT * FROM ts_interval_width_by('forecast_intervals', product_id, lower_90, upper_90);

-- Quantile loss
SELECT * FROM ts_quantile_loss_by('forecasts', product_id, actual, predicted, 0.9);
```

### Combining Metrics

```sql
WITH mae AS (SELECT * FROM ts_mae_by('results', id, actual, predicted)),
rmse AS (SELECT * FROM ts_rmse_by('results', id, actual, predicted)),
bias AS (SELECT * FROM ts_bias_by('results', id, actual, predicted))
SELECT
    mae.id,
    ROUND(mae.mae, 2) AS mae,
    ROUND(rmse.rmse, 2) AS rmse,
    ROUND(bias.bias, 2) AS bias
FROM mae
JOIN rmse ON mae.id = rmse.id
JOIN bias ON mae.id = bias.id;
```

---

## Metric Formulas

### Point Forecast Metrics

| Metric | Formula | Range |
|--------|---------|-------|
| MAE | `mean(\|y - ŷ\|)` | [0, ∞) |
| MSE | `mean((y - ŷ)²)` | [0, ∞) |
| RMSE | `sqrt(mean((y - ŷ)²))` | [0, ∞) |
| MAPE | `mean(\|y - ŷ\| / \|y\|) × 100` | [0, ∞) |
| SMAPE | `mean(\|y - ŷ\| / (\|y\| + \|ŷ\|)) × 200` | [0, 200] |
| Bias | `mean(y - ŷ)` | (-∞, ∞) |

### Interval Metrics

| Metric | Formula | Purpose |
|--------|---------|---------|
| Coverage | `mean(lower ≤ actual ≤ upper)` | Calibration |
| Interval Width | `mean(upper - lower)` | Sharpness |

---

## Choosing a Metric

| Situation | Recommended Metric |
|-----------|-------------------|
| General comparison | MAE or RMSE |
| Large errors are costly | RMSE |
| Scale-independent | MAPE or SMAPE |
| Small values possible | SMAPE (avoids division issues) |
| Cross-series comparison | MASE |
| Probabilistic forecasts | Quantile Loss, Coverage |
| Detect systematic bias | Bias |

---

## Key Concepts

### MAE vs RMSE

| Aspect | MAE | RMSE |
|--------|-----|------|
| Outlier sensitivity | Low | High |
| Interpretation | Average error | Typical error |
| Units | Same as data | Same as data |

### MAPE vs SMAPE

| Aspect | MAPE | SMAPE |
|--------|------|-------|
| Bounded | No | Yes [0, 200] |
| Symmetric | No | Yes |
| Zero actuals | Undefined | Defined |

### Bias Interpretation

| Bias Value | Interpretation |
|------------|----------------|
| Negative | Under-predicting (forecasts too low) |
| Near zero | Unbiased |
| Positive | Over-predicting (forecasts too high) |

---

## Tips

1. **Use multiple metrics** - No single metric tells the whole story.

2. **Match metric to business** - If large errors are costly, use RMSE.

3. **Beware of MAPE** - It can be misleading for small values.

4. **Check bias** - Systematic over/under-prediction affects planning.

5. **Verify calibration** - Coverage should match the nominal level (e.g., 90% for 90% intervals).

---

## Related Functions

- `ts_backtest_auto_by()` - Generate backtest results to evaluate
- `ts_forecast_by()` - Generate forecasts
- `ts_conformal_by()` - Create prediction intervals
