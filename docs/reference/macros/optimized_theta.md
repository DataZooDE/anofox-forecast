# OptimizedTheta

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'OptimizedTheta', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'OptimizedTheta', horizon, params);

-- With exogenous variables
SELECT * FROM ts_forecast_exog_by('table', group_col, date_col, value_col, 'x1,x2', 'future_table', 'OptimizedTheta', horizon, params);
```

## Description

Optimized Theta Method. Automatically finds the optimal theta parameter that minimizes forecast error. Supports exogenous variables for incorporating external regressors.

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
-- Basic usage (theta automatically optimized)
SELECT * FROM ts_forecast_by(
    'monthly_sales',
    product_id,
    date,
    quantity,
    'OptimizedTheta',
    6,
    {}
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
    {}
);
```

## Best For

- Production forecasting without manual tuning
- When standard Theta with theta=2 doesn't perform well
- Incorporating external factors (with exogenous support)
- Automated forecasting pipelines
