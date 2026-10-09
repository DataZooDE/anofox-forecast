# CrostonSBA

> Croston with Syntetos-Boylan bias correction

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'CrostonSBA', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'CrostonSBA', horizon, frequency, params);
```

## Description

Croston's Method with Syntetos-Boylan Approximation (SBA). Applies a bias correction to classic Croston's method, which tends to overestimate demand. Generally more accurate than CrostonClassic.

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
-- Bias-corrected forecasting
SELECT * FROM ts_forecast_by(
    'spare_parts_orders',
    part_id,
    date,
    quantity,
    'CrostonSBA',
    30,
    '1d'
);

-- For inventory planning
SELECT * FROM ts_forecast_by(
    'slow_moving_items',
    sku_id,
    date,
    demand,
    'CrostonSBA',
    28,
    '1d',
    MAP{'confidence_level': '0.90'}
);
```

## Best For

- **Recommended over CrostonClassic** in most cases
- Intermittent demand with bias concerns
- Inventory management and service level planning
- When CrostonClassic overestimates demand
