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
| `seasonal_period` | INTEGER | **Yes** | — | Seasonal period (e.g., 7 for weekly, 12 for monthly) |
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
