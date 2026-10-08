# M4 Daily family benchmark comparison

Harness time includes a constant CLI-subprocess start + parquet round-trip overhead per model (ANOFOX_USE_CLI=1 path), identical for both labels.

| family | model | v1.5.5 s | 2.0 s | delta % | MASE/MAE/RMSE identical | max abs yhat diff | rows differing |
|---|---|---|---|---|---|---|---|
| baseline | Naive | 5.43 | 5.29 | -2.6 | yes | 0 | 0 |
| baseline | SeasonalNaive | 1.62 | 1.77 | +9.2 | yes | 0 | 0 |
| baseline | RandomWalkDrift | 5.25 | 5.52 | +5.1 | yes | 0 | 0 |
| baseline | SMA | 4.94 | 5.23 | +6.0 | yes | 0 | 0 |
| baseline | SeasonalWindowAverage | 2.38 | 2.67 | +12.2 | yes | 0 | 0 |
| ets | SES | 5.60 | 5.33 | -4.8 | yes | 0 | 0 |
| ets | SESOptimized | 5.76 | 5.35 | -7.1 | yes | 0 | 0 |
| ets | SeasonalES | 1.75 | 1.77 | +1.1 | yes | 0 | 0 |
| ets | SeasonalESOptimized | 2.68 | 2.75 | +2.6 | yes | 0 | 0 |
| ets | Holt | 5.83 | 5.53 | -5.1 | yes | 0 | 0 |
| ets | HoltWinters | 2.97 | 2.92 | -1.6 | yes | 0 | 0 |
| ets | AutoETS | 27.18 | 28.66 | +5.5 | yes | 0 | 0 |
| theta | Theta | 1.59 | 2.08 | +30.8 | yes | 0 | 0 |
| theta | OptimizedTheta | 4.09 | 5.30 | +29.5 | yes | 0 | 0 |
| theta | DynamicTheta | 1.70 | 2.17 | +27.9 | yes | 0 | 0 |
| theta | DynamicOptimizedTheta | 26.86 | 25.18 | -6.2 | yes | 0 | 0 |
| arima | AutoARIMA | 65.64 | 72.19 | +10.0 | yes | 0 | 0 |
| mfles | MFLES | 3.04 | 3.50 | +15.3 | yes | 0 | 0 |
| mstl | MSTL | 30.72 | 28.08 | -8.6 | yes | 0 | 0 |
