# SMA

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'SMA', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'SMA', horizon, params);
```

## Description

Simple Moving Average. Forecasts the average of the last `window` observations. A step up from Naive, smooths out noise while remaining simple.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `window` | INTEGER | No | 5 | Number of periods to average |
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
-- Basic usage (default window=5)
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'SMA',
    7,
    {}
);

-- With custom window size
SELECT * FROM ts_forecast_by(
    'weekly_data',
    store_id,
    date,
    revenue,
    'SMA',
    4,
    {'window': 12}
);
```

## Best For

- Noisy data with no clear trend or seasonality
- Short-term forecasting where recent average is a good predictor
- Simple baseline that accounts for recent variation
- Data that fluctuates around a stable mean
