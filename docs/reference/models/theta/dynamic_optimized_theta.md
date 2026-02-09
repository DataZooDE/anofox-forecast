# DynamicOptimizedTheta

> Combines dynamic theta adaptation with automatic optimization

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'DynamicOptimizedTheta', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'DynamicOptimizedTheta', horizon, params);
```

## Description

Dynamic Optimized Theta Method. Combines dynamic theta adaptation with automatic optimization. Finds optimal parameters while allowing time-varying behavior.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_period` | INTEGER | No* | — | Seasonal period (required for seasonal data) |
| `confidence_level` | DOUBLE | No | 0.95 | Confidence for prediction intervals |
| `include_fitted` | BOOLEAN | No | false | Return in-sample fitted values |
| `include_residuals` | BOOLEAN | No | false | Return residuals |

*Seasonality is NOT auto-detected. Pass `seasonal_period` explicitly for seasonal data.

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
-- Basic usage (fully automatic)
SELECT * FROM ts_forecast_by(
    'monthly_sales',
    product_id,
    date,
    quantity,
    'DynamicOptimizedTheta',
    6,
    {}
);

-- With seasonal period hint
SELECT * FROM ts_forecast_by(
    'quarterly_data',
    region_id,
    date,
    revenue,
    'DynamicOptimizedTheta',
    8,
    {'seasonal_period': 4}
);
```

## Best For

- Most sophisticated Theta variant
- Non-stationary data with evolving patterns
- Automated production forecasting
- When you want the best of both dynamic and optimized approaches
