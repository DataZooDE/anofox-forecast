# Holt

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'Holt', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'Holt', horizon, params);
```

## Description

Holt's Linear Trend Method (Double Exponential Smoothing). Extends SES by adding a trend component. Suitable for data with trend but no seasonality.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `alpha` | DOUBLE | No | 0.3 | Level smoothing coefficient (0-1) |
| `beta` | DOUBLE | No | 0.1 | Trend smoothing coefficient (0-1) |
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
-- Basic usage (default parameters)
SELECT * FROM ts_forecast_by(
    'monthly_revenue',
    store_id,
    date,
    revenue,
    'Holt',
    6,
    {}
);

-- With custom smoothing parameters
SELECT * FROM ts_forecast_by(
    'quarterly_sales',
    region_id,
    date,
    sales,
    'Holt',
    4,
    {'alpha': 0.4, 'beta': 0.2}
);
```

## Best For

- Data with linear trend but no seasonality
- Short to medium-term forecasting with trending data
- Growth metrics, sales trends
- When SES doesn't capture the upward/downward movement
