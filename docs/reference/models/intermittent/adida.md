# ADIDA

> Aggregate-Disaggregate approach for sparse intermittent demand

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'ADIDA', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'ADIDA', horizon, params);
```

## Description

Aggregate-Disaggregate Intermittent Demand Approach. Aggregates the time series to a lower frequency where demand is more regular, forecasts at that level, then disaggregates back to the original frequency.

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
    'intermittent_orders',
    product_id,
    date,
    quantity,
    'ADIDA',
    30,
    {}
);

-- For very sparse data
SELECT * FROM ts_forecast_by(
    'rare_events',
    event_type,
    date,
    count,
    'ADIDA',
    14,
    {'confidence_level': 0.90}
);
```

## Best For

- Very sparse intermittent demand
- When Croston methods don't perform well
- Data that becomes more regular at higher aggregation levels
- Extremely low-volume products
