# Model Selection Guide - Choosing the Right Forecasting Model

## Decision Tree

```
Do you know which model to use?
│
├─ NO → Start with AutoETS ✅
│       (Handles most cases automatically)
│
└─ YES → Answer these questions:
         │
         ├─ Multiple seasonality? (daily + weekly + yearly)
         │  └─ YES → MSTL, AutoMSTL, or TBATS
         │
         ├─ Intermittent demand? (many zeros)
         │  └─ YES → Croston, ADIDA, IMAPA
         │
         ├─ Simple pattern? (need fast forecast)
         │  └─ YES → SeasonalNaive, Theta
         │
         └─ Complex pattern? (need high accuracy)
            └─ YES → AutoARIMA, AutoETS
```

## Model Categories

### 1. Automatic Selection (⭐ Recommended)

**When unsure, start here!**

| Model | Best For | Speed | Accuracy | Complexity |
|-------|----------|-------|----------|------------|
| **AutoETS** | General purpose, seasonal data | Medium | High | Auto |
| **AutoARIMA** | Complex patterns, trend+seasonality | Slow | Very High | Auto |
| **AutoMFLES** | Multiple seasonality | Medium | High | Auto |
| **AutoMSTL** | Multiple seasonality, decomposition | Fast | High | Auto |
| **AutoTBATS** | Complex multiple seasonality | Very Slow | Very High | Auto |

**Example**:
```sql
-- Let the algorithm choose
SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, {'seasonal_period': 7});
```

### 2. Simple & Fast Models

**When**: Speed matters, patterns are simple

| Model | Pattern | Speed | Example Use Case |
|-------|---------|-------|------------------|
| **Naive** | No pattern | Instant | Random walk (stock prices) |
| **SeasonalNaive** | Seasonality only | Instant | Weekly sales (no trend) |
| **SMA** | Smoothing | Fast | Noise reduction |
| **SeasonalWindowAverage** | Seasonal averaging | Fast | Seasonal products |

**Example**:
```sql
-- Fastest forecast
SELECT * FROM TS_FORECAST('sales', date, amount, 'SeasonalNaive', 28, {'seasonal_period': 7});
```

### 3. Exponential Smoothing

**When**: Trend and/or seasonality present

| Model | Components | Optimization | Use Case |
|-------|------------|--------------|----------|
| **SES** | Level only | Manual | Smooth, no trend |
| **SESOptimized** | Level only | Automatic | Smooth, no trend |
| **Holt** | Level + trend | Manual | Linear trend |
| **HoltWinters** | Level + trend + seasonal | Manual | Classic seasonal |
| **SeasonalES** | Level + seasonal | Manual | Seasonal, no trend |
| **SeasonalESOptimized** | Level + seasonal | Automatic | Seasonal, no trend |

**Example**:
```sql
-- Seasonal data with trend
SELECT * FROM TS_FORECAST('sales', date, amount, 'HoltWinters', 28, {
    'seasonal_period': 7,
    'multiplicative': false,  -- Additive seasonality
    'alpha': 0.2,
    'beta': 0.1,
    'gamma': 0.3
});
```

### 4. State Space Models (ETS)

**When**: Need flexible modeling framework

**ETS Formula**: Y = f(Error, Trend, Seasonal)

| Configuration | Code | Use When |
|---------------|------|----------|
| (A,N,N) | error_type=0, trend_type=0, season_type=0 | Simple level |
| (A,A,N) | error_type=0, trend_type=1, season_type=0 | Linear trend |
| (A,Ad,N) | error_type=0, trend_type=2, season_type=0 | Damped trend |
| (A,N,A) | error_type=0, trend_type=0, season_type=1 | Seasonal only |
| (A,A,A) | error_type=0, trend_type=1, season_type=1 | Trend + seasonal |

**Example**:
```sql
-- Manual ETS
SELECT * FROM TS_FORECAST('sales', date, amount, 'ETS', 28, {
    'seasonal_period': 7,
    'error_type': 0,      -- Additive errors
    'trend_type': 1,      -- Additive trend
    'season_type': 1      -- Additive seasonality
});

-- Or let AutoETS choose
SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, {'seasonal_period': 7});
```

### 5. ARIMA Models

**When**: Need to model autocorrelation explicitly

**Parameters**:
- **p**: Autoregressive order (how many past values)
- **d**: Differencing (make stationary)
- **q**: Moving average order (how many past errors)

| Model | Configuration | Use When |
|-------|---------------|----------|
| AR(1) | p=1, d=0, q=0 | Strong autocorrelation |
| MA(1) | p=0, d=0, q=1 | Shock response |
| ARIMA(1,1,1) | p=1, d=1, q=1 | Non-stationary with trend |
| SARIMA(1,1,1)(1,1,1,7) | + seasonal | Seasonal non-stationary |

**Example**:
```sql
-- Manual ARIMA
SELECT * FROM TS_FORECAST('sales', date, amount, 'ARIMA', 28, {
    'p': 1, 'd': 1, 'q': 1,           -- Non-seasonal
    'P': 1, 'D': 1, 'Q': 1, 's': 7    -- Seasonal
});

-- Or let AutoARIMA choose
SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoARIMA', 28, {'seasonal_period': 7});
```

### 6. Theta Methods

**When**: Data with trend and seasonality, need simplicity

| Model | Optimization | Use When |
|-------|--------------|----------|
| **Theta** | Manual | Know optimal θ parameter |
| **OptimizedTheta** | Automatic | Let it optimize θ |
| **DynamicTheta** | Adaptive | Changing patterns |
| **DynamicOptimizedTheta** | Auto adaptive | Complex changing patterns |

**Example**:
```sql
-- Simple and effective
SELECT * FROM TS_FORECAST('sales', date, amount, 'OptimizedTheta', 28, {'seasonal_period': 7});
```

### 7. Multiple Seasonality Models

**When**: Data has multiple seasonal patterns (e.g., hourly data with daily + weekly patterns)

| Model | Method | Speed | Best For |
|-------|--------|-------|----------|
| **MSTL** | STL decomposition | Fast | Clear multiple patterns |
| **AutoMSTL** | Auto MSTL | Fast | Auto multiple patterns |
| **MFLES** | Multiple FLES | Medium | Complex multiple seasonality |
| **AutoMFLES** | Auto MFLES | Medium | Auto multiple complex |
| **TBATS** | Trigonometric + Box-Cox | Slow | Very complex patterns |
| **AutoTBATS** | Auto TBATS | Very Slow | Auto very complex |

**Example**:
```sql
-- Hourly data with daily (24) and weekly (168) patterns
SELECT * FROM TS_FORECAST('hourly_sales', timestamp, amount, 'AutoMSTL', 168, {
    'seasonal_periods': [24, 168]  -- Daily and weekly
});
```

### 8. Intermittent Demand Models

**When**: Data has many zeros (sparse demand)

| Model | Method | Use When |
|-------|--------|----------|
| **CrostonClassic** | Classic Croston | Moderate intermittency |
| **CrostonOptimized** | Optimized parameters | Moderate intermittency |
| **CrostonSBA** | Syntetos-Boylan | Less biased |
| **ADIDA** | Aggregate-Disaggregate | Aggregation helpful |
| **IMAPA** | Moving average | Smoothing needed |
| **TSB** | Teunter-Syntetos-Babai | Obsolescence |

**Example**:
```sql
-- Spare parts demand (many zeros)
SELECT * FROM TS_FORECAST('spare_parts', date, demand, 'CrostonOptimized', 28, MAP{});
```

## Selection Criteria

### By Data Characteristics

#### Frequency
| Frequency | Seasonal Period | Recommended Models |
|-----------|----------------|---------------------|
| **Hourly** | 24, 168 | MSTL, TBATS |
| **Daily** | 7, 30, 365 | AutoETS, SeasonalNaive |
| **Weekly** | 4, 52 | AutoETS, Theta |
| **Monthly** | 12 | AutoETS, AutoARIMA, Theta |
| **Quarterly** | 4 | AutoETS, Holt |
| **Yearly** | - | Holt, AutoARIMA (non-seasonal) |

#### Data Size
| Series Length | Recommended | Avoid |
|---------------|-------------|-------|
| **< 30 obs** | Naive, SMA | AutoARIMA, TBATS |
| **30-100 obs** | SeasonalNaive, Theta, AutoETS | TBATS |
| **100-365 obs** | AutoETS, AutoARIMA, MSTL | - |
| **> 365 obs** | Any model | - |

#### Pattern Complexity
| Pattern | Model Complexity | Examples |
|---------|------------------|----------|
| **Simple** | Naive, SeasonalNaive | Weekly sales, no trend |
| **Moderate** | Theta, ETS, HoltWinters | Trending seasonal sales |
| **Complex** | AutoETS, AutoARIMA | Multiple patterns |
| **Very Complex** | TBATS, MSTL | Hourly data, multiple seasonality |

### By Business Requirements

#### Speed vs Accuracy Trade-off

```
Fast ←──────────────────────────────────→ Accurate
│                                           │
Naive                                    AutoTBATS
SeasonalNaive                           AutoARIMA
Theta                                   AutoETS
HoltWinters                             MSTL
```

**Example**: Production forecasting (need speed)
```sql
SELECT * FROM TS_FORECAST_BY('sales', product_id, date, amount, 'SeasonalNaive', 7, {'seasonal_period': 7});
```

**Example**: Strategic planning (need accuracy)
```sql
SELECT * FROM TS_FORECAST('revenue', date, amount, 'AutoARIMA', 90, {'seasonal_period': 7});
```

## Model Comparison Framework

### Compare Multiple Models

```sql
-- Split data into train/test
CREATE TABLE train AS
SELECT * FROM sales WHERE date < DATE '2023-10-01';

CREATE TABLE test AS
SELECT * FROM sales WHERE date >= DATE '2023-10-01' AND date < DATE '2023-11-01';

-- Generate forecasts from different models
WITH models AS (
    VALUES 
        ('AutoETS'),
        ('AutoARIMA'),
        ('Theta'),
        ('SeasonalNaive')
),
forecasts AS (
    SELECT 
        m.column0 AS model_name,
        fc.*
    FROM models m
    CROSS JOIN LATERAL (
        SELECT * FROM TS_FORECAST(
            'train', date, amount, m.column0, 30,
            CASE 
                WHEN m.column0 = 'SeasonalNaive' THEN {'seasonal_period': 7}
                WHEN m.column0 = 'Theta' THEN {'seasonal_period': 7}
                WHEN m.column0 IN ('AutoETS', 'AutoARIMA') THEN {'seasonal_period': 7}
                ELSE MAP{}
            END
        )
    ) fc
),
evaluation AS (
    SELECT 
        f.model_name,
        TS_MAE(LIST(t.amount), LIST(f.point_forecast)) AS mae,
        TS_RMSE(LIST(t.amount), LIST(f.point_forecast)) AS rmse,
        TS_MAPE(LIST(t.amount), LIST(f.point_forecast)) AS mape,
        TS_COVERAGE(LIST(t.amount), LIST(f.lower), LIST(f.upper)) AS coverage
    FROM forecasts f
    JOIN test t ON f.date_col = t.date
    GROUP BY f.model_name
)
SELECT 
    model_name,
    ROUND(mae, 2) AS mae,
    ROUND(rmse, 2) AS rmse,
    ROUND(mape, 2) AS mape_pct,
    ROUND(coverage * 100, 1) AS coverage_pct,
    CASE 
        WHEN mae = MIN(mae) OVER () THEN '🌟 Best'
        ELSE ''
    END AS recommendation
FROM evaluation
ORDER BY mae;
```

## Model Strengths & Weaknesses

### AutoETS

**Strengths**:
- ✅ Automatic parameter selection
- ✅ Handles trend + seasonality
- ✅ Fast optimization
- ✅ Robust to noise
- ✅ Interpretable

**Weaknesses**:
- ❌ Single seasonality only
- ❌ Assumes exponential smoothing appropriate

**Best for**: General-purpose forecasting, 80% of use cases

### AutoARIMA

**Strengths**:
- ✅ Handles complex autocorrelation
- ✅ Very flexible
- ✅ Can model differencing
- ✅ Often most accurate

**Weaknesses**:
- ❌ Slower than ETS
- ❌ Can overfit with small data
- ❌ Less interpretable

**Best for**: Complex economic/financial data, when accuracy is critical

### SeasonalNaive

**Strengths**:
- ✅ Extremely fast
- ✅ No parameters to tune
- ✅ Good baseline
- ✅ Works with short series

**Weaknesses**:
- ❌ Ignores trend
- ❌ Simple intervals
- ❌ Lower accuracy for complex data

**Best for**: Simple seasonal patterns, benchmarking, large-scale forecasting

### Theta

**Strengths**:
- ✅ Good accuracy with minimal tuning
- ✅ Fast
- ✅ Handles trend well
- ✅ Robust

**Weaknesses**:
- ❌ Limited seasonality handling
- ❌ Fixed decomposition approach

**Best for**: Data with trend and seasonality, need reliability

### TBATS

**Strengths**:
- ✅ Multiple complex seasonality
- ✅ Box-Cox transformation
- ✅ ARMA errors
- ✅ Very flexible

**Weaknesses**:
- ❌ Very slow
- ❌ Can overfit
- ❌ Requires long series

**Best for**: Hourly/sub-daily data with multiple seasonality (when accuracy >> speed)

### Croston Methods

**Strengths**:
- ✅ Designed for intermittent demand
- ✅ Handles many zeros well
- ✅ Separates demand probability and size

**Weaknesses**:
- ❌ Not for continuous demand
- ❌ Limited seasonality handling

**Best for**: Spare parts, infrequent purchases, slow-moving inventory

## Selection Flowchart

```
Start
  │
  ├─ Data frequency?
  │  ├─ Hourly/Sub-daily → MSTL, TBATS
  │  ├─ Daily → AutoETS, SeasonalNaive, AutoARIMA
  │  ├─ Weekly → AutoETS, Theta
  │  └─ Monthly/Quarterly → AutoETS, AutoARIMA
  │
  ├─ Intermittent (>30% zeros)?
  │  └─ YES → Croston, ADIDA, IMAPA
  │
  ├─ Multiple seasonality?
  │  └─ YES → MSTL, AutoMSTL, TBATS
  │
  ├─ Speed critical?
  │  ├─ YES → SeasonalNaive, Theta
  │  └─ NO → AutoETS, AutoARIMA
  │
  └─ Need highest accuracy?
     └─ YES → AutoARIMA or AutoETS (compare both)
```

## Practical Examples

### Example 1: Retail Sales (Daily)

**Pattern**: Weekly seasonality + trend

```sql
-- Recommended: AutoETS (fast + accurate)
SELECT * FROM TS_FORECAST_BY('daily_sales', sku, date, units_sold,
                             'AutoETS', 28, {'seasonal_period': 7});

-- Alternative: SeasonalNaive (if speed critical)
SELECT * FROM TS_FORECAST_BY('daily_sales', sku, date, units_sold,
                             'SeasonalNaive', 28, {'seasonal_period': 7});

-- Alternative: AutoARIMA (if accuracy critical)
SELECT * FROM TS_FORECAST_BY('daily_sales', sku, date, units_sold,
                             'AutoARIMA', 28, {'seasonal_period': 7});
```

### Example 2: Website Traffic (Hourly)

**Pattern**: Daily (24h) + weekly (168h) seasonality

```sql
-- Recommended: AutoMSTL (handles multiple seasonality)
SELECT * FROM TS_FORECAST('hourly_traffic', timestamp, visitors,
                          'AutoMSTL', 168,  -- 1 week ahead
                          {'seasonal_periods': [24, 168]});

-- Alternative: TBATS (more complex but slower)
SELECT * FROM TS_FORECAST('hourly_traffic', timestamp, visitors,
                          'AutoTBATS', 168,
                          {'seasonal_periods': [24, 168]});
```

### Example 3: Spare Parts Demand (Intermittent)

**Pattern**: Many zeros, occasional spikes

```sql
-- Recommended: CrostonOptimized
SELECT * FROM TS_FORECAST_BY('spare_parts', part_number, date, demand,
                             'CrostonOptimized', 90, MAP{});

-- Alternative: ADIDA (if aggregation helps)
SELECT * FROM TS_FORECAST_BY('spare_parts', part_number, date, demand,
                             'ADIDA', 90, MAP{});
```

### Example 4: Monthly Revenue (Long-term)

**Pattern**: Yearly seasonality + trend

```sql
-- Recommended: AutoETS or AutoARIMA
SELECT * FROM TS_FORECAST('monthly_revenue', month, revenue,
                          'AutoETS', 12,  -- 1 year ahead
                          {'seasonal_period': 12});

-- Compare with Theta
SELECT * FROM TS_FORECAST('monthly_revenue', month, revenue,
                          'OptimizedTheta', 12,
                          {'seasonal_period': 12});
```

### Example 5: Electricity Demand (Half-hourly)

**Pattern**: Daily (48) + weekly (336) + yearly seasonality

```sql
-- Recommended: TBATS or MSTL
SELECT * FROM TS_FORECAST('electricity_demand', timestamp, kwh,
                          'AutoMSTL', 336,  -- 1 week
                          {'seasonal_periods': [48, 336]});
```

## Parameter Tuning

### When to Tune vs When to Use Auto

**Use Auto** (90% of cases):
- Exploratory analysis
- Production forecasting at scale
- Unknown optimal parameters
- Need reliability over perfection

**Tune Manually** (10% of cases):
- Benchmark performance
- Specific domain knowledge
- Fine-tuning after Auto selection
- Research/experimentation

### How to Tune

```sql
-- Step 1: Start with Auto
CREATE TABLE auto_forecast AS
SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, 
                          {'seasonal_period': 7, 'return_insample': true});

-- Step 2: Check fit quality
SELECT 
    TS_R2(LIST(actual), insample_fitted) AS r_squared,
    model_name
FROM auto_forecast, actuals;

-- Step 3: If not satisfied, try manual tuning
CREATE TABLE manual_forecast AS
SELECT * FROM TS_FORECAST('sales', date, amount, 'ETS', 28, {
    'seasonal_period': 7,
    'error_type': 1,      -- Try multiplicative
    'trend_type': 2,      -- Try damped
    'season_type': 1,
    'return_insample': true
});

-- Step 4: Compare
SELECT 
    'AutoETS' AS approach,
    TS_R2(LIST(actual), (SELECT insample_fitted FROM auto_forecast)) AS r2
FROM actuals
UNION ALL
SELECT 
    'Manual ETS',
    TS_R2(LIST(actual), (SELECT insample_fitted FROM manual_forecast))
FROM actuals;
```

## Model Ensemble (Future Feature)

**Concept**: Combine multiple models for better accuracy

```sql
-- Future implementation might look like:
SELECT * FROM TS_FORECAST('sales', date, amount, 'Ensemble', 28, {
    'models': ['AutoETS', 'AutoARIMA', 'Theta'],
    'weights': [0.5, 0.3, 0.2],  -- Or 'auto' for automatic weighting
    'seasonal_period': 7
});
```

**Current workaround**: Average forecasts manually
```sql
WITH ets AS (
    SELECT forecast_step, point_forecast AS ets_forecast
    FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, {'seasonal_period': 7})
),
arima AS (
    SELECT forecast_step, point_forecast AS arima_forecast
    FROM TS_FORECAST('sales', date, amount, 'AutoARIMA', 28, {'seasonal_period': 7})
)
SELECT 
    forecast_step,
    ROUND((ets_forecast + arima_forecast) / 2.0, 2) AS ensemble_forecast
FROM ets
JOIN arima USING (forecast_step);
```

## Troubleshooting

### Model Fails or Returns Errors

**Error**: "Series too short for seasonal model"
```sql
-- Solution: Use non-seasonal model or get more data
SELECT * FROM TS_FORECAST('sales', date, amount, 'Naive', 28, MAP{});
```

**Error**: "ETS season length must be at least 2"
```sql
-- Solution: Set appropriate seasonal_period
{'seasonal_period': 7}  -- Not 1!
```

**Error**: "Constant series detected"
```sql
-- Solution: Remove constant series
CREATE TABLE variable AS
SELECT * FROM TS_DROP_CONSTANT('sales', product_id, amount);
```

### Poor Forecast Accuracy

**Check**:
1. Data quality: `TS_STATS()`, `TS_QUALITY_REPORT()`
2. Seasonality: `TS_DETECT_SEASONALITY_ALL()`
3. Changepoints: `TS_DETECT_CHANGEPOINTS_BY()`
4. Model fit: Check `insample_fitted` with `return_insample: true`

**Try**:
1. Different models
2. Different seasonal_period
3. Longer training window
4. Data transformation (log, differencing)
5. Remove outliers
6. Handle changepoints separately

## Summary

**Quick Reference**:

| Your Situation | Recommended Model |
|----------------|-------------------|
| **Don't know what to use** | AutoETS |
| **Need speed** | SeasonalNaive |
| **Need accuracy** | AutoARIMA |
| **Multiple seasonality** | AutoMSTL, TBATS |
| **Intermittent demand** | CrostonOptimized |
| **Simple patterns** | Theta, SeasonalNaive |
| **Complex patterns** | AutoETS, AutoARIMA |
| **Forecasting 1000s of series** | SeasonalNaive, AutoETS |

**Decision Process**:
1. Start with AutoETS
2. Check accuracy with `TS_MAPE()`, `TS_COVERAGE()`
3. If < 20% MAPE and good coverage → Deploy!
4. If not satisfactory → Try AutoARIMA or specialized model
5. Compare models and select best

**Next**: [Parameters Guide](12_parameters.md) - Detailed parameter tuning

---

**Remember**: "All models are wrong, but some are useful" - George Box

The best model is the one that works for YOUR data and YOUR business needs!

