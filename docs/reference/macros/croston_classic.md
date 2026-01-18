# CrostonClassic

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'CrostonClassic', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'CrostonClassic', horizon, params);
```

## Description

Classic Croston's Method for intermittent demand forecasting. Separately forecasts the demand size and the inter-demand interval using exponential smoothing. Designed for sparse data with many zeros.

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
-- Basic usage for spare parts demand
SELECT * FROM ts_forecast_by(
    'spare_parts_orders',
    part_id,
    date,
    quantity,
    'CrostonClassic',
    30,
    {}
);

-- For intermittent sales data
SELECT * FROM ts_forecast_by(
    'low_volume_products',
    sku_id,
    date,
    sales,
    'CrostonClassic',
    14,
    {'confidence_level': 0.90}
);
```

## Best For

- Intermittent demand with many zero values
- Spare parts and slow-moving inventory
- Low-volume products
- Data where demand occurs sporadically
