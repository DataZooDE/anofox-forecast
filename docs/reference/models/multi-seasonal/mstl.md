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
> internally (`detect_seasonality`) and uses it as the only entry — **erroring if none is
> detected** (naming `MSTL`). There is no default `[12]` fallback. Passing `seasonal_periods`
> explicitly via `ts_detect_periods_agg` is still recommended for control and reproducibility.
> `ts_forecast_agg` follows the same rule — it auto-detects when `seasonal_periods` is
> omitted, erroring if none is detected (naming `MSTL`).

`MSTL` accepts exogenous regressors via `ts_forecast_exog_by` and stays itself —
`model_name` reports `"MSTL with regressors"`. On that exog path, `yhat_lower`/`yhat_upper`
are `NULL` (MSTL has no native interval support once regressors are attached) — use the
conformal prediction functions for calibrated coverage there.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_periods` | INTEGER[] | No | auto-detected (single period) | Array of seasonal periods (e.g., '[7, 365]'); **errors if none can be auto-detected and none is given** — no default `[12]` |
| `confidence_level` | DOUBLE | No | 0.90 | Confidence for prediction intervals |

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier (only for `_by` variant) |
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast |
| `yhat_lower` | DOUBLE | Lower prediction interval |
| `yhat_upper` | DOUBLE | Upper prediction interval |

`yhat_lower` / `yhat_upper` are the crate `MSTL` model's own prediction interval at
`confidence_level` (point-only, `NULL` bounds, on the exog path — see above) — see
[07-forecasting.md's "Prediction intervals, fitted values and input handling"](../../api/07-forecasting.md#prediction-intervals-fitted-values-and-input-handling)
for calibration caveats shared across all models.

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

```sql
-- No seasonal period can be detected and none is given -- hard error naming MSTL
CREATE OR REPLACE TABLE trend_only AS
SELECT 1 AS id, (DATE '2020-01-01' + INTERVAL (i) DAY) AS ds, 10.0 + i * 0.3 AS y
FROM range(0, 20) t(i);

SELECT * FROM ts_forecast_by('trend_only', id, ds, y, 'MSTL', 3, '1d', MAP{});
-- Invalid Input Error: Invalid input: no seasonal period could be detected for model 'MSTL';
--                      pass seasonal_period (or seasonal_periods) explicitly
```

```sql
-- With exogenous variables — MSTL stays itself; yhat_lower/yhat_upper are NULL on this path
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

-- seasonal_period passed explicitly: the temperature regressor's period-5 wobble is too
-- weak relative to the linear trend for auto-detection to reliably find it (see the Note
-- above — auto-detection is a convenience, not a guarantee).
SELECT DISTINCT model_name, yhat_lower, yhat_upper FROM ts_forecast_exog_by(
    'sales_history', product_id, date, revenue, ['temperature'],
    'future_weather', date, ['temperature'], '1d', 'MSTL', 7, MAP{'seasonal_period': '5'}
);
-- model_name = 'MSTL with regressors'; yhat_lower/yhat_upper = NULL
```

## Best For

- Long time series with complex multiple seasonality
- When you need interpretable decomposition
- Retail data with weekly and yearly patterns
- Energy and traffic data with multiple periodicities
