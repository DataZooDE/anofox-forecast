# SES

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'SES', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'SES', horizon, params);
```

## Description

Simple Exponential Smoothing. Produces flat forecasts using exponentially weighted average of past observations. More recent observations have higher weight. Good for data with no trend or seasonality.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `alpha` | DOUBLE | No | 0.3 | Smoothing coefficient (0-1, higher = more weight on recent) |
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
-- Basic usage (default alpha=0.3)
SELECT * FROM ts_forecast_by(
    'daily_inventory',
    sku_id,
    date,
    stock_level,
    'SES',
    7,
    {}
);

-- With custom smoothing
SELECT * FROM ts_forecast_by(
    'sensor_data',
    sensor_id,
    timestamp,
    reading,
    'SES',
    24,
    {'alpha': 0.5}
);
```

## Best For

- Data with no trend or seasonality
- Short-term forecasting
- Stable time series that fluctuate around a mean
- When you want more control than Naive but simple implementation
