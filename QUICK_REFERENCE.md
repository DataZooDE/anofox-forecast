# Anofox-Forecast Quick Reference

## Load Extension
```sql
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';
```

---

## All 14 Models

### Baseline (⚡⚡⚡⚡⚡ Fastest)
```sql
TS_FORECAST(date, value, 'Naive', 30, NULL)
TS_FORECAST(date, value, 'SMA', 30, {'window': 7})
TS_FORECAST(date, value, 'SeasonalNaive', 30, {'seasonal_period': 7})
```

### Exponential Smoothing (⚡⚡⚡⚡ Fast)
```sql
TS_FORECAST(date, value, 'SES', 30, {'alpha': 0.3})
TS_FORECAST(date, value, 'Holt', 30, {'alpha': 0.3, 'beta': 0.1})
TS_FORECAST(date, value, 'HoltWinters', 30, {'seasonal_period': 7})
```

### State Space (⚡⚡⚡ Medium)
```sql
TS_FORECAST(date, value, 'ETS', 30, {
    'trend_type': 1, 'season_type': 1, 'season_length': 7
})
TS_FORECAST(date, value, 'Theta', 30, {'seasonal_period': 7})
```

### Multiple Seasonality ⭐ (⚡⚡⚡ Medium)
```sql
TS_FORECAST(date, value, 'MFLES', 30, {'seasonal_periods': [7, 30]})
TS_FORECAST(date, value, 'MSTL', 30, {'seasonal_periods': [7, 30]})
```

### Automatic Selection (⚡⚡ Slow but Smart)
```sql
TS_FORECAST(date, value, 'AutoETS', 30, {'season_length': 7})
TS_FORECAST(date, value, 'AutoMFLES', 30, {'seasonal_periods': [7, 30]})
TS_FORECAST(date, value, 'AutoMSTL', 30, {'seasonal_periods': [7, 30]})
TS_FORECAST(date, value, 'AutoARIMA', 30, {'seasonal_period': 7})
```

---

## Common Patterns

### Single Series
```sql
SELECT TS_FORECAST(date, sales, 'Theta', 30, {'seasonal_period': 7}) AS forecast
FROM sales_data;
```

### Batch Forecasting
```sql
SELECT 
    product_id,
    TS_FORECAST(date, sales, 'AutoETS', 30, {'season_length': 7}) AS forecast
FROM sales_data
GROUP BY product_id;
```

### Unnest to Rows
```sql
SELECT 
    product_id,
    UNNEST(forecast.forecast_step) AS step,
    UNNEST(forecast.point_forecast) AS value,
    UNNEST(forecast.lower_95) AS ci_lower,
    UNNEST(forecast.upper_95) AS ci_upper
FROM (
    SELECT product_id, TS_FORECAST(date, sales, 'Naive', 14, NULL) AS forecast
    FROM sales_data GROUP BY product_id
);
```

---

## Model Selection Guide

**I don't know the pattern** → `AutoETS` or `AutoMSTL`

**No trend, no seasonality** → `Naive` or `SMA`

**Trend only** → `Holt` or `ETS(A,A,N)`

**Seasonality only** → `SeasonalNaive` or `Theta`

**Trend + ONE seasonality** → `HoltWinters` or `Theta`

**Multiple seasonalities** → `MFLES` or `MSTL` ⭐

**Need best accuracy** → `AutoARIMA` (slowest)

**Need speed** → `Naive` (fastest)

---

## Parameter Quick Reference

### Array Parameters (Multiple Seasons)
```sql
'seasonal_periods': [7, 30, 365]  -- Weekly, Monthly, Yearly
```

### ETS Component Types
```sql
'error_type': 0     -- 0=Additive, 1=Multiplicative
'trend_type': 1     -- 0=None, 1=Add, 2=Mult, 3=DampedAdd, 4=DampedMult
'season_type': 1    -- 0=None, 1=Add, 2=Mult
```

### MSTL Methods
```sql
'trend_method': 0      -- 0=Linear, 1=SES, 2=Holt, 3=None
'seasonal_method': 0   -- 0=Cyclic, 1=AutoETSAdd, 2=AutoETSMult
```

---

## Output Format
```sql
{
    forecast_step: [1, 2, 3, ...],       -- Steps ahead
    point_forecast: [100.5, 102.3, ...], -- Point forecasts
    lower_95: [95.2, 97.1, ...],         -- Lower CI
    upper_95: [105.8, 107.5, ...],       -- Upper CI
    model_name: 'ModelName'              -- Model used
}
```

---

## Performance Tips

1. **Start with Naive** for baseline
2. **Use Theta** for general purpose
3. **Try AutoETS** for unknown patterns
4. **Use MFLES/MSTL** for multiple seasonalities
5. **Reserve AutoARIMA** for when accuracy is critical
6. **GROUP BY** works perfectly - forecast thousands at once!

---

## Example: Complete Workflow

```sql
-- 1. Load extension
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- 2. Create/load data
CREATE TABLE sales AS SELECT ...;

-- 3. Forecast (choose any model)
SELECT 
    product_id,
    region,
    TS_FORECAST(date, sales, 'MFLES', 30, {
        'seasonal_periods': [7, 30]
    }) AS forecast
FROM sales
WHERE date >= '2024-01-01'
GROUP BY product_id, region;

-- 4. Extract results
SELECT 
    product_id,
    region,
    UNNEST(forecast.forecast_step) AS day,
    UNNEST(forecast.point_forecast) AS forecast_value
FROM previous_query;
```

---

**14 Models | GROUP BY Fixed | Multiple Seasonality | Production Ready** ✅

