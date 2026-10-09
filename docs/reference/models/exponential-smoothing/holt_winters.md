# HoltWinters

> Triple Exponential Smoothing - handles trend and seasonality together

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'HoltWinters', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'HoltWinters', horizon, frequency, params);
```

## Description

Holt-Winters Seasonal Method (Triple Exponential Smoothing). Extends Holt's method by adding a seasonal component. The workhorse for data with both trend and seasonality.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_period` | INTEGER | No (auto-detected; error if no period is detected) | — | Seasonal period (e.g., 7 for weekly, 12 for monthly); auto-detected from the series when omitted. There is no default period — the old silent default of 12/2 is gone (see [07-forecasting.md's "Former silent fallbacks" list](../../../api/07-forecasting.md#former-silent-fallbacks--now-hard-errors)) |
| `confidence_level` | DOUBLE | No | 0.90 | Confidence for prediction intervals |

HoltWinters does not take smoothing-parameter keys (`alpha`, `beta`, `gamma`) from SQL — those
are rejected by the parameter validator (`ValidateParamKeys`). The crate's internal smoothing
is not configurable from the SQL surface today.

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
for calibrated coverage use the conformal functions. HoltWinters is additive (trend and
seasonal components are added, not multiplied) and does not accept regressors in
`ts_forecast_exog_by` (errors).

## SQL Example

```sql
-- Weekly seasonality with trend
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'HoltWinters',
    14,
    '1d',
    MAP{'seasonal_period': '7'}
);

-- Monthly data with yearly seasonality (no alpha/beta/gamma keys accepted)
SELECT * FROM ts_forecast_by(
    'monthly_revenue',
    store_id,
    date,
    revenue,
    'HoltWinters',
    12,
    '1d',
    MAP{'seasonal_period': '12'}
);
```

## Best For

- Data with both trend and single seasonality
- Classic retail, demand forecasting
- Monthly data with yearly patterns
- Daily data with weekly patterns
