# Naive

> Simplest baseline model - repeats the last observed value

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'Naive', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'Naive', horizon, frequency, params);
```

## Description

The simplest forecasting method. Repeats the last observed value for all future periods. Use as a **baseline** to evaluate more complex models.

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
for calibrated coverage use the conformal functions. Naive's interval is identical to R's
`naive()`: half-width = z · RMS(first differences) · √h. The fitted value of the first row is
always `NULL` (no prior observation to repeat it against). `Naive` is the one baseline model
in this reference that supports regressors via `ts_forecast_exog_by` — every other model on
this page errors on regressors.

## SQL Example

```sql
-- Basic usage (baseline forecast)
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'Naive',
    7,
    '1d',
    MAP{}
);

-- Compare with more complex model
WITH naive AS (
    SELECT * FROM ts_forecast_by('sales', product_id, date, val, 'Naive', 12, '1d', MAP{})
),
ets AS (
    SELECT * FROM ts_forecast_by('sales', product_id, date, val, 'AutoETS', 12, '1d', MAP{})
)
SELECT n.product_id, n.ds, n.yhat AS naive_fcst, e.yhat AS ets_fcst
FROM naive n JOIN ets e ON n.product_id = e.product_id AND n.ds = e.ds;
```

## Best For

- Establishing a baseline for model comparison
- Random walk data (stock prices)
- Very short forecast horizons
- When historical patterns provide no predictive value
