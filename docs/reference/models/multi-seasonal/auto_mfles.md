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

> **Note:** `AutoMFLES` is a documented, accepted **deprecated alias** of `MFLES` — it runs
> the exact same code path (not a separate model-selection algorithm), and `model_name`
> reports `"MFLES"` (not `"AutoMFLES"`). When `seasonal_periods` is omitted, `ts_forecast_by`
> auto-detects a single period internally and uses it as the only entry, or runs non-seasonal
> if nothing is detected; passing `seasonal_periods` explicitly via `ts_detect_periods_agg` is
> still recommended for control.

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

`yhat_lower` / `yhat_upper` are the crate `MFLES` model's own prediction interval at
`confidence_level` (reported under `model_name = "MFLES"`) — see
[07-forecasting.md's "Prediction intervals, fitted values and input handling"](../../api/07-forecasting.md#prediction-intervals-fitted-values-and-input-handling)
for calibration caveats shared across all models.

## SQL Example

```sql
-- Step 1: Detect seasonality (aggregate form — the _by table-macro form has a known bug
-- that returns an all-NULL row; see docs/api/07-forecasting.md's Handling Seasonality note)
SELECT meter_id, round(ts_detect_periods_agg(timestamp, consumption, 'fft').primary_period) AS primary_period
FROM hourly_energy GROUP BY meter_id;

-- Step 2: Forecast with detected seasonal periods — model_name reports "MFLES", not "AutoMFLES"
SELECT DISTINCT model_name FROM ts_forecast_by(
    'hourly_energy',
    meter_id,
    timestamp,
    consumption,
    'AutoMFLES',
    168,
    '1h',
    MAP{'seasonal_periods': '[24, 168]'}
);
-- model_name = 'MFLES'
```

## Best For

- Data with multiple seasonal patterns (e.g., daily and weekly)
- High-frequency data (hourly, sub-hourly)
- Complex seasonality that single-period models can't capture
- Energy consumption, call center volumes, web traffic
