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

`yhat_lower` / `yhat_upper` are the crate `MFLES` model's own prediction interval at
`confidence_level` — see
[07-forecasting.md's "Prediction intervals, fitted values and input handling"](../../api/07-forecasting.md#prediction-intervals-fitted-values-and-input-handling)
for calibration caveats shared across all models.

`MFLES` accepts exogenous regressors via `ts_forecast_exog_by` and stays itself —
`model_name` reports `"MFLES with regressors"`. `MFLES` is also the one model in this family
that can run with **no** seasonal period at all (no `seasonal_periods`, nothing detected):
it falls back to non-seasonal rather than erroring.

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

-- With exogenous variables — MFLES stays itself ("MFLES with regressors")
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
    'future_weather', date, ['temperature'], '1d', 'MFLES', 7, MAP{}
);
-- model_name = 'MFLES with regressors'
```

## Best For

- High-frequency data with multiple seasonalities
- When you know the exact seasonal periods
- Energy demand, call center volumes
- Data with daily, weekly, and yearly patterns
