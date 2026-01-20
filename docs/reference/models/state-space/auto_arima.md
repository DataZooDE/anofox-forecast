# AutoARIMA

> Automatic ARIMA model selection with optional exogenous variables

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'AutoARIMA', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'AutoARIMA', horizon, params);

-- With exogenous variables
SELECT * FROM ts_forecast_exog_by('table', group_col, date_col, value_col, 'x1,x2', 'future_table', 'AutoARIMA', horizon, params);
```

## Description

Automatic ARIMA (AutoRegressive Integrated Moving Average) model selection. Automatically determines the optimal (p, d, q) orders and seasonal components. Supports exogenous variables (ARIMAX) for incorporating external regressors.

> **Important:** Seasonality is NOT auto-detected. Use `ts_detect_periods_by` first to detect the seasonal period, then pass it explicitly via `seasonal_period`.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_period` | INTEGER | **Yes*** | — | Seasonal period (e.g., 7 for weekly, 12 for monthly) |
| `confidence_level` | DOUBLE | No | 0.95 | Confidence for prediction intervals |
| `include_fitted` | BOOLEAN | No | false | Return in-sample fitted values |
| `include_residuals` | BOOLEAN | No | false | Return residuals |

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier (only for `_by` variant) |
| `ds` | TIMESTAMP | Forecast timestamp |
| `forecast` | DOUBLE | Point forecast |
| `lower` | DOUBLE | Lower prediction interval |
| `upper` | DOUBLE | Upper prediction interval |

*Required for seasonal data. Without `seasonal_period`, AutoARIMA will select a non-seasonal model.

## SQL Example

```sql
-- Step 1: Detect seasonality
SELECT * FROM ts_detect_periods_by('daily_sales', product_id, date, quantity, MAP{});

-- Step 2: Forecast with detected seasonal period
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'AutoARIMA',
    12,
    {'seasonal_period': 7}
);

-- With exogenous variables (ARIMAX)
SELECT * FROM ts_forecast_exog_by(
    'sales',
    product_id,
    date,
    amount,
    'temperature,promotion',
    'future_exog',
    'AutoARIMA',
    12,
    {}
);
```

## Best For

- Time series with complex autocorrelation structures
- When you need to incorporate external regressors (promotions, weather, etc.)
- Stationary or differenced stationary data
- Alternative to ETS when data shows ARIMA-like patterns
