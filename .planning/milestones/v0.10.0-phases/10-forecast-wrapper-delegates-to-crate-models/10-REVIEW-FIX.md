---
phase: 10-forecast-wrapper-delegates-to-crate-models
fixed_at: 2026-10-09T17:46:24Z
review_path: .planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-REVIEW.md
iteration: 1
findings_in_scope: 4
fixed: 4
skipped: 0
status: all_fixed
---

# Phase 10: Code Review Fix Report

**Fixed at:** 2026-10-09T17:46:24Z
**Source review:** .planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-REVIEW.md
**Iteration:** 1

**Summary:**
- Findings in scope: 4 (CR-01, WR-01, WR-02, WR-03)
- Fixed: 4
- Skipped: 0

## Fixed Issues

### CR-01: `forecast_with_exog` bypassed D-07's `resolve_period` — MSTL/AutoMSTL with regressors silently defaulted to seasonal_period=1 instead of erroring

**Files modified:** `crates/anofox-fcst-core/src/forecast.rs`, `crates/anofox-fcst-core/tests/forecast_exog.rs`
**Commits:** `e84bcd9` (route through `resolve_period()`), `879fd51` (followup — reorder call to run after the `exog_capable` gate so non-exog-capable, period-requiring models like `HoltWinters` keep reporting "does not support exogenous regressors" first, matching `test/sql/ts_forecast_crate_delegation.test`'s pre-existing expectation)
**Applied fix:** `forecast_with_exog`'s inline `.unwrap_or(1)`/`else { 1 }` period-resolution code was replaced with a call to the same `resolve_period()` helper `forecast()` uses, placed right after the `exog_capable` check (not immediately after `prepare_series`, which would have inverted error precedence for non-exog-capable period-requiring models). Added `mstl_with_regressors_and_no_detectable_period_errors`: MSTL + a real (non-constant) regressor + no `seasonal_period` + an undetectable-seasonality series now returns a hard `InvalidInput` naming MSTL, instead of silently fitting `MSTLForecaster::new(vec![1])`. Verified: `cargo test -p anofox-fcst-core` (242+10+17 relevant tests pass), `cargo fmt --check`, `cargo clippy -D warnings`, and the rebuilt extension's `test/sql/ts_forecast_crate_delegation.test` (40/40 assertions, including the pre-existing `HoltWinters` exog-error case whose message this followup commit preserved).

### WR-01: `forecast_crate_exog` never validated `arima_order`/`seasonal_order` against non-ARIMA models on the regressors-present path

**Files modified:** `crates/anofox-fcst-core/src/forecast.rs`, `crates/anofox-fcst-core/tests/forecast_exog.rs`
**Commit:** `12f6a1e`
**Applied fix:** Added the same `model != ModelType::ARIMA && (arima_order.is_some() || seasonal_order.is_some())` guard `forecast()` already applies, placed near the top of `forecast_with_exog` so it runs independent of whether a regressor is non-constant (i.e. independent of whether execution later reaches `forecast_crate_exog` directly or delegates to `forecast()`). Added `arima_order_on_non_arima_model_errors_on_exog_path`: ETS + `arima_order` + a real regressor now errors naming both `arima_order` and `ETS`, instead of silently ignoring the parameter. Verified via `cargo test -p anofox-fcst-core --test forecast_exog` (10/10 pass) plus the full workspace suite, fmt, and clippy.

### WR-02: `ts_forecast_agg` STRUCT return type omitted `residuals`/`aic`/`bic`/`mse` despite the function description promising them

**File modified:** `src/aggregate_functions/ts_forecast_agg.cpp`
**Commit:** `9b27901`
**Applied fix:** Extended `GetForecastAggResultType()` and `TsForecastAggFinalize` to actually populate `residuals` (NaN-safe list, keyed by `n_residuals` not `n_fitted` — same n_residuals/n_fitted distinction this phase's FFI fix introduced elsewhere), `aic`, `bic`, `mse` from the crate `ForecastResult` the Finalize call already has in scope (mirroring the pattern already used by `ts_forecast.cpp`'s scalar/table surfaces); added `opts.include_residuals = true` to the FFI call; null/empty-list-safed the four new fields on the pre-existing error branch. Chose "add the fields" over "reword the description" per the task's stated preference, since the data was already available in the FFI result with zero new FFI surface needed — trivial and safe. Verified end-to-end against the rebuilt extension: `ts_forecast_agg(..., 'AutoETS', ...)` returns `len(residuals) = 24` and a real `mse`; the ARIMA-without-`arima_order` error path returns an empty `residuals` list and `NULL` `aic`/`bic`/`mse` (not uninitialized memory).

### WR-03: ARIMA order strings silently truncated past 31 bytes instead of erroring

**Files modified:** `src/table_functions/ts_forecast_native.cpp`, `src/table_functions/ts_cv_forecast_native.cpp`, `src/aggregate_functions/ts_forecast_agg.cpp`, `src/scalar_functions/ts_forecast_scalar.cpp`, `src/table_functions/ts_forecast.cpp`, `test/sql/ts_forecast_crate_delegation.test`
**Commit:** `f406e6b`
**Applied fix:** Added an explicit `InvalidInputException` length check (`> sizeof(...::arima_order_str) - 1` / `seasonal_order_str`) at every site that copies `arima_order`/`seasonal_order` into the fixed 32-byte FFI buffers: bind time in `ts_forecast_native.cpp` and `ts_cv_forecast_native.cpp`; per-row aggregate state init in `ts_forecast_agg.cpp`; per-row params-MAP parsing in `ts_forecast_scalar.cpp` and `ts_forecast.cpp`'s exog path (both accept these params per-row, not just at bind time). Added two SQL regression tests in `test/sql/ts_forecast_crate_delegation.test` (33-character `arima_order` and `seasonal_order` values) proving the explicit "too long" error fires instead of silent `strncpy`/`memcpy` truncation. Verified against the rebuilt extension: both the `ts_forecast_by` table-function path (SQL test, 40/40 assertions) and the `ts_forecast_agg` aggregate path (manual query) now raise `Invalid Input Error: arima_order value is too long (max 31 characters): '...'` instead of truncating.

## Verification Summary

All verification ran in the main working tree on branch `fix/statistical-correctness` (no worktree — explicitly disabled per the fix workflow's instructions and this project's standing note that worktree isolation leaks into the main tree here).

- `cargo fmt --all -- --check`: clean
- `cargo clippy --all-targets --all-features --workspace -- -D warnings`: clean
- `cargo test --workspace`: all tests pass (anofox-fcst-core: 242 unit + 11+10+17+12+1+6 integration + 12 doc tests; anofox-fcst-ffi: 30 unit + 47 integration + 3 doc tests, all ignored/no-fail)
- Extension rebuilt twice (`RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release`, incremental against the existing `build/release` tree — no new build tree created, per the disk-space constraint) to pick up the CR-01 followup reorder and the final WR-03 state
- `./build/release/test/unittest "test/sql/ts_forecast_crate_delegation.test"`: 40/40 assertions pass (includes the two new WR-03 tests and the pre-existing HoltWinters-exog-error case CR-01's followup commit preserved)
- Spot-checked `test/sql/ts_backtest_equivalence.test` (44/44), `test/sql/ts_model_distinctness.test` (34/34), `test/sql/ts_native_model_names.test` (63/63) — all pass, no regressions from the forecast.rs/*.cpp changes
- `test/sql/ts_forecast_exog.test` was skipped by the harness itself (`require json`, pre-existing environment gap unrelated to this fix — the same extension build serves `ts_forecast_crate_delegation.test` directly)
- FFI header `src/include/anofox_fcst_ffi.h` did not regenerate (no FFI signature changes in this fix set) — confirmed via `git status`, nothing to commit there
- Manually verified WR-02's `residuals`/`aic`/`bic`/`mse` end-to-end against the built extension (success path and the pre-existing error path)
- Manually verified WR-03's new bind-time/per-row errors end-to-end on both the `ts_forecast_by` and `ts_forecast_agg` surfaces

One deviation from a literal reading of the review's suggested CR-01 patch is documented above (commit `879fd51`): placing `resolve_period()` immediately after `prepare_series`, as the review's inline suggestion showed, broke an existing SQL test's error-message expectation for non-exog-capable, period-requiring models (e.g. `HoltWinters` + regressors). The fix was adapted to resolve the period only after the `exog_capable` gate, which satisfies D-07 for MSTL/AutoMSTL (the only model that is both `requires_seasonal_period` and `exog_capable`) while preserving the more relevant "does not support exogenous regressors" error for every other period-requiring model.

---

_Fixed: 2026-10-09T17:46:24Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
