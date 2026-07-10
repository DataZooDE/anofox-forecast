# Laplace

> Streaming distributional forecaster — likelihood-weighted mixture of leaves with three zero-config selectors

## Signature

```sql
-- Multiple series (grouped)
SELECT * FROM ts_forecast_by('table', group_col, date_col, value_col,
    'Laplace', horizon, frequency, params);
```

## Description

A streaming distributional shell over EMA / drift / AR(1) / damped-Holt leaves — inspired by [microprediction/skaters](https://github.com/microprediction/skaters). Each leaf produces a per-horizon predictive distribution; the mixture weights update online by likelihood, so the shell adapts to regime shifts without a batch refit. Point forecasts come from the moment-match of the per-horizon GaussianMixture; intervals from the requested confidence level.

Three zero-config selectors decide which leaves populate the pool. The point-forecast surface is what `ts_forecast_by` returns today; per-horizon mixture parameters (weights, means, stds) will be exposed by the upcoming `ts_forecast_dist_by` table function (PR B of the distributional series).

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `horizon` | INTEGER | Yes | — | Number of periods to forecast |
| `seasonal_period` | INTEGER | No | none | Seasonal period — adds a `SeasonalEmaLeaf` to the pool |
| `laplace_variant` | VARCHAR | No | `'auto'` | Selector: `auto`, `auto_aid`, `skaters` |
| `laplace_seasonal_batch_init` | BOOLEAN | No | false | Batch-init the seasonal-EMA phase levels from the last training cycle |
| `confidence_level` | DOUBLE | No | 0.90 | Confidence for prediction intervals |

## Variant Selector

| Variant | Leaf pool | Best for |
|---------|-----------|----------|
| `auto` (default) | EMA / drift / AR(1) / damped-Holt (+ seasonal-EMA if `seasonal_period` set) | Smooth continuous series with adequate history — economic, financial, non-demand |
| `auto_aid` | Above + AID-driven distribution family (Poisson / NegativeBinomial / RectifiedNormal / seasonal-Croston) | Retail SKU / intermittent-demand counts |
| `skaters` | Full skaters ensemble — multi-h scoring, stacking, larger leaf set (Yeo-Johnson, fractional-diff, OU, terminal-CRPS) | Slower, more robust; use when accuracy matters more than latency |

**Empirical guidance** (from `anofox-forecast`'s M-competition benchmarks):

| Panel | Best selector |
|-------|---------------|
| Retail counts / intermittent (M5-Daily, dominick) | `auto_aid` — competitive with AutoETS at ~30–42× the speed |
| Smooth economic / continuous (M3 monthly, M4 daily) | `auto` — close to AutoTheta, +5–15% on well-behaved data |
| Short-history seasonal (N < 50, tourism_yearly) | Streaming leaves haven't warmed up — use `AutoTheta` instead |
| Growing amplitude / phase-shifted seasonal | Any Laplace — but **do not enable `laplace_seasonal_batch_init`** (see below) |

## The batch-init trade-off

`laplace_seasonal_batch_init: 1` initialises the seasonal-EMA phase levels from the last training cycle instead of the streaming softmax warmup. Behaviour, measured on `anofox-forecast`'s reference synthetics ([sipemu/anofox-forecast#195](https://github.com/sipemu/anofox-forecast/issues/195)):

| Amplitude regime | With batch_init | Recommendation |
|------------------|-----------------|----------------|
| Constant amplitude | MAE 2.18 → 0.07 on strong-seasonal N=48 | **Enable** |
| Declining amplitude (recent < earlier) | 5.49× → 1.10× peak-ratio recovery on regime change | **Enable** |
| Growing amplitude (retail expanding) | Softmax abandons seasonal-EMA for a differenced-EMA leaf → forecast collapses to flat | **Avoid** |
| Phase-shifted seasonality | Same failure mode — collapse to flat | **Avoid** |
| Trending panels (M-competition tourism-shape) | Batch-init additive seasonal-EMA displaces the multiplicative seasonal leaf | **Avoid** |

Rule of thumb: safe on stationary or declining-amplitude series; enable per pipeline once you've classified the amplitude regime. Default off in this extension.

The `model_name` column tags the state so downstream code can filter:

```
Laplace(auto)                              -- no seasonal
Laplace(auto,seasonal=7)                   -- weekly seasonal
Laplace(auto_aid,seasonal=12)              -- monthly retail counts
Laplace(auto,seasonal=12,batch_init)       -- opt-in batch init active
Laplace(skaters,seasonal=7)                -- fuller ensemble
```

## Returns

Same shape as any `ts_forecast_by` call: `(group_col, forecast_step, ds, yhat, yhat_lower, yhat_upper, model_name)`.

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier |
| `forecast_step` | INTEGER | 1..horizon |
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast — mean of the mixture |
| `yhat_lower` / `yhat_upper` | DOUBLE | Prediction interval at `confidence_level` |
| `model_name` | VARCHAR | Tagged variant + seasonal + batch_init state |

## SQL Examples

### Default — auto on monthly retail

```sql
-- 12-month forecast, weekly seasonal_period not applicable to monthly data;
-- for monthly retail use seasonal_period=12
SELECT * FROM ts_forecast_by(
    'sales_monthly', product_id, ds, y,
    'Laplace', 12, '1mo',
    MAP{'seasonal_period': '12'}
);
```

### Intermittent counts — auto_aid

```sql
-- Retail SKU with many zero-days; AID picks Poisson/NegBin/Croston leaves
SELECT * FROM ts_forecast_by(
    'sales_daily', item_id, ds, y,
    'Laplace', 28, '1d',
    MAP{'seasonal_period': '7', 'laplace_variant': 'auto_aid'}
);
```

### Amplitude-declining seasonal series — batch_init

```sql
-- Series where recent seasonal amplitude is smaller than historical.
-- Batch init recovers the recent-cycle level; without it the streaming
-- warmup can leave the seasonal-EMA leaf under-weighted.
SELECT * FROM ts_forecast_by(
    'sales_monthly', product_id, ds, y,
    'Laplace', 12, '1mo',
    MAP{'seasonal_period': '12', 'laplace_seasonal_batch_init': '1'}
);
```

### STRUCT param syntax (recommended)

```sql
-- Keeps numeric params typed
SELECT * FROM ts_forecast_by(
    'sales_monthly', product_id, ds, y,
    'Laplace', 12, '1mo',
    {seasonal_period: 12, laplace_variant: 'auto_aid', laplace_seasonal_batch_init: 1}
);
```

### Slice metrics by variant post-hoc

```sql
-- Fit all three variants, join with actuals, compare per-variant MAE
WITH fcst AS (
    SELECT 'auto'     AS variant, * FROM ts_forecast_by('train', id, ds, y, 'Laplace', 12, '1mo', MAP{'seasonal_period':'12','laplace_variant':'auto'})
    UNION ALL SELECT 'auto_aid', * FROM ts_forecast_by('train', id, ds, y, 'Laplace', 12, '1mo', MAP{'seasonal_period':'12','laplace_variant':'auto_aid'})
    UNION ALL SELECT 'skaters',  * FROM ts_forecast_by('train', id, ds, y, 'Laplace', 12, '1mo', MAP{'seasonal_period':'12','laplace_variant':'skaters'})
)
SELECT variant,
       ROUND(AVG(ABS(t.y - f.yhat)), 3) AS mae
FROM fcst f JOIN test t ON f.id = t.id AND f.ds = t.ds
GROUP BY variant ORDER BY mae;
```

## Best For

- **Retail / demand panels** — `auto_aid` matches AutoETS accuracy at ~30× the speed on M5-monthly (24k series, 12-month horizon: 0.5 s vs 15 s wall clock).
- **Large-panel throughput** — Streaming leaves, no per-series solver — DuckDB GROUP BY parallelism gives full-CPU utilisation.
- **Non-negative bounded targets** — `auto` produces zero negative forecasts on M5-monthly; `auto_aid` produces ≤ 0.003 % — much lower than AutoETS's 7.4 % or AutoTheta's 3.9 %.
- **Downstream distributional workflows** — per-horizon GaussianMixture available (surfaced via `ts_forecast_dist_by` in the next PR of this series).

## Known Limitations

- **Short-history seasonal panels** (N < 24) — the streaming warmup doesn't complete; consider `laplace_seasonal_batch_init` if you know the amplitude is stationary / declining, else use `AutoTheta`.
- **Growing amplitude with `batch_init` enabled** — the softmax abandons the seasonal-EMA leaf; the forecast collapses to a flat line. Turn `batch_init` off.
- **Ultra-sparse daily counts (M5-Daily)** — the mixture stays honest and misses the tail spikes AutoETS damps into; on aggregate MAE it can trail by ~5 %. sMAPE and negative-forecast rate stay best-in-class.

## References

- Reference implementation and upstream benchmarks: [`anofox-forecast::models::laplace`](https://github.com/sipemu/anofox-forecast) (Rust crate).
- Batch-init trade-off analysis and worked amplitude-decline case: [sipemu/anofox-forecast#195](https://github.com/sipemu/anofox-forecast/issues/195).
- Full M5-monthly / M5-daily benchmark comparing Laplace vs AutoETS / AutoTheta / SeasEs: attached to the PR series introducing this model.
