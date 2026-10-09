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

> **Note:** `AutoMFLES` currently runs the exact same code path as `MFLES` (an alias, not a
> separate model-selection algorithm). When `seasonal_periods` is omitted, `ts_forecast_by`
> auto-detects a single period internally and uses it as the only entry; passing
> `seasonal_periods` explicitly via `ts_detect_periods_by` is still recommended for control.
> (being corrected in v0.10.0, Phase 10)

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_periods` | INTEGER[] | No | auto-detected (single period) | Array of seasonal periods (e.g., `'[24, 168]'` for daily+weekly); auto-detected (single period) when omitted |
| `confidence_level` | DOUBLE | No | 0.90 | Confidence for prediction intervals |

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
    MAP{'seasonal_periods': '[24, 168]'}
);
```

## Best For

- Data with multiple seasonal patterns (e.g., daily and weekly)
- High-frequency data (hourly, sub-hourly)
- Complex seasonality that single-period models can't capture
- Energy consumption, call center volumes, web traffic
