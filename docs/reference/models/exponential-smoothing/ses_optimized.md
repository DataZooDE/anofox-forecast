# SESOptimized

> Simple Exponential Smoothing with automatically optimized alpha parameter

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'SESOptimized', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'SESOptimized', horizon, frequency, params);
```

## Description

Optimized Simple Exponential Smoothing. Same as SES but automatically finds the optimal alpha value that minimizes forecast error on historical data.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `confidence_level` | DOUBLE | No | 0.90 | Confidence for prediction intervals |

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier (only for `_by` variant) |
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast |
| `yhat_lower` | DOUBLE | Lower prediction interval |
| `yhat_upper` | DOUBLE | Upper prediction interval |

`yhat_lower` / `yhat_upper` are currently a heuristic `point ± z·SD(history)·√h` band, not a
model-based interval and not statistically calibrated. For calibrated coverage, use conformal
prediction intervals (see `docs/api/11-conformal-prediction.md`) on top of the point forecast.
(being corrected in v0.10.0, Phase 10)

## SQL Example

```sql
-- Basic usage (alpha automatically optimized)
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'SESOptimized',
    14,
    '1d',
    MAP{}
);

-- With an explicit confidence level
SELECT * FROM ts_forecast_by(
    'weekly_data',
    store_id,
    date,
    revenue,
    'SESOptimized',
    8,
    '1d',
    MAP{'confidence_level': '0.80'}
);
```

## Best For

- Data with no trend or seasonality
- When you don't want to manually tune the alpha parameter
- Slightly better accuracy than fixed-alpha SES
- Production forecasting where manual tuning is impractical
