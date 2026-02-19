# TBATS

> Trigonometric seasonality with Box-Cox, ARMA errors, and trend

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'TBATS', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'TBATS', horizon, frequency, params);
```

## Description

Trigonometric seasonality, Box-Cox transformation, ARMA errors, Trend and Seasonal components. A sophisticated model for complex seasonal patterns using trigonometric (Fourier) representation of seasonality. Requires explicit seasonal periods.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_periods` | INTEGER[] | **Yes** | — | Array of seasonal periods |
| `use_box_cox` | BOOLEAN | No | true | Apply Box-Cox transformation |
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
-- Daily and weekly seasonality
SELECT * FROM ts_forecast_by(
    'hourly_demand',
    region_id,
    timestamp,
    demand,
    'TBATS',
    168,
    '1h',
    {'seasonal_periods': '[24, 168]'}
);

-- Without Box-Cox transformation
SELECT * FROM ts_forecast_by(
    'sub_daily_data',
    sensor_id,
    timestamp,
    reading,
    'TBATS',
    48,
    '1h',
    {'seasonal_periods': '[24, 168]', 'use_box_cox': false}
);
```

## Best For

- Complex seasonal patterns with non-integer periods
- High-frequency data with multiple seasonalities
- Data requiring variance stabilization (Box-Cox)
- When MSTL doesn't capture the seasonality well
