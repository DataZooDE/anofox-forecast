# SeasonalES

> Seasonal Exponential Smoothing - captures seasonality without trend

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'SeasonalES', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'SeasonalES', horizon, frequency, params);
```

## Description

Seasonal Exponential Smoothing. Combines SES with a seasonal component but without a trend component. For data with stable seasonality and no trend.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_period` | INTEGER | **Yes** | — | Seasonal period (e.g., 7 for weekly, 12 for monthly) |
| `confidence_level` | DOUBLE | No | 0.90 | Confidence for prediction intervals |

SeasonalES does not take smoothing-parameter keys (`alpha`, `gamma`) from SQL — those are
rejected by the parameter validator (`ValidateParamKeys`). The crate's internal smoothing is
not configurable from the SQL surface today.

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
-- Weekly seasonality, no trend
SELECT * FROM ts_forecast_by(
    'daily_visits',
    store_id,
    date,
    visitors,
    'SeasonalES',
    14,
    '1d',
    MAP{'seasonal_period': '7'}
);

-- With an explicit confidence level (no alpha/gamma keys accepted)
SELECT * FROM ts_forecast_by(
    'weekly_patterns',
    location_id,
    date,
    traffic,
    'SeasonalES',
    21,
    '1d',
    MAP{'seasonal_period': '7', 'confidence_level': '0.80'}
);
```

## Best For

- Seasonal data with no clear trend
- Stable, repeating patterns
- Data where the mean level doesn't change over time
- Simpler alternative to HoltWinters when trend isn't needed
