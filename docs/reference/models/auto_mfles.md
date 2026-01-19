# AutoMFLES

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'AutoMFLES', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'AutoMFLES', horizon, params);
```

## Description

Automatic Multiple Frequency Local Exponential Smoothing. Automatically detects and handles multiple seasonal patterns in the data. Combines Fourier series representation with exponential smoothing for complex seasonality.

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
    'hourly_energy',
    meter_id,
    timestamp,
    consumption,
    'AutoMFLES',
    168,
    {}
);

-- With explicit multiple seasonal periods (daily and weekly)
SELECT * FROM ts_forecast_by(
    'hourly_sales',
    store_id,
    timestamp,
    sales,
    'AutoMFLES',
    168,
    {'seasonal_periods': '[24, 168]'}
);
```

## Best For

- Data with multiple seasonal patterns (e.g., daily and weekly)
- High-frequency data (hourly, sub-hourly)
- Complex seasonality that single-period models can't capture
- Energy consumption, call center volumes, web traffic
