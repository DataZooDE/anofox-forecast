# TSB

> Teunter-Syntetos-Babai Method for declining intermittent demand

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'TSB', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'TSB', horizon, frequency, params);
```

## Description

Teunter-Syntetos-Babai Method. An alternative to Croston that separately estimates demand probability and demand size. Better handles obsolescence and declining demand patterns.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `confidence_level` | DOUBLE | No | 0.90 | Confidence for prediction intervals |

TSB does not take smoothing-parameter keys (`alpha_d`, `alpha_p`) from SQL — those are rejected
by the parameter validator (`ValidateParamKeys`). The crate's internal smoothing is not
configurable from the SQL surface today.

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
for calibrated coverage use the conformal functions. Like the Croston family, TSB's point
forecasts match `statsforecast` per the statistical audit, but interval calibration for
intermittent demand is not R-validated — prefer the conformal functions for calibrated
coverage. TSB does not accept regressors in `ts_forecast_exog_by` (errors).

## SQL Example

```sql
-- Basic usage (automatic smoothing; no alpha_d/alpha_p keys accepted)
SELECT * FROM ts_forecast_by(
    'spare_parts_demand',
    part_id,
    date,
    quantity,
    'TSB',
    30,
    '1d'
);

-- With an explicit confidence level
SELECT * FROM ts_forecast_by(
    'obsolete_items',
    sku_id,
    date,
    demand,
    'TSB',
    14,
    '1d',
    MAP{'confidence_level': '0.80'}
);
```

## Best For

- Items with declining demand (obsolescence)
- When demand probability changes over time
- Alternative to Croston for certain demand patterns
- Items transitioning to end-of-life
