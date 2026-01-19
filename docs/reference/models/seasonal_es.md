# SeasonalES

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'SeasonalES', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'SeasonalES', horizon, params);
```

## Description

Seasonal Exponential Smoothing. Combines SES with a seasonal component but without a trend component. For data with stable seasonality and no trend.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_period` | INTEGER | **Yes** | — | Seasonal period (e.g., 7 for weekly, 12 for monthly) |
| `alpha` | DOUBLE | No | 0.3 | Level smoothing coefficient (0-1) |
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
-- Weekly seasonality, no trend
SELECT * FROM ts_forecast_by(
    'daily_visits',
    store_id,
    date,
    visitors,
    'SeasonalES',
    14,
    {'seasonal_period': 7}
);

-- With custom smoothing
SELECT * FROM ts_forecast_by(
    'weekly_patterns',
    location_id,
    date,
    traffic,
    'SeasonalES',
    21,
    {'seasonal_period': 7, 'alpha': 0.4, 'gamma': 0.2}
);
```

## Best For

- Seasonal data with no clear trend
- Stable, repeating patterns
- Data where the mean level doesn't change over time
- Simpler alternative to HoltWinters when trend isn't needed
