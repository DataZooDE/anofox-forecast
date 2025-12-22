# ETS (Exponential Smoothing) Benchmark

Comprehensive benchmark of Error-Trend-Seasonal exponential smoothing models from **anofox-forecast** and **statsforecast** on M4 Competition datasets.

## Latest Results

**M4 Daily Dataset** (4,227 series, horizon=14, seasonality=7):

| Implementation | Model | MASE | MAE | RMSE | Time (s) |
|----------------|-------|------|-----|------|----------|
| Statsforecast | Holt | **1.132** | 172.86 | 204.44 | 154 |
| Anofox | AutoETS | **1.148** | 175.79 | 207.48 | 556 |
| Statsforecast | HoltWinters | **1.148** | 177.14 | 208.90 | 1,094 |
| Anofox | HoltWinters | 1.152 | 175.92 | 207.42 | 176 |
| Statsforecast | SESOpt | 1.154 | 178.32 | 209.79 | 6 |
| Anofox | SeasonalESOptimized | 1.203 | 186.67 | 218.23 | 9 |
| Statsforecast | AutoETS | 1.227 | 188.14 | 227.63 | 3,179 |
| Statsforecast | SES | 1.231 | 191.79 | 222.13 | 3 |
| Statsforecast | SeasonalESOptimized | 1.457 | 226.82 | 261.36 | 10 |
| Statsforecast | SeasonalES | 1.608 | 249.17 | 278.42 | 6 |

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