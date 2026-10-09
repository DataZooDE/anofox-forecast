# Theta

> Theta Method - decomposes series into trend and drift components

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'Theta', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'Theta', horizon, frequency, params);
```

## Description

Standard Theta Method. Decomposes the time series into two "theta lines" - one capturing long-term trend (SES) and one capturing short-term behavior (drift). Combines forecasts from both lines with equal weights.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_period` | INTEGER | No | auto-detected | Seasonal period; auto-detected when both `seasonal_period` and `seasonal_periods` are omitted |
| `confidence_level` | DOUBLE | No | 0.90 | Confidence for prediction intervals |

The `theta` key itself is **not** an accepted SQL parameter — it is rejected by the parameter
validator (`ValidateParamKeys`). There is no way to pass a custom theta value from SQL today;
the crate's internal curvature parameter is fixed.

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
-- Basic usage (no theta key accepted from SQL)
SELECT * FROM ts_forecast_by(
    'monthly_sales',
    product_id,
    date,
    quantity,
    'Theta',
    6,
    '1mo'
);

-- With an explicit seasonal period
SELECT * FROM ts_forecast_by(
    'quarterly_revenue',
    region_id,
    date,
    revenue,
    'Theta',
    4,
    '3mo',
    MAP{'seasonal_period': '4'}
);
```

## Best For

- Monthly or quarterly data
- Data with trend
- Simple but effective forecasting
- M3 competition-winning approach
