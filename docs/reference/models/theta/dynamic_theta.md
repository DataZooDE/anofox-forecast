# DynamicTheta

> Theta Method with time-varying parameters for non-stationary data

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'DynamicTheta', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'DynamicTheta', horizon, frequency, params);

-- With exogenous variables
SELECT * FROM ts_forecast_exog_by('table', group_col, date_col, value_col, ['x1','x2'], 'future_table', future_date_col, ['x1','x2'], frequency, 'DynamicTheta', horizon, params);
```

## Description

Dynamic Theta Method. Uses time-varying theta values that adapt to local data characteristics. Better for non-stationary data where optimal theta changes over time.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_period` | INTEGER | No | auto-detected | Seasonal period; auto-detected when both `seasonal_period` and `seasonal_periods` are omitted |
| `confidence_level` | DOUBLE | No | 0.90 | Confidence for prediction intervals |

The `theta` key itself is **not** an accepted SQL parameter — it is rejected by the parameter
validator (`ValidateParamKeys`). There is no way to pass a custom initial theta value from SQL
today.

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier (only for `_by` variant) |
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast |
| `yhat_lower` | DOUBLE | Lower prediction interval |
| `yhat_upper` | DOUBLE | Upper prediction interval |

`yhat_lower` / `yhat_upper` are currently a heuristic `point ± z·SD(history)·√h` band, not a
model-based interval and not statistically calibrated. For calibrated coverage, use conformal
prediction intervals (see `docs/api/11-conformal-prediction.md`) on top of the point forecast.
(being corrected in v0.10.0, Phase 10)

## SQL Example

```sql
-- Basic usage (no theta key accepted from SQL)
SELECT * FROM ts_forecast_by(
    'monthly_sales',
    product_id,
    date,
    quantity,
    'DynamicTheta',
    6,
    '1mo'
);

-- With an explicit seasonal period
SELECT * FROM ts_forecast_by(
    'quarterly_data',
    region_id,
    date,
    revenue,
    'DynamicTheta',
    4,
    '3mo',
    MAP{'seasonal_period': '4'}
);
```

## Best For

- Non-stationary data where patterns change over time
- Long time series with evolving characteristics
- When standard Theta is too rigid
- Data with regime changes
