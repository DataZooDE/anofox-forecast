# SeasonalESOptimized

> Seasonal Exponential Smoothing with auto-optimized parameters

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'SeasonalESOptimized', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'SeasonalESOptimized', horizon, frequency, params);
```

## Description

Optimized Seasonal Exponential Smoothing. Same as SeasonalES but automatically finds optimal alpha and gamma values that minimize forecast error.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_period` | INTEGER | No (auto-detected; error if no period is detected) | — | Seasonal period (e.g., 7 for weekly, 12 for monthly); auto-detected from the series when omitted. There is no default period — the old silent default of 12/2 is gone (see [07-forecasting.md's "Former silent fallbacks" list](../../../api/07-forecasting.md#former-silent-fallbacks--now-hard-errors)) |
| `confidence_level` | DOUBLE | No | 0.90 | Confidence for prediction intervals |

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
for calibrated coverage use the conformal functions. Since anofox-forecast 0.16.0,
SeasonalESOptimized shares SeasonalES's per-slot `ETS(A,N,N)` variance for its interval width
(not individually R-parity-tested) and does not accept regressors in `ts_forecast_exog_by`
(errors).

## SQL Example

```sql
-- Automatically optimized parameters
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'SeasonalESOptimized',
    14,
    '1d',
    MAP{'seasonal_period': '7'}
);

-- With an explicit confidence level
SELECT * FROM ts_forecast_by(
    'weekly_data',
    store_id,
    date,
    revenue,
    'SeasonalESOptimized',
    28,
    '1d',
    MAP{'seasonal_period': '7', 'confidence_level': '0.80'}
);
```

## Best For

- Seasonal data with no trend
- Production forecasting where manual tuning is impractical
- When you want SeasonalES but don't want to tune alpha/gamma
- Automated forecasting pipelines
