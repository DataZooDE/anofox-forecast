# MFLES

> Multiple Frequency Local Exponential Smoothing for multi-seasonal patterns

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'MFLES', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'MFLES', horizon, frequency, params);

-- With exogenous variables
SELECT * FROM ts_forecast_exog_by('table', group_col, date_col, value_col, ['x1','x2'], 'future_table', future_date_col, ['x1','x2'], frequency, 'MFLES', horizon, params);
```

## Description

Multiple Frequency Local Exponential Smoothing. Handles multiple seasonal patterns using Fourier series representation combined with exponential smoothing. Passing explicit seasonal periods is recommended; when `seasonal_periods` is omitted, `ts_forecast_by` auto-detects a single period and uses it as the only entry.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_periods` | INTEGER[] | No | auto-detected (single period) | Array of seasonal periods (e.g., '[24, 168]'); when omitted, falls back to a single auto-detected period, or no seasonality if none is detected |
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
-- Daily and weekly seasonality (hourly data)
SELECT * FROM ts_forecast_by(
    'hourly_energy',
    meter_id,
    timestamp,
    consumption,
    'MFLES',
    168,
    '1h',
    MAP{'seasonal_periods': '[24, 168]'}
);

-- Weekly and yearly seasonality (daily data)
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'MFLES',
    90,
    '1d',
    MAP{'seasonal_periods': '[7, 365]'}
);

-- With exogenous variables
SELECT * FROM ts_forecast_exog_by(
    'sales',
    product_id,
    date,
    amount,
    ['temperature', 'holiday'],
    'future_exog',
    date,
    ['temperature', 'holiday'],
    '1d',
    'MFLES',
    30,
    MAP{'seasonal_periods': '[7, 365]'}
);
```

## Best For

- High-frequency data with multiple seasonalities
- When you know the exact seasonal periods
- Energy demand, call center volumes
- Data with daily, weekly, and yearly patterns
