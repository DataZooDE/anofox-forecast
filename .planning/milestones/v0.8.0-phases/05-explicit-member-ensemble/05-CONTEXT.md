# Phase 5: Explicit-Member Ensemble - Context

**Gathered:** 2026-08-31
**Status:** Ready for planning
**Mode:** Smart discuss (autonomous) — grey areas proposed in batch, user accepted recommended answers

<domain>
## Phase Boundary

Expose a **user-named-member** ensemble to SQL: the user supplies an explicit list of member model names plus a combination method; the extension fits each named member per series and combines them, reusing Phase 4's combination-method plumbing (`parse_combination_method` + the crate `CombinationMethod` enum). Delivers ENS-02.

**In scope:** a dedicated `ts_forecast_ensemble_by(...)` macro + backing native table function; member list as a `VARCHAR[]`; the same six combination methods as Phase 4; a shared `seasonal_period`; internal-consistency cross-check (fixed method → ensemble == manual weighted combination of each named member's independent forecast); runnable verified example; docs.

**Out of scope (later / deferred):** ensemble prediction intervals (Phase 6, EPI-01); member/weight introspection (Phase 6, INSP-01); per-member parameter maps (deferred beyond v1); custom hand-supplied weights (ENS-F1); panel/VAR ensembles (ENS-F2).
</domain>

<decisions>
## Implementation Decisions

### Explicit-Member Surface Shape
- **Dedicated macro `ts_forecast_ensemble_by(...)`** — NOT an overload of `ts_forecast_by`'s method-string. Rationale: the roadmap success criteria names it; a member *list* + combination method does not fit the single method-string of `ts_forecast_by`. New native table function + macro, reusing Phase 4's FFI ensemble/combination plumbing.
- **Member list as SQL `VARCHAR[]`** — e.g. `members := ['AutoARIMA','AutoETS','Theta']`. Idiomatic DuckDB list.
- **Member vocabulary reuses the existing `ts_forecast_by` model-name vocabulary** (`ModelType::from_str`) — same names users already know (the 36 model strings). Unknown member names → clean `InvalidParameter` error naming the offending member.
- **Combination reuses Phase 4** — `parse_combination_method` (all six methods + aliases) feeds the crate `Ensemble::new(members).with_method(method)` path, so the blend math is identical to AutoEnsemble and the cross-check is exact.

### Params & Defaults
- **Default `combination_method`: Mean** (consistent with Phase 4 default; makes the cross-check the default path).
- **Shared params only in v1** — each named member fits with its own defaults plus a single shared `seasonal_period` (`0` → non-seasonal). Per-member parameter maps are deferred (a future milestone).
- **Minimum 2 members** — an ensemble of one is degenerate; fewer than 2 named members → clear error.
- **Duplicate members allowed** — the same model name may appear more than once; the crate handles it and weights apply per instance.

### Claude's Discretion
- Exact macro parameter names/order (e.g. `members`, `combination_method`, `seasonal_period`, horizon/freq), the FFI marshalling shape for the `VARCHAR[]` member list (e.g. a packed length-prefixed C string array vs a delimited buffer), and where the name→`Box<dyn Forecaster>` factory lives (see code_context) are Claude's discretion — follow the established GARCH/Kalman + Phase 4 conventions.
- Whether to emit `model_name` as `'Ensemble'` or `'Ensemble(<methods>)'` — plain `'Ensemble'` recommended, mirroring Phase 4's plain `'AutoEnsemble'`.

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- **Phase 4 combination plumbing:** `parse_combination_method(Option<&str>) -> Result<CombinationMethod>` in `crates/anofox-fcst-core/src/forecast.rs` (all six methods + aliases, rejects `custom`) — reuse verbatim.
- **Crate explicit-ensemble API** (`anofox-forecast` 0.15.3): `models::ensemble::Ensemble::new(models: Vec<Box<dyn Forecaster>>)`, `.with_method(CombinationMethod)`, `.fit(&ts)`, `.predict(h)`, plus `.weights()`/`.method()` accessors (Phase 6 introspection). `models::convenience::ensemble_best_k` and `models::traits::{ModelSpec, ModelRegistry}` (`.create() -> BoxedForecaster`) show the registry/factory pattern.
- **Model-name dispatch:** `ModelType::from_str` (forecast.rs ~line 170–290) already maps the 36 model-name strings → `ModelType`; reuse for validating/parsing member names.

### Established Patterns
- Delivery pattern: Rust FFI export → C++ native table function → registration in `src/anofox_forecast_extension.cpp` → `ts_*_by` macro in `src/macros/ts_macros.cpp` → `examples/*.sql` → `docs/`.
- The panel/table-in macro lesson ([[project_panel_macro_subselect_pattern]]): table-in macros must wrap `query_table(...)` in a subselect, not pass a bare TABLE arg. This is a *panel* concern — `ts_forecast_ensemble_by` is per-series like `ts_forecast_by`, so it likely follows the `ts_forecast_by` native-table-function pattern (group_col, date_col, value_col) rather than the panel pattern; confirm during planning.
- Additive-ABI discipline ([[feedback_verify_executor_reports]] run lesson): if extending existing FFI option structs, append fields at the END.

### Integration Points / KEY DESIGN QUESTION (for research/planning)
- **name → `Box<dyn Forecaster>` factory.** The existing `forecast.rs` dispatch runs each model **inline** (`forecast_*(values, horizon, …) -> (Vec, Vec, Vec)`) and does NOT return boxed forecasters, so `Ensemble::new(Vec<Box<dyn Forecaster>>)` cannot reuse it directly. Planning must decide how to construct each member as a `Box<dyn Forecaster>` from its name — either via the crate `ModelRegistry`/`ModelSpec` factories, or a new `build_forecaster(ModelType, period) -> Box<dyn Forecaster>` helper mirroring the existing match. This is the central implementation risk; research it first.

</code_context>

<specifics>
## Specific Ideas

- Cross-check (DoD): for a fixed combination method, `ts_forecast_ensemble_by(members := ['A','B','C'], combination_method := 'mean', ...)` must equal the manual arithmetic/weighted mean of each named member's independently-computed `ts_forecast_by` forecast, within numerical tolerance — demonstrated by a runnable `examples/*.sql` against the built extension.
- Mean is the cleanest cross-check; for weighted methods (WeightedMSE/InverseAIC/Stacking/HorizonAdaptive) the "manual weighted combination" cross-check requires the crate's weights — that reconciliation may lean on the Phase 6 weight-introspection surface, so a Mean (equal-weight) cross-check is the guaranteed Phase 5 DoD signal.
- Reuse the same runnable-example + PR #230 doc-snippet verification discipline as Phase 4 ([[feedback_verify_sql_docs]]).

</specifics>

<deferred>
## Deferred Ideas

- Ensemble prediction intervals (conformal) → Phase 6 (EPI-01).
- Member/weight introspection → Phase 6 (INSP-01). Keep `Ensemble::weights()`/`.method()` reachable when wiring Phase 5.
- Per-member parameter maps (each member its own params) → future milestone.
- Custom hand-supplied weights (`CombinationMethod::Custom`, ENS-F1) and panel/VAR ensembles (ENS-F2) → deferred beyond v0.8.0.

</deferred>
