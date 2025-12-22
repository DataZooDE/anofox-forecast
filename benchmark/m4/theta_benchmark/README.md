# Theta Benchmark

Comprehensive benchmark comparing Theta method variants between **Anofox-forecast** and **Statsforecast** on M4 Competition datasets.

## Latest Results

**M4 Daily Dataset** (4,227 series, horizon=14, seasonality=7):

### Point Forecasts Only (Prediction Intervals Excluded)

| Implementation | Model | MASE | MAE | RMSE | Time (s) |
|----------------|-------|------|-----|------|----------|
| Anofox | OptimizedTheta | **1.149** | 178.08 | 209.53 | 1,418 |
| Statsforecast | AutoTheta | **1.149** | 178.15 | 209.60 | 2,327 |
| Statsforecast | OptimizedTheta | 1.151 | 178.44 | 209.91 | 751 |
| Statsforecast | DynamicTheta | 1.153 | 178.83 | 210.33 | 472 |
| Statsforecast | Theta | 1.154 | 178.85 | 210.36 | 512 |
| Anofox | AutoTheta | 1.155 | 179.06 | 210.56 | ~773 |
| Anofox | DynamicOptimizedTheta | 1.155 | 179.06 | 210.56 | 773 |
| Statsforecast | DynamicOptimizedTheta | 1.156 | 178.97 | 210.52 | 612 |
| Anofox | DynamicTheta | 1.226 | 191.41 | 221.94 | 14 |
| Anofox | Theta | 1.226 | 191.46 | 222.00 | 19 |

**Note**: Anofox AutoTheta defaults to DOTM (Dynamic Optimized Theta Method) and provides 
identical results to DynamicOptimizedTheta. For comprehensive model selection across all 4 
variants, use `{'model': 'all'}` (slower).

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