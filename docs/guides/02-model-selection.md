# Model Selection Guide

> How to choose the right forecasting model for your data

## Overview

The extension provides 32 forecasting models. This guide helps you select the right one.

**Use this guide to:**
- Analyze your data's characteristics (trend, seasonality, intermittency)
- Match data patterns to appropriate model categories
- Start with baselines (Naive, SeasonalNaive) before trying complex models
- Use automatic selection models (AutoETS, AutoARIMA) when unsure
- Handle special cases: multiple seasonalities, sparse demand, regime changes

## Decision Framework

### Step 1: Understand Your Data

```sql
-- Check basic statistics and patterns
SELECT
    product_id,
    (ts_stats(LIST(quantity ORDER BY date))).*
FROM sales
GROUP BY product_id;

-- Detect seasonality
SELECT
    product_id,
    (ts_detect_periods(LIST(quantity ORDER BY date))).primary_period AS detected_period,
    (ts_stats(LIST(quantity ORDER BY date))).trend_strength AS trend
FROM sales
GROUP BY product_id;
```

### Step 2: Match Characteristics to Models

| Your Data Has | Recommended Models | Why |
|---------------|-------------------|-----|
| No clear patterns | `Naive`, `SES`, `SESOptimized` | Simple baselines work well |
| Upward/downward trend | `Holt`, `Theta`, `RandomWalkDrift` | Captures trend component |
| Single seasonal pattern | `SeasonalNaive`, `HoltWinters`, `SeasonalES` | Models one seasonal cycle |
| Multiple seasons (e.g., weekly + yearly) | `MSTL`, `MFLES`, `TBATS` | Handles complex seasonality |
| Many zeros (intermittent demand) | `CrostonClassic`, `CrostonSBA`, `TSB` | Specialized for sparse data |
| Unknown patterns | `AutoETS`, `AutoARIMA`, `AutoTheta` | Automatic model selection |

## Model Categories

### Baseline Models

Start here to establish a benchmark:

```sql
-- Naive: repeats last value
SELECT * FROM ts_forecast_by('sales', id, date, val, 'Naive', 12, '1d');

-- Seasonal Naive: repeats last seasonal cycle
SELECT * FROM ts_forecast_by('sales', id, date, val, 'SeasonalNaive', 12, '1d',
    {'seasonal_period': 7});
```

### Exponential Smoothing

Good default choice for most business data:

```sql
-- Simple Exponential Smoothing (no trend, no seasonality)
SELECT * FROM ts_forecast_by('sales', id, date, val, 'SES', 12, '1d');

-- Holt (trend, no seasonality)
SELECT * FROM ts_forecast_by('sales', id, date, val, 'Holt', 12, '1d');

-- Holt-Winters (trend + seasonality)
SELECT * FROM ts_forecast_by('sales', id, date, val, 'HoltWinters', 12, '1d',
    {'seasonal_period': 7});
```

### Automatic Models

Let the algorithm choose:

```sql
-- AutoETS automatically selects error, trend, and seasonal components
SELECT * FROM ts_forecast_by('sales', id, date, val, 'AutoETS', 12, '1d');

-- AutoARIMA finds optimal ARIMA parameters
SELECT * FROM ts_forecast_by('sales', id, date, val, 'AutoARIMA', 12, '1d');

-- AutoTheta selects best theta variant
SELECT * FROM ts_forecast_by('sales', id, date, val, 'AutoTheta', 12, '1d');
```

### Multiple Seasonality

For data with complex patterns (e.g., hourly data with daily and weekly cycles):

```sql
-- MSTL handles multiple seasonal periods
SELECT * FROM ts_forecast_by('sales', id, date, val, 'MSTL', 24, '1h',
    {'seasonal_periods': '[24, 168]'});  -- daily (24h) and weekly (168h)

-- MFLES is faster for high-frequency data
SELECT * FROM ts_forecast_by('sales', id, date, val, 'MFLES', 24, '1h',
    {'seasonal_periods': '[24, 168]'});
```

### Intermittent Demand

For data with many zeros (spare parts, slow-moving inventory):

```sql
-- Croston's method for intermittent demand
SELECT * FROM ts_forecast_by('inventory', id, date, demand, 'CrostonSBA', 12, '1d');

-- TSB for better bias correction
SELECT * FROM ts_forecast_by('inventory', id, date, demand, 'TSB', 12, '1d');
```

## Comparing Models

Always compare multiple models using cross-validation:

```sql
-- Compare different models using backtest
WITH model_comparison AS (
    SELECT 'AutoETS' AS model_tested, * FROM ts_backtest_auto(
        'sales', id, date, val, 7, 3, '1d', {'method': 'AutoETS'})
    UNION ALL
    SELECT 'Theta' AS model_tested, * FROM ts_backtest_auto(
        'sales', id, date, val, 7, 3, '1d', {'method': 'Theta'})
    UNION ALL
    SELECT 'Naive' AS model_tested, * FROM ts_backtest_auto(
        'sales', id, date, val, 7, 3, '1d', {'method': 'Naive'})
)
SELECT
    model_tested,
    ROUND(AVG(abs_error), 2) AS avg_mae,
    ROUND(AVG(fold_metric_score), 2) AS avg_rmse
FROM model_comparison
GROUP BY model_tested
ORDER BY avg_mae;
```

## Best Practices

1. **Always compare against Naive** - if you can't beat Naive, your data may not be forecastable
2. **Use cross-validation** - don't just fit on all data and assume it generalizes
3. **Check residuals** - look for patterns the model missed
4. **Consider business context** - sometimes simpler models are more interpretable
5. **Match granularity** - use models appropriate for your forecast horizon

## Quick Reference

| Scenario | First Try | Alternative |
|----------|-----------|-------------|
| Don't know data characteristics | `AutoETS` | `AutoARIMA` |
| Daily retail sales | `HoltWinters` | `MSTL` |
| Weekly financial data | `Theta` | `AutoETS` |
| Hourly sensor data | `MFLES` | `MSTL` |
| Spare parts demand | `CrostonSBA` | `TSB` |
| Short series (< 20 points) | `Naive` | `SES` |

---

*See also: [Getting Started](01-getting-started.md) | [Cross-Validation](03-cross-validation.md)*
