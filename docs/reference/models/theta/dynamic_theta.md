# DynamicTheta

> Theta Method with time-varying parameters for non-stationary data

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'DynamicTheta', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'DynamicTheta', horizon, frequency, params);

-- With exogenous variables
SELECT * FROM ts_forecast_exog_by('table', group_col, date_col, value_col, 'x1,x2', 'future_table', 'DynamicTheta', horizon, frequency, params);
```

## Description

Dynamic Theta Method. Uses time-varying theta values that adapt to local data characteristics. Better for non-stationary data where optimal theta changes over time.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `theta` | DOUBLE | No | 2.0 | Initial theta parameter |
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
-- Basic usage
SELECT * FROM ts_forecast_by(
    'monthly_sales',
    product_id,
    date,
    quantity,
    'DynamicTheta',
    6,
    '1mo'
);

-- With custom initial theta
SELECT * FROM ts_forecast_by(
    'quarterly_data',
    region_id,
    date,
    revenue,
    'DynamicTheta',
    4,
    '3mo',
    {'theta': 2.5}
);
```

## Best For

- Non-stationary data where patterns change over time
- Long time series with evolving characteristics
- When standard Theta is too rigid
- Data with regime changes
