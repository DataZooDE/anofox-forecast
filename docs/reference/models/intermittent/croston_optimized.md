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
| `confidence_level` | DOUBLE | No | 0.90 | Confidence for prediction intervals |

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier (only for `_by` variant) |
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast |
| `yhat_lower` | DOUBLE | Lower prediction interval |
| `yhat_upper` | DOUBLE | Upper prediction interval |

`yhat_lower` / `yhat_upper` are currently a heuristic `point ± z·SD(history)·√h` band, not a
model-based interval and not statistically calibrated. For calibrated coverage, use conformal
prediction intervals (see `docs/api/11-conformal-prediction.md`) on top of the point forecast.
(being corrected in v0.10.0, Phase 10)

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

-- With an explicit confidence level
SELECT * FROM ts_forecast_by(
    'intermittent_sales',
    product_id,
    date,
    units,
    'CrostonOptimized',
    14,
    '1d',
    MAP{'confidence_level': '0.80'}
);
```

## Best For

- Intermittent demand forecasting
- When CrostonClassic doesn't perform well
- Production systems without manual tuning
- Spare parts and slow-moving inventory
