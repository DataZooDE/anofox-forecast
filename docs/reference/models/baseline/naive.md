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
-- Basic usage (baseline forecast)
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'Naive',
    7,
    '1d',
    {}
);

-- Compare with more complex model
WITH naive AS (
    SELECT * FROM ts_forecast_by('sales', id, date, val, 'Naive', 12, '1d', {})
),
ets AS (
    SELECT * FROM ts_forecast_by('sales', id, date, val, 'AutoETS', 12, '1d', {})
)
SELECT n.id, n.ds, n.forecast AS naive_fcst, e.forecast AS ets_fcst
FROM naive n JOIN ets e ON n.id = e.id AND n.ds = e.ds;
```

## Best For

- Establishing a baseline for model comparison
- Random walk data (stock prices)
- Very short forecast horizons
- When historical patterns provide no predictive value
