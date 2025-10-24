# Model Parameters Guide

This document describes the parameters available for each forecasting model in the DuckDB Time Series Extension.

## Parameter Syntax

Parameters are passed as STRUCT literals using DuckDB's `{'key': value}` syntax:

```sql
TS_FORECAST(date, value, 'ModelName', horizon, {'param1': value1, 'param2': value2})
```

For models with no parameters or to use defaults, pass `NULL`:

```sql
TS_FORECAST(date, value, 'Naive', horizon, NULL)
```

## Available Models and Parameters

### 1. Naive

The Naive forecast uses the last observed value as the forecast for all future periods.

**Parameters:** None

**Example:**
```sql
SELECT TS_FORECAST(date, value, 'Naive', 7, NULL) AS forecast
FROM time_series_data;
```

---

### 2. SMA (Simple Moving Average)

Forecasts using a simple moving average of the most recent observations.

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `window` | INTEGER | No | 5 | Number of recent observations to average |

**Validation:**
- `window` must be positive

**Examples:**

```sql
-- Use default window (5)
SELECT TS_FORECAST(date, value, 'SMA', 7, NULL) AS forecast
FROM time_series_data;

-- Custom window
SELECT TS_FORECAST(date, value, 'SMA', 7, {'window': 10}) AS forecast
FROM time_series_data;

-- Short-term smoothing
SELECT TS_FORECAST(date, value, 'SMA', 7, {'window': 3}) AS forecast
FROM time_series_data;
```

---

### 3. SeasonalNaive

Uses the observation from the same season in the previous cycle as the forecast.

**Parameters:**

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `seasonal_period` | INTEGER | **Yes** | N/A | Length of the seasonal cycle |

**Validation:**
- `seasonal_period` must be positive

**Examples:**

```sql
-- Weekly seasonality (7 days)
SELECT TS_FORECAST(date, value, 'SeasonalNaive', 14, {'seasonal_period': 7}) AS forecast
FROM daily_data
GROUP BY product_id;

-- Monthly seasonality (12 months)
SELECT TS_FORECAST(month, value, 'SeasonalNaive', 6, {'seasonal_period': 12}) AS forecast
FROM monthly_data;

-- Hourly data with daily seasonality (24 hours)
SELECT TS_FORECAST(hour, value, 'SeasonalNaive', 48, {'seasonal_period': 24}) AS forecast
FROM hourly_data;
```

---

## Parameter Tuning Examples

### Comparing Different SMA Windows

```sql
-- Compare forecasts with different window sizes
SELECT 
    'window=3' AS config,
    UNNEST(TS_FORECAST(date, value, 'SMA', 5, {'window': 3}).point_forecast) AS forecast
FROM sales_data
UNION ALL
SELECT 
    'window=7',
    UNNEST(TS_FORECAST(date, value, 'SMA', 5, {'window': 7}).point_forecast)
FROM sales_data
UNION ALL
SELECT 
    'window=14',
    UNNEST(TS_FORECAST(date, value, 'SMA', 5, {'window': 14}).point_forecast)
FROM sales_data;
```

### Using Parameters with GROUP BY

```sql
SELECT 
    product_id,
    region,
    TS_FORECAST(date, sales, 'SMA', 30, {'window': 14}) AS forecast
FROM sales_data
GROUP BY product_id, region
ORDER BY product_id, region;
```

## Error Handling

The extension validates parameters before creating forecasts:

```sql
-- ERROR: window must be positive
SELECT TS_FORECAST(date, value, 'SMA', 7, {'window': 0});

-- ERROR: seasonal_period is required
SELECT TS_FORECAST(date, value, 'SeasonalNaive', 7, NULL);

-- ERROR: seasonal_period must be positive
SELECT TS_FORECAST(date, value, 'SeasonalNaive', 7, {'seasonal_period': -1});
```

## Future Models (Coming Soon)

The following models with parameters are planned:

### ETS (Error, Trend, Seasonality)
- `error_type`: 'additive' or 'multiplicative'
- `trend_type`: 'none', 'additive', or 'multiplicative'
- `seasonal_type`: 'none', 'additive', or 'multiplicative'
- `seasonal_period`: integer

### ARIMA
- `p`: autoregressive order
- `d`: differencing order
- `q`: moving average order
- `P`: seasonal AR order
- `D`: seasonal differencing order
- `Q`: seasonal MA order
- `seasonal_period`: integer

### Theta
- `theta`: decomposition parameter
- `seasonal_period`: integer (optional)

## Best Practices

1. **Start Simple**: Begin with Naive or SMA before trying more complex models
2. **Tune Window Size**: For SMA, start with window ≈ horizon and adjust based on data volatility
3. **Match Seasonality**: Ensure `seasonal_period` matches your data's actual seasonal pattern
4. **Validate Parameters**: Use smaller test datasets to validate parameter choices before scaling
5. **Group-wise Parameters**: Currently, all groups use the same parameters. For group-specific tuning, use separate queries.

## See Also

- `DEMO_AGGREGATE.sql` - Comprehensive examples of all models
- `README.md` - General extension documentation
- `QUICKSTART.md` - Getting started guide

