# SESOptimized

> Simple Exponential Smoothing with automatically optimized alpha parameter

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'SESOptimized', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'SESOptimized', horizon, frequency, params);
```

## Description

Optimized Simple Exponential Smoothing. Same as SES but automatically finds the optimal alpha value that minimizes forecast error on historical data.

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
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast |
| `yhat_lower` | DOUBLE | Lower prediction interval |
| `yhat_upper` | DOUBLE | Upper prediction interval |

## SQL Example

```sql
-- Basic usage (alpha automatically optimized)
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'SESOptimized',
    14,
    '1d',
    {}
);

-- With fitted values for analysis
SELECT * FROM ts_forecast_by(
    'weekly_data',
    store_id,
    date,
    revenue,
    'SESOptimized',
    8,
    '1d',
    {'include_fitted': true}
);
```

## Best For

- Data with no trend or seasonality
- When you don't want to manually tune the alpha parameter
- Slightly better accuracy than fixed-alpha SES
- Production forecasting where manual tuning is impractical
