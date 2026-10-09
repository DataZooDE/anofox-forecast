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

`AutoTheta` accepts exogenous regressors via `ts_forecast_exog_by` and stays itself —
`model_name` reports the selected variant plus `" with regressors"`, e.g.
`"AutoTheta(OTM) with regressors"`.

> **Note:** When both `seasonal_period` and `seasonal_periods` are omitted, `ts_forecast_by`
> (and `ts_cv_forecast_by`) auto-detects the seasonal period internally (`detect_seasonality`);
> a period that can't be detected falls back to non-seasonal (AutoTheta can run non-seasonally).
> Passing `seasonal_period` explicitly via `ts_detect_periods_agg` is still recommended for
> control and reproducibility — `ts_forecast_agg` does NOT auto-detect.

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

`yhat_lower` / `yhat_upper` are the crate `AutoTheta` model's own prediction interval at
`confidence_level` — see
[07-forecasting.md's "Prediction intervals, fitted values and input handling"](../../api/07-forecasting.md#prediction-intervals-fitted-values-and-input-handling)
for calibration caveats shared across all models.

> **Known issue:** interval width is roughly **2× too wide** at `h ≥ 6` versus the reference
> R implementation, same as the rest of the Theta family (tracked for Phase 17).

## SQL Example

```sql
-- Step 1: Detect seasonality (aggregate form — the _by table-macro form has a known bug
-- that returns an all-NULL row; see docs/api/07-forecasting.md's Handling Seasonality note)
SELECT product_id, round(ts_detect_periods_agg(date, quantity, 'fft').primary_period) AS primary_period
FROM monthly_sales GROUP BY product_id;

-- Step 2: Forecast with detected seasonal period
SELECT DISTINCT model_name FROM ts_forecast_by(
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
SELECT DISTINCT model_name FROM ts_forecast_by(
    'monthly_sales',
    product_id,
    date,
    quantity,
    'AutoTheta',
    6,
    '1mo'
);

-- With exogenous variables — AutoTheta stays itself, reporting the selected variant
CREATE OR REPLACE TABLE sales_history AS
SELECT product_id,
       (DATE '2024-01-01' + INTERVAL (i) DAY) AS date,
       100.0 + product_id * 10.0 + 0.5 * i + 15.0 * temp AS revenue,
       temp AS temperature
FROM (SELECT i, 0.8 + (i % 5) * 0.02 AS temp FROM range(0, 60) t(i)) d
CROSS JOIN (SELECT unnest([1, 2]) AS product_id);

CREATE OR REPLACE TABLE future_weather AS
SELECT product_id,
       (DATE '2024-03-01' + INTERVAL (i) DAY) AS date,
       0.9 + (i % 5) * 0.02 AS temperature
FROM range(0, 7) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS product_id);

SELECT DISTINCT model_name FROM ts_forecast_exog_by(
    'sales_history', product_id, date, revenue, ['temperature'],
    'future_weather', date, ['temperature'], '1d', 'AutoTheta', 7, MAP{}
);
-- model_name = 'AutoTheta(OTM) with regressors' (selected variant will vary with data)
```

## Best For

- Time series with trend
- Monthly or quarterly data
- Simple, robust forecasting with good accuracy
- When ETS and ARIMA don't perform well
