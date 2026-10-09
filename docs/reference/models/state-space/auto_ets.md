# AutoETS

> Automatic ETS model selection based on information criteria

## Signature

```sql
-- Multiple series (grouped) - recommended
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'AutoETS', horizon, frequency, params);

-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'AutoETS', horizon, params);
```

## Description

Automatic Exponential Smoothing State Space model selection. Automatically selects the best ETS model (Error, Trend, Seasonal components) based on information criteria.

> **Note:** When both `seasonal_period` and `seasonal_periods` are omitted, `ts_forecast_by`
> (and `ts_cv_forecast_by`) auto-detects the seasonal period internally (`detect_seasonality`,
> using the first detected period; 1 if none found). Passing `seasonal_period` explicitly via
> `ts_detect_periods_by` is still recommended for control and reproducibility — `ts_forecast_agg`
> does NOT auto-detect.
>
> If the underlying crate's ETS model search errors out, AutoETS currently falls back silently
> to HoltWinters, Holt, or SES while the output is still labelled `AutoETS`.
> (being corrected in v0.10.0, Phase 10)

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
SELECT * FROM ts_detect_periods_by('daily_sales', product_id, date, quantity, MAP{});
-- Returns: primary_period = 7 (weekly)

-- Step 2: Forecast with detected seasonal period
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'AutoETS',
    30,
    '1d',
    MAP{'seasonal_period': '7'}
);

-- Non-seasonal data (no seasonal_period needed)
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'AutoETS',
    12,
    '1d'
);

-- With custom confidence level
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'AutoETS',
    12,
    '1d',
    MAP{'seasonal_period': '7', 'confidence_level': '0.90'}
);
```

## Best For

- Unknown data characteristics (automatic model selection)
- General-purpose forecasting when you don't know which model to use
- Time series with or without trend and seasonality
- First choice for most forecasting tasks
