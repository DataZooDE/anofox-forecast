# TSB

> Teunter-Syntetos-Babai Method for declining intermittent demand

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'TSB', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'TSB', horizon, params);
```

## Description

Teunter-Syntetos-Babai Method. An alternative to Croston that separately estimates demand probability and demand size. Better handles obsolescence and declining demand patterns.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `alpha_d` | DOUBLE | No | auto | Demand size smoothing coefficient |
| `alpha_p` | DOUBLE | No | auto | Demand probability smoothing coefficient |
| `confidence_level` | DOUBLE | No | 0.95 | Confidence for prediction intervals |
| `include_fitted` | BOOLEAN | No | false | Return in-sample fitted values |
| `include_residuals` | BOOLEAN | No | false | Return residuals |

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier (only for `_by` variant) |
| `ds` | TIMESTAMP | Forecast timestamp |
| `forecast` | DOUBLE | Point forecast |
| `lower` | DOUBLE | Lower prediction interval |
| `upper` | DOUBLE | Upper prediction interval |

## SQL Example

```sql
-- Basic usage (automatic smoothing)
SELECT * FROM ts_forecast_by(
    'spare_parts_demand',
    part_id,
    date,
    quantity,
    'TSB',
    30,
    {}
);

-- With custom smoothing parameters
SELECT * FROM ts_forecast_by(
    'obsolete_items',
    sku_id,
    date,
    demand,
    'TSB',
    14,
    {'alpha_d': 0.2, 'alpha_p': 0.3}
);
```

## Best For

- Items with declining demand (obsolescence)
- When demand probability changes over time
- Alternative to Croston for certain demand patterns
- Items transitioning to end-of-life
