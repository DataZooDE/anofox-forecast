# CrostonOptimized

> Optimized Croston's Method with auto-tuned smoothing parameters

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'CrostonOptimized', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'CrostonOptimized', horizon, frequency, params);
```

## Description

Optimized Croston's Method. Same as CrostonClassic but automatically optimizes the smoothing parameters for better forecast accuracy.

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
-- Optimized parameters for spare parts
SELECT * FROM ts_forecast_by(
    'spare_parts_orders',
    part_id,
    date,
    quantity,
    'CrostonOptimized',
    30,
    '1d'
);

-- Production forecasting
SELECT * FROM ts_forecast_by(
    'intermittent_sales',
    product_id,
    date,
    units,
    'CrostonOptimized',
    14,
    '1d',
    {'include_fitted': true}
);
```

## Best For

- Intermittent demand forecasting
- When CrostonClassic doesn't perform well
- Production systems without manual tuning
- Spare parts and slow-moving inventory
