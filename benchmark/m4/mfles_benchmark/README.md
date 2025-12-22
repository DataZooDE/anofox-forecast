# MFLES Benchmark

Comprehensive benchmarking comparing MFLES (Multiple Forecast Length Exponential Smoothing) implementations from **anofox-forecast** and **statsforecast**.

## Latest Results

**M4 Daily Dataset** (4,227 series, horizon=14, seasonality=7):

| Implementation | Model | MASE | MAE | RMSE | Time (s) |
|----------------|-------|------|-----|------|----------|
| Anofox | MFLES | **1.179** | 181.62 | 212.87 | 21 |
| Statsforecast | MFLES | 1.184 | 185.38 | 217.10 | 161 |

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
