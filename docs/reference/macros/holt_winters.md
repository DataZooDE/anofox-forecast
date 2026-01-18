# HoltWinters

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'HoltWinters', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'HoltWinters', horizon, params);
```

## Description

Holt-Winters Seasonal Method (Triple Exponential Smoothing). Extends Holt's method by adding a seasonal component. The workhorse for data with both trend and seasonality.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_period` | INTEGER | **Yes** | — | Seasonal period (e.g., 7 for weekly, 12 for monthly) |
| `alpha` | DOUBLE | No | 0.3 | Level smoothing coefficient (0-1) |
| `beta` | DOUBLE | No | 0.1 | Trend smoothing coefficient (0-1) |
| `gamma` | DOUBLE | No | 0.1 | Seasonal smoothing coefficient (0-1) |
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
-- Weekly seasonality with trend
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'HoltWinters',
    14,
    {'seasonal_period': 7}
);

-- Monthly data with yearly seasonality
SELECT * FROM ts_forecast_by(
    'monthly_revenue',
    store_id,
    date,
    revenue,
    'HoltWinters',
    12,
    {'seasonal_period': 12, 'alpha': 0.2, 'beta': 0.1, 'gamma': 0.15}
);
```

## Best For

- Data with both trend and single seasonality
- Classic retail, demand forecasting
- Monthly data with yearly patterns
- Daily data with weekly patterns
