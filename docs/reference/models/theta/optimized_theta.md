# OptimizedTheta

> Theta Method with auto-optimized theta parameter

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'OptimizedTheta', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'OptimizedTheta', horizon, frequency, params);

-- With exogenous variables
SELECT * FROM ts_forecast_exog_by('table', group_col, date_col, value_col, ['x1','x2'], 'future_table', future_date_col, ['x1','x2'], frequency, 'OptimizedTheta', horizon, params);
```

## Description

Optimized Theta Method. Automatically finds the optimal theta parameter that minimizes forecast error. Supports exogenous variables for incorporating external regressors.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_period` | INTEGER | No | auto-detected | Seasonal period; auto-detected when both `seasonal_period` and `seasonal_periods` are omitted |
| `confidence_level` | DOUBLE | No | 0.90 | Confidence for prediction intervals |

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier (only for `_by` variant) |
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast |
| `yhat_lower` | DOUBLE | Lower prediction interval |
| `yhat_upper` | DOUBLE | Upper prediction interval |

`yhat_lower` / `yhat_upper` are the crate `OptimizedTheta` model's own prediction interval at
`confidence_level` — see
[07-forecasting.md's "Prediction intervals, fitted values and input handling"](../../api/07-forecasting.md#prediction-intervals-fitted-values-and-input-handling)
for calibration caveats shared across all models.

> **Known issue:** interval width is roughly **2× too wide** at `h ≥ 6` versus the reference
> R implementation, same as the rest of the Theta family (tracked for Phase 17).

`OptimizedTheta` accepts exogenous regressors via `ts_forecast_exog_by` and stays itself —
`model_name` reports `"OptimizedTheta with regressors"`.

## SQL Example

```sql
-- Non-seasonal data
SELECT * FROM ts_forecast_by(
    'monthly_sales',
    product_id,
    date,
    quantity,
    'OptimizedTheta',
    6,
    '1mo'
);

-- Seasonal data (with explicit period)
SELECT * FROM ts_forecast_by(
    'monthly_sales',
    product_id,
    date,
    quantity,
    'OptimizedTheta',
    12,
    '1mo',
    MAP{'seasonal_period': '12'}
);

-- With exogenous variables — OptimizedTheta stays itself ("OptimizedTheta with regressors")
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
    'future_weather', date, ['temperature'], '1d', 'OptimizedTheta', 7, MAP{}
);
-- model_name = 'OptimizedTheta with regressors'
```

## Best For

- Production forecasting without manual tuning
- When standard Theta with theta=2 doesn't perform well
- Incorporating external factors (with exogenous support)
- Automated forecasting pipelines
