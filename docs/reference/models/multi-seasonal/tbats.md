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

Trigonometric seasonality, Box-Cox transformation, ARMA errors, Trend and Seasonal components. A sophisticated model for complex seasonal patterns using trigonometric (Fourier) representation of seasonality.

> **Note:** `use_box_cox` is **not** an accepted SQL parameter — it is rejected by the
> parameter validator (`ValidateParamKeys`). When `seasonal_periods` is omitted,
> `ts_forecast_by` auto-detects a single period internally and uses it as the only entry —
> falling back to the fixed period 12 if nothing is detected.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_periods` | INTEGER[] | No | auto-detected (single period) or `[12]` | Array of seasonal periods; see Note above when omitted |
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
-- Daily and weekly seasonality
SELECT * FROM ts_forecast_by(
    'hourly_demand',
    region_id,
    timestamp,
    demand,
    'TBATS',
    168,
    '1h',
    MAP{'seasonal_periods': '[24, 168]'}
);

-- Single seasonal period, custom confidence level (use_box_cox is rejected, not passable)
SELECT * FROM ts_forecast_by(
    'sub_daily_data',
    sensor_id,
    timestamp,
    reading,
    'TBATS',
    48,
    '1h',
    MAP{'seasonal_periods': '[24, 168]', 'confidence_level': '0.80'}
);
```

## Best For

- Complex seasonal patterns with non-integer periods
- High-frequency data with multiple seasonalities
- Data requiring variance stabilization (Box-Cox)
- When MSTL doesn't capture the seasonality well
