# ARIMA

> AutoRegressive Integrated Moving Average with explicit order specification

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'ARIMA', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'ARIMA', horizon, frequency, params);
```

## Description

`ARIMA` runs a real crate `ARIMA`/`SARIMA` fit at an **explicit, required** order — not a
heuristic and not configurable implicitly. You must pass `arima_order` (`[p,d,q]`); an
optional `seasonal_order` (`[P,D,Q]`) plus `seasonal_period` engages a seasonal `SARIMA` fit.

Calling `'ARIMA'` without `arima_order` is a hard SQL error suggesting `AutoARIMA` for
automatic order search — there is no silent fallback to a fixed-coefficient stand-in.

`model_name` always reports the fitted order: `"ARIMA(p,d,q)"` for a non-seasonal fit, or
`"ARIMA(p,d,q)(P,D,Q)[s]"` once a seasonal order is engaged. When the crate's own AICc
comparison keeps a non-zero intercept, the name additionally carries:
- `" with drift"` when `d + D == 1` (the crate compares drift/no-drift by AICc; this differs
  from R's `Arima()` default of `include.drift = FALSE`, which never considers a drift term
  unless explicitly requested)
- `" with non-zero mean"` when `d + D == 0`

On the AirPassengers airline model `(0,1,1)(0,1,1)[12]` and on non-seasonal `d = 0` orders,
the fitted forecast matches R's `Arima()` at the same order within 1%.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `arima_order` | `[p,d,q]` (JSON-array string) | **Yes** | — | Non-seasonal order; `p,q ≤ 10`, `d ≤ 2` |
| `seasonal_order` | `[P,D,Q]` (JSON-array string) | No | — | Seasonal order; requires `seasonal_period > 1` too; `P,Q ≤ 10`, `D ≤ 2` |
| `seasonal_period` | INTEGER | Required with `seasonal_order` | — | Seasonal period `s` |
| `confidence_level` | DOUBLE | No | 0.90 | Confidence for prediction intervals |

Calling without `arima_order` is a hard SQL error:

```sql
SELECT * FROM ts_forecast_by('sales_data', product_id, date, revenue, 'ARIMA', 7, '1d', MAP{});
-- Invalid Input Error: Invalid input: ARIMA requires arima_order=[p,d,q] (e.g. [1,1,1]);
--                       use 'AutoARIMA' for automatic order selection
```

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier (only for `_by` variant) |
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast |
| `yhat_lower` | DOUBLE | Lower prediction interval |
| `yhat_upper` | DOUBLE | Upper prediction interval |

`yhat_lower` / `yhat_upper` are the crate `ARIMA`/`SARIMA` model's own prediction interval at
`confidence_level` — see
[07-forecasting.md's "Prediction intervals, fitted values and input handling"](../../api/07-forecasting.md#prediction-intervals-fitted-values-and-input-handling)
for calibration caveats shared across all models and how to get conformal coverage instead.

## SQL Example

```sql
CREATE OR REPLACE TABLE daily_sales AS
SELECT product_id,
       (DATE '2024-01-01' + INTERVAL (i) DAY) AS date,
       100.0 + product_id * 10.0 + 0.5 * i AS quantity
FROM range(0, 60) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS product_id);

-- Non-seasonal ARIMA(1,1,1) — explicit order required (output date column is always "ds")
SELECT forecast_step, ds, ROUND(yhat, 2) AS yhat, model_name
FROM ts_forecast_by(
    'daily_sales', product_id, date, quantity, 'ARIMA', 7, '1d',
    MAP{'arima_order': '[1,1,1]'}
)
ORDER BY product_id, forecast_step;
-- model_name = 'ARIMA(1,1,1) with drift' (d+D==1 and the crate's AICc comparison kept drift)

-- ARIMA(0,1,1)(0,1,1)[12] -- the AirPassengers "airline model" -- via an explicit seasonal order
CREATE OR REPLACE TABLE air AS
SELECT 'A' AS id,
       ('1949-01-01'::DATE + (i || ' months')::INTERVAL)::DATE AS ds,
       112.0 + 5.0 * i + 40.0 * sin(2 * pi() * i / 12.0) AS y
FROM range(0, 60) t(i);

SELECT forecast_step, ds, ROUND(yhat, 2) AS yhat, model_name
FROM ts_forecast_by(
    'air', id, ds, y, 'ARIMA', 3, '1mo',
    MAP{'arima_order': '[0,1,1]', 'seasonal_order': '[0,1,1]', 'seasonal_period': '12'}
)
ORDER BY forecast_step;
-- model_name = 'ARIMA(0,1,1)(0,1,1)[12]'
```

## Best For

- When you know (or have identified via ACF/PACF/`auto.arima()`) the exact order to fit
- Reproducible, explicit Box-Jenkins modelling where the order must not silently change
- Use `AutoARIMA` instead when you want the crate to search for the order itself
