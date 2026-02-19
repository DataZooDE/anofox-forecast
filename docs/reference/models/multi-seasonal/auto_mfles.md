# AutoMFLES

> AutoMFLES with automatic model selection for multi-seasonality

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'AutoMFLES', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'AutoMFLES', horizon, frequency, params);
```

## Description

Multiple Frequency Local Exponential Smoothing with automatic model selection. Handles multiple seasonal patterns in the data. Combines Fourier series representation with exponential smoothing for complex seasonality.

> **Important:** Seasonality is NOT auto-detected. Use `ts_detect_periods_by` first to detect seasonal periods, then pass them explicitly via `seasonal_periods`.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_periods` | INTEGER[] | **Yes** | — | Array of seasonal periods (e.g., `'[24, 168]'` for daily+weekly) |
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
-- Step 1: Detect seasonality
SELECT * FROM ts_detect_periods_by('hourly_energy', meter_id, timestamp, consumption, MAP{});
-- Returns: periods = [24, 168] (daily and weekly)

-- Step 2: Forecast with detected seasonal periods
SELECT * FROM ts_forecast_by(
    'hourly_energy',
    meter_id,
    timestamp,
    consumption,
    'AutoMFLES',
    168,
    '1h',
    {'seasonal_periods': '[24, 168]'}
);
```

## Best For

- Data with multiple seasonal patterns (e.g., daily and weekly)
- High-frequency data (hourly, sub-hourly)
- Complex seasonality that single-period models can't capture
- Energy consumption, call center volumes, web traffic
