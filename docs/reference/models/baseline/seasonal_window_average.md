# SeasonalWindowAverage

> Seasonal Window Average - combines SeasonalNaive with SMA

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'SeasonalWindowAverage', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'SeasonalWindowAverage', horizon, params);
```

## Description

Seasonal Window Average. Forecasts using the average of values from the same seasonal position over a sliding window. Combines the ideas of SeasonalNaive and SMA.

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
-- Weekly seasonality
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'SeasonalWindowAverage',
    14,
    {'seasonal_period': 7}
);

-- Monthly seasonality
SELECT * FROM ts_forecast_by(
    'monthly_data',
    store_id,
    date,
    revenue,
    'SeasonalWindowAverage',
    12,
    {'seasonal_period': 12}
);
```

## Best For

- Seasonal data with noise
- When SeasonalNaive is too sensitive to outliers
- Stable seasonal patterns with some variation
- Simple robust seasonal forecasting
