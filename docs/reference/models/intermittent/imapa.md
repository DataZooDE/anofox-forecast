# IMAPA

> Multi-aggregation prediction for intermittent demand

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'IMAPA', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'IMAPA', horizon, frequency, params);
```

## Description

Intermittent Multiple Aggregation Prediction Algorithm. Combines forecasts from multiple temporal aggregation levels using optimal weights. An extension of ADIDA that considers multiple aggregation levels simultaneously.

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
for calibrated coverage use the conformal functions. Like the Croston family, IMAPA's point
forecasts match `statsforecast` per the statistical audit, but interval calibration for
intermittent demand is not R-validated — prefer the conformal functions for calibrated
coverage. IMAPA does not accept regressors in `ts_forecast_exog_by` (errors).

## SQL Example

```sql
-- Basic usage
SELECT * FROM ts_forecast_by(
    'intermittent_sales',
    product_id,
    date,
    units,
    'IMAPA',
    30,
    '1d'
);

-- For spare parts inventory
SELECT * FROM ts_forecast_by(
    'spare_parts_orders',
    part_id,
    date,
    quantity,
    'IMAPA',
    60,
    '1d',
    MAP{'confidence_level': '0.95'}
);
```

## Best For

- Intermittent demand forecasting at multiple scales
- When optimal aggregation level is unknown
- Service parts and maintenance demand
- Ensemble approach to intermittent forecasting
