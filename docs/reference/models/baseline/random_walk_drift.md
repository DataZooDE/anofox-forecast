# RandomWalkDrift

> Random walk with drift - captures linear trend without seasonality

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'RandomWalkDrift', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'RandomWalkDrift', horizon, frequency, params);
```

## Description

Random Walk with Drift. Extends Naive by adding a constant drift (average change) to each forecast step. Captures linear trend without seasonal components.

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
for calibrated coverage use the conformal functions. RandomWalkDrift's interval is the
drift-model band, widening with `h` from the in-sample residual standard deviation.

## SQL Example

```sql
-- Basic usage
SELECT * FROM ts_forecast_by(
    'monthly_sales',
    product_id,
    date,
    quantity,
    'RandomWalkDrift',
    6,
    '1d',
    MAP{}
);

-- For trending data
CREATE OR REPLACE TABLE growth_metrics AS
SELECT metric_id,
       (DATE '2020-01-01' + INTERVAL (i) DAY) AS date,
       100.0 + metric_id * 10.0 + 0.8 * i AS value
FROM range(0, 60) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS metric_id);

SELECT * FROM ts_forecast_by(
    'growth_metrics',
    metric_id,
    date,
    value,
    'RandomWalkDrift',
    12,
    '1d',
    MAP{'confidence_level': '0.90'}
);
```

## Best For

- Data with linear trend but no seasonality
- Baseline for trending data comparisons
- Financial data with drift
- Simple trend extrapolation
