# MSTL

> Multiple Seasonal-Trend decomposition using LOESS

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'MSTL', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'MSTL', horizon, frequency, params);
```

## Description

Multiple Seasonal-Trend decomposition using LOESS. Decomposes the series into trend, multiple seasonal components, and remainder. Forecasts by extrapolating each component separately. Requires explicit seasonal periods.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_periods` | INTEGER[] | **Yes** | — | Array of seasonal periods (e.g., '[7, 365]') |
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
-- Weekly and yearly seasonality
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id,
    date,
    quantity,
    'MSTL',
    30,
    '1d',
    {'seasonal_periods': '[7, 365]'}
);

-- Daily and weekly for hourly data
SELECT * FROM ts_forecast_by(
    'hourly_energy',
    meter_id,
    timestamp,
    consumption,
    'MSTL',
    168,
    '1h',
    {'seasonal_periods': '[24, 168]'}
);
```

## Best For

- Long time series with complex multiple seasonality
- When you need interpretable decomposition
- Retail data with weekly and yearly patterns
- Energy and traffic data with multiple periodicities
