# AutoTBATS

> AutoTBATS with automatic model selection for complex seasonality

## Signature

```sql
-- Single series
SELECT * FROM ts_forecast('table', date_col, value_col, 'AutoTBATS', horizon, params);

-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col, 'AutoTBATS', horizon, frequency, params);
```

## Description

TBATS (Trigonometric seasonality, Box-Cox transformation, ARMA errors, Trend and Seasonal components) with automatic model selection. A sophisticated model for complex seasonal patterns using trigonometric representation of seasonality.

> **Note:** When `seasonal_periods` is omitted, `ts_forecast_by` auto-detects a single period
> internally and uses it as the only entry — falling back to the fixed period 12 if nothing is
> detected. Passing `seasonal_periods` explicitly via `ts_detect_periods_by` is still
> recommended for control and reproducibility.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_periods` | INTEGER[] | No | auto-detected (single period) or `[12]` | Array of seasonal periods (e.g., `'[24, 168]'` for daily+weekly); see Note above when omitted |
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
SELECT * FROM ts_detect_periods_by('hourly_demand', region_id, timestamp, demand, MAP{});
-- Returns: periods = [24, 168] (daily and weekly)

-- Step 2: Forecast with detected seasonal periods
SELECT * FROM ts_forecast_by(
    'hourly_demand',
    region_id,
    timestamp,
    demand,
    'AutoTBATS',
    168,
    '1h',
    MAP{'seasonal_periods': '[24, 168]'}
);
```

## Best For

- Complex seasonal patterns with non-integer periods
- High-frequency data with multiple seasonalities
- When MSTL doesn't capture the seasonality well
- Data requiring Box-Cox transformation for variance stabilization
