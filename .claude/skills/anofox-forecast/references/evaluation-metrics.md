# Evaluation Metrics Reference

**Use scalar functions with `LIST()` and `GROUP BY`.** The `_by` table macros are deprecated (~2400x slower, threading issues).

## Pattern

```sql
SELECT group_col,
    ts_<metric>(LIST(actual ORDER BY date), LIST(forecast ORDER BY date)) AS metric
FROM source GROUP BY group_col;
```

**IMPORTANT:** Always use `ORDER BY` in `LIST()` for temporal correctness.

## Scalar Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `ts_mae` | `(DOUBLE[], DOUBLE[]) -> DOUBLE` | Mean Absolute Error |
| `ts_mse` | `(DOUBLE[], DOUBLE[]) -> DOUBLE` | Mean Squared Error |
| `ts_rmse` | `(DOUBLE[], DOUBLE[]) -> DOUBLE` | Root Mean Squared Error |
| `ts_mape` | `(DOUBLE[], DOUBLE[]) -> DOUBLE` | Mean Absolute Percentage Error |
| `ts_smape` | `(DOUBLE[], DOUBLE[]) -> DOUBLE` | Symmetric MAPE |
| `ts_r2` | `(DOUBLE[], DOUBLE[]) -> DOUBLE` | R-squared |
| `ts_bias` | `(DOUBLE[], DOUBLE[]) -> DOUBLE` | Bias (mean error) |
| `ts_mase` | `(DOUBLE[], DOUBLE[], DOUBLE[]) -> DOUBLE` | Mean Absolute Scaled Error (actual, forecast, baseline) |
| `ts_rmae` | `(DOUBLE[], DOUBLE[], DOUBLE[]) -> DOUBLE` | Relative MAE (actual, pred1, pred2); < 1 means pred1 better |
| `ts_coverage` | `(DOUBLE[], DOUBLE[], DOUBLE[]) -> DOUBLE` | Interval coverage (actual, lower, upper) |
| `ts_quantile_loss` | `(DOUBLE[], DOUBLE[], DOUBLE) -> DOUBLE` | Quantile loss (actual, forecast, quantile) |

## Examples

### Basic per-series metrics
```sql
SELECT unique_id,
    ts_mae(LIST(y ORDER BY ds), LIST(yhat ORDER BY ds)) AS mae,
    ts_rmse(LIST(y ORDER BY ds), LIST(yhat ORDER BY ds)) AS rmse
FROM results GROUP BY unique_id;
```

### CV metrics (per series, fold, model)
```sql
SELECT unique_id, fold_id, model_name,
    ts_mae(LIST(y ORDER BY ds), LIST(yhat ORDER BY ds)) AS mae,
    ts_rmse(LIST(y ORDER BY ds), LIST(yhat ORDER BY ds)) AS rmse,
    ts_mape(LIST(y ORDER BY ds), LIST(yhat ORDER BY ds)) AS mape
FROM cv_forecasts GROUP BY unique_id, fold_id, model_name;
```

### Global metric (no grouping)
```sql
SELECT ts_mae(LIST(y ORDER BY ds), LIST(yhat ORDER BY ds)) AS mae FROM results;
```

### MASE with baseline
```sql
SELECT unique_id,
    ts_mase(LIST(y ORDER BY ds), LIST(yhat ORDER BY ds), LIST(naive ORDER BY ds)) AS mase
FROM results GROUP BY unique_id;
```

### Relative MAE (model comparison)
```sql
SELECT unique_id,
    ts_rmae(LIST(y ORDER BY ds), LIST(ets ORDER BY ds), LIST(naive ORDER BY ds)) AS rmae
FROM results GROUP BY unique_id;
-- rmae < 1 means ETS outperforms Naive
```

### Coverage
```sql
SELECT unique_id,
    ts_coverage(LIST(y ORDER BY ds), LIST(yhat_lower ORDER BY ds), LIST(yhat_upper ORDER BY ds)) AS cov
FROM results GROUP BY unique_id;
```

## Deprecated Table Macros
Do NOT use: ts_mae_by, ts_mse_by, ts_rmse_by, ts_mape_by, ts_smape_by, ts_r2_by, ts_bias_by, ts_mase_by, ts_rmae_by, ts_coverage_by, ts_quantile_loss_by.

Require `SET threads=1` for correct results. Use scalar functions instead.
