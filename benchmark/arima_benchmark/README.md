# ARIMA Benchmark

Comprehensive benchmark of **AutoARIMA** from anofox-forecast on M4 Competition datasets.

## Latest Results

**M4 Daily Dataset** (4,227 series, horizon=14, seasonality=7):

| Implementation | Model | MASE | MAE | RMSE | Time (s) |
|----------------|-------|------|-----|------|----------|
| Statsforecast | AutoARIMA | **1.150** | 176.88 | 208.43 | 2,923 |
| Anofox | AutoARIMA | 1.212 | 183.95 | 216.36 | 5.2 |

### Key Findings

**Accuracy:**
- **Statsforecast AutoARIMA achieves MASE 1.150** - Best ARIMA result, competitive with top methods
- **Anofox AutoARIMA achieves MASE 1.212** - Good accuracy, 5.4% worse than Statsforecast
- **Both beat seasonal baselines**: Much better than SeasonalNaive (1.441)
- **Statsforecast competitive with best**: Only 0.3% worse than RandomWalkWithDrift (1.147)
- **Anofox close to optimized methods**: Within 5.5% of OptimizedTheta/AutoETS (1.148-1.149)

**Speed:**
- **Anofox: 5.2s** for 4,227 series (~0.001s per series) - **Extremely fast**
- **Statsforecast: 2,923s** (48.7 min) for 4,227 series (~0.69s per series)
- **Anofox is 562x faster** than Statsforecast with only 5.4% accuracy loss
- **Fastest automatic method**: Anofox much faster than AutoETS (466s) and OptimizedTheta (900-1000s)

**Practical Insights:**
- **Best Speed/Accuracy Trade-off**: Anofox provides excellent accuracy in seconds
- **Best Accuracy**: Statsforecast when you can afford 49 minutes
- **Automatic Model Selection**: Both implementations eliminate manual parameter tuning
- **Production Ready**: Anofox fast enough for real-time applications
- **SQL-Native**: Anofox integrates seamlessly with DuckDB workflows

**Comparison with Other Methods:**
- **Statsforecast AutoARIMA** (1.150):
  - vs RandomWalkWithDrift (1.147): 0.3% worse
  - vs AutoETS (1.148): 0.2% worse
  - vs OptimizedTheta (1.149): 0.1% worse - **effectively tied for best complex model**
- **Anofox AutoARIMA** (1.212):
  - vs RandomWalkWithDrift (1.147): 5.7% worse, but automatic ARIMA order selection
  - vs AutoETS (1.148): 5.6% worse, but 90x faster (5s vs 466s)
  - vs OptimizedTheta (1.149): 5.5% worse, but 200x faster (5s vs 1,033s)
  - vs Statsforecast AutoARIMA (1.150): 5.4% worse, but **562x faster** (5s vs 2,923s)

**Recommendations:**
- **Fast Automatic Forecasting**: Use Anofox AutoARIMA for speed (5s, MASE 1.212)
- **Best ARIMA Accuracy**: Use Statsforecast AutoARIMA if you can afford 49 minutes (MASE 1.150)
- **Real-time Applications**: Anofox is the only viable choice for low-latency forecasting
- **Batch Processing**: Statsforecast provides marginally better accuracy for offline workflows
- **SQL Workflows**: Anofox offers native DuckDB integration

### Implementation Comparison

**Why the speed difference?**
- **Anofox**: Optimized for SQL-native execution, streamlined search space
- **Statsforecast**: More exhaustive ARIMA order search, extensive cross-validation
- **Trade-off**: Anofox sacrifices 5.4% accuracy for 562x speedup

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
- 5.2s for 4,227 series (~0.001s per series)
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
- Can afford 49 minutes for 5.4% accuracy improvement
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
- 2,923s for 4,227 series (~0.69s per series)
- Achieves best ARIMA accuracy (MASE 1.150)
- **562x slower than Anofox** but **5.4% more accurate**
- Competitive with best complex models (AutoETS, OptimizedTheta)

**When to Use:**
- Need best possible ARIMA accuracy
- Can afford 49 minutes for batch forecasting
- Working in Python ecosystem
- Offline or batch processing workflows
- Accuracy is more important than speed

**When Not to Use:**
- Need real-time forecasting (< 10s)
- SQL-native workflow required
- Speed is critical (use Anofox instead)
- 5.4% accuracy improvement not worth 562x slowdown

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
