# Phase 4: AutoEnsemble Surface + Combination Methods - Context

**Gathered:** 2026-08-30
**Status:** Ready for planning
**Mode:** Smart discuss (autonomous) — grey areas proposed in batch, user accepted recommended answers

<domain>
## Phase Boundary

Expose the crate's `AutoEnsemble` (auto-fit ARIMA/ETS/Theta, rank by MSE, combine top-K) to SQL per series, plus the six `CombinationMethod` variants that govern how the selected members are blended (Mean, Median, WeightedMSE, InverseAIC, Stacking, HorizonAdaptive).

**In scope:** the AutoEnsemble point-forecast surface on `ts_forecast_by`; `top_k`, `combination_method`, `seasonal_period` parameters; all six combination methods reachable and correct; runnable verified example; internal-consistency cross-check (Mean combination == arithmetic mean of member forecasts).

**Out of scope (later phases):** explicit user-named member lists (Phase 5, ENS-02); ensemble prediction intervals (Phase 6, EPI-01); member/weight introspection (Phase 6, INSP-01). Custom hand-supplied weights (`CombinationMethod::Custom`) and panel/VAR ensembles remain deferred (ENS-F1, ENS-F2).

</domain>

<decisions>
## Implementation Decisions

### SQL Surface & Parameter Passing
- **Delivery vehicle:** `method := 'AutoEnsemble'` on the existing `ts_forecast_by` — per-series method-string dispatch, the same lower-risk vehicle used for GARCH/Kalman in v0.7.0 (roadmap-specified). No new top-level macro.
- **Parameter transport:** extend the `ForecastOptions` FFI struct *additively* with ensemble fields (mirrors how `garch_p`/`garch_q`/`kalman_model` were added) and surface them through the existing `ts_forecast_by` params MAP/STRUCT. No parallel options struct; preserve ABI back-compat for all pre-ensemble methods.
- **`combination_method` representation:** case-insensitive string — `'mean'`, `'median'`, `'weighted_mse'`, `'inverse_aic'`, `'stacking'`, `'horizon_adaptive'` (accept common spellings, e.g. `'weightedmse'`). Maps to the crate `CombinationMethod` enum.
- **Default `combination_method`:** **Mean** (overrides the crate's `WeightedMSE` default). Rationale: makes the required internal-consistency cross-check (combined == manual arithmetic mean of member forecasts) the default, most predictable path and the easiest success-criterion to demonstrate.

### Defaults & Edge Behavior
- **Default `top_k`:** 3 (crate default).
- **`seasonal_period`:** reuse the existing `ts_forecast_by` `seasonal_period` param; `0` → non-seasonal (`None`), `p>1` → seasonal AutoARIMA/AutoETS/AutoTheta.
- **Stacking `folds`:** fixed internally, not exposed in SQL for v1 (crate marks it "reserved for future cross-validation"; currently uses second-half in-sample holdout). Can be surfaced later without ABI break.
- **Candidate families:** fixed to ARIMA/ETS/Theta — crate-defined in `AutoEnsemble::fit`, not user-configurable in Phase 4.
- **Fewer than `top_k` families fit:** combine whatever fitted successfully (crate already ranks `candidates.len()` and takes `min(top_k, len)`); propagate the crate error as a DuckDB exception only when *no* model fits (`ConvergenceFailure`).

### Claude's Discretion
- Exact param key names inside the MAP (e.g. `top_k` vs `k`), string-normalization helper, and where the enum-string parse lives (FFI vs C++) are Claude's discretion — follow the existing GARCH/Kalman param-parsing convention.
- Whether the FFI carries `top_k` as `c_int` and `combination_method` as a fixed-size `[c_char; N]` (consistent with `kalman_model`) is an implementation detail at Claude's discretion.

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- **Crate API** (`anofox-forecast` 0.15.3): `models::ensemble::AutoEnsemble`, `AutoEnsembleConfig { top_k, combination_method, seasonal_period }`, `CombinationMethod` enum (six variants incl. `Stacking { folds }`), `Ensemble` with `.weights()` / `.horizon_weights()` / `.method()` accessors (the latter feed Phase 6 introspection). `AutoEnsemble` implements the standard `Forecaster` trait (`fit` + `predict`), so it slots into existing single-series dispatch.
- **FFI options struct:** `crates/anofox-fcst-ffi/src/types.rs:373` `ForecastOptions` — additive fields `garch_p`/`garch_q` (`c_int`) and `kalman_model` (`[c_char; 32]`) at lines 407–413 are the exact precedent for adding `top_k` + `combination_method`. Method dispatch match at `types.rs:1605`.
- **C++ dispatch/macro pattern:** `src/table_functions/ts_forecast_native.cpp` (method dispatch) + `src/macros/ts_macros.cpp` (`ts_forecast_by` param MAP/STRUCT plumbing) — GARCH/Kalman params flow through here.

### Established Patterns
- Delivery pattern: Rust FFI `#[no_mangle] extern "C"` export → C++ table function → registration in `src/anofox_forecast_extension.cpp` → `ts_*_by` macro → `examples/*.sql` → `docs/`.
- ABI extended additively for new-model params (GARCH/Kalman precedent) — integration-checker confirmed no breakage across pre-existing methods.
- Parallelism stays at the DuckDB GROUP BY layer; no custom threading.

### Integration Points
- Method-string match in FFI (`types.rs:1605`) and C++ forecast dispatch — add an `"AutoEnsemble"` arm.
- `ts_forecast_by` macro param surface in `src/macros/ts_macros.cpp`.
- New non-globbed C++ sources (if any) must be explicitly listed in CMakeLists.

</code_context>

<specifics>
## Specific Ideas

- Success-criterion example must demonstrate: fixed `Mean` combination → ensemble point forecast equals the manual arithmetic mean of each member's independent forecast, run through the built extension (`examples/*.sql`).
- Verify all six combination methods produce a valid forecast per series; Mean vs Median must produce visibly different point forecasts on a skewed series (COMB-01 demonstrability).

</specifics>

<deferred>
## Deferred Ideas

- Explicit user-named member ensemble → Phase 5 (ENS-02).
- Ensemble prediction intervals via conformal path → Phase 6 (EPI-01).
- Member/weight introspection surface → Phase 6 (INSP-01). Note: crate `Ensemble::weights()` / `.horizon_weights()` accessors are the data source; keep them reachable when wiring Phase 4.
- Custom hand-supplied weights (`CombinationMethod::Custom`, ENS-F1) and panel/VAR ensembles (ENS-F2) — deferred beyond v0.8.0.
- Exposing Stacking `folds` in SQL — deferred until the crate uses it for real CV.

</deferred>
