# MSTL

> Multiple Seasonal-Trend decomposition using LOESS

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'MSTL', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'MSTL', horizon, frequency, params);
```

## Description

Multiple Seasonal-Trend decomposition using LOESS. Decomposes the series into trend, multiple seasonal components, and remainder. Forecasts by extrapolating each component separately.

> **Note:** When `seasonal_periods` is omitted, `ts_forecast_by` auto-detects a single period
> internally (`detect_seasonality`) and uses it as the only entry — falling back to the fixed
> period 12 if nothing is detected. Passing `seasonal_periods` explicitly via
> `ts_detect_periods_by` is still recommended for control and reproducibility.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_periods` | INTEGER[] | No | auto-detected (single period) or `[12]` | Array of seasonal periods (e.g., '[7, 365]'); see Note above when omitted |
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
-- Weekly and yearly seasonality
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'MSTL',
    30,
    '1d',
    MAP{'seasonal_periods': '[7, 365]'}
);

-- Daily and weekly for hourly data
SELECT * FROM ts_forecast_by(
    'hourly_energy',
    meter_id,
    timestamp,
    consumption,
    'MSTL',
    168,
    '1h',
    MAP{'seasonal_periods': '[24, 168]'}
);
```

## Best For

- Long time series with complex multiple seasonality
- When you need interpretable decomposition
- Retail data with weekly and yearly patterns
- Energy and traffic data with multiple periodicities
