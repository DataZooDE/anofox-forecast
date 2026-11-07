# SMA

> Simple Moving Average - forecasts using average of recent observations

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'SMA', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'SMA', horizon, frequency, params);
```

## Description

Simple Moving Average. Forecasts the average of the last `window` observations. A step up from Naive, smooths out noise while remaining simple.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `window` | INTEGER | No | 5 | Number of periods to average |
| `confidence_level` | DOUBLE | No | 0.90 | Confidence for prediction intervals |

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier (only for `_by` variant) |
| `forecast_step` | INTEGER | Steps ahead of the last observation, `1` to `horizon` |
| `ds` | (same type as input) | Forecast timestamp, always named `ds` whatever the input date column is called (see [`ts_forecast_by` Returns](../../../api/07-forecasting.md#ts_forecast_by)) |
| `yhat` | DOUBLE | Point forecast |
| `yhat_lower` | DOUBLE | Lower prediction interval |
| `yhat_upper` | DOUBLE | Upper prediction interval |
| `model_name` | VARCHAR | Model used |
| `level` | DOUBLE | Interval coverage (`confidence_level`, default `0.9`); `NULL` when the model returns no interval (since `2026.10.31`) |

`yhat_lower` / `yhat_upper` are the model's own prediction interval (crate
`predict_with_intervals`) at `confidence_level` (default 0.90). See
[Prediction intervals, fitted values and input handling](../../../api/07-forecasting.md#prediction-intervals-fitted-values-and-input-handling);
for calibrated coverage use the conformal functions. SMA's band is flat across the horizon:
half-width = z · σ · √(1 + 1/window), where σ is the residual standard deviation.

## SQL Example

```sql
-- Basic usage (default window=5)
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'SMA',
    7,
    '1d',
    MAP{}
);

-- With custom window size
SELECT * FROM ts_forecast_by(
    'weekly_data',
    store_id,
    date,
    revenue,
    'SMA',
    4,
    '1d',
    MAP{'window': '12'}
);
```

## Best For

- Noisy data with no clear trend or seasonality
- Short-term forecasting where recent average is a good predictor
- Simple baseline that accounts for recent variation
- Data that fluctuates around a stable mean
