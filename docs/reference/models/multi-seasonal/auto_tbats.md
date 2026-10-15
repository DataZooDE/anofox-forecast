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

> **Note:** `use_box_cox` is **not** an accepted SQL parameter — it is rejected by the
> parameter validator (`ValidateParamKeys`). When `seasonal_periods` is omitted,
> `ts_forecast_by` auto-detects a single period internally and uses it as the only entry —
> **erroring if nothing is detected** (naming `AutoTBATS`). There is no default `[12]`
> fallback.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_periods` | INTEGER[] | No | auto-detected (single period) | Array of seasonal periods (e.g., `'[24, 168]'` for daily+weekly); **errors if none can be auto-detected and none is given** — no default `[12]` |
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

> **0.16.0 change:** `AutoTBATS`'s interval now uses the innovations state-space variance
> (previously 3–8% of an equivalent `statsforecast` reference width — far too narrow). Not
> individually R-parity-tested; see
> [07-forecasting.md's prediction-intervals section](../../api/07-forecasting.md#prediction-intervals)
> for the models that ARE R-parity-tested (FCST-09, Phase 17 plan 17-03).

## SQL Example

```sql
-- Step 1: Detect seasonality (ts_detect_periods_by has a known NULL-row bug;
-- use the aggregate form)
SELECT region_id, ts_detect_periods_agg(timestamp, demand, 'fft').primary_period AS period
FROM hourly_demand GROUP BY region_id;

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

```sql
-- No seasonal period can be detected and none is given -- hard error naming AutoTBATS
CREATE OR REPLACE TABLE trend_only_tbats AS
SELECT 1 AS id, (DATE '2020-01-01' + INTERVAL (i) DAY) AS ds, 10.0 + i * 0.3 AS y
FROM range(0, 20) t(i);

SELECT * FROM ts_forecast_by('trend_only_tbats', id, ds, y, 'AutoTBATS', 3, '1d', MAP{});
-- Invalid Input Error: Invalid input: no seasonal period could be detected for model
--                      'AutoTBATS'; pass seasonal_period (or seasonal_periods) explicitly
```

## Best For

- Complex seasonal patterns with non-integer periods
- High-frequency data with multiple seasonalities
- When MSTL doesn't capture the seasonality well
- Data requiring Box-Cox transformation for variance stabilization
