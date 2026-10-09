# AutoARIMA

> Automatic ARIMA model selection with optional exogenous variables

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'AutoARIMA', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'AutoARIMA', horizon, frequency, params);

-- With exogenous variables
SELECT * FROM ts_forecast_exog_by('table', group_col, date_col, value_col, ['x1','x2'], 'future_table', future_date_col, ['x1','x2'], frequency, 'AutoARIMA', horizon, params);
```

## Description

Automatic ARIMA (AutoRegressive Integrated Moving Average) model selection. Automatically
searches `(p, d, q)` orders and seasonal `(P, D, Q, s)` components via the crate's own
order search, and reports the SELECTED order in `model_name` (e.g. `"AutoARIMA(2,0,1)"` or
`"AutoARIMA(1,1,1)(0,1,1)[12]"`). The selected order can differ from R's `auto.arima()` on
the same series (a different order-search heuristic) — the FITTED order's own forecast still
matches R within 1% at that order (tracked for Phase 11).

Supports exogenous variables via `ts_forecast_exog_by`: the crate's own order search runs
WITH the regressors attached, and `model_name` reports the selected order plus
`" with regressors"` (e.g. `"AutoARIMA(3,0,1)(0,1,0)[9] with regressors"`) — never a fixed
`"ARIMAX"` stand-in name.

> **Note:** When both `seasonal_period` and `seasonal_periods` are omitted, `ts_forecast_by`
> (and `ts_cv_forecast_by`) auto-detects the seasonal period internally (`detect_seasonality`);
> a period that can't be detected falls back to non-seasonal (AutoARIMA can run non-seasonally).
> Passing `seasonal_period` explicitly via `ts_detect_periods_agg` is still recommended for
> control and reproducibility — `ts_forecast_agg` does NOT auto-detect.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_period` | INTEGER | No | auto-detected | Seasonal period (e.g., 7 for weekly, 12 for monthly); auto-detected when omitted, non-seasonal if none found |
| `confidence_level` | DOUBLE | No | 0.90 | Confidence for prediction intervals |

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier (only for `_by` variant) |
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast |
| `yhat_lower` | DOUBLE | Lower prediction interval |
| `yhat_upper` | DOUBLE | Upper prediction interval |

`yhat_lower` / `yhat_upper` are the crate `AutoARIMA` model's own prediction interval at the
SELECTED order, at `confidence_level` — see
[07-forecasting.md's "Prediction intervals, fitted values and input handling"](../../api/07-forecasting.md#prediction-intervals-fitted-values-and-input-handling)
for the order-selection known issue and calibration caveats shared across all models.

## SQL Example

```sql
CREATE OR REPLACE TABLE daily_sales AS
SELECT product_id,
       (DATE '2024-01-01' + INTERVAL (i) DAY) AS date,
       50.0 + product_id * 5.0 + 6.0 * sin(2 * pi() * i / 7.0) AS quantity
FROM range(0, 130) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS product_id);

-- Forecast with explicit seasonal period; model_name reports the selected order
SELECT DISTINCT model_name
FROM ts_forecast_by(
    'daily_sales', product_id, date, quantity, 'AutoARIMA', 12, '1d',
    MAP{'seasonal_period': '7'}
);

-- With exogenous variables: the order search runs WITH the regressor attached;
-- model_name reports the selected order plus " with regressors"
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

SELECT DISTINCT model_name
FROM ts_forecast_exog_by(
    'sales_history', product_id, date, revenue, ['temperature'],
    'future_weather', date, ['temperature'], '1d', 'AutoARIMA', 7, MAP{}
);
-- model_name = 'AutoARIMA(3,0,1)(0,1,0)[9] with regressors' (selected order will vary with data)
```

## Best For

- Time series with complex autocorrelation structures
- When you need to incorporate external regressors (promotions, weather, etc.)
- Stationary or differenced stationary data
- Alternative to ETS when data shows ARIMA-like patterns
