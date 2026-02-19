# OptimizedTheta

> Theta Method with auto-optimized theta parameter

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'OptimizedTheta', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'OptimizedTheta', horizon, frequency, params);

-- With exogenous variables
SELECT * FROM ts_forecast_exog_by('table', group_col, date_col, value_col, 'x1,x2', 'future_table', 'OptimizedTheta', horizon, frequency, params);
```

## Description

Optimized Theta Method. Automatically finds the optimal theta parameter that minimizes forecast error. Supports exogenous variables for incorporating external regressors.

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
-- Non-seasonal data
SELECT * FROM ts_forecast_by(
    'monthly_sales',
    product_id,
    date,
    quantity,
    'OptimizedTheta',
    6,
    '1mo'
);

-- Seasonal data (with explicit period)
SELECT * FROM ts_forecast_by(
    'monthly_sales',
    product_id,
    date,
    quantity,
    'OptimizedTheta',
    12,
    '1mo',
    {'seasonal_period': 12}
);

-- With exogenous variables
SELECT * FROM ts_forecast_exog_by(
    'sales',
    product_id,
    date,
    amount,
    'temperature,promotion',
    'future_exog',
    'OptimizedTheta',
    6,
    '1mo'
);
```

## Best For

- Production forecasting without manual tuning
- When standard Theta with theta=2 doesn't perform well
- Incorporating external factors (with exogenous support)
- Automated forecasting pipelines
