# AutoTheta

> Automatic Theta method selection and optimization

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'AutoTheta', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'AutoTheta', horizon, frequency, params);
```

## Description

Automatic Theta method selection. The Theta method decomposes the time series into two "theta lines" and combines forecasts from each. AutoTheta automatically optimizes the theta parameter and selects between standard and dynamic variants.

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
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast |
| `yhat_lower` | DOUBLE | Lower prediction interval |
| `yhat_upper` | DOUBLE | Upper prediction interval |

*Required for seasonal data. Without `seasonal_period`, AutoTheta will select a non-seasonal model.

## SQL Example

```sql
-- Step 1: Detect seasonality
SELECT * FROM ts_detect_periods_by('monthly_sales', product_id, date, quantity, MAP{});

-- Step 2: Forecast with detected seasonal period
SELECT * FROM ts_forecast_by(
    'monthly_sales',
    product_id,
    date,
    quantity,
    'AutoTheta',
    12,
    '1mo',
    {'seasonal_period': 12}
);

-- Non-seasonal data
SELECT * FROM ts_forecast_by(
    'monthly_sales',
    product_id,
    date,
    quantity,
    'AutoTheta',
    6,
    '1mo'
);
```

## Best For

- Time series with trend
- Monthly or quarterly data
- Simple, robust forecasting with good accuracy
- When ETS and ARIMA don't perform well
