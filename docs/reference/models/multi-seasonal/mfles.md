# MFLES

> Multiple Frequency Local Exponential Smoothing for multi-seasonal patterns

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'MFLES', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'MFLES', horizon, frequency, params);

-- With exogenous variables
SELECT * FROM ts_forecast_exog_by('table', group_col, date_col, value_col, 'x1,x2', 'future_table', 'MFLES', horizon, frequency, params);
```

## Description

Multiple Frequency Local Exponential Smoothing. Handles multiple seasonal patterns using Fourier series representation combined with exponential smoothing. Requires explicit seasonal periods.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_periods` | INTEGER[] | **Yes** | — | Array of seasonal periods (e.g., '[24, 168]') |
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
-- Daily and weekly seasonality (hourly data)
SELECT * FROM ts_forecast_by(
    'hourly_energy',
    meter_id,
    timestamp,
    consumption,
    'MFLES',
    168,
    '1h',
    {'seasonal_periods': '[24, 168]'}
);

-- Weekly and yearly seasonality (daily data)
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'MFLES',
    90,
    '1d',
    {'seasonal_periods': '[7, 365]'}
);

-- With exogenous variables
SELECT * FROM ts_forecast_exog_by(
    'sales',
    product_id,
    date,
    amount,
    'temperature,holiday',
    'future_exog',
    'MFLES',
    30,
    '1d',
    {'seasonal_periods': '[7, 365]'}
);
```

## Best For

- High-frequency data with multiple seasonalities
- When you know the exact seasonal periods
- Energy demand, call center volumes
- Data with daily, weekly, and yearly patterns
