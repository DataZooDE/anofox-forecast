# CrostonSBA

> Croston with Syntetos-Boylan bias correction

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'CrostonSBA', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'CrostonSBA', horizon, params);
```

## Description

Croston's Method with Syntetos-Boylan Approximation (SBA). Applies a bias correction to classic Croston's method, which tends to overestimate demand. Generally more accurate than CrostonClassic.

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
| `ds` | TIMESTAMP | Forecast timestamp |
| `forecast` | DOUBLE | Point forecast |
| `lower` | DOUBLE | Lower prediction interval |
| `upper` | DOUBLE | Upper prediction interval |

## SQL Example

```sql
-- Bias-corrected forecasting
SELECT * FROM ts_forecast_by(
    'spare_parts_orders',
    part_id,
    date,
    quantity,
    'CrostonSBA',
    30,
    {}
);

-- For inventory planning
SELECT * FROM ts_forecast_by(
    'slow_moving_items',
    sku_id,
    date,
    demand,
    'CrostonSBA',
    28,
    {'confidence_level': 0.90}
);
```

## Best For

- **Recommended over CrostonClassic** in most cases
- Intermittent demand with bias concerns
- Inventory management and service level planning
- When CrostonClassic overestimates demand
