# ARIMA Benchmark

Comprehensive benchmark of **AutoARIMA** from anofox-forecast on M4 Competition datasets.

## Latest Results

**M4 Daily Dataset** (4,227 series, horizon=14, seasonality=7):

| Implementation | Model | MASE | MAE | RMSE | Time (s) |
|----------------|-------|------|-----|------|----------|
| Statsforecast | AutoARIMA | **1.150** | 176.82 | 208.63 | 7,299 |
| Anofox | AutoARIMA | 1.212 | 183.95 | 216.36 | 6.2 |

## Implementation Comparison

**Why the speed difference?**
- **Anofox**: Optimized for SQL-native execution, streamlined search space
- **Statsforecast**: More exhaustive ARIMA order search, extensive cross-validation
- **Trade-off**: Anofox sacrifices 5.4% accuracy for 1,176x speedup

**Why the accuracy difference?**
- Statsforecast likely explores more (p,d,q)(P,D,Q)m combinations
- Different information criteria or validation strategies
- More sophisticated stepwise search algorithm

For production SQL workflows requiring low latency, Anofox AutoARIMA provides excellent speed/accuracy balance.

## Implementation Details

### Anofox AutoARIMA

**Description:**
Automatically selects optimal ARIMA(p,d,q)(P,D,Q)m orders through systematic search with information criteria.

**Auto-Selection Process:**
1. Tests multiple (p, d, q) combinations for non-seasonal component
2. Tests multiple (P, D, Q) combinations for seasonal component
3. Uses AIC/BIC to select best model
4. Fits selected model on full training data
5. Generates forecasts with prediction intervals

**SQL Example:**
```sql
SELECT * FROM TS_FORECAST_BY(
    'sales', store_id, date, revenue,
    'AutoARIMA', 14,
    {
        'seasonal_period': 7,
        'confidence_level': 0.95
    }
);
```

**Parameters:**
- `seasonal_period`: Seasonal cycle length (default: auto-detect)
  - 7 for daily data with weekly patterns
  - 12 for monthly data with yearly patterns
  - 24 for hourly data with daily patterns
- `confidence_level`: Prediction interval confidence (default: 0.95)
  - 0.95 = 95% confidence intervals
  - 0.90 = 90% confidence intervals

**Model Selection:**
AutoARIMA tests combinations within these ranges:
- **p (AR order)**: 0-5
- **d (Differencing)**: 0-2
- **q (MA order)**: 0-5
- **P (Seasonal AR)**: 0-2
- **D (Seasonal Differencing)**: 0-1
- **Q (Seasonal MA)**: 0-2

Selection criteria:
- Minimizes AIC (Akaike Information Criterion)
- Balances model fit vs complexity
- Prevents overfitting through penalization

**Performance:**
- 6.2s for 4,227 series (~0.001s per series)
- MASE 1.212
- SQL-native execution in DuckDB
- Zero-copy data access

**Output:**
- Point forecasts
- Lower prediction bounds (confidence intervals)
- Upper prediction bounds (confidence intervals)
- Forecast step numbers

**When to Use:**
- Need fast automatic forecasting (< 10s)
- SQL-native workflow
- Real-time or low-latency applications
- Acceptable accuracy without manual tuning
- Want prediction intervals

**When Not to Use:**
- Need absolute best ARIMA accuracy (use Statsforecast AutoARIMA instead)
- Can afford 122 minutes for 5.4% accuracy improvement
- Working exclusively in Python (statsforecast integration simpler)

### Statsforecast AutoARIMA

**Description:**
Industry-standard AutoARIMA implementation from Nixtla's Statsforecast library with exhaustive model search and cross-validation.

**Python Example:**
```python
from statsforecast import StatsForecast
from statsforecast.models import AutoARIMA

models = [AutoARIMA(season_length=7)]
sf = StatsForecast(models=models, freq='D', n_jobs=-1)
forecasts = sf.forecast(df=train_df, h=14, level=[95])
```

**Parameters:**
- `season_length`: Seasonal period (default: 1 = non-seasonal)
- `d`: Max non-seasonal differences (default: None = auto)
- `D`: Max seasonal differences (default: None = auto)
- `max_p`: Max AR order (default: 5)
- `max_q`: Max MA order (default: 5)
- `max_P`: Max seasonal AR order (default: 2)
- `max_Q`: Max seasonal MA order (default: 2)
- `approximation`: Use approximation for speed (default: None = auto)

**Model Selection Process:**
1. More exhaustive search space than Anofox
2. Stepwise search with information criteria (AIC/BIC)
3. Extensive cross-validation
4. Tests broader range of ARIMA configurations
5. More sophisticated order selection algorithm

**Performance:**
- 7,299s for 4,227 series (~1.73s per series)
- Achieves best ARIMA accuracy (MASE 1.150)
- **1,176x slower than Anofox** but **5.4% more accurate**
- Competitive with best complex models (AutoETS, OptimizedTheta)

**When to Use:**
- Need best possible ARIMA accuracy
- Can afford 122 minutes for batch forecasting
- Working in Python ecosystem
- Offline or batch processing workflows
- Accuracy is more important than speed

**When Not to Use:**
- Need real-time forecasting (< 10s)
- SQL-native workflow required
- Speed is critical (use Anofox instead)
- 5.4% accuracy improvement not worth 1,176x slowdown

### ARIMA Method Overview

**What is ARIMA?**
AutoRegressive Integrated Moving Average (ARIMA) models time series as combination of:

1. **AR (AutoRegressive)**: Linear combination of past values
   - p = number of lag observations (AR order)

2. **I (Integrated)**: Differencing to make series stationary
   - d = degree of differencing

3. **MA (Moving Average)**: Linear combination of past forecast errors
   - q = size of moving average window (MA order)

**Seasonal ARIMA (SARIMA):**
Extends ARIMA to handle seasonality:
- (P, D, Q)m - seasonal component with period m
- P = seasonal AR order
- D = seasonal differencing
- Q = seasonal MA order
- m = seasonal period (7 for weekly, 12 for monthly, etc.)

**Full notation:** ARIMA(p,d,q)(P,D,Q)m

**Why ARIMA Works:**
- Captures complex temporal patterns
- Handles trend and seasonality
- Statistical foundation (Box-Jenkins methodology)
- Widely tested and validated
- Generates prediction intervals

**AutoARIMA Advantage:**
- Eliminates manual order selection
- Tests multiple combinations systematically
- Balances fit vs complexity automatically
- Saves time compared to manual tuning

### Comparison: Anofox vs Manual ARIMA

| Feature | AutoARIMA | Manual ARIMA |
|---------|-----------|--------------|
| **Order Selection** | Automatic | Manual trial-and-error |
| **Speed** | 5.2s | Hours of analysis |
| **Expertise Required** | None | High (Box-Jenkins method) |
| **Reproducibility** | High | Variable |
| **Optimal Orders** | Data-driven | May miss optimal configuration |

## Evaluation Metrics

All benchmarks measure:
- **MASE** (Mean Absolute Scaled Error) - Primary metric, scale-independent
- **MAE** (Mean Absolute Error) - Absolute forecast error
- **RMSE** (Root Mean Squared Error) - Penalizes large errors
- **Time** - Total execution time in seconds

MASE < 1.0 means the model beats a naive seasonal baseline.
