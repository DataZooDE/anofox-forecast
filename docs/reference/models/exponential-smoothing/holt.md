# Holt

> Holt's Linear Trend Method - handles data with trend but no seasonality

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'Holt', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'Holt', horizon, frequency, params);
```

## Description

Holt's Linear Trend Method (Double Exponential Smoothing). Extends SES by adding a trend component. Suitable for data with trend but no seasonality.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `confidence_level` | DOUBLE | No | 0.90 | Confidence for prediction intervals |

Holt does not take smoothing-parameter keys (`alpha`, `beta`) from SQL — those, along with
`include_fitted` and `include_residuals`, are rejected by the parameter validator
(`ts_forecast_native.cpp` `ValidateParamKeys`). The crate's internal Holt fit is not
configurable from the SQL surface today.

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier (only for `_by` variant) |
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast |
| `yhat_lower` | DOUBLE | Lower prediction interval |
| `yhat_upper` | DOUBLE | Upper prediction interval |

`yhat_lower` / `yhat_upper` are the model's own prediction interval (crate
`predict_with_intervals`) at `confidence_level` (default 0.90). See
[Prediction intervals, fitted values and input handling](../../../api/07-forecasting.md#prediction-intervals-fitted-values-and-input-handling);
for calibrated coverage use the conformal functions. Holt does not accept regressors in
`ts_forecast_exog_by` (errors).

## SQL Example

```sql
-- Basic usage (default parameters)
SELECT * FROM ts_forecast_by(
    'monthly_revenue',
    store_id,
    date,
    revenue,
    'Holt',
    6,
    '1d',
    MAP{}
);

-- With an explicit confidence level (the only tunable key Holt accepts from SQL)
SELECT * FROM ts_forecast_by(
    'quarterly_sales',
    region_id,
    date,
    sales,
    'Holt',
    4,
    '1d',
    MAP{'confidence_level': '0.80'}
);
```

## Best For

- Data with linear trend but no seasonality
- Short to medium-term forecasting with trending data
- Growth metrics, sales trends
- When SES doesn't capture the upward/downward movement
