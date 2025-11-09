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

### Key Findings

**Accuracy:**
- **Best Model**: RandomWalkWithDrift (MASE 1.147) - Optimal for this dataset
- **Identical Accuracy**: Anofox and Statsforecast produce identical results for Naive, RandomWalkWithDrift
- **Simple is Effective**: RandomWalkWithDrift outperforms seasonal models significantly
- **Naive Nearly As Good**: MASE 1.153 (only 0.5% worse than best)

**Speed:**
- **Exceptional Performance**: All baseline models complete in < 0.4 seconds for 4,227 series
- **Anofox Faster**: Anofox models run ~2x faster than Statsforecast despite identical accuracy
- **Fastest Overall**: Anofox SMA at 0.16s

**Practical Insights:**
1. **RandomWalkWithDrift** performs best by combining naive forecasting with linear trend
2. **Seasonal models underperform** on this dataset, suggesting weak 7-day seasonality patterns
3. **Production Recommendation**: Use RandomWalkWithDrift for fast, accurate baseline (0.17s)
4. **Quick Baseline**: Naive model provides near-optimal accuracy (1.153) in 0.19s

## Implementation Details

### Anofox Baseline Models

Implemented using SQL-native `TS_FORECAST_BY()` function executed in DuckDB.

**Models:**
1. **Naive**: Uses last observed value as forecast
   ```sql
   SELECT * FROM TS_FORECAST_BY(
       'sales', store_id, date, revenue,
       'Naive', 14, {}
   );
   ```

2. **SeasonalNaive**: Uses same season's value from previous cycle
   ```sql
   SELECT * FROM TS_FORECAST_BY(
       'sales', store_id, date, revenue,
       'SeasonalNaive', 14,
       {'seasonal_period': 7}  -- Weekly seasonality
   );
   ```

3. **RandomWalkWithDrift**: Naive + linear trend
   ```sql
   SELECT * FROM TS_FORECAST_BY(
       'sales', store_id, date, revenue,
       'RandomWalkWithDrift', 14, {}
   );
   ```
   - Calculates drift (average change per period) from training data
   - Applies drift linearly across forecast horizon
   - Best performer for Daily data (MASE 1.147)

4. **SMA (Simple Moving Average)**: Average of recent observations
   ```sql
   SELECT * FROM TS_FORECAST_BY(
       'sales', store_id, date, revenue,
       'SMA', 14,
       {'window': 7}  -- 7-period window
   );
   ```

5. **SeasonalWindowAverage**: Seasonal moving average
   ```sql
   SELECT * FROM TS_FORECAST_BY(
       'sales', store_id, date, revenue,
       'SeasonalWindowAverage', 14,
       {'seasonal_period': 7, 'window': 3}
   );
   ```

**Performance Characteristics:**
- **Fast**: All models complete in 0.16-0.21s for 4,227 series
- **SQL-Native**: Execute directly in DuckDB without data transfer
- **Batched**: Process multiple series efficiently via `TS_FORECAST_BY()`
- **Memory Efficient**: Streaming computation, no intermediate copies

### Statsforecast Baseline Models

Implemented using Nixtla's Statsforecast library with parallel processing.

**Models:**
```python
from statsforecast.models import (
    Naive,
    SeasonalNaive,
    RandomWalkWithDrift,
    WindowAverage,              # Equivalent to SMA
    SeasonalWindowAverage
)

models = [
    Naive(),
    SeasonalNaive(season_length=7),
    RandomWalkWithDrift(),
    WindowAverage(window_size=7),
    SeasonalWindowAverage(season_length=7, window_size=3),
]

sf = StatsForecast(models=models, freq='D', n_jobs=-1)
forecasts = sf.forecast(df=train_df, h=14, level=[95])
```

**Characteristics:**
- **Multi-core**: Parallel processing across all CPU cores
- **Pandas-based**: Requires DataFrame conversion from DuckDB
- **Identical Accuracy**: Produces same forecasts as Anofox for most models
- **Slower**: ~2x slower than Anofox (0.38s vs 0.16-0.21s)

### Model Comparison: Anofox vs Statsforecast

| Feature | Anofox | Statsforecast |
|---------|--------|---------------|
| **Integration** | SQL-native, DuckDB execution | Python library, requires data export |
| **Speed** | 0.16-0.21s (faster) | 0.38s (all models batched) |
| **Accuracy** | Identical for Naive, RWD | Identical for Naive, RWD |
| **API** | SQL function calls | Python API |
| **Parallelism** | DuckDB engine | Multi-core Python |
| **Memory** | Streaming, zero-copy | DataFrame conversions required |

**When to Use Each:**
- **Anofox**: When working in SQL, need fast execution, prefer zero-copy operations
- **Statsforecast**: When working in Python, need advanced features, already using their ecosystem

### Evaluation Metrics

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
