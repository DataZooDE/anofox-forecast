# AutoTBATS

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'AutoTBATS', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'AutoTBATS', horizon, params);
```

## Description

Automatic TBATS (Trigonometric seasonality, Box-Cox transformation, ARMA errors, Trend and Seasonal components) model selection. A sophisticated model for complex seasonal patterns using trigonometric representation of seasonality.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_periods` | INTEGER[] | No | auto-detect | Array of seasonal periods |
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
-- Basic usage (auto-detect seasonality)
SELECT * FROM ts_forecast_by(
    'hourly_demand',
    region_id,
    timestamp,
    demand,
    'AutoTBATS',
    168,
    {}
);

-- With explicit multiple seasonal periods
SELECT * FROM ts_forecast_by(
    'sub_daily_data',
    sensor_id,
    timestamp,
    reading,
    'AutoTBATS',
    48,
    {'seasonal_periods': '[24, 168]'}
);
```

## Best For

- Complex seasonal patterns with non-integer periods
- High-frequency data with multiple seasonalities
- When MSTL doesn't capture the seasonality well
- Data requiring Box-Cox transformation for variance stabilization
