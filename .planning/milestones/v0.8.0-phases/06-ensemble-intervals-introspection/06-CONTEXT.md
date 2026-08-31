# Phase 6: Ensemble Intervals & Introspection - Context

**Gathered:** 2026-08-31
**Status:** Ready for planning
**Mode:** Smart discuss (autonomous) — grey areas proposed in batch; the crate-limitation-driven scope decisions were accepted by the user.

<domain>
## Phase Boundary

Two capabilities on top of the ensemble surfaces built in Phases 4–5:
- **EPI-01** — attach distribution-free prediction intervals to an ensemble point forecast **through the existing conformal path** (learn + apply). No new interval machinery.
- **INSP-01** — inspect which member models an ensemble selected and their combination weights, per series.

**In scope:** a runnable+verified example (+docs) demonstrating conformal intervals on ensemble forecasts (both AutoEnsemble and explicit-member); a NEW introspection function returning per-series member/weight rows; DoD internal-consistency (weights consistent with the chosen combination method).

**Out of scope / deferred:** new conformal/interval algorithms (reuse only); AutoEnsemble inner-ensemble combination-weights beyond what the crate exposes (upstream-crate limitation — see below); custom weights (ENS-F1); panel/VAR ensembles (ENS-F2).
</domain>

<decisions>
## Implementation Decisions

### EPI-01 — Ensemble prediction intervals
- **Demonstrate + document the EXISTING conformal path — NO new interval code.** The conformal machinery (`ts_cv_folds_by` + `ts_cv_forecast_by` + `ts_conformal_by` / `ts_conformal_calibrate`, split/adaptive/asymmetric/per-step) is model-agnostic — it operates on actual-vs-forecast residual columns regardless of which model produced the forecast. Phase 6 delivers a runnable `examples/*.sql` (+docs) showing learn→apply conformal intervals on an ensemble point forecast for BOTH the AutoEnsemble (`ts_forecast_by(...,'AutoEnsemble',...)`) and the explicit-member (`ts_forecast_ensemble_by`) surfaces, producing lower/upper bounds per horizon step. This matches the milestone DoD ("EPI-01 routes through the existing conformal path — reuse, do not build new interval machinery").
- Verification: the example must show non-degenerate lower ≤ point ≤ upper bounds per step, produced by the existing conformal functions applied to backtested ensemble forecasts, run against the built extension.

### INSP-01 — Introspection scope (driven by crate 0.15.3 limits)
- **Crate reality:** `Ensemble.weights() -> &[f64]` is public (explicit-member surface). `AutoEnsemble` exposes `all_scores() -> &[(String, f64)]` (candidate name + in-sample MSE, sorted; top-K = selected) and `model_count()`, but exposes **NO accessor for the inner ensemble's combination weights**.
- **Explicit-member ensemble:** FULL introspection — return each member's name (user-supplied, known to our FFI) + its combination `weight` per series, for all six methods (via `Ensemble.weights()`). Weights must be consistent with the method (Mean → equal; WeightedMSE/InverseAIC/Stacking/HorizonAdaptive → the crate-computed weights).
- **AutoEnsemble:** return the SELECTED member names + their in-sample MSE `score` (via `all_scores()[..model_count]`), plus the combination `weight` only where it is cleanly/trivially available (e.g. Mean → equal weights). Where the crate does not expose the weight (WeightedMSE/InverseAIC/Stacking/HorizonAdaptive on AutoEnsemble), return the member+score with a NULL weight (or omit weight) and **document the gap as an upstream-crate limitation / future enhancement**. Do NOT re-implement AutoEnsemble's selection to fabricate weights (fragile, drift-prone).

### INSP-01 — Surface & shape
- **Dedicated function, long format.** A new per-series introspection function (working name `ts_ensemble_inspect_by` / `ts_ensemble_weights_by` — exact name at Claude's discretion, follow the `_by` convention) returning LONG-format rows: `(group_key, member_name, weight, rank_or_score)` — one row per member per series. Idiomatic DuckDB (easy to JOIN/filter). NOT companion columns on the forecast output.
- It should cover both surfaces: for the explicit-member ensemble it takes the same member-list + method inputs; for AutoEnsemble it takes the AutoEnsemble config (top_k, method, seasonal_period). Whether this is ONE function with a mode, or TWO sibling functions (one per surface), is Claude's discretion — pick the lower-complexity option during planning (a single explicit-member introspection + an AutoEnsemble introspection may be cleaner than a mode flag).

### Claude's Discretion
- Exact introspection function name(s) and column names; whether AutoEnsemble introspection is a separate function from explicit-member introspection; the FFI shape for returning (name, weight, score) triples per series; whether `rank` (1..k by ascending MSE) and/or `score` (MSE) columns are both included.
- Whether to reuse the Phase 5 `forecast_explicit_ensemble` construction path (build the `Ensemble`, then read `.weights()`) as the introspection backend — recommended, since it already builds the Ensemble.

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- **Conformal surface (EPI-01):** `ts_conformal_by(backtest_results, group_col, actual_col, forecast_col, point_forecast_col, params)` and `ts_conformal_calibrate(...)` in `src/macros/ts_macros.cpp` (~line 1554+); CV via `ts_cv_folds_by` + `ts_cv_forecast_by`. Backing scalar in `src/scalar_functions/conformal.cpp`. These are model-agnostic — feed them ensemble forecasts.
- **Crate introspection accessors:** `Ensemble.weights()`, `.method()`, `.horizon_weights()`, `.model_count()`; `AutoEnsemble.all_scores()` (name, MSE), `.model_count()`. (`anofox-forecast` 0.15.3.)
- **Phase 5 backend:** `forecast_explicit_ensemble` / `build_forecaster` in `crates/anofox-fcst-core/src/forecast.rs` already build the `Ensemble` from member names — the natural place to also read `.weights()` for introspection. `parse_combination_method` reused.
- **Phase 4 backend:** `forecast_auto_ensemble` builds `AutoEnsemble` — read `all_scores()` for selected members + MSE.
- **FFI/C++/macro delivery pattern** as in Phases 4–5; the new introspection function is per-series (like `ts_forecast_ensemble_by`).

### Established Patterns
- Delivery: Rust FFI export → C++ scalar/native fn in `src/` → registration in `src/anofox_forecast_extension.cpp` → `ts_*_by` macro in `src/macros/ts_macros.cpp` → `examples/*.sql` → `docs/`.
- **New non-globbed C++ source MUST be added to CMakeLists.txt `EXTENSION_SOURCES`** (Phase 5 lesson — silent build + missing runtime fn otherwise).
- `make header` after FFI edits. Runnable example against the BUILT extension (PR #230). Reuse the `_ts_forecast_scalar` per-series ScalarFunction dispatch precedent (Phase 5 critical dispatch note — the `_by` macros dispatch to a scalar function, not a table-in-out function).
- **Independently verify executor reports against git+disk and re-run examples** ([[feedback_verify_executor_reports]]) — two executors fabricated commits/SUMMARY earlier this milestone.

### Integration Points
- INSP-01 new FFI export returning per-series (member_name, weight, score) triples + count → new C++ scalar fn → `ts_ensemble_inspect_by` (or split) macro. EPI-01 needs NO new code — only example + docs wiring existing conformal functions to ensemble forecasts.

</code_context>

<specifics>
## Specific Ideas

- EPI-01 example: backtest an ensemble forecast (CV), learn a conformal quantile, apply it to the ensemble point forecast → lower/upper per step; show it for both AutoEnsemble and explicit-member.
- INSP-01 DoD internal-consistency: for the explicit-member ensemble with Mean, every member's weight == 1/k (equal); for WeightedMSE, weights are inverse-MSE normalized and sum to 1 — assert `sum(weight)=1` per series and the Mean-equal-weight case, run against the built extension.
- Reuse PR #230 doc-snippet verification discipline ([[feedback_verify_sql_docs]]).

</specifics>

<deferred>
## Deferred Ideas

- AutoEnsemble inner-ensemble combination weights for WeightedMSE/InverseAIC/Stacking/HorizonAdaptive — blocked by crate 0.15.3 (no accessor). File as a future enhancement / upstream request; do not re-implement selection.
- New conformal/interval algorithms (IDR/QRA/CQR/EnbPI/binned) — out of scope; existing conformal coverage is sufficient.
- Custom hand-supplied weights (ENS-F1) and panel/VAR ensembles (ENS-F2) — deferred beyond v0.8.0.
- Per-member parameters (from Phase 5) — still deferred.

</deferred>
