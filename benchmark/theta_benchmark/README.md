# Theta Benchmark

Comprehensive benchmark comparing Theta method variants between **Anofox-forecast** and **Statsforecast** on M4 Competition datasets.

## Recent Performance Optimizations (2025-11-10)

Major C++ performance improvements implemented across all Theta methods:

### Optimizations Completed

1. **StateMatrix Allocation (CRITICAL)**: Eliminated repeated allocations in forecast loop
   - Previously: Created new StateMatrix on every call (~1000x per fit)
   - Now: Pre-allocate and reuse workspace across iterations
   - **Expected improvement: 30-50% faster fitting**

2. **Vector Copy Elimination**: Removed unnecessary return-by-value copies
   - Created `theta_utils` namespace with in-place functions
   - Eliminated copies in `deseasonalize()` and `reseasonalize()`
   - **Expected improvement: 10-20% faster, reduced memory usage**

3. **Gradient Buffer Pre-allocation**: Reuse workspace in L-BFGS optimization
   - Pre-allocated `ThetaGradients::Workspace` for all iterations
   - Eliminated 600-800 allocations during 200 L-BFGS iterations
   - **Expected improvement: 5-15% faster optimization**

4. **Reserved Capacity**: Pre-sized seasonal observation vectors
   - Added `reserve()` calls to prevent dynamic reallocations
   - **Expected improvement: 2-5% faster, reduced fragmentation**

5. **Code Consolidation**: Extracted duplicated deseasonalize logic
   - Removed ~150 lines of duplicated code
   - Improved maintainability

6. **Temporary Model Removal**: Direct utility function calls
   - Removed unnecessary Theta/DynamicTheta instantiation in optimizers
   - **Expected improvement: 2-5% faster**

### Expected Overall Impact

- **Fitting time**: 40-60% faster
- **Optimization time**: 30-50% faster  
- **Memory usage**: 30-40% reduction in peak allocations
- **Code quality**: Smaller binary, better maintainability

**Note**: Benchmark results below pre-date these optimizations. Re-run pending to quantify actual improvements.

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

#### 5. AutoTheta - Automatic Model Selection

**Description:**
Automatically selects the best Theta variant. By default uses DOTM (Dynamic Optimized Theta Method)
which won the M4 forecasting competition. Optionally can evaluate all 4 variants (STM, OTM, DSTM, DOTM)
and select the best based on MSE.

Features:
- Automatic seasonality detection using ACF test
- Seasonal decomposition (additive/multiplicative/auto)
- L-BFGS optimization for fast parameter estimation
- Model selection based on MSE

**SQL Example:**
```sql
-- Default: Use DOTM (fastest, recommended)
SELECT * FROM TS_FORECAST_BY(
    'sales', store_id, date, revenue,
    'AutoTheta', 14,
    {'seasonal_period': 7}
);

-- Evaluate all 4 variants and select best (slower)
SELECT * FROM TS_FORECAST_BY(
    'sales', store_id, date, revenue,
    'AutoTheta', 14,
    {'seasonal_period': 7, 'model': 'all'}
);

-- Force specific variant
SELECT * FROM TS_FORECAST_BY(
    'sales', store_id, date, revenue,
    'AutoTheta', 14,
    {'seasonal_period': 7, 'model': 'OTM'}
);
```

**Parameters:**
- `seasonal_period`: Seasonal cycle length
- `model`: Optional - 'STM', 'OTM', 'DSTM', 'DOTM', or 'all' (default: DOTM only)
- `decomposition_type`: 'auto', 'additive', or 'multiplicative' (default: auto)
- `nmse`: Multi-step MSE evaluation (default: 3)

**Performance:**
- ~773s for 4,227 series (~0.18s per series) with default DOTM
- MASE 1.155 (same as DynamicOptimizedTheta)
- **Recommended for**: Production use with automatic tuning

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
