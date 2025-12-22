# MSTL Benchmark

Comprehensive benchmark comparing MSTL (Multiple Seasonal-Trend decomposition
using Loess) implementations between **Anofox-forecast** and **Statsforecast**
on M4 Competition datasets.

## Latest Results

**M4 Daily Dataset** (4,227 series, horizon=14, seasonality=7):

### Point Forecasts Only (Prediction Intervals Excluded)

| Implementation | Model | MASE | MAE | RMSE | Time (s) |
|----------------|-------|------|-----|------|----------|
| Anofox | MSTL | 1.302 | 202.82 | 232.93 | **0.60** |
| Statsforecast | MSTL | **1.200** | **184.34** | **216.14** | 425 |

## Evaluation Metrics

**MASE (Mean Absolute Scaled Error)** - Primary metric:
```
MASE = MAE / naive_baseline_error

Where:
- MAE = mean(|y_true - y_pred|)
- naive_baseline_error = mean(|y_train[t] - y_train[t-seasonality]|)

MASE < 1.0 = Better than naive seasonal baseline
MASE = 1.0 = Equal to naive seasonal baseline
MASE > 1.0 = Worse than naive seasonal baseline
```

**MAE (Mean Absolute Error)**:
```
MAE = mean(|y_true - y_pred|)
```

**RMSE (Root Mean Squared Error)**:
```
RMSE = sqrt(mean((y_true - y_pred)²))
```
- Penalizes larger errors more heavily than MAE

All metrics calculated:
1. Per individual time series
2. Averaged across all series for aggregate performance