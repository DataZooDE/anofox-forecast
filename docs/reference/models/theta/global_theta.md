# GlobalTheta

> Pooled Theta method for panel forecasting — no seasonal period required

## Signature

```sql
-- Panel / multi-series (cross-series learning, fit-once-emit-many)
SELECT * FROM ts_forecast_panel_by(
    'source_table',
    group_col,
    date_col,
    target_col,
    'GlobalTheta',
    horizon,
    frequency
    -- No params needed; seasonal_period is ignored
);
```

## Description

GlobalTheta fits a single smoothing parameter `alpha` across **all series simultaneously** using `GlobalTheta` from the `anofox-forecast` crate (Standard Theta Method, `theta=2.0`). Each series retains its own per-series level and slope (computed via OLS). The shared `alpha` is found by minimizing the total SSE across the panel.

Unlike per-series `Theta` / `AutoTheta` (which fits N independent models), GlobalTheta pools the smoothing optimization over the whole panel. This is effective when individual series are short but collectively provide enough data to identify a good global smoothing rate.

**No seasonal period:** GlobalTheta does not decompose seasonality. If your panel has strong seasonal patterns, use `GlobalETS` with `seasonal_period` instead.

**Ragged panel handling:** Series with different lengths are automatically aligned to a shared date grid (union of dates, gap-filled with linear interpolation). Series with fewer than 10 valid observations after alignment are dropped and surfaced as `DROPPED: too_short` rows.

**Minimum panel size:** At least 3 series must pass the drop threshold for the global fit to proceed.

**Point forecasts only (v1):** Prediction intervals are not yet available via `ts_forecast_panel_by`. Use the conformal prediction surface (`ts_conformal_by`) in a separate step if intervals are needed.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `source_table` | VARCHAR | Yes | — | Source table name (quoted string) |
| `group_col` | IDENTIFIER | Yes | — | Series identifier column (unquoted) |
| `date_col` | IDENTIFIER | Yes | — | Date/timestamp column (unquoted) |
| `target_col` | IDENTIFIER | Yes | — | Target value column (unquoted) |
| `method` | VARCHAR | Yes | — | Must be `'GlobalTheta'` |
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `frequency` | VARCHAR | Yes | — | Time step: `'1d'`, `'1h'`, `'1mo'`, etc. |

No model-specific params are accepted. Any `seasonal_period` in the `params` MAP is silently ignored.

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `<group_col>` | (same as input) | Series identifier |
| `forecast_step` | INTEGER | Horizon step (1-based) |
| `<date_col>` | TIMESTAMP | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast (linear extrapolation with shared alpha, per-series level+slope) |
| `model_name` | VARCHAR | `'GlobalTheta'` for kept series; `'DROPPED: too_short'` for series with < 10 valid observations |

## SQL Example

```sql
-- Trended panel, 3 series, 14-day forecast — no seasonal config needed
-- Uses the verified example from global_panel_forecasting_examples.sql
CREATE OR REPLACE TABLE panel_sales AS
    SELECT 'Series_A' AS product_id,
           DATE '2024-01-01' + INTERVAL (i) DAY AS ds,
           100.0 + i * 0.6 + 5.0 * SIN(2 * PI() * i / 7.0) AS y
    FROM generate_series(0, 29) t(i)
    UNION ALL
    SELECT 'Series_B',
           DATE '2024-01-01' + INTERVAL (i) DAY,
           80.0 + i * 0.4 + 4.0 * COS(2 * PI() * i / 7.0)
    FROM generate_series(0, 27) t(i)
    UNION ALL
    SELECT 'Series_C',
           DATE '2024-01-01' + INTERVAL (i) DAY,
           60.0 + i * 0.8 + 3.0 * SIN(2 * PI() * i / 7.0 + 1.0)
    FROM generate_series(0, 24) t(i);

SELECT product_id, forecast_step, ds, ROUND(yhat, 2) AS yhat, model_name
FROM ts_forecast_panel_by(
    'panel_sales',
    product_id,
    ds,
    y,
    'GlobalTheta',
    14,
    '1d'
)
ORDER BY product_id, forecast_step;
```

**Compare GlobalETS vs GlobalTheta on the same panel:**
```sql
SELECT 'GlobalETS' AS method, product_id, forecast_step, ROUND(yhat, 2) AS yhat
FROM ts_forecast_panel_by('panel_sales', product_id, ds, y, 'GlobalETS', 7, '1d')
UNION ALL
SELECT 'GlobalTheta', product_id, forecast_step, ROUND(yhat, 2)
FROM ts_forecast_panel_by('panel_sales', product_id, ds, y, 'GlobalTheta', 7, '1d')
ORDER BY product_id, method, forecast_step;
```

## Best For

- Panels of **trended series** with minimal configuration needs
- Situations where GlobalETS seasonal modeling is unnecessary (non-seasonal data)
- Quick panel baseline using the Theta method's balance of trend extrapolation and smoothing
- Large panels of short series where individual Theta fits are unreliable

## See Also

- [`GlobalETS`](../exponential-smoothing/global_ets.md) — pooled ETS with optional seasonality
- [`GlobalCroston`](../intermittent/global_croston.md) — pooled Croston (intermittent/spare-parts panels)
- [`ts_forecast_panel_by`](../../../api/07-forecasting.md#panel--global-forecasting-ts_forecast_panel_by) — panel API reference
- [`AutoTheta`](auto_theta.md) — per-series Theta with automatic variant selection
