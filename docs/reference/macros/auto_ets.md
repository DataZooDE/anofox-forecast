# AutoETS

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'AutoETS', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'AutoETS', horizon, params);
```

## Description

Automatic Exponential Smoothing State Space model selection. Automatically selects the best ETS model (Error, Trend, Seasonal components) based on information criteria. This is the **default model** for most forecasting tasks.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
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
-- Basic usage (recommended starting point)
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'AutoETS',
    12,
    {}
);

-- With custom confidence level
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'AutoETS',
    12,
    {'confidence_level': 0.90}
);

-- With explicit seasonal period
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'AutoETS',
    30,
    {'seasonal_period': 7}
);
```

## Best For

- Unknown data characteristics (automatic model selection)
- General-purpose forecasting when you don't know which model to use
- Time series with or without trend and seasonality
- First choice for most forecasting tasks
