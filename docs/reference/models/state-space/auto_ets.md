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

`'ETS'` without a `model` spec runs this exact same selection and reports the result under
the `ETS` name — see [ets.md](ets.md).

> **Note:** When both `seasonal_period` and `seasonal_periods` are omitted, `ts_forecast_by`
> (and `ts_cv_forecast_by`) auto-detects the seasonal period internally (`detect_seasonality`);
> a period that can't be detected falls back to non-seasonal (AutoETS can run non-seasonally).
> Passing `seasonal_period` explicitly via `ts_detect_periods_agg` is still recommended for
> control and reproducibility — `ts_forecast_agg` follows the same auto-detection rule.
>
> A crate fit/predict failure or panic during the ETS model search is now a hard SQL error
> naming `AutoETS` — **never** a silent HoltWinters/Holt/SES substitution reported under the
> `AutoETS` name. (anofox-forecast 0.15.10 no longer panics on the constant-series case this
> fallback originally existed for.)

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

`yhat_lower` / `yhat_upper` are the crate `AutoETS` model's own prediction interval at
`confidence_level` — see
[07-forecasting.md's "Prediction intervals, fitted values and input handling"](../../api/07-forecasting.md#prediction-intervals-fitted-values-and-input-handling)
for the known seasonal-ETS interval issue and calibration caveats shared across all models.

## SQL Example

```sql
-- Step 1: Detect seasonality (aggregate form — the _by table-macro form has a known bug
-- that returns an all-NULL row; see docs/api/07-forecasting.md's Handling Seasonality note)
SELECT product_id, round(ts_detect_periods_agg(date, quantity, 'fft').primary_period) AS primary_period
FROM daily_sales GROUP BY product_id;
-- Returns: primary_period = 7 (weekly)

-- Step 2: Forecast with detected seasonal period
SELECT DISTINCT model_name FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'AutoETS',
    30,
    '1d',
    MAP{'seasonal_period': '7'}
);

-- Non-seasonal data (no seasonal_period needed — AutoETS can also run non-seasonally)
SELECT DISTINCT model_name FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'AutoETS',
    12,
    '1d'
);

-- With custom confidence level
SELECT DISTINCT model_name FROM ts_forecast_by(
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
