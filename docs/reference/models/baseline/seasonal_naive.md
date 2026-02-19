# SeasonalNaive

> Repeats values from the same season in the previous cycle

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'SeasonalNaive', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'SeasonalNaive', horizon, frequency, params);
```

## Description

Seasonal Naive method. Repeats values from the same season in the previous cycle. For example, Monday's forecast equals last Monday's value. Essential **baseline for seasonal data**.

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
-- Weekly seasonality (daily data)
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'SeasonalNaive',
    14,
    '1d',
    {'seasonal_period': 7}
);

-- Monthly seasonality (monthly data)
SELECT * FROM ts_forecast_by(
    'monthly_revenue',
    store_id,
    date,
    revenue,
    'SeasonalNaive',
    12,
    '1d',
    {'seasonal_period': 12}
);
```

## Best For

- Strong seasonal patterns with little trend
- Baseline for seasonal forecasting comparisons
- Retail data with weekly patterns
- Monthly data with yearly seasonality
