# ETS (Exponential Smoothing) Benchmark

Comprehensive benchmark of Error-Trend-Seasonal exponential smoothing models from **anofox-forecast** on M4 Competition datasets.

## Latest Results

**M4 Daily Dataset** (4,227 series, horizon=14, seasonality=7):

| Implementation | Model | MASE | MAE | RMSE | Time (s) | Note |
|----------------|-------|------|-----|------|----------|------|
| Anofox | AutoETS | **1.148** | 175.79 | 207.48 | 466 | ⭐ Best complex model |
| Anofox | HoltWinters | **1.152** | 175.92 | 207.42 | 117 | Fast & accurate |
| Anofox | SeasonalESOptimized | 1.203 | 186.67 | 218.23 | 8.0 | Optimized parameters |
| Anofox | SeasonalES | 1.355 | 210.88 | 240.48 | 1.1 | Basic seasonal ES |

### Key Findings

**Accuracy:**
- **AutoETS achieves MASE 1.148** - Ties for best complex model across all benchmarks!
- **HoltWinters nearly matches** with MASE 1.152 (only 0.3% worse)
- Both AutoETS and HoltWinters beat or match OptimizedTheta (1.149)
- All ETS models beat seasonal baseline (MASE < 1.4)

**Speed:**
- **SeasonalES**: Fastest at 1.1s, but lowest accuracy (MASE 1.355)
- **SeasonalESOptimized**: Good balance at 8.0s (MASE 1.203)
- **HoltWinters**: 117s for near-optimal accuracy (MASE 1.152)
- **AutoETS**: 466s (~8 min) for automatic selection and best accuracy

**Practical Recommendations:**
- **Production Default**: AutoETS for best accuracy with automatic model selection (8 min)
- **Fast & Accurate**: HoltWinters when you can't afford 8 minutes (117s, MASE 1.152)
- **Quick Baseline**: SeasonalESOptimized for rapid prototyping (8s, MASE 1.203)
- **Fastest**: SeasonalES when speed matters most (1s, MASE 1.355)

**Comparison with Other Methods:**
- AutoETS (1.148) ties with RandomWalkWithDrift (1.147) as overall best
- AutoETS beats OptimizedTheta (1.149), AutoARIMA (1.212)
- HoltWinters (1.152) competitive with best Theta variants

## Implementation Details

### AutoETS - Automatic Model Selection

**Description:**
AutoETS automatically selects the best ETS model configuration by evaluating multiple combinations of Error, Trend, and Seasonal components.

**Model Selection Grid:**
- **Error**: {None, Additive, Multiplicative}
- **Trend**: {None, Additive, Multiplicative, Damped}
- **Seasonal**: {None, Additive, Multiplicative}

**Selection Process:**
1. Evaluates up to 30 model configurations
2. Uses Information Criterion (AIC/BIC) for selection
3. Automatically handles seasonality detection
4. Fits best model on full training data

**SQL Example:**
```sql
SELECT * FROM TS_FORECAST_BY(
    'sales', store_id, date, revenue,
    'AutoETS', 14,
    {
        'season_length': 7,      -- Weekly seasonality
        'model': 'ZZZ'           -- Auto-select all components
    }
);
```

**Parameters:**
- `season_length`: Seasonal period (default: auto-detect)
- `model`: ETS specification string (default: "ZZZ" = auto)
  - First letter: Error (A=additive, M=multiplicative, Z=auto)
  - Second letter: Trend (N=none, A=additive, M=multiplicative, Z=auto)
  - Third letter: Seasonal (N=none, A=additive, M=multiplicative, Z=auto)

**Performance:**
- 466s for 4,227 series (~0.11s per series)
- Achieves best complex model accuracy (MASE 1.148)
- Automatic selection eliminates manual tuning

### HoltWinters - Triple Exponential Smoothing

**Description:**
Holt-Winters method with level, trend, and seasonal components. Fast alternative to AutoETS when model structure is known.

**Components:**
- **Level**: Base value smoothing
- **Trend**: Linear trend component
- **Seasonal**: Seasonal pattern (additive or multiplicative)

**SQL Example:**
```sql
SELECT * FROM TS_FORECAST_BY(
    'sales', store_id, date, revenue,
    'HoltWinters', 14,
    {
        'seasonal_period': 7,
        'multiplicative': false,  -- Use additive seasonality
        'alpha': 0.2,             -- Level smoothing (optional)
        'beta': 0.1,              -- Trend smoothing (optional)
        'gamma': 0.3              -- Seasonal smoothing (optional)
    }
);
```

**Parameters:**
- `seasonal_period`: Length of seasonal cycle (required)
- `multiplicative`: Use multiplicative seasonality (default: false = additive)
- `alpha`: Level smoothing parameter (0-1, default: optimized)
- `beta`: Trend smoothing parameter (0-1, default: optimized)
- `gamma`: Seasonal smoothing parameter (0-1, default: optimized)

**Performance:**
- 117s for 4,227 series (~0.03s per series)
- MASE 1.152 (only 0.3% worse than AutoETS)
- 4x faster than AutoETS, near-optimal accuracy

**When to Use:**
- Known seasonal pattern (weekly, monthly, etc.)
- Need fast execution with excellent accuracy
- Production systems where 117s is acceptable

### SeasonalESOptimized - Optimized Seasonal Smoothing

**Description:**
Seasonal exponential smoothing with automatically optimized parameters (alpha, gamma). Simpler than HoltWinters, no trend component.

**SQL Example:**
```sql
SELECT * FROM TS_FORECAST_BY(
    'sales', store_id, date, revenue,
    'SeasonalESOptimized', 14,
    {'seasonal_period': 7}
);
```

**Parameters:**
- `seasonal_period`: Length of seasonal cycle (required)
- Smoothing parameters (alpha, gamma) automatically optimized

**Performance:**
- 8.0s for 4,227 series (~0.002s per series)
- MASE 1.203 (reasonable accuracy)
- Fast alternative when trend is not needed

**When to Use:**
- Seasonal data without strong trend
- Need very fast execution (< 10s)
- Acceptable accuracy for non-critical forecasts

### SeasonalES - Basic Seasonal Smoothing

**Description:**
Simple seasonal exponential smoothing with fixed parameters. Fastest ETS variant but requires manual parameter tuning.

**SQL Example:**
```sql
SELECT * FROM TS_FORECAST_BY(
    'sales', store_id, date, revenue,
    'SeasonalES', 14,
    {
        'seasonal_period': 7,
        'alpha': 0.3,      -- Level smoothing
        'gamma': 0.2       -- Seasonal smoothing
    }
);
```

**Parameters:**
- `seasonal_period`: Length of seasonal cycle (required)
- `alpha`: Level smoothing parameter (0-1, required)
- `gamma`: Seasonal smoothing parameter (0-1, required)

**Performance:**
- 1.1s for 4,227 series (~0.0003s per series)
- MASE 1.355 (lowest accuracy among ETS models)
- Fastest ETS method, good for real-time applications

**When to Use:**
- Ultra-fast forecasting required (< 2s)
- Known good parameters from prior tuning
- Speed more important than accuracy

### ETS Model Selection Guide

| Scenario | Recommended Model | Reason |
|----------|-------------------|--------|
| Best accuracy, automatic | AutoETS | Optimal selection, MASE 1.148 |
| Fast & accurate | HoltWinters | 117s, MASE 1.152 |
| Quick prototyping | SeasonalESOptimized | 8s, MASE 1.203 |
| Ultra-fast, known params | SeasonalES | 1s, manual tuning |
| Unknown seasonality | AutoETS | Auto-detection |
| Strong trend + seasonality | HoltWinters | Triple smoothing |
| Seasonality, no trend | SeasonalESOptimized | Simpler, faster |

### Statsforecast Comparison

**Status**: Not yet benchmarked

Future work will compare Anofox ETS with Statsforecast implementations:
- StatsForecast AutoETS
- Statsforecast ETS variants

Based on baseline and theta benchmarks, we expect:
- Similar accuracy (±1-2%)
- Statsforecast possibly faster due to optimizations
- Anofox advantage: SQL-native, zero-copy

## Evaluation Metrics

All benchmarks measure:
- **MASE** (Mean Absolute Scaled Error) - Primary metric, scale-independent
- **MAE** (Mean Absolute Error) - Absolute forecast error
- **RMSE** (Root Mean Squared Error) - Penalizes large errors
- **Time** - Total execution time in seconds

MASE < 1.0 means the model beats a naive seasonal baseline.
