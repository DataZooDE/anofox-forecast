# IMAPA

> Multi-aggregation prediction for intermittent demand

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'IMAPA', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'IMAPA', horizon, params);
```

## Description

Intermittent Multiple Aggregation Prediction Algorithm. Combines forecasts from multiple temporal aggregation levels using optimal weights. An extension of ADIDA that considers multiple aggregation levels simultaneously.

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
-- Basic usage
SELECT * FROM ts_forecast_by(
    'intermittent_demand',
    item_id,
    date,
    quantity,
    'IMAPA',
    30,
    {}
);

-- For spare parts inventory
SELECT * FROM ts_forecast_by(
    'spare_parts',
    part_number,
    date,
    demand,
    'IMAPA',
    60,
    {'confidence_level': 0.95}
);
```

## Best For

- Intermittent demand forecasting at multiple scales
- When optimal aggregation level is unknown
- Service parts and maintenance demand
- Ensemble approach to intermittent forecasting
