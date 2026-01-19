# Theta

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'Theta', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'Theta', horizon, params);
```

## Description

Standard Theta Method. Decomposes the time series into two "theta lines" - one capturing long-term trend (SES) and one capturing short-term behavior (drift). Combines forecasts from both lines with equal weights.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `theta` | DOUBLE | No | 2.0 | Theta parameter (controls curvature adjustment) |
| `seasonal_period` | INTEGER | No | 0 | Seasonal period (0 = auto-detect) |
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
-- Basic usage (default theta=2.0)
SELECT * FROM ts_forecast_by(
    'monthly_sales',
    product_id,
    date,
    quantity,
    'Theta',
    6,
    {}
);

-- With custom theta parameter
SELECT * FROM ts_forecast_by(
    'quarterly_revenue',
    region_id,
    date,
    revenue,
    'Theta',
    4,
    {'theta': 3.0}
);
```

## Best For

- Monthly or quarterly data
- Data with trend
- Simple but effective forecasting
- M3 competition-winning approach
