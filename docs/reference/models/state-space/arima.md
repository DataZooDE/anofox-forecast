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

`ARIMA` does **not** accept explicit `(p, d, q)` / `(P, D, Q, s)` order parameters from SQL —
those keys are rejected by the parameter validator (`ts_forecast_native.cpp` `ValidateParamKeys`).
The model currently implemented behind the `'ARIMA'` name is a fixed heuristic: AR(1) on the
first-differenced series with a hardcoded AR coefficient of 0.5 (falling back to Naive when the
series has fewer than 5 observations) — not a configurable Box-Jenkins ARIMA fit.
(being corrected in v0.10.0, Phase 10)

For order selection, information-criteria-based model search, and optional exogenous
regressors, use `AutoARIMA` (see `auto_arima.md`) instead — it calls into the crate's real
ARIMA search rather than this fixed heuristic.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `confidence_level` | DOUBLE | No | 0.90 | Confidence for prediction intervals |

No order parameters (`p`, `d`, `q`, `P`, `D`, `Q`, `s`) are accepted — see Description.

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
-- ARIMA's fixed AR(1)-on-differences heuristic (no order params accepted)
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'ARIMA',
    14,
    '1d',
    MAP{}
);

-- Prefer AutoARIMA for real order selection and seasonal support
SELECT * FROM ts_forecast_by(
    'weekly_data',
    store_id,
    date,
    revenue,
    'AutoARIMA',
    28,
    '1d',
    MAP{'seasonal_period': '7'}
);
```

## Best For

- Quick baseline comparison against AutoARIMA
- Not recommended for production use given orders are not configurable — use AutoARIMA instead
