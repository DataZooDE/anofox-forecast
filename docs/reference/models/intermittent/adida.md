# ADIDA

> Aggregate-Disaggregate approach for sparse intermittent demand

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'ADIDA', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'ADIDA', horizon, frequency, params);
```

## Description

Aggregate-Disaggregate Intermittent Demand Approach. Aggregates the time series to a lower frequency where demand is more regular, forecasts at that level, then disaggregates back to the original frequency.

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
for calibrated coverage use the conformal functions. Like the Croston family, ADIDA's point
forecasts match `statsforecast` per the statistical audit, but interval calibration for
intermittent demand is not R-validated — prefer the conformal functions for calibrated
coverage. ADIDA does not accept regressors in `ts_forecast_exog_by` (errors).

## SQL Example

```sql
-- Basic usage
CREATE OR REPLACE TABLE intermittent_orders AS
SELECT product_id, (DATE '2020-01-01' + INTERVAL (i) DAY) AS date,
       CASE WHEN (i + product_id) % 6 = 0 THEN 2.0 + (i % 3) ELSE 0.0 END AS quantity
FROM range(0, 60) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS product_id);

SELECT * FROM ts_forecast_by(
    'intermittent_orders',
    product_id,
    date,
    quantity,
    'ADIDA',
    30,
    '1d'
);

-- For very sparse data
CREATE OR REPLACE TABLE rare_events AS
SELECT event_type, (DATE '2020-01-01' + INTERVAL (i) DAY) AS date,
       CASE WHEN (i + event_type) % 10 = 0 THEN 1.0 ELSE 0.0 END AS count
FROM range(0, 60) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS event_type);

SELECT * FROM ts_forecast_by(
    'rare_events',
    event_type,
    date,
    count,
    'ADIDA',
    14,
    '1d',
    MAP{'confidence_level': '0.90'}
);
```

## Best For

- Very sparse intermittent demand
- When Croston methods don't perform well
- Data that becomes more regular at higher aggregation levels
- Extremely low-volume products
