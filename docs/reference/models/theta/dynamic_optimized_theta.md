# DynamicOptimizedTheta

> Combines dynamic theta adaptation with automatic optimization

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'DynamicOptimizedTheta', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'DynamicOptimizedTheta', horizon, frequency, params);
```

## Description

Dynamic Optimized Theta Method. Combines dynamic theta adaptation with automatic optimization. Finds optimal parameters while allowing time-varying behavior.

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

`yhat_lower` / `yhat_upper` are the crate `DynamicOptimizedTheta` model's own prediction
interval at `confidence_level` — see
[07-forecasting.md's "Prediction intervals, fitted values and input handling"](../../api/07-forecasting.md#prediction-intervals-fitted-values-and-input-handling)
for calibration caveats shared across all models.

> **Known issue:** interval width is roughly **2× too wide** at `h ≥ 6` versus the reference
> R implementation, same as the rest of the Theta family (tracked for Phase 17).

`DynamicOptimizedTheta` accepts exogenous regressors via `ts_forecast_exog_by` and stays
itself — `model_name` reports `"DynamicOptimizedTheta with regressors"`.

## SQL Example

```sql
-- Basic usage (fully automatic)
SELECT * FROM ts_forecast_by(
    'monthly_sales',
    product_id,
    date,
    quantity,
    'DynamicOptimizedTheta',
    6,
    '1mo'
);

-- With seasonal period hint
SELECT * FROM ts_forecast_by(
    'quarterly_data',
    region_id,
    date,
    revenue,
    'DynamicOptimizedTheta',
    8,
    '3mo',
    MAP{'seasonal_period': '4'}
);

-- With exogenous variables — DynamicOptimizedTheta stays itself ("... with regressors")
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
    'future_weather', date, ['temperature'], '1d', 'DynamicOptimizedTheta', 7, MAP{}
);
-- model_name = 'DynamicOptimizedTheta with regressors'
```

## Best For

- Most sophisticated Theta variant
- Non-stationary data with evolving patterns
- Automated production forecasting
- When you want the best of both dynamic and optimized approaches
