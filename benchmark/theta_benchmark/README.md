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
| Anofox | DynamicOptimizedTheta | 1.155 | 179.06 | 210.56 | 773 |
| Statsforecast | DynamicOptimizedTheta | 1.156 | 178.97 | 210.52 | 612 |
| Anofox | DynamicTheta | 1.226 | 191.41 | 221.94 | 14 |
| Anofox | Theta | 1.226 | 191.46 | 222.00 | 19 |

### Key Findings

**Accuracy:**
- **Best Overall**: anofox-OptimizedTheta and statsforecast-AutoTheta tie at MASE 1.149
- **Top Tier**: All optimized variants achieve MASE 1.149-1.156 (within 0.6% of best)
- **Non-optimized Anofox**: MASE 1.226 (6.7% worse, but 50x faster)
- **All Theta variants beat Auto ARIMA** (1.212) and competitive with AutoETS (1.148)

**Speed:**
- **Fastest**: Anofox non-optimized variants (14-19s) - Excellent for production
- **Optimized Anofox**: 773-1,418s (~13-24 min) for marginal accuracy gain
- **Statsforecast All Models**: 4,675s (~78 min) for all 5 variants total
- **Speed vs Accuracy**: Non-optimized Anofox 100x+ faster with only 6.7% accuracy loss

**Implementation Comparison:**
- **Anofox OptimizedTheta**: Best overall (MASE 1.149, 1,418s)
- **Statsforecast AutoTheta**: Tied best (MASE 1.149, 2,327s)
- **Anofox advantage**: Fast non-optimized variants (14-19s) for production

**Practical Recommendations:**
- **Production Default**: Anofox Theta (19s, MASE 1.226) - Fast and reliable
- **Best Accuracy**: Anofox OptimizedTheta (MASE 1.149, 1,418s / 24 min)
- **Time Budget < 1 min**: Use Anofox Theta or DynamicTheta
- **Time Budget 10-25 min**: Use optimized variants for 6-7% accuracy improvement

**Comparison with Other Methods:**
- vs RandomWalkWithDrift (1.147): Theta slightly worse but competitive
- vs AutoETS (1.148): Theta matches best complex model
- vs AutoARIMA (1.212): Theta significantly better (5.2% improvement)

## Implementation Details

### Anofox Theta Variants

All Theta variants implemented using SQL-native `TS_FORECAST_BY()` function.

#### 1. Theta - Standard Theta Method

**Description:**
Classic Theta method with theta=2.0. Decomposes series into long-term trend and short-term fluctuations.

**SQL Example:**
```sql
SELECT * FROM TS_FORECAST_BY(
    'sales', store_id, date, revenue,
    'Theta', 14,
    {
        'seasonal_period': 7,
        'theta_param': 2.0  -- Default theta value
    }
);
```

**Parameters:**
- `seasonal_period`: Seasonal cycle length (default: auto-detect)
- `theta_param`: Theta parameter (default: 2.0, typical range: 0-3)

**Performance:**
- 19s for 4,227 series (~0.004s per series)
- MASE 1.226
- **Fastest Theta variant**, ideal for production

#### 2. OptimizedTheta - Auto-Optimized Parameter

**Description:**
Automatically optimizes theta parameter via cross-validation to find best value for each series.

**SQL Example:**
```sql
SELECT * FROM TS_FORECAST_BY(
    'sales', store_id, date, revenue,
    'OptimizedTheta', 14,
    {'seasonal_period': 7}
);
```

**Parameters:**
- `seasonal_period`: Seasonal cycle length (default: auto-detect)
- Theta parameter automatically optimized per series

**Performance:**
- 1,418s for 4,227 series (~0.34s per series)
- MASE 1.149 - **Best Anofox accuracy**
- 75x slower than non-optimized, 6.7% accuracy gain

#### 3. DynamicTheta - Dynamic Theta Method

**Description:**
Dynamic variant that adapts to changing patterns, with fixed theta=2.0.

**SQL Example:**
```sql
SELECT * FROM TS_FORECAST_BY(
    'sales', store_id, date, revenue,
    'DynamicTheta', 14,
    {
        'seasonal_period': 7,
        'theta_param': 2.0
    }
);
```

**Parameters:**
- `seasonal_period`: Seasonal cycle length
- `theta_param`: Theta parameter (default: 2.0)

**Performance:**
- 14s for 4,227 series (~0.003s per series)
- MASE 1.226
- Similar to standard Theta, fastest variant

#### 4. DynamicOptimizedTheta - Dynamic + Optimized

**Description:**
Combines dynamic adaptation with automatic theta parameter optimization.

**SQL Example:**
```sql
SELECT * FROM TS_FORECAST_BY(
    'sales', store_id, date, revenue,
    'DynamicOptimizedTheta', 14,
    {'seasonal_period': 7}
);
```

**Parameters:**
- `seasonal_period`: Seasonal cycle length
- Theta parameter automatically optimized

**Performance:**
- 773s for 4,227 series (~0.18s per series)
- MASE 1.155
- Slightly worse than OptimizedTheta, faster

### Statsforecast Theta Variants

Implemented using Nixtla's Statsforecast library with parallel processing.

**Models:**
```python
from statsforecast import StatsForecast
from statsforecast.models import (
    AutoTheta,
    Theta,
    OptimizedTheta,
    DynamicTheta,
    DynamicOptimizedTheta
)

models = [
    AutoTheta(season_length=7),
    Theta(season_length=7),
    OptimizedTheta(season_length=7),
    DynamicTheta(season_length=7),
    DynamicOptimizedTheta(season_length=7),
]

sf = StatsForecast(models=models, freq='D', n_jobs=-1)
forecasts = sf.forecast(df=train_df, h=14, level=[95])
```

**Key Variants:**
1. **AutoTheta**: Automatic model selection - Best accuracy (MASE 1.149)
2. **Theta**: Standard theta=2.0
3. **OptimizedTheta**: Auto-optimized theta parameter
4. **DynamicTheta**: Dynamic adaptation
5. **DynamicOptimizedTheta**: Dynamic + optimization

**Characteristics:**
- **Batch Processing**: All 5 models run together in 693s
- **Multi-core**: Parallel processing across CPU cores
- **Pandas-based**: Requires DataFrame conversion
- **Prediction Intervals**: Generates confidence bounds automatically

### Theta Method Selection Guide

| Scenario | Recommended Model | Reason |
|----------|-------------------|--------|
| Production, speed critical | Anofox Theta | 20s, MASE 1.226, reliable |
| Best accuracy | Anofox OptimizedTheta | MASE 1.149, 17 min |
| Best accuracy + fast | Statsforecast AutoTheta | MASE 1.149, 11.5 min |
| Time budget < 1 minute | Anofox Theta/DynamicTheta | 19-20s |
| Batch forecasting multiple models | Statsforecast | All 5 variants in 11.5 min |
| SQL-native workflow | Anofox variants | DuckDB integration |
| Python workflow | Statsforecast | Python ecosystem |

### Theta Method Overview

**What is Theta?**
The Theta method decomposes a time series into two or more "theta lines" representing different aspects:
- **Theta Line 0**: Long-term trend
- **Theta Line 2**: Short-term fluctuations + seasonality

The forecast combines extrapolations of these lines, weighted by the theta parameter.

**Why Theta Works:**
- Robust to different data patterns
- Simple exponential smoothing with modifications
- Handles seasonality effectively
- Computationally efficient (non-optimized variants)

**Theta Parameter:**
- theta < 1: More weight to short-term
- theta = 1: Simple exponential smoothing
- theta = 2: Standard Theta (default)
- theta > 2: More weight to long-term trend

## Evaluation Metrics

All benchmarks measure:
- **MASE** (Mean Absolute Scaled Error) - Primary metric, scale-independent
- **MAE** (Mean Absolute Error) - Absolute forecast error
- **RMSE** (Root Mean Squared Error) - Penalizes large errors
- **Time** - Total execution time in seconds

MASE < 1.0 means the model beats a naive seasonal baseline.
