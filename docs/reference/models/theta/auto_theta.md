# AutoTheta

> Automatic Theta method selection and optimization

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'AutoTheta', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'AutoTheta', horizon, frequency, params);
```

## Description

Automatic Theta method selection. The Theta method decomposes the time series into two "theta lines" and combines forecasts from each. AutoTheta automatically optimizes the theta parameter and selects between standard and dynamic variants.

> **Note:** When both `seasonal_period` and `seasonal_periods` are omitted, `ts_forecast_by`
> (and `ts_cv_forecast_by`) auto-detects the seasonal period internally (`detect_seasonality`,
> using the first detected period; 1 if none found). Passing `seasonal_period` explicitly via
> `ts_detect_periods_by` is still recommended for control and reproducibility — `ts_forecast_agg`
> does NOT auto-detect.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_period` | INTEGER | No | auto-detected | Seasonal period (e.g., 7 for weekly, 12 for monthly); auto-detected when omitted |
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
-- Step 1: Detect seasonality
SELECT * FROM ts_detect_periods_by('monthly_sales', product_id, date, quantity, MAP{});

-- Step 2: Forecast with detected seasonal period
SELECT * FROM ts_forecast_by(
    'monthly_sales',
    product_id,
    date,
    quantity,
    'AutoTheta',
    12,
    '1mo',
    MAP{'seasonal_period': '12'}
);

-- Non-seasonal data
SELECT * FROM ts_forecast_by(
    'monthly_sales',
    product_id,
    date,
    quantity,
    'AutoTheta',
    6,
    '1mo'
);
```

## Best For

- Time series with trend
- Monthly or quarterly data
- Simple, robust forecasting with good accuracy
- When ETS and ARIMA don't perform well
