# GlobalCroston

> Pooled Croston method for intermittent demand panel forecasting

## Signature

```sql
-- Panel / multi-series intermittent demand (fit-once-emit-many)
SELECT * FROM ts_forecast_panel_by(
    'source_table',
    group_col,
    date_col,
    target_col,
    'GlobalCroston',
    horizon,
    frequency,
    MAP {'croston_variant': 'SBA'}  -- optional; default = 'Classic'
);
```

## Description

GlobalCroston fits a single smoothing parameter `alpha` across **all series simultaneously** using `GlobalCroston` from the `anofox-forecast` crate. It is designed for **intermittent demand panels** — series with many zero-demand periods and occasional non-zero demand events (e.g., spare-parts orders, slow-moving inventory).

GlobalCroston separates demand occurrences from inter-demand intervals for each series, then optimizes a shared `alpha` that minimizes the combined MSE over all demand sub-sequences across the panel. Each series retains its own per-series demand level and interval level states.

**Flat forecast:** Croston always produces a **constant forecast** (the same value for every horizon step). The forecast is `demand_level / interval_level` (per-series), optionally multiplied by the SBA bias correction factor `(1 - alpha/2)`.

**Non-negative output:** All forecasts are guaranteed non-negative. An all-zero series (no demand events) predicts `0.0` for all steps — this is the correct behavior for a series with no demand history.

**Two variants:**
- **Classic** (default): `demand_level / interval_level` — can overestimate demand.
- **SBA** (Syntetos-Boylan Approximation): `demand_level / interval_level * (1 - alpha/2)` — downward bias correction, recommended in most cases.

**No seasonal period:** Croston models operate on demand event sub-sequences, not calendar positions. The `seasonal_period` param is irrelevant and ignored.

**Ragged panel handling:** Series are aligned to a shared date grid for consistency. Series with fewer than 10 valid observations are dropped and surfaced as `DROPPED: too_short` rows.

**All-zero panel protection:** If no series in the panel has at least 2 demand events after alignment, the function returns a `ConvergenceFailure` error. Ensure the intermittent panel has real demand history.

**Point forecasts only (v1):** Prediction intervals are not yet available via `ts_forecast_panel_by`. Use the conformal prediction surface (`ts_conformal_by`) in a separate step if intervals are needed.

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `source_table` | VARCHAR | Yes | — | Source table name (quoted string) |
| `group_col` | IDENTIFIER | Yes | — | Series identifier column (unquoted) |
| `date_col` | IDENTIFIER | Yes | — | Date/timestamp column (unquoted) |
| `target_col` | IDENTIFIER | Yes | — | Target value column (unquoted). Non-zero values = demand events; zeros = non-demand periods. |
| `method` | VARCHAR | Yes | — | Must be `'GlobalCroston'` |
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `frequency` | VARCHAR | Yes | — | Time step: `'1d'`, `'1h'`, `'1mo'`, etc. |
| `croston_variant` | VARCHAR (in MAP) | No | `'Classic'` | Bias correction variant: `'Classic'` (default) or `'SBA'` (recommended). |

## Returns

| Column | Type | Description |
|--------|------|-------------|
| `<group_col>` | (same as input) | Series identifier |
| `forecast_step` | INTEGER | Horizon step (1-based) |
| `<date_col>` | TIMESTAMP | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast (constant / flat per series; always ≥ 0) |
| `model_name` | VARCHAR | `'GlobalCroston'` for kept series; `'DROPPED: too_short'` for series with < 10 valid observations |

## SQL Example

```sql
-- Intermittent demand panel (3 SKUs)
-- Uses the verified example from global_panel_forecasting_examples.sql
CREATE OR REPLACE TABLE panel_intermittent AS
    SELECT 'Item_A' AS item_id,
           DATE '2024-01-01' + INTERVAL (i) DAY AS ds,
           CASE WHEN i % 4 = 0 THEN 3.0 WHEN i % 7 = 0 THEN 5.0 ELSE 0.0 END AS qty
    FROM generate_series(0, 29) t(i)
    UNION ALL
    SELECT 'Item_B',
           DATE '2024-01-01' + INTERVAL (i) DAY,
           CASE WHEN i % 5 = 0 THEN 2.0 WHEN i % 9 = 0 THEN 4.0 ELSE 0.0 END
    FROM generate_series(0, 27) t(i)
    UNION ALL
    SELECT 'Item_C',
           DATE '2024-01-01' + INTERVAL (i) DAY,
           CASE WHEN i % 3 = 0 THEN 1.0 WHEN i % 11 = 0 THEN 6.0 ELSE 0.0 END
    FROM generate_series(0, 24) t(i);

-- Classic Croston (no variant param needed)
SELECT item_id, forecast_step, ds, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_panel_by(
    'panel_intermittent',
    item_id,
    ds,
    qty,
    'GlobalCroston',
    6,
    '1d'
)
ORDER BY item_id, forecast_step;

-- SBA variant (recommended — downward bias correction)
SELECT item_id, forecast_step, ds, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_panel_by(
    'panel_intermittent',
    item_id,
    ds,
    qty,
    'GlobalCroston',
    6,
    '1d',
    MAP {'croston_variant': 'SBA'}
)
ORDER BY item_id, forecast_step;
```

**Compare Classic vs SBA:**
```sql
SELECT 'Classic' AS variant, item_id, forecast_step, ROUND(yhat, 4) AS yhat
FROM ts_forecast_panel_by('panel_intermittent', item_id, ds, qty, 'GlobalCroston', 6, '1d')
UNION ALL
SELECT 'SBA', item_id, forecast_step, ROUND(yhat, 4)
FROM ts_forecast_panel_by('panel_intermittent', item_id, ds, qty, 'GlobalCroston', 6, '1d',
    MAP {'croston_variant': 'SBA'})
ORDER BY item_id, variant, forecast_step;
```

## Best For

- **Spare-parts, MRO, or slow-moving inventory** panels
- Panels with many zero-demand periods and irregular non-zero demand events
- Situations where per-series Croston fits are unreliable due to short history
- Use **SBA variant** when Classic forecasts appear to overestimate demand (common)
- Use **Classic** as the conservative baseline before trying SBA

## See Also

- [`CrostonSBA`](croston_sba.md) — per-series SBA (fits N independent models)
- [`CrostonClassic`](croston_classic.md) — per-series Classic Croston
- [`GlobalETS`](../exponential-smoothing/global_ets.md) — pooled ETS for regular-demand panels
- [`GlobalTheta`](../theta/global_theta.md) — pooled Theta for trended panels
- [`ts_forecast_panel_by`](../../../api/07-forecasting.md#panel--global-forecasting-ts_forecast_panel_by) — panel API reference
