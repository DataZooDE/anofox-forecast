# Phase 3: Classical & Multivariate Models - Context

**Gathered:** 2026-08-21
**Status:** Ready for planning
**Mode:** Smart discuss (autonomous) — 16 decisions across 4 areas, all recommendations accepted

<domain>
## Phase Boundary

Expose three classical/multivariate models from `anofox-forecast 0.15.3`:
- **GARCH** (`models::garch::GARCH`) — conditional-volatility forecasting, via a new method arm on the existing `ts_forecast_by` surface (`method = 'GARCH'`).
- **Kalman** (`models::kalman_forecaster::KalmanForecaster`) — state-space smoothing/forecasting, via `ts_forecast_by` (`method = 'Kalman'`).
- **VAR** (`models::var::VAR`) — multivariate vector-autoregression, via a **new dedicated function** `ts_forecast_var_by` (N value columns → N per-variable forecasts). This is the flagged design risk: a different I/O shape from every existing univariate `ts_forecast_by` method.

Delivers requirements **CLAS-01** (GARCH), **CLAS-02** (Kalman), **CLAS-03** (VAR).

In scope: point forecasts, behavioral-parity benchmarks, docs + runnable examples. Out of scope: prediction intervals (deferred), VAR auto-order-selection.

</domain>

<decisions>
## Implementation Decisions

### Area 1 — GARCH
- Exposed via the **existing `ts_forecast_by` surface** as a new `ModelType` arm `method = 'GARCH'` (locked by success criterion 1). Add `GARCH` to the `ModelType` enum + string dispatch in `crates/anofox-fcst-core/src/forecast.rs` and wire it into the unified forecast pipeline.
- **Output is conditional volatility (standard deviation)** = `sqrt(GARCH::forecast_variance(horizon))`. `forecast_value` carries volatility, NOT variance — this MUST be documented explicitly so users aren't misled.
- **Default GARCH(1,1)** (`GARCH::garch_1_1()`); `p` and `q` overridable through the `params` MAP.
- Coefficients (`omega`, `alpha`, `beta`) are **auto-estimated by fit**; optional advanced overrides may be exposed via `params` but are not required.

### Area 2 — Kalman
- Exposed via **`ts_forecast_by` method = 'Kalman'** (new `ModelType` arm; locked by success criterion 2).
- **Default state-space = local level** (`KalmanForecaster::local_level()`); `local_linear_trend` selectable via `params{'kalman_model': 'local_level' | 'local_linear_trend'}`.
- The `_by` surface **returns h-step forecasts** (consistent with all other `ts_forecast_by` methods). In-sample smoothing exists in the crate but is not what this surface emits.
- Spec selector param key = **`kalman_model`**.

### Area 3 — VAR (multivariate; the design risk)
- **New dedicated function `ts_forecast_var_by`**, backed by the core **`VAR` struct** (`VAR::fit(&[Vec<f64>])` → `predict(horizon) -> Vec<Vec<f64>>`, K series × horizon) — NOT the single-series `VARForecaster` trait wrapper.
- **Multiple value columns are passed as a `LIST` parameter**: `ts_forecast_var_by(source, group_col?, date_col, value_cols := ['y1','y2','y3'], horizon, frequency, order := 1, ...)`. The list names the K variables.
- **Output is LONG format**: `{variable, forecast_date, forecast_value}` — one row per (variable, horizon step). Chosen over wide (one column per variable) because long handles arbitrary N without a dynamic schema and matches every existing long-format surface.
- **Lag order via an explicit `order` param** (`VAR::new`/`VARForecaster::new(order)`), default lag 1, overridable. Auto-order-selection is deferred.

### Area 4 — Benchmark, Intervals & Docs
- GARCH & Kalman parity checked against **statsmodels / the `arch` package under `benchmark/.venv`** (R as a fallback reference), **behavioral/approximate** parity — same standard as Phases 1–2. (statsforecast lacks GARCH, so it is not the baseline here.)
- **VAR is benchmarked on a synthetic VAR(1) dataset** with known coefficients, compared against statsmodels `VAR` — because no multivariate M4/M5 dataset exists in `benchmark/`. The crate's own `generate_var1_data`-style construction is a valid reference generator.
- **Point forecasts only for v1**; prediction intervals deferred (GARCH emits variance point forecasts, Kalman/VAR point forecasts).
- Docs in **`docs/api/`** and **`docs/reference/models/`**, plus runnable **`examples/*.sql`** snippets verified end-to-end against the built extension (per success criteria + PR #230 rule).

### Claude's Discretion
- Exact `params` keys for GARCH advanced coefficient overrides and for VAR beyond `order`.
- Whether `ts_forecast_var_by` takes an optional `group_col` (per-panel VAR) or is single-panel for v1 — pick the simpler shape that still satisfies CLAS-03; document it.
- The synthetic VAR(1) generator's exact coefficients/size for the benchmark.

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- `ts_forecast_by` macro + `_ts_forecast_native` table function + the `ModelType` string dispatch in `crates/anofox-fcst-core/src/forecast.rs` (~line 155+) — GARCH and Kalman are new arms here, reusing the entire univariate pipeline (collect → FFI → long-format emit).
- The Phase 2 `ts_forecast_panel_by` / `_ts_forecast_panel_native.cpp` is the closest analog for the NEW VAR multivariate table function (fit-once-emit-many, Finalize barrier, ragged handling) — reuse its structure for `ts_forecast_var_by`.
- **Panel/table-in macro gotcha (from Phase 2):** wrap `query_table(source::VARCHAR)` in a subselect `(SELECT ... FROM query_table(...))`, never pass it as a bare TABLE arg, or the macro silently fails to register. Applies to the new VAR macro.
- Benchmark harness under `benchmark/` + the `benchmark/.venv` (statsmodels/scipy/arch live there, NOT system python3).

### Established Patterns
- Delivery pattern (locked): Rust FFI export → C++ table function → registration in `src/anofox_forecast_extension.cpp` → `ts_*_by` macro in `src/macros/ts_macros.cpp` → `examples/*.sql` → docs.
- DuckDB GROUP BY / native-Finalize parallelism only; no custom threading, no table-in/table-out beyond the established native-function Finalize-barrier pattern.
- FFI: `#[no_mangle] pub unsafe extern "C"`, `catch_unwind` panic containment, checked multiplications for buffer sizing (a Phase-2 code-review lesson), error propagation via out-params — mirror the Phase 2 panel FFI.

### Integration Points
- Upstream API (verified in `~/.cargo/registry/.../anofox-forecast-0.15.3`):
  - `models::garch::GARCH::{new(p,q), garch_1_1(), builder(), forecast_variance(horizon) -> Result<Vec<f64>>, conditional_variance(), is_stationary()}`.
  - `models::kalman_forecaster::KalmanForecaster::{local_level(), local_linear_trend(), with_model(StateSpaceModel)}` (+ the `kalman.rs` state-space core).
  - `models::var::VAR::{fit(&[Vec<f64>]), predict(horizon) -> Vec<Vec<f64>>}` — the true multivariate path; `var_forecaster::VARForecaster::new(order)` is the single-series trait wrapper (not used for the multivariate surface).
- GARCH/Kalman need new `ModelType` enum variants + FFI method-string arms; VAR needs a brand-new FFI export (multi-series in → multi-series out) + a new native table function + the `ts_forecast_var_by` macro.

</code_context>

<specifics>
## Specific Ideas

- VAR function name is fixed to **`ts_forecast_var_by`** (requirement CLAS-03 names it).
- GARCH `forecast_value` documented as **volatility (std-dev)**, not variance — a documentation must-have.
- Each of the three models needs a committed benchmark under `benchmark/` (GARCH/Kalman vs statsmodels/arch on a suitable univariate series; VAR vs statsmodels on the synthetic VAR(1) set).
- Python benchmarks/cross-checks run under `benchmark/.venv/bin/python`, never system python3 (Phase-1 precedent).

</specifics>

<deferred>
## Deferred Ideas

- **Prediction intervals** for GARCH/Kalman/VAR — route through the existing conformal path later, not built into these surfaces in v1.
- **VAR automatic lag-order selection** (AIC/BIC) — explicit `order` param only for v1.
- **Per-panel VAR** (a `group_col` fanning out independent VAR fits) — v1 may be single-panel; revisit if needed.
- GARCH advanced-coefficient user overrides beyond p/q — auto-fit is the v1 default.

</deferred>
