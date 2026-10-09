# DynamicTheta

> Theta Method with time-varying parameters for non-stationary data

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'DynamicTheta', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'DynamicTheta', horizon, frequency, params);

-- With exogenous variables
SELECT * FROM ts_forecast_exog_by('table', group_col, date_col, value_col, ['x1','x2'], 'future_table', future_date_col, ['x1','x2'], frequency, 'DynamicTheta', horizon, params);
```

## Description

Dynamic Theta Method. Uses time-varying theta values that adapt to local data characteristics. Better for non-stationary data where optimal theta changes over time.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_period` | INTEGER | No | auto-detected | Seasonal period; auto-detected when both `seasonal_period` and `seasonal_periods` are omitted |
| `confidence_level` | DOUBLE | No | 0.90 | Confidence for prediction intervals |

The `theta` key itself is **not** an accepted SQL parameter — it is rejected by the parameter
validator (`ValidateParamKeys`). There is no way to pass a custom initial theta value from SQL
today.

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier (only for `_by` variant) |
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast |
| `yhat_lower` | DOUBLE | Lower prediction interval |
| `yhat_upper` | DOUBLE | Upper prediction interval |

`yhat_lower` / `yhat_upper` are the crate `DynamicTheta` model's own prediction interval at
`confidence_level` — see
[07-forecasting.md's "Prediction intervals, fitted values and input handling"](../../api/07-forecasting.md#prediction-intervals-fitted-values-and-input-handling)
for calibration caveats shared across all models.

> **Known issue:** interval width is roughly **2× too wide** at `h ≥ 6` versus the reference
> R implementation, same as the rest of the Theta family (fix planned for v0.10.0, Phase 17).

`DynamicTheta` accepts exogenous regressors via `ts_forecast_exog_by` and stays itself —
`model_name` reports `"DynamicTheta with regressors"`.

## SQL Example

```sql
-- Basic usage (no theta key accepted from SQL)
SELECT * FROM ts_forecast_by(
    'monthly_sales',
    product_id,
    date,
    quantity,
    'DynamicTheta',
    6,
    '1mo'
);

-- With an explicit seasonal period
SELECT * FROM ts_forecast_by(
    'quarterly_data',
    region_id,
    date,
    revenue,
    'DynamicTheta',
    4,
    '3mo',
    MAP{'seasonal_period': '4'}
);

-- With exogenous variables — DynamicTheta stays itself ("DynamicTheta with regressors")
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
    'future_weather', date, ['temperature'], '1d', 'DynamicTheta', 7, MAP{}
);
-- model_name = 'DynamicTheta with regressors'
```

## Best For

- Non-stationary data where patterns change over time
- Long time series with evolving characteristics
- When standard Theta is too rigid
- Data with regime changes
