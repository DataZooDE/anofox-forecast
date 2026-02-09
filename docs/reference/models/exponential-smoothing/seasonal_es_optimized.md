# SeasonalESOptimized

> Seasonal Exponential Smoothing with auto-optimized parameters

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'SeasonalESOptimized', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'SeasonalESOptimized', horizon, params);
```

## Description

Optimized Seasonal Exponential Smoothing. Same as SeasonalES but automatically finds optimal alpha and gamma values that minimize forecast error.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_period` | INTEGER | **Yes** | — | Seasonal period (e.g., 7 for weekly, 12 for monthly) |
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
-- Automatically optimized parameters
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'SeasonalESOptimized',
    14,
    {'seasonal_period': 7}
);

-- With fitted values for analysis
SELECT * FROM ts_forecast_by(
    'weekly_data',
    store_id,
    date,
    revenue,
    'SeasonalESOptimized',
    28,
    {'seasonal_period': 7, 'include_fitted': true}
);
```

## Best For

- Seasonal data with no trend
- Production forecasting where manual tuning is impractical
- When you want SeasonalES but don't want to tune alpha/gamma
- Automated forecasting pipelines
