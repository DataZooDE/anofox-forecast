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
| `confidence_level` | DOUBLE | No | 0.95 | Confidence for prediction intervals |
| `include_fitted` | BOOLEAN | No | false | Return in-sample fitted values |
| `include_residuals` | BOOLEAN | No | false | Return residuals |

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier (only for `_by` variant) |
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast |
| `yhat_lower` | DOUBLE | Lower prediction interval |
| `yhat_upper` | DOUBLE | Upper prediction interval |

## SQL Example

```sql
-- Basic usage
SELECT * FROM ts_forecast_by(
    'monthly_sales',
    product_id,
    date,
    revenue,
    'RandomWalkDrift',
    6,
    '1d',
    {}
);

-- For trending data
SELECT * FROM ts_forecast_by(
    'growth_metrics',
    metric_id,
    date,
    value,
    'RandomWalkDrift',
    12,
    '1d',
    {'confidence_level': 0.90}
);
```

## Best For

- Data with linear trend but no seasonality
- Baseline for trending data comparisons
- Financial data with drift
- Simple trend extrapolation
