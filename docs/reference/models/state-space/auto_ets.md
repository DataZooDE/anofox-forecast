# AutoETS

> Automatic ETS model selection based on information criteria

## Signature

```sql
-- Multiple series (grouped) - recommended
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'AutoETS', horizon, params);

-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'AutoETS', horizon, params);
```

## Description

Automatic Exponential Smoothing State Space model selection. Automatically selects the best ETS model (Error, Trend, Seasonal components) based on information criteria.

> **Important:** Seasonality is NOT auto-detected. Use `ts_detect_periods_by` first to detect the seasonal period, then pass it explicitly via `seasonal_period`.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_period` | INTEGER | **Yes*** | — | Seasonal period (e.g., 7 for weekly, 12 for monthly) |
| `confidence_level` | DOUBLE | No | 0.95 | Confidence for prediction intervals |
| `include_fitted` | BOOLEAN | No | false | Return in-sample fitted values |
| `include_residuals` | BOOLEAN | No | false | Return residuals |

*Required for seasonal data. Without `seasonal_period`, AutoETS will select a non-seasonal model.

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier (only for `_by` variant) |
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast |
| `yhat_lower` | DOUBLE | Lower prediction interval |
| `yhat_upper` | DOUBLE | Upper prediction interval |

## SQL Example

```sql
-- Step 1: Detect seasonality
SELECT * FROM ts_detect_periods_by('daily_sales', product_id, date, quantity, MAP{});
-- Returns: primary_period = 7 (weekly)

-- Step 2: Forecast with detected seasonal period
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'AutoETS',
    30,
    {'seasonal_period': 7}
);

-- Non-seasonal data (no seasonal_period needed)
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'AutoETS',
    12
);

-- With custom confidence level
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'AutoETS',
    12,
    {'seasonal_period': 7, 'confidence_level': 0.90}
);
```

## Best For

- Unknown data characteristics (automatic model selection)
- General-purpose forecasting when you don't know which model to use
- Time series with or without trend and seasonality
- First choice for most forecasting tasks
