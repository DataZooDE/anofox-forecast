# Theta

> Theta Method - decomposes series into trend and drift components

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'Theta', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'Theta', horizon, frequency, params);
```

## Description

Standard Theta Method. Decomposes the time series into two "theta lines" - one capturing long-term trend (SES) and one capturing short-term behavior (drift). Combines forecasts from both lines with equal weights.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_period` | INTEGER | No | auto-detected | Seasonal period; auto-detected when both `seasonal_period` and `seasonal_periods` are omitted |
| `confidence_level` | DOUBLE | No | 0.90 | Confidence for prediction intervals |

The `theta` key itself is **not** an accepted SQL parameter — it is rejected by the parameter
validator (`ValidateParamKeys`). There is no way to pass a custom theta value from SQL today;
the crate's internal curvature parameter is fixed.

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier (only for `_by` variant) |
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast |
| `yhat_lower` | DOUBLE | Lower prediction interval |
| `yhat_upper` | DOUBLE | Upper prediction interval |

`yhat_lower` / `yhat_upper` are the crate `Theta` model's own prediction interval at
`confidence_level` — see
[07-forecasting.md's "Prediction intervals, fitted values and input handling"](../../api/07-forecasting.md#prediction-intervals-fitted-values-and-input-handling)
for calibration caveats shared across all models.

> **Known issue:** `Theta`'s interval is roughly **2× too wide** at `h ≥ 6` versus the
> reference R implementation (fix planned for v0.10.0, Phase 17). This applies to the whole Theta family
> — `Theta`, `OptimizedTheta`, `DynamicTheta`, `DynamicOptimizedTheta`, `AutoTheta`.

`Theta` accepts exogenous regressors via `ts_forecast_exog_by` and stays itself —
`model_name` reports `"Theta with regressors"`, never a plain-Theta substitution under a
different name.

## SQL Example

```sql
-- Basic usage (no theta key accepted from SQL)
SELECT * FROM ts_forecast_by(
    'monthly_sales',
    product_id,
    date,
    quantity,
    'Theta',
    6,
    '1mo'
);

-- With an explicit seasonal period
SELECT * FROM ts_forecast_by(
    'quarterly_revenue',
    region_id,
    date,
    revenue,
    'Theta',
    4,
    '3mo',
    MAP{'seasonal_period': '4'}
);

-- With exogenous regressors — Theta stays itself ("Theta with regressors")
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
    'future_weather', date, ['temperature'], '1d', 'Theta', 7, MAP{}
);
-- model_name = 'Theta with regressors'
```

## Best For

- Monthly or quarterly data
- Data with trend
- Simple but effective forecasting
- M3 competition-winning approach
