# CrostonClassic

> Classic Croston's Method for intermittent demand with many zeros

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'CrostonClassic', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'CrostonClassic', horizon, frequency, params);
```

## Description

Classic Croston's Method for intermittent demand forecasting. Separately forecasts the demand size and the inter-demand interval using exponential smoothing. Designed for sparse data with many zeros.

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
for calibrated coverage use the conformal functions. CrostonClassic's point forecasts match
`statsforecast` per the statistical audit, but interval calibration for intermittent demand
is not R-validated — for calibrated coverage, prefer the conformal functions over this
interval. CrostonClassic does not accept regressors in `ts_forecast_exog_by` (errors).

## SQL Example

```sql
-- Basic usage for spare parts demand
SELECT * FROM ts_forecast_by(
    'spare_parts_orders',
    part_id,
    date,
    quantity,
    'CrostonClassic',
    30,
    '1d'
);

-- For intermittent sales data
CREATE OR REPLACE TABLE low_volume_products AS
SELECT sku_id, (DATE '2020-01-01' + INTERVAL (i) DAY) AS date,
       CASE WHEN (i + sku_id) % 7 = 0 THEN 2.0 + (i % 3) ELSE 0.0 END AS sales
FROM range(0, 60) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS sku_id);

SELECT * FROM ts_forecast_by(
    'low_volume_products',
    sku_id,
    date,
    sales,
    'CrostonClassic',
    14,
    '1d',
    MAP{'confidence_level': '0.90'}
);
```

## Best For

- Intermittent demand with many zero values
- Spare parts and slow-moving inventory
- Low-volume products
- Data where demand occurs sporadically
