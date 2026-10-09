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
> **erroring if nothing is detected** (naming `TBATS`). There is no default `[12]` fallback.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_periods` | INTEGER[] | No | auto-detected (single period) | Array of seasonal periods; **errors if none can be auto-detected and none is given** — no default `[12]` |
| `confidence_level` | DOUBLE | No | 0.90 | Confidence for prediction intervals |

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier (only for `_by` variant) |
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast |
| `yhat_lower` | DOUBLE | Lower prediction interval |
| `yhat_upper` | DOUBLE | Upper prediction interval |

`yhat_lower` / `yhat_upper` are the crate `TBATS` model's own prediction interval at
`confidence_level` — see
[07-forecasting.md's "Prediction intervals, fitted values and input handling"](../../api/07-forecasting.md#prediction-intervals-fitted-values-and-input-handling)
for calibration caveats shared across all models.

> **Known issue:** `TBATS`'s interval width is **3–8% of** the equivalent `statsforecast`
> reference width — far too narrow (fix planned for v0.10.0, Phase 11).

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

```sql
-- No seasonal period can be detected and none is given -- hard error naming TBATS
CREATE OR REPLACE TABLE trend_only AS
SELECT 1 AS id, (DATE '2020-01-01' + INTERVAL (i) DAY) AS ds, 10.0 + i * 0.3 AS y
FROM range(0, 20) t(i);

SELECT * FROM ts_forecast_by('trend_only', id, ds, y, 'TBATS', 3, '1d', MAP{});
-- Invalid Input Error: Invalid input: no seasonal period could be detected for model 'TBATS';
--                      pass seasonal_period (or seasonal_periods) explicitly
```

## Best For

- Complex seasonal patterns with non-integer periods
- High-frequency data with multiple seasonalities
- Data requiring variance stabilization (Box-Cox)
- When MSTL doesn't capture the seasonality well
