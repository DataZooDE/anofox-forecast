# GlobalETS

> Cross-series pooled ETS for panel / multi-series forecasting

## Signature

```sql
-- Panel / multi-series (cross-series learning, fit-once-emit-many)
SELECT * FROM ts_forecast_panel_by(
    'source_table',
    group_col,
    date_col,
    target_col,
    'GlobalETS',
    horizon,
    frequency,
    MAP{'seasonal_period': '7'}  -- optional; 0 or omit = non-seasonal
);
```

## Description

GlobalETS fits a single set of ETS smoothing parameters across **all series simultaneously** using `GlobalAutoETS` from the `anofox-forecast` crate. It then predicts per-series forecasts from each series' own initial state. This differs from per-series `AutoETS` (which fits N independent models) — GlobalETS pools the optimization, making it faster and better-regularized on large panels where individual series are short.

`GlobalAutoETS` automatically selects the best ETS spec from the `Reduced` model pool (8 candidates: `ANN`, `MNN`, `AAdN`, `MAdN`, `ANA`, `MNM`, `AAdA`, `MAdM`) by minimizing per-series negative log-likelihood. Use `model_pool: 'Complete'` (19 candidates) for higher accuracy at the cost of longer fit time.

**Ragged panel handling:** Series with different lengths are automatically aligned to a shared date grid (union of dates, gap-filled with linear interpolation). Series with fewer than 10 valid observations after alignment are dropped and surfaced as `DROPPED: too_short` rows rather than causing an error.

**Minimum panel size:** At least 3 series must pass the drop threshold for the global fit to proceed.

**Point forecasts only (v1):** Prediction intervals are not yet available via `ts_forecast_panel_by`. Use the conformal prediction surface (`ts_conformal_by`) in a separate step if intervals are needed.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `source_table` | VARCHAR | Yes | — | Source table name (quoted string) |
| `group_col` | IDENTIFIER | Yes | — | Series identifier column (unquoted) |
| `date_col` | IDENTIFIER | Yes | — | Date/timestamp column (unquoted) |
| `target_col` | IDENTIFIER | Yes | — | Target value column (unquoted) |
| `method` | VARCHAR | Yes | — | Must be `'GlobalETS'` |
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `frequency` | VARCHAR | Yes | — | Time step: `'1d'`, `'1h'`, `'1mo'`, etc. |
| `seasonal_period` | VARCHAR (in MAP) | No | `'0'` | Period for seasonal ETS (e.g., `'7'` for weekly). `'0'` = non-seasonal only. |
| `model_pool` | VARCHAR (in MAP) | No | `'Reduced'` | ETS spec search space: `'Reduced'` (8 models, default) or `'Complete'` (19 models). |

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `<group_col>` | (same as input) | Series identifier |
| `forecast_step` | INTEGER | Horizon step (1-based) |
| `<date_col>` | TIMESTAMP | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast |
| `model_name` | VARCHAR | `'GlobalETS'` for kept series; `'DROPPED: too_short'` for series with < 10 valid observations |

## SQL Example

```sql
-- Weekly seasonal panel (3 series, 14-day horizon)
-- Uses the verified example from global_panel_forecasting_examples.sql
CREATE OR REPLACE TABLE seasonal_panel AS
    SELECT 'Alpha' AS uid,
           DATE '2024-01-01' + INTERVAL (i) DAY AS ds,
           50.0 + 20.0 * SIN(2 * PI() * i / 7.0) + 0.2 * i AS y
    FROM generate_series(0, 55) t(i)
    UNION ALL
    SELECT 'Beta',
           DATE '2024-01-01' + INTERVAL (i) DAY,
           30.0 + 15.0 * COS(2 * PI() * i / 7.0) + 0.3 * i
    FROM generate_series(0, 48) t(i)
    UNION ALL
    SELECT 'Gamma',
           DATE '2024-01-03' + INTERVAL (i) DAY,
           40.0 + 10.0 * SIN(2 * PI() * i / 7.0 + 0.5) + 0.1 * i
    FROM generate_series(0, 41) t(i);

SELECT uid AS series, forecast_step, ROUND(yhat, 2) AS yhat, model_name
FROM ts_forecast_panel_by(
    'seasonal_panel',
    uid,
    ds,
    y,
    'GlobalETS',
    7,
    '1d',
    MAP {'seasonal_period': '7'}
)
ORDER BY series, forecast_step;
```

**Non-seasonal (default):**
```sql
-- method='GlobalETS' without seasonal_period uses Reduced pool, non-seasonal specs only
SELECT * FROM ts_forecast_panel_by('panel', product_id, ds, y, 'GlobalETS', 14, '1d');
```

## Best For

- Large panels of related series with **shared seasonal dynamics** (e.g., retail SKUs, sensor feeds with common weekly patterns)
- Situations where individual series are too short for reliable per-series ETS fits
- Pooled forecast evaluation across many series using a single model call
- Use `model_pool: 'Complete'` when accuracy matters more than speed; use `'Reduced'` (default) for large panels

## See Also

- [`ts_forecast_by`](../../../api/07-forecasting.md) — per-series independent forecasting (33 models)
- [`GlobalTheta`](../theta/global_theta.md) — pooled Theta (trended series, no seasonal config needed)
- [`GlobalCroston`](../intermittent/global_croston.md) — pooled Croston (intermittent/spare-parts panels)
- [`ts_forecast_panel_by`](../../../api/07-forecasting.md#panel--global-forecasting-ts_forecast_panel_by) — panel API reference
