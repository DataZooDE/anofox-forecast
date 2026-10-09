# CrostonSBA

> Croston with Syntetos-Boylan bias correction

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'CrostonSBA', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'CrostonSBA', horizon, frequency, params);
```

## Description

Croston's Method with Syntetos-Boylan Approximation (SBA). Applies a bias correction to classic Croston's method, which tends to overestimate demand. Generally more accurate than CrostonClassic.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `confidence_level` | DOUBLE | No | 0.90 | Confidence for prediction intervals |

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier (only for `_by` variant) |
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast |
| `yhat_lower` | DOUBLE | Lower prediction interval |
| `yhat_upper` | DOUBLE | Upper prediction interval |

`yhat_lower` / `yhat_upper` are the model's own prediction interval (crate
`predict_with_intervals`) at `confidence_level` (default 0.90). See
[Prediction intervals, fitted values and input handling](../../../api/07-forecasting.md#prediction-intervals-fitted-values-and-input-handling);
for calibrated coverage use the conformal functions. Like CrostonClassic, interval
calibration for intermittent demand is not R-validated — prefer the conformal functions for
calibrated coverage. CrostonSBA does not accept regressors in `ts_forecast_exog_by` (errors).

## SQL Example

```sql
-- Bias-corrected forecasting
SELECT * FROM ts_forecast_by(
    'spare_parts_orders',
    part_id,
    date,
    quantity,
    'CrostonSBA',
    30,
    '1d'
);

-- For inventory planning
CREATE OR REPLACE TABLE slow_moving_items AS
SELECT sku_id, (DATE '2020-01-01' + INTERVAL (i) DAY) AS date,
       CASE WHEN (i + sku_id) % 8 = 0 THEN GREATEST(0.0, 4.0 - 0.02 * i) ELSE 0.0 END AS demand
FROM range(0, 60) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS sku_id);

SELECT * FROM ts_forecast_by(
    'slow_moving_items',
    sku_id,
    date,
    demand,
    'CrostonSBA',
    28,
    '1d',
    MAP{'confidence_level': '0.90'}
);
```

## Best For

- **Recommended over CrostonClassic** in most cases
- Intermittent demand with bias concerns
- Inventory management and service level planning
- When CrostonClassic overestimates demand
