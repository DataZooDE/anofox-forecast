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
- 556s for 4,227 series (~0.13s per series)
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
- 176s for 4,227 series (~0.04s per series)
- MASE 1.152 (only 0.3% worse than AutoETS)
- 3x faster than AutoETS, near-optimal accuracy

**When to Use:**
- Known seasonal pattern (weekly, monthly, etc.)
- Need fast execution with excellent accuracy
- Production systems where 176s is acceptable

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
- 9s for 4,227 series (~0.002s per series)
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

### Statsforecast ETS Models

**Implementation:**
Industry-standard ETS models from Nixtla's Statsforecast library with parallel processing.

**Python Example:**
```python
from statsforecast import StatsForecast
from statsforecast.models import (
    SimpleExponentialSmoothing,
    SimpleExponentialSmoothingOptimized,
    SeasonalExponentialSmoothing,
    SeasonalExponentialSmoothingOptimized,
    Holt,
    HoltWinters,
    AutoETS,
)

models = [
    SimpleExponentialSmoothing(alpha=0.5),
    SimpleExponentialSmoothingOptimized(),
    SeasonalExponentialSmoothing(season_length=7, alpha=0.5),
    SeasonalExponentialSmoothingOptimized(season_length=7),
    Holt(),
    HoltWinters(season_length=7),
    AutoETS(season_length=7),
]

sf = StatsForecast(models=models, freq='D', n_jobs=-1)
forecasts = sf.forecast(df=train_df, h=14)
```

**Performance:**
- **Holt**: 1.132 MASE - **Best ETS model overall!**
- **HoltWinters**: 1.148 MASE (tied with Anofox AutoETS)
- **AutoETS**: 1.227 MASE (worse than Anofox AutoETS)
- **Total time**: 1,687s (~28 min) for all 7 models
- **Multi-core**: Parallel processing across CPU cores

**Key Differences from Anofox:**
- **Holt model**: Statsforecast Holt (1.132) outperforms all Anofox models
- **AutoETS**: Anofox AutoETS (1.148) beats Statsforecast AutoETS (1.227) by 6.4%
- **Seasonal models**: Statsforecast seasonal models struggle (1.457-1.608 MASE)
- **Overall average**: Anofox wins (1.214 vs 1.280)

**When to Use Statsforecast:**
- Need absolute best ETS accuracy (use Holt model specifically)
- Working in Python ecosystem
- Want to test multiple ETS variants in batch
- Can afford 28 minutes for comprehensive testing

**When to Use Anofox:**
- Need automatic model selection (AutoETS 6.4% better)
- Want faster execution (117s vs ~241s per model)
- SQL-native workflow
- Speed is important (8s optimized models)

### ETS Model Selection Guide

| Scenario | Recommended Model | Reason |
|----------|-------------------|--------|
| Best accuracy | Statsforecast Holt | MASE 1.132, if you know to use it |
| Best automatic | Anofox AutoETS | MASE 1.148, auto-selects best config |
| Fast & accurate | Anofox HoltWinters | 117s, MASE 1.152 |
| Quick prototyping | Anofox SeasonalESOptimized | 8s, MASE 1.203 |
| Ultra-fast | Anofox SeasonalES | 1s, manual tuning |
| Unknown seasonality | Anofox AutoETS | Auto-detection |
| Strong trend + seasonality | Anofox HoltWinters | Triple smoothing |
| Python batch testing | Statsforecast | Test 7 models in 28 min |

## Evaluation Metrics

All benchmarks measure:
- **MASE** (Mean Absolute Scaled Error) - Primary metric, scale-independent
- **MAE** (Mean Absolute Error) - Absolute forecast error
- **RMSE** (Root Mean Squared Error) - Penalizes large errors
- **Time** - Execution time in seconds per model (each model is timed individually for fair comparison)

MASE < 1.0 means the model beats a naive seasonal baseline.
