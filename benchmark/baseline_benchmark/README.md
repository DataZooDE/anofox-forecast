# Baseline Models Benchmark

Comprehensive benchmark comparing Anofox and Statsforecast implementations of baseline/basic forecasting models on M4 Competition datasets.

## Latest Results

**M4 Daily Dataset** (4,227 series, horizon=14, seasonality=7):

| Implementation | Model | MASE | MAE | RMSE | Time (s) |
|----------------|-------|------|-----|------|----------|
| Anofox | RandomWalkWithDrift | **1.147** | 183.48 | 215.10 | 0.25 |
| Statsforecast | RandomWalkWithDrift | **1.147** | 183.48 | 215.10 | 4.78 |
| Anofox | Naive | 1.153 | 180.83 | 212.00 | 0.24 |
| Statsforecast | Naive | 1.153 | 180.83 | 212.00 | 3.17 |
| Anofox | SMA | 1.343 | 209.01 | 237.98 | 0.22 |
| Statsforecast | WindowAverage | 1.380 | 214.88 | 243.65 | 4.56 |
| Anofox | SeasonalNaive | 1.441 | 227.11 | 263.74 | 0.22 |
| Statsforecast | SeasonalNaive | 1.452 | 227.12 | 262.16 | 3.51 |
| Anofox | SeasonalWindowAverage | 1.961 | 300.48 | 326.69 | 0.26 |
| Statsforecast | SeasonalWindowAverage | 2.209 | 334.23 | 359.39 | 6.20 |

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
