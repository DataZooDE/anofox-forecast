# SES

> Simple Exponential Smoothing - weighted average with exponential decay

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'SES', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'SES', horizon, frequency, params);
```

## Description

Simple Exponential Smoothing. Produces flat forecasts using exponentially weighted average of past observations. More recent observations have higher weight. Good for data with no trend or seasonality.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `confidence_level` | DOUBLE | No | 0.90 | Confidence for prediction intervals |

SES does not take a smoothing-parameter key (`alpha`) from SQL — it is rejected by the
parameter validator (`ValidateParamKeys`). The crate's internal alpha is fixed and not
configurable from the SQL surface today.

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier (only for `_by` variant) |
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast |
| `yhat_lower` | DOUBLE | Lower prediction interval |
| `yhat_upper` | DOUBLE | Upper prediction interval |

`yhat_lower` / `yhat_upper` are the model's own prediction interval (crate
`predict_with_intervals`) at `confidence_level` (default 0.90). See
[Prediction intervals, fitted values and input handling](../../../api/07-forecasting.md#prediction-intervals-fitted-values-and-input-handling);
for calibrated coverage use the conformal functions.

**Known issue:** SES's interval bands are flat in `h` — the half-width does not widen across
the forecast horizon (crate `anofox-forecast` 0.15.10; R's `ses()` widens with `h`). Tracked
for Phase 11/17.

## SQL Example

```sql
-- Basic usage (alpha is fixed internally; no alpha key accepted)
SELECT * FROM ts_forecast_by(
    'daily_inventory',
    sku_id,
    date,
    stock_level,
    'SES',
    7,
    '1d',
    MAP{}
);

-- With an explicit confidence level
SELECT * FROM ts_forecast_by(
    'sensor_data',
    sensor_id,
    timestamp,
    reading,
    'SES',
    24,
    '1d',
    MAP{'confidence_level': '0.80'}
);
```

## Best For

- Data with no trend or seasonality
- Short-term forecasting
- Stable time series that fluctuate around a mean
- When you want more control than Naive but simple implementation
