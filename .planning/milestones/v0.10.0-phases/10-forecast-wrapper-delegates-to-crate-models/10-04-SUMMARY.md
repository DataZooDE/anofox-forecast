---
phase: 10-forecast-wrapper-delegates-to-crate-models
plan: 04
subsystem: forecasting
tags: [rust, cpp, duckdb, anofox-forecast, exogenous, arima, theta, ets, mfles, mstl, ffi]

requires:
  - phase: 10-01
    provides: forecast()/forecast_with_exog() crate-passthrough foundation (D-01/D-02/D-03/D-11/D-12)
  - phase: 10-02
    provides: real crate ARIMA/SARIMA fit, strict FFI option-string parsing pattern
  - phase: 10-03
    provides: shared new_* forecaster constructors, ForecastOptionsExog -> ForecastOptions delegation for the no-regressor branch, resolve_period/requires_seasonal_period
provides:
  - "forecast_with_exog routes every exog-capable model (Naive, ARIMA/AutoARIMA, Theta family, ETS/AutoETS, MFLES, MSTL) through the crate's OWN exog support (CalendarAnnotations + predict_with_exog_intervals) instead of a wrapper OLS-on-residuals hack"
  - "AutoARIMA with regressors runs its real order search WITH the regressors attached; model_name reports the selected order + ' with regressors', never a fixed 'ARIMAX' stand-in"
  - "A regressor constant in history gets a 0 effect by construction (dropped before the crate ever sees it); every regressor dropped (or none given) delegates to forecast() itself with no suffix"
  - "Regressor NULLs travel via validity end to end: Rust prepare_exog_regressors (NaN/Inf/future-NULL policy) and FFI ExogenousRegressor.validity/future_validity (read via build_values, mapped to NaN) through to the SQL layer's ExtractNestedListValues bitmask marshalling"
  - "A non-exog-capable model (HoltWinters/SES/GARCH/Kalman/Croston/...) given a real regressor is a hard InvalidInput naming the model"
  - "_ts_forecast_exog is a ScalarFunctionSet with the original 5-arg form and a new 6-arg overload (params ANY) carrying every ts_forecast_by option onto the exog surface; both ts_forecast_exog/ts_forecast_exog_by macros pass params through"
  - "SetListFromArray/SetListFromArrayNanSafe: a null FFI result pointer is SQL NULL for that list FIELD (never unreserved-memory length>0); NaN fitted/residual elements are individually SQL NULL (not the NaN bit pattern) in fitted/residuals across all three scalar functions in ts_forecast.cpp"
affects: ["10-05", "10-06", "10-07", "10-08", "10-09"]

actuals:
  tokens: 31500
  tasks: 3
  commits: 3
  plan_head_before: aaf2c9a

tech-stack:
  added: []
  patterns:
    - "fit_predict_exog<M: Forecaster>(model, ts, horizon, future_map, level, label, name_fn) — one generic fit+predict_with_exog_intervals+naming helper reused by every exog-capable ModelType arm in forecast_crate_exog, appending \" with regressors\" exactly once so every call site's name_fn only produces the bare model name (mirrors extract_forecast's role for the non-exog path)"
    - "prepare_exog_regressors(exog) -> Result<Option<ExogenousData>>: the single regressor policy gate (Inf -> error, all-NaN historical -> error, interior/edge NaN -> fill_nulls_interpolate, constant-after-interpolation -> drop, kept-column future NaN -> error) run once before any model-specific dispatch; None means \"no real regressors\" and the caller delegates to forecast()"
    - "C++ scalar functions accepting optional params: register two ScalarFunction overloads (5-arg, 6-arg) under one ScalarFunctionSet name sharing ONE callback, branching internally on args.ColumnCount() — avoids duplicating the whole per-row extraction/FFI-call body for the params-vs-no-params cases"

key-files:
  created:
    - .planning/phases/10-forecast-wrapper-delegates-to-crate-models/deferred-items.md
  modified:
    - crates/anofox-fcst-core/src/forecast.rs
    - crates/anofox-fcst-ffi/src/types.rs
    - crates/anofox-fcst-ffi/src/lib.rs
    - crates/anofox-fcst-ffi/tests/core_ffi_parity.rs
    - crates/anofox-fcst-core/tests/forecast_exog.rs
    - crates/anofox-fcst-core/tests/exog_ols_memory.rs
    - src/include/anofox_fcst_ffi.h
    - src/table_functions/ts_forecast.cpp
    - src/macros/ts_macros.cpp
    - test/sql/ts_forecast_crate_delegation.test
    - test/sql/ts_forecast_exog.test
    - validation/reference/sql/check_dep01_exog.sql

key-decisions:
  - "ExogenousRegressor.validity/future_validity were added to the FFI struct in Task 1 (interface-first) with every caller passing null, so Task 1's new tests already target the FINAL struct shape; Task 2 wired the Rust read side and Task 3 wired the C++ write side — no second ABI-breaking struct change needed mid-plan."
  - "A regressor constant in history is DROPPED (never passed to the crate at all) rather than passed through with a 0 coefficient forced after the fact — this sidesteps the crate's own OLS pre-regression hitting a singular normal-equation system, and makes 'every regressor constant' collapse naturally onto the existing no-regressor forecast() delegation path (one code path, not two)."
  - "The wrapper OLS functions (fit_ols_regression, apply_regression, forecast_arima_with_exog, forecast_theta_with_exog, forecast_mfles_with_exog) are deleted outright, not softened or kept behind a flag — per D-13 there is no legacy-compatibility mode in this milestone. anofox-regression stays a declared Cargo dependency (now core-unused) since removing it touches Cargo.lock and the Phase 9 snapshot bless helper; recorded as a follow-up, not fixed here."
  - "_ts_forecast_exog's 6-argument overload shares ONE callback with the 5-argument overload (branching on args.ColumnCount()) rather than two near-duplicate functions, keeping the historical/future-regressor extraction, FFI call, and NULL-safe result marshalling logic in exactly one place."
  - "Pre-existing test/sql/ts_model_distinctness.test SeasonalES/SeasonalESOptimized staleness (traced to Plan 10-03's resolve_period change, never re-blessed) is OUT OF SCOPE for this plan (Rule: only auto-fix issues directly caused by the current task) — logged to deferred-items.md and .planning/WINDOWS.md instead of fixed, since fixing it would require re-blessing a fixture this plan does not own and did not break."

patterns-established:
  - "Regressor validity is a first-class concept end to end: Rust's NaN-is-missing convention (ExogenousData.historical/future: Vec<f64> with NaN sentinels) <-> FFI ExogenousRegressor.validity/future_validity bitmasks (ceil(n/64) words, bit=valid) <-> SQL ExtractNestedListValues's per-element validity output — any future exog-data-carrying FFI struct should follow the same validity-bitmask-plus-NaN-sentinel pairing rather than inventing a new missing-value convention."

requirements-completed: [FCST-05, FCST-08, FCST-01, FCST-03]

coverage:
  - id: D1
    description: "AutoARIMA with regressors runs the crate's own exog-aware order search (not a fixed ARIMA stand-in); model_name reports the real selected order followed by ' with regressors', bit-identical to the crate AutoARIMA fitted directly with the same regressor via CalendarAnnotations + predict_with_exog_intervals"
    requirement: FCST-05
    verification:
      - kind: integration
        ref: "crates/anofox-fcst-core/tests/forecast_exog.rs#autoarima_exog_runs_order_search"
        status: pass
      - kind: integration
        ref: "crates/anofox-fcst-ffi/tests/core_ffi_parity.rs#exog_autoarima_parity_through_ffi"
        status: pass
      - kind: e2e
        ref: "test/sql/ts_forecast_crate_delegation.test (AutoARIMA(...) with regressors regexp assertion), run via ./build/release/test/unittest"
        status: pass
    human_judgment: false
  - id: D2
    description: "OptimizedTheta/DynamicTheta/DynamicOptimizedTheta/AutoTheta stay themselves with regressors (never a plain-Theta substitution); ETS spec, ARIMA orders, MFLES and MSTL also route through the crate's own exog support"
    requirement: FCST-05
    verification:
      - kind: integration
        ref: "crates/anofox-fcst-core/tests/forecast_exog.rs#optimized_theta_stays_itself,ets_spec_with_regressors"
        status: pass
      - kind: e2e
        ref: "test/sql/ts_forecast_crate_delegation.test (OptimizedTheta with regressors model_name assertion)"
        status: pass
    human_judgment: false
  - id: D3
    description: "A regressor constant in history (e.g. a holiday flag absent from training) gets a 0 effect and the forecast stays finite; a model without exog support given regressors is an SQL error naming the model; without regressors ts_forecast_exog_by honours every ts_forecast_by option"
    requirement: FCST-05
    verification:
      - kind: integration
        ref: "crates/anofox-fcst-core/tests/forecast_exog.rs#constant_regressor_has_zero_effect,non_exog_model_with_regressors_errors"
        status: pass
      - kind: e2e
        ref: "test/sql/ts_forecast_crate_delegation.test (zero-effect regressor max-abs-diff assertion; HoltWinters-with-regressor statement error; GARCH params through the 6-arg overload)"
        status: pass
    human_judgment: false
  - id: D4
    description: "Regressor NULLs travel via validity: interior historical NULLs are interpolated, an all-NULL historical regressor or any NULL future regressor value is an error naming the regressor; a validity bit cleared at the FFI boundary behaves exactly like a NaN there"
    requirement: FCST-08
    verification:
      - kind: integration
        ref: "crates/anofox-fcst-core/tests/forecast_exog.rs#regressor_nan_policy"
        status: pass
      - kind: integration
        ref: "crates/anofox-fcst-ffi/tests/core_ffi_parity.rs#exog_regressor_validity_bit_cleared_behaves_like_nan,exog_future_validity_bit_cleared_is_invalid_input"
        status: pass
      - kind: e2e
        ref: "test/sql/ts_forecast_crate_delegation.test (future-NULL statement error, historical-interior-NULL success)"
        status: pass
    human_judgment: false
  - id: D5
    description: "_ts_forecast / _ts_forecast_exog emit SQL NULL bounds (not uninitialised memory) when a model has no intervals, and SQL NULL residuals at interpolated/trimmed positions; validation/reference/sql/check_dep01_exog.sql still prints DEP01_SQL_PASS for both designs"
    requirement: FCST-01
    verification:
      - kind: e2e
        ref: "test/sql/ts_forecast_crate_delegation.test (all-NULL series IS NULL; NaN==NULL residual-position assertion; MSTL-with-regressors NULL bounds assertion)"
        status: pass
      - kind: manual_procedural
        ref: "validation/reference/sql/check_dep01_exog.sql run via duckdb CLI — prints DEP01_SQL_PASS typical and DEP01_SQL_PASS 15k"
        status: pass
    human_judgment: false
  - id: D6
    description: "D-12 trailing-NULL y realignment holds through the SQL macro surface: RandomWalkDrift forecast_step=1 lands on the period after the ORIGINAL last row (not the last valid row), with the correct drift-projected value"
    requirement: FCST-03
    verification:
      - kind: e2e
        ref: "test/sql/ts_forecast_crate_delegation.test (rwd_trailing_null yhat=32/ds=2020-02-02 assertion)"
        status: pass
    human_judgment: false

duration: ~3h (single session)
completed: 2026-10-09
status: complete
---

# Phase 10 Plan 04: Exogenous Path Native Crate Routing Summary

**`forecast_with_exog` now routes AutoARIMA, ARIMA, Naive, the full Theta family, ETS/AutoETS, MFLES and MSTL through the crate's own exogenous-regressor support end to end — SQL params, FFI validity bitmasks, and a shared Rust fit-predict-name helper — deleting the wrapper OLS-on-residuals hack and its "ARIMAX"/"ThetaX"/"MFLESX" stand-in names entirely.**

## Performance

- **Duration:** ~3h (single session; 3 task commits)
- **Tasks:** 3 (all completed as planned, no checkpoints)
- **Files modified:** 12 (11 modified, 1 created)

## Accomplishments

- `forecast_crate_exog` (new) dispatches every `exog_capable` `ModelType` — `Naive`, `ARIMA`/`AutoARIMA`, `Theta`/`OptimizedTheta`/`DynamicTheta`/`DynamicOptimizedTheta`/`AutoTheta`, `ETS`/`AutoETS`, `MFLES`/`AutoMFLES`, `MSTL`/`AutoMSTL` — to the SAME crate constructor the non-exog path uses, attaches every validated regressor via `CalendarAnnotations::with_regressor` + `TimeSeries::set_calendar`, and predicts with `predict_with_exog_intervals`. A shared generic `fit_predict_exog<M: Forecaster>` helper appends `" with regressors"` to the bare model name exactly once, so auto-selection types (`AutoARIMA`, `AutoTheta`, `AutoETS`/`'ETS'` without a spec) report what they ACTUALLY selected (e.g. `"AutoTheta(DOTM) with regressors"`, `"ETS(A,A,N) with regressors"`) rather than a fixed stand-in.
- `prepare_exog_regressors` is the single regressor policy gate (D-10/D-12, FCST-08) run once before any model dispatch: `±Inf` anywhere is a hard error naming the regressor and position; an all-`NaN` historical regressor is a hard error; interior/edge historical gaps are filled via `fill_nulls_interpolate`; a regressor that is CONSTANT after interpolation is dropped entirely (0 effect by construction, sidesteps a singular normal-equation system in the crate's own OLS pre-regression) — and a dropped regressor's future values are never checked for NULLs, since the column never reaches the crate. When every regressor is dropped (or none were given), `forecast_with_exog` delegates to `forecast()` itself with no suffix, exactly like the no-regressor case.
- Deleted the entire wrapper OLS path: `fit_ols_regression`, `apply_regression`, `forecast_arima_with_exog`, `forecast_theta_with_exog`, `forecast_mfles_with_exog`, the `"ARIMAX"`/`"ThetaX"`/`"MFLESX"` stand-in names, the inline `fit_ols_regression_matches_numpy_lstsq_fixture` DEP-01 test (replaced by `forecast_exog.rs`'s `exog_typical_regression_effect`), and the `use anofox_regression::prelude::*` import — `anofox-regression` stays a declared Cargo dependency (now core-unused) per the plan's own discretion note.
- `ExogenousRegressor.validity`/`.future_validity` (`*const u64`, added Task 1) are now READ on the Rust side (`build_values`, already existed for the main series, de-`#[allow(dead_code)]`'d) and WRITTEN on the C++ side (`ExtractNestedListValues`'s new per-regressor bitmask output) — a regressor value masked invalid at either layer behaves exactly like an explicit `NaN`/`NULL` there, proven bit-identically through both layers independently.
- `_ts_forecast_exog` is now a `ScalarFunctionSet` with the original 5-argument form and a new 6-argument overload (`params ANY`) sharing ONE callback (`args.ColumnCount()` branch) with local `Exog*`-prefixed parse helpers mirroring `ts_forecast_scalar.cpp`'s `ParseStringParam`/`ParseInt64Param`/`ParseDoubleParam`/`ValidateParams` — every `ts_forecast_by` option (`seasonal_period`, `seasonal_periods`, `confidence_level`, `window`, `model_pool`, `laplace_variant`, `garch_p`/`garch_q`, `kalman_model`, `top_k`/`combination_method`, `arima_order`/`seasonal_order`) now reaches the exog surface too; both `ts_forecast_exog`/`ts_forecast_exog_by` macros pass `params` through.
- `SetListFromArray` makes the whole list FIELD SQL NULL when the FFI result pointer is null (T-10-13 — a model with no native interval support previously left a nonzero-length list backed by unreserved/uninitialised memory); `SetListFromArrayNanSafe` additionally marks each individual `NaN` `fitted`/`residuals` element (at interpolated/trimmed positions, D-11/D-12) as an invalid SQL-NULL list entry — applied across all three scalar functions in `ts_forecast.cpp` (`TsForecastFunction`, `TsForecastExogFunction`, `TsForecastWithModelFunction`). A historical/future regressor column-count mismatch is now an `InvalidInputException` naming both counts (T-10-14) instead of the old silent-truncation loop; `INVALID_INPUT`/`INVALID_MODEL` from the FFI call now throw instead of hiding behind a per-row `NULL`; `confidence_level` default changed `0.95` -> `0.90` to match `ts_forecast_by`.

## Task Commits

Each task was committed atomically:

1. **Task 1: Tracer — AutoARIMA exog runs crate's own order search via predict_with_exog_intervals** — `581d8fe` (feat)
2. **Task 2: Every exog-capable model via crate exog, constant regressors get 0 effect, wrapper OLS deleted** — `4b68566` (feat)
3. **Task 3: SQL exog surface carries params + regressor validity, NULL-safe lists; build + DEP-01 SQL re-run** — `0b60784` (feat)

**Plan metadata:** (this commit, immediately following)

## Files Created/Modified

- `crates/anofox-fcst-core/src/forecast.rs` — `exog_capable`, `prepare_exog_regressors`, `fit_predict_exog<M: Forecaster>`, `forecast_crate_exog`, `auto_arima_model_name` (shared with the non-exog path), `regressor_name`; `forecast_with_exog` rewritten around these; wrapper OLS functions and their inline DEP-01 test deleted.
- `crates/anofox-fcst-ffi/src/types.rs` — `ExogenousRegressor.validity`/`.future_validity` (`*const u64`).
- `crates/anofox-fcst-ffi/src/lib.rs` — `anofox_ts_forecast_exog` reads `validity`/`future_validity` via `build_values`.
- `crates/anofox-fcst-ffi/tests/core_ffi_parity.rs` — `exog_autoarima_parity_through_ffi`, `exog_regressor_validity_bit_cleared_behaves_like_nan`, `exog_future_validity_bit_cleared_is_invalid_input`.
- `crates/anofox-fcst-core/tests/forecast_exog.rs` (new) — 8 tests covering D-10/D-12/FCST-05/FCST-08.
- `crates/anofox-fcst-core/tests/exog_ols_memory.rs` — doc comment updated to describe the crate's own exog path; assertion/limit unchanged.
- `src/include/anofox_fcst_ffi.h` — cbindgen-regenerated, purely additive.
- `src/table_functions/ts_forecast.cpp` — `SetListFromArray`/`SetListFromArrayNanSafe`, `ExtractNestedListValues` validity output, `Exog*` param-parsing helpers, rewritten `TsForecastExogFunction` (5-arg + 6-arg shared callback), `ScalarFunctionSet` registration.
- `src/macros/ts_macros.cpp` — `ts_forecast_exog`/`ts_forecast_exog_by` pass `params` as the 6th `_ts_forecast_exog` argument.
- `test/sql/ts_forecast_crate_delegation.test` — 2 new sections (FCST-05/08/01), 12 new assertions (38/38 total), run natively against the built extension.
- `test/sql/ts_forecast_exog.test` — updated stand-in-name assertions to `"<Model> with regressors"`, `'ARIMA'` without `arima_order` now a `statement error`, two toy AutoARIMA-with-regressor cases bumped to 8+ points (Rule 1 — directly caused by this plan's behavior changes).
- `validation/reference/sql/check_dep01_exog.sql` — comments updated to describe the crate's own exog fit; assertions unchanged, both `DEP01_SQL_PASS` lines still print.
- `.planning/phases/10-forecast-wrapper-delegates-to-crate-models/deferred-items.md` (new) — logs the pre-existing `ts_model_distinctness.test` staleness.

## Decisions Made

See frontmatter `key-decisions`. Most significant: deleting the wrapper OLS path outright (no legacy-compatibility flag, per D-13) now that the crate's own exog support (which does its own internal OLS pre-regression via Cholesky normal equations, per 10-RESEARCH's crate survey) makes the wrapper's hand-rolled `anofox-regression` OLS entirely redundant.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Test-design] `test/sql/ts_forecast_exog.test`'s toy 6-point AutoARIMA-with-regressor cases needed more data**
- **Found during:** Task 3, running the full `test/sql/*.test` suite (not required by the plan's own `<verify>`, but run as part of this plan's DoD)
- **Issue:** `test/sql/ts_forecast_exog.test` predates this plan and asserted the OLD wrapper-OLS behavior directly: `.model` equal to the literal strings `"ARIMAX"`/`"ThetaX"`/`"MFLESX"` (now deleted), `'ARIMA'` without an `arima_order` silently working (now a hard error per D-08/D-09, same as the non-exog surface), and two AutoARIMA-with-regressor cases on 6-point toy series that the OLD wrapper handled fine (it ran AutoARIMA on OLS RESIDUALS, a plain series) but the crate's own exog-aware order search cannot fit with so few points relative to its default `max_p=5, max_q=5` search space.
- **Fix:** Updated the stand-in-name assertions to the new `"<Model> with regressors"` convention, changed the bare-`'ARIMA'` case to `statement error` naming `arima_order`, and widened the three too-small AutoARIMA-with-regressor series to 8-30 points (matching the file's own first, already-adequate example).
- **Files modified:** `test/sql/ts_forecast_exog.test`
- **Verification:** `./build/release/test/unittest "test/sql/ts_forecast_exog.test"` — 0 failed assertions (10 run, 1 section skipped for `require json`, verified separately via the duckdb CLI with `INSTALL/LOAD json`).
- **Committed in:** `0b60784` (Task 3 commit)

### Deferred (NOT fixed — out of scope)

**`test/sql/ts_model_distinctness.test`'s `SeasonalES`/`SeasonalESOptimized` expected values are stale**, traced to Plan 10-03's `resolve_period` change (`e01e87b`) rather than anything in this plan. Logged to `deferred-items.md` and `.planning/WINDOWS.md` (entry id 2, kind `unmet-truth`) per the scope-boundary rule — fixing it would mean re-blessing a fixture this plan does not own and did not break.

---

**Total deviations:** 1 auto-fixed (test-design, directly caused by this plan's behavior changes), 1 deferred (pre-existing, logged not fixed)
**Impact on plan:** The auto-fix is in-scope (the exact tests broken by this plan's own deletions/behavior changes); the deferral keeps the plan's blast radius limited to its stated scope (the exogenous path) rather than silently absorbing an unrelated Plan 10-03 regression.

## Issues Encountered

- The first draft of the SQL `regexp_matches` assertion for AutoARIMA's seasonal model-name form (`^AutoARIMA\(.*\) with regressors$`) failed against the ACTUAL seasonal format (`"AutoARIMA(p,d,q)(P,D,Q)[s] with regressors"`, ending in `]` not `)`) — fixed by using the same precise triplet-based pattern already proven correct in `forecast_exog.rs`'s manual matcher (`^AutoARIMA\(\d+,\d+,\d+\)(\(\d+,\d+,\d+\)\[\d+\])? with regressors$`).
- Two SQL assertions referencing a `WITH gen AS (...)`-scoped CTE from inside a trailing `FROM (...) AS gen` subquery failed with "Catalog Error: Table with name gen does not exist" — DuckDB CTEs are not visible to correlated subqueries the way FROM-clause table aliases are; fixed by promoting `WITH gen AS (...)` to the top of the whole statement.

## Known Stubs

None — all deliverables are fully wired; no placeholder/mock data paths were introduced.

## Threat Flags

None beyond what the plan's own `<threat_model>` already covers — T-10-12 (validity buffer sizing/null convention), T-10-13 (null-pointer-to-SQL-NULL, never unreserved memory), T-10-14 (regressor count-mismatch exception) and T-10-15 (constant-regressor/Inf handling before the crate's own OLS) are all mitigated exactly as the threat register specifies.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- Plan 10-05 and later plans in this phase can rely on: `forecast_with_exog` has no remaining wrapper-OLS code path (D-10 fully closed for every crate-exog-capable model); the FFI `ExogenousRegressor` struct's validity fields are the established pattern for any future per-element-missing-value FFI payload; `_ts_forecast_exog`'s 6-arg-overload-sharing-one-callback pattern is reusable for any other scalar function that needs an optional-params variant without duplicating its body.
- `cargo test --workspace` (330 tests), `cargo fmt --all -- --check`, and `cargo clippy --all-targets --all-features -- -D warnings` are all green on `HEAD` (`0b60784`).
- `RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release` succeeds; `./build/release/test/unittest "test/sql/ts_forecast_crate_delegation.test"` passes (38/38 assertions, up from 10-03's 25/25); `check_dep01_exog.sql` prints both `DEP01_SQL_PASS` lines.
- Docs (`docs/api/07-forecasting.md`, `docs/reference/models/**`, the `anofox-forecast-models` skill) still reference the deleted `"ARIMAX"`/`"ThetaX"`/`"MFLESX"` naming in places — flagged (same as 10-03's note) for whichever later plan owns the docs-update pass; this plan's scope was the Rust/FFI/SQL surface and its regression tests, not prose documentation.
- `test/sql/ts_model_distinctness.test`'s stale `SeasonalES`/`SeasonalESOptimized` expected values (Plan 10-03 regression, not this plan's) remain open in `.planning/WINDOWS.md` — needs a re-bless pass.

---
*Phase: 10-forecast-wrapper-delegates-to-crate-models*
*Completed: 2026-10-09*

## Self-Check: PASSED

All claimed files found on disk (`forecast.rs`, `types.rs`, `lib.rs`, `core_ffi_parity.rs`, `forecast_exog.rs`, `exog_ols_memory.rs`, `anofox_fcst_ffi.h`, `ts_forecast.cpp`, `ts_macros.cpp`, `ts_forecast_crate_delegation.test`, `ts_forecast_exog.test`, `check_dep01_exog.sql`, `deferred-items.md`, this SUMMARY) and all claimed commits (`581d8fe`, `4b68566`, `0b60784`) found in `git log --oneline --all`.
