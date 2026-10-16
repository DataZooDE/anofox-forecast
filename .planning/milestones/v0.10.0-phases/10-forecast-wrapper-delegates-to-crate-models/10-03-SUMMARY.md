---
phase: 10-forecast-wrapper-delegates-to-crate-models
plan: 03
subsystem: forecasting
tags: [rust, cpp, duckdb, anofox-forecast, ensemble, ets, kalman, panel, ffi]

requires:
  - phase: 10-01
    provides: forecast()/forecast_with_exog() crate-passthrough foundation (D-01/D-02/D-03/D-11/D-12)
  - phase: 10-02
    provides: real crate ARIMA/SARIMA fit, strict FFI option-string parsing pattern (c_field_str, parse_order_triplet), n_residuals fix, natively-run FCST-01..04 SQL test harness
provides:
  - "'ETS' without a spec runs the crate AutoETS selection via a shared new_auto_ets constructor (identical construction to standalone 'AutoETS'); the 'ETS(ETS(...))' double-wrap bug is fixed"
  - "resolve_period + requires_seasonal_period: HoltWinters/SeasonalES/SeasonalESOptimized/SeasonalNaive/SeasonalWindowAverage/MSTL/AutoMSTL/TBATS/AutoTBATS auto-detect a period regardless of auto_detect_seasonality and hard-error (never default to 12) when nothing is detected"
  - "Shared forecaster constructors (new_auto_arima, new_auto_theta, new_theta, new_optimized_theta, new_dynamic_theta, new_dynamic_optimized_theta, new_holt_winters, new_seasonal_es, new_seasonal_es_optimized, new_seasonal_naive, new_sma, new_seasonal_window_average, new_kalman) used by BOTH the standalone forecast_* path and build_forecaster's ensemble-member arms — members are now bit-identical to their standalone model by construction"
  - "build_forecaster(model_type, period, n_obs): n_obs drives SeasonalWindowAverage's n_seasons (was hard-coded to 2); seasonal-required members with no usable period are InvalidParameter, never a silent default of 12"
  - "AutoETS crate fit/predict failure or panic is a hard InvalidInput naming AutoETS (never a silent HoltWinters/Holt/SES substitution)"
  - "AutoMFLES/AutoMSTL report model_name 'MFLES'/'MSTL' exactly (D-06) — 'Auto' remains only a documented, accepted input alias"
  - "KalmanSpec + parse_kalman_spec: model_name is always 'Kalman(local_level)'/'Kalman(local_linear_trend)'; an unknown kalman_model is InvalidParameter"
  - "forecast_inspect/forecast_explain propagate parse_model_pool errors instead of silently swallowing them via `if let Ok`"
  - "forecast_with_exog's no-regressor/unsupported-model branch delegates to forecast() itself via a new ForecastOptionsExog -> ForecastOptions conversion — every option (ets_spec, garch_p/q, kalman_model, window, seasonal_periods, model_pool, laplace/ensemble options, arima orders, confidence level, include flags) is honoured identically to the non-exog surface; the second hand-maintained forecast_with_model dispatch is deleted"
  - "FFI panel path: croston_variant/model_pool are case-insensitive and strict (invalid UTF-8 or an unrecognised value is InvalidInput — never a silent Classic/Reduced default)"
  - "forecast_explicit_ensemble gains a confidence_level parameter and returns the crate Ensemble's own predict_with_intervals bounds via extract_forecast (extract_point_forecast deleted)"
  - "FFI anofox_ts_forecast_ensemble gained a confidence_level parameter and copies real lower/upper bounds instead of hardcoding null"
  - "ts_forecast_ensemble_by SQL macro gained a named confidence_level parameter (default 0.90); the C++ scalar function is a ScalarFunctionSet with both the original 7-argument overload (0.90 default) and a new 8-argument overload"
affects: ["10-04", "10-05", "10-06", "10-07", "10-08", "10-09"]

actuals:
  tokens: 33250
  tasks: 3
  commits: 3
  plan_head_before: 0a16449

tech-stack:
  added: []
  patterns:
    - "Shared forecaster constructors (new_*): every standalone forecast_* function and build_forecaster's matching ensemble-member arm call the SAME crate constructor — proven by an inline cargo test (build_forecaster_matches_standalone_forecast_for_every_member) that fits every constructible ModelType both ways and asserts bit-identical point forecasts"
    - "resolve_period(values, model, seasonal_period, auto_detect, has_period_list): single D-07 period-resolution entry point reused by forecast(), forecast_inspect, forecast_explain, forecast_explicit_ensemble, inspect_explicit_ensemble — auto-detection forced for requires_seasonal_period models regardless of the auto_detect_seasonality flag, and a hard InvalidInput (never a default) when nothing is detected and no explicit list is given"
    - "ForecastOptionsExog -> ForecastOptions via From<&ForecastOptionsExog>, letting forecast_with_exog's no-regressor branch delegate to forecast() itself instead of maintaining a second hand-written dispatch that can silently drift"

key-files:
  created:
    - crates/anofox-fcst-core/tests/forecast_fallbacks.rs
  modified:
    - crates/anofox-fcst-core/src/forecast.rs
    - crates/anofox-fcst-ffi/src/lib.rs
    - crates/anofox-fcst-ffi/tests/core_ffi_parity.rs
    - src/include/anofox_fcst_ffi.h
    - src/table_functions/ts_forecast_ensemble_native.cpp
    - src/macros/ts_macros.cpp
    - test/sql/ts_forecast_crate_delegation.test
    - validation/reference/fixtures/forecast_snapshot/point_forecasts.json

key-decisions:
  - "'ETS' without a spec and standalone 'AutoETS' share one forecast_auto_ets_core(..., auto_label: bool) implementation — auto_label controls only the model_name prefix ('ETS(...)' vs 'AutoETS(...)'), guaranteeing the two call sites can never diverge in construction again."
  - "The pre-10-03 catch_unwind fallback (AutoETS panic/error -> hand-picked HoltWinters/Holt/SES) is deleted outright, not softened: a crate failure is now InvalidInput naming AutoETS. anofox-forecast 0.15.10 no longer panics on the constant-series case the fallback was originally written for (verified empirically), so this is a correctness fix, not a behavior regression on any series exercised by the test suite."
  - "forecast_explicit_ensemble's `period` parameter keeps its historical 'usize, 0/1=non-seasonal' shape rather than becoming an Option/auto_detect-flag pair: period==0 now triggers a one-shot detect_seasonality call (matching ts_forecast_by's own seasonal_period=0 default), and build_forecaster's own per-model-type period guard is the enforcement point for seasonal-required members — avoids threading a new auto_detect flag through two public function signatures for a single-call-site need."
  - "SeasonalWindowAverage's TODO ENS-03 (ensemble members hard-coded n_seasons=2) is resolved by threading n_obs through build_forecaster's signature instead of deferring to a future per-member pre-fit hook — the Ensemble::fit() call already has the series length available at the point build_forecaster is invoked from forecast_explicit_ensemble/inspect_explicit_ensemble."

patterns-established:
  - "An inline cargo test that mechanically enumerates every build_forecaster-constructible ModelType and asserts bit-identical point forecasts against the standalone forecast() path is the regression guard for 'ensemble member == standalone model' going forward — any future standalone-path change that forgets to update build_forecaster's matching arm now fails this test immediately, rather than silently drifting until a user notices disparate ensemble/standalone results."

requirements-completed: [FCST-07]

coverage:
  - id: D1
    description: "'ETS' without a spec runs the crate AutoETS selection (same construction as standalone 'AutoETS') and reports the chosen spec e.g. 'ETS(M,A,M)'; an explicit ETS spec reports 'ETS(M,A,M)' with no double-wrap; 'AutoETS' reports 'AutoETS(M,A,M)'"
    requirement: FCST-06
    verification:
      - kind: integration
        ref: "crates/anofox-fcst-core/tests/forecast_fallbacks.rs#ets_without_spec_runs_autoets,ets_spec_name_has_no_double_wrap"
        status: pass
      - kind: integration
        ref: "crates/anofox-fcst-ffi/tests/core_ffi_parity.rs#parity_ets_default"
        status: pass
      - kind: e2e
        ref: "test/sql/ts_forecast_crate_delegation.test (FCST-07 regexp_matches assertion), run via ./build/release/test/unittest"
        status: pass
    human_judgment: false
  - id: D2
    description: "An ensemble member built by build_forecaster produces a bit-identical point forecast to the standalone model with the same parameters, for every member-eligible ModelType; ['ETS','ETS'] (Mean) equals standalone 'ETS' through ts_forecast_ensemble_by"
    requirement: FCST-07
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/forecast.rs#tests::build_forecaster_matches_standalone_forecast_for_every_member"
        status: pass
      - kind: integration
        ref: "crates/anofox-fcst-core/tests/forecast_fallbacks.rs#ensemble_of_two_ets_equals_standalone_ets"
        status: pass
      - kind: e2e
        ref: "test/sql/ts_forecast_crate_delegation.test (FCST-07 ensemble-vs-standalone count(*)=0 assertion)"
        status: pass
    human_judgment: false
  - id: D3
    description: "AutoETS crate failure or panic is a hard InvalidInput naming AutoETS — never a silent HoltWinters/Holt/SES substitution under the AutoETS name"
    requirement: FCST-06
    verification:
      - kind: integration
        ref: "crates/anofox-fcst-core/tests/forecast_fallbacks.rs#auto_ets_constant_series_never_silently_substitutes"
        status: pass
    human_judgment: false
  - id: D4
    description: "HoltWinters/SeasonalES/SeasonalESOptimized/SeasonalNaive/SeasonalWindowAverage/MSTL/AutoMSTL/TBATS/AutoTBATS auto-detect a seasonal period regardless of auto_detect_seasonality, and hard-error (never default to 12/2) when nothing is detected and no seasonal_periods list is given"
    requirement: FCST-06
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/forecast.rs#tests::test_all_model_names_match_enum (seasonal_period_for helper), build_forecaster_seasonal_naive_without_period_errors"
        status: pass
      - kind: integration
        ref: "crates/anofox-fcst-core/tests/forecast_fallbacks.rs#holt_winters_without_period_errors_naming_model,holt_winters_auto_detected_period_matches_explicit,seasonal_naive_explicit_period_1_errors,forecast_explain_mstl_without_period_errors"
        status: pass
      - kind: e2e
        ref: "test/sql/ts_forecast_crate_delegation.test (HoltWinters-on-24-point-ramp statement error 'seasonal period')"
        status: pass
    human_judgment: false
  - id: D5
    description: "AutoMFLES/AutoMSTL report model_name 'MFLES'/'MSTL' exactly; Kalman reports 'Kalman(local_level)' or 'Kalman(local_linear_trend)' and an unknown kalman_model is InvalidParameter; a misspelled panel croston_variant or model_pool is InvalidInput"
    requirement: FCST-06
    verification:
      - kind: integration
        ref: "crates/anofox-fcst-core/tests/forecast_fallbacks.rs#auto_mstl_reports_mstl_name_and_matches_standalone,auto_mfles_reports_mfles_name_and_matches_standalone,kalman_model_name_reports_spec,kalman_model_misspelled_errors_naming_param,parse_kalman_spec_table"
        status: pass
      - kind: unit
        ref: "crates/anofox-fcst-ffi/src/lib.rs#panel_ffi_tests::croston_variant_case_insensitive_and_strict,model_pool_case_insensitive_and_strict,ffi_croston_variant_misspelled_returns_invalid_input"
        status: pass
      - kind: e2e
        ref: "test/sql/ts_forecast_crate_delegation.test (AutoMSTL/AutoMFLES/Kalman model_name assertions, Kalman local_lvl + panel croston_variant sab statement errors)"
        status: pass
    human_judgment: false
  - id: D6
    description: "ts_forecast_ensemble_by returns the crate Ensemble's prediction interval at the requested confidence_level (default 0.90) instead of NULL"
    requirement: FCST-01
    verification:
      - kind: e2e
        ref: "test/sql/ts_forecast_crate_delegation.test (ensemble yhat_lower/yhat_upper non-NULL + bracketing assertion), run via ./build/release/test/unittest against the built extension"
        status: pass
      - kind: manual_procedural
        ref: "duckdb CLI smoke test: ts_forecast_ensemble_by(...) default call returns non-NULL intervals; confidence_level := 0.5 narrows them relative to the 0.90 default"
        status: pass
    human_judgment: false
  - id: D7
    description: "forecast_with_exog's no-regressor branch honours every ts_forecast_by option (ets_spec, garch_p/q, kalman_model, and the rest) because it now runs forecast() itself"
    requirement: FCST-05
    verification:
      - kind: integration
        ref: "crates/anofox-fcst-core/tests/forecast_fallbacks.rs#forecast_with_exog_honours_garch_order_with_no_regressors,forecast_with_exog_honours_kalman_model_with_no_regressors,forecast_with_exog_honours_ets_spec_with_no_regressors"
        status: pass
    human_judgment: false

duration: ~45min (single session)
completed: 2026-10-09
status: complete
---

# Phase 10 Plan 03: Fallback Elimination, Ensemble-Member Parity, and Ensemble Intervals Summary

**Every remaining silent fallback in `forecast.rs` (AutoETS panic, default seasonal periods, AutoMSTL/AutoMFLES relabeling, unknown Kalman/Croston/model_pool strings) now errors or reports exactly what ran; ensemble members built by `build_forecaster` are bit-identical to their standalone counterpart by construction; and `ts_forecast_ensemble_by` returns the crate `Ensemble`'s own prediction interval instead of a hardcoded NULL.**

## Performance

- **Duration:** ~45 min (single session; 3 task commits ~15:45–16:20)
- **Tasks:** 3 (all completed as planned, no checkpoints)
- **Files modified:** 8 (7 modified, 1 created)

## Accomplishments

- `new_auto_ets(period, pool)` is the single shared constructor behind standalone `'AutoETS'`, `'ETS'` without a spec (D-05), and `build_forecaster`'s `ETS`/`AutoETS` ensemble-member arms — an `['ETS','ETS']` ensemble now equals standalone `'ETS'` bit-for-bit, and the `"ETS(ETS(M,A,M))"` double-wrap bug (`ETSSpec::short_name()` already returns the fully-formed text) is fixed.
- `resolve_period`/`requires_seasonal_period` (D-07): HoltWinters, SeasonalES, SeasonalESOptimized, SeasonalNaive, SeasonalWindowAverage, MSTL, AutoMSTL, TBATS, AutoTBATS now auto-detect a period regardless of `auto_detect_seasonality`, and hard-error naming the model when nothing is detected and no `seasonal_periods` list is given — every `vec![12]`/`else { 12 }`/`.max(2)` default in `forecast.rs` is gone.
- 13 new shared forecaster constructors (`new_auto_arima`, `new_auto_theta`, `new_theta`, `new_optimized_theta`, `new_dynamic_theta`, `new_dynamic_optimized_theta`, `new_holt_winters`, `new_seasonal_es`, `new_seasonal_es_optimized`, `new_seasonal_naive`, `new_sma`, `new_seasonal_window_average`, `new_kalman`) used by both the standalone path and `build_forecaster`; `build_forecaster`'s signature grew an `n_obs` parameter so `SeasonalWindowAverage`'s `n_seasons` is no longer hard-coded to 2. One real bug caught in the process: the SMA ensemble-member arm was building a crate `WindowAverage`, a different type from the standalone path's `SimpleMovingAverage` — fixed by routing both through `new_sma`.
- AutoETS crate fit/predict failure or panic is now `InvalidInput` naming AutoETS (deleted the `legacy_ets_fallback`/hand-picked HoltWinters-Holt-SES substitution entirely); AutoMFLES/AutoMSTL report model_name `"MFLES"`/`"MSTL"` exactly (D-06); `KalmanSpec`/`parse_kalman_spec` give Kalman a strict `"Kalman(local_level)"`/`"Kalman(local_linear_trend)"` name and reject unknown specs; `forecast_inspect`/`forecast_explain` propagate `parse_model_pool` errors instead of swallowing them.
- `forecast_with_exog`'s no-regressor/unsupported-model branch now delegates to `forecast()` itself via a new `From<&ForecastOptionsExog> for ForecastOptions`, so every option is honoured identically to the non-exog surface; the second hand-maintained `forecast_with_model` dispatch (161 lines) is deleted entirely.
- FFI panel path (`anofox_ts_forecast_panel`): `croston_variant`/`model_pool` are case-insensitive and strict — invalid UTF-8 or an unrecognised value is `InvalidInput`, never a silent Classic/Reduced default.
- `forecast_explicit_ensemble` gained a `confidence_level` parameter and now returns the crate `Ensemble`'s own `predict_with_intervals` bounds via `extract_forecast` (`extract_point_forecast` deleted); the FFI, the C++ `_ts_forecast_ensemble_native` scalar function (now a `ScalarFunctionSet` with 7-arg and 8-arg overloads), and the `ts_forecast_ensemble_by` SQL macro (new named `confidence_level` parameter, default 0.90) all carry it through end to end — proven against the built extension.

## Task Commits

Each task was committed atomically:

1. **Task 1: Tracer — 'ETS' without spec runs crate AutoETS via shared new_auto_ets constructor** — `e0e2ce3` (feat)
2. **Task 2: Every silent fallback becomes an error or reported identity; shared constructors for ensemble-member parity** — `e01e87b` (feat)
3. **Task 3: Crate Ensemble intervals through FFI/C++/macro; build + SQL proof for FCST-06/07** — `30d74b6` (feat)

**Plan metadata:** (this commit, immediately following)

## Files Created/Modified

- `crates/anofox-fcst-core/src/forecast.rs` — `new_auto_ets`/`format_auto_ets_name`/`forecast_auto_ets_core`; `resolve_period`/`requires_seasonal_period`; 13 new shared `new_*` constructors; `KalmanSpec`/`parse_kalman_spec`; `build_forecaster(model_type, period, n_obs)`; `From<&ForecastOptionsExog> for ForecastOptions`; `forecast_explicit_ensemble(..., confidence_level)`; deleted `forecast_with_model`, `extract_point_forecast`, `legacy_ets_fallback`.
- `crates/anofox-fcst-core/tests/forecast_fallbacks.rs` (new) — 17 tests covering D-05/D-06/D-07/FCST-07.
- `crates/anofox-fcst-ffi/src/lib.rs` — `anofox_ts_forecast_ensemble(..., confidence_level, ...)` copies real lower/upper via `alloc_or_error`; panel `PanelForecastError::InvalidParameter`, strict case-insensitive `croston_variant`/`model_pool` parsing, invalid-UTF-8 rejection; 4 new `panel_ffi_tests`.
- `crates/anofox-fcst-ffi/tests/core_ffi_parity.rs` — `parity_ets_default` rewritten against the real crate `AutoETS` model with a model_name pattern check.
- `src/include/anofox_fcst_ffi.h` — cbindgen-regenerated, purely additive (`confidence_level` parameter).
- `src/table_functions/ts_forecast_ensemble_native.cpp` — bind data `confidence_level` field (default 0.90); `ScalarFunctionSet` with 7-arg and 8-arg overloads; FFI call passes `confidence_level`; output struct reads real `yhat_lower`/`yhat_upper`.
- `src/macros/ts_macros.cpp` — `ts_forecast_ensemble_by` named parameter `confidence_level` (default `"0.90"`); doc text updated.
- `test/sql/ts_forecast_crate_delegation.test` — 3 new sections (FCST-07, FCST-01 ensemble, FCST-06) totaling 25 assertions, run natively against the built extension.
- `validation/reference/fixtures/forecast_snapshot/point_forecasts.json` — re-blessed; every changed key is a reviewed, expected D-05/D-06/D-07 consequence (see Deviations).

## Decisions Made

See frontmatter `key-decisions`. Most significant: deleting the AutoETS catch_unwind fallback outright rather than softening it, after confirming empirically that `anofox-forecast` 0.15.10 no longer panics on the constant-series case the fallback was written for (issue #192) — so the removal is a correctness fix (no silent model substitution under a different name), not a behavior regression on any series this test suite exercises.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] SMA ensemble members used a different crate type than the standalone path**
- **Found during:** Task 2, writing the shared-constructor refactor
- **Issue:** `build_forecaster`'s `ModelType::SMA` arm constructed crate `WindowAverage`; the standalone `forecast_sma` constructed crate `SimpleMovingAverage` — two different crate types that could legitimately produce different point forecasts for the "same" model name.
- **Fix:** Added `new_sma(window) -> SimpleMovingAverage`; both call sites now use it.
- **Files modified:** `crates/anofox-fcst-core/src/forecast.rs`
- **Verification:** `build_forecaster_matches_standalone_forecast_for_every_member` (new inline test) asserts bit-identical SMA output between the two paths.
- **Committed in:** `e01e87b` (Task 2 commit)

**2. [Rule 1 - Test-design] `test_all_model_names_match_enum`'s synthetic "seasonal" data didn't reliably auto-detect its embedded period**
- **Found during:** Task 2, first `cargo test -p anofox-fcst-core --lib` after wiring `resolve_period` into `forecast()`
- **Issue:** The test's 60-point trend+sine series (period ~7) relies on `detect_seasonality` for models newly requiring a period (D-07); detection failed on this specific trend-dominated series, so `HoltWinters`/`SeasonalNaive`/etc. in the `exact_cases`/`prefix_cases` loops started erroring instead of fitting — a test-data limitation, not a `forecast()` bug (detector accuracy is Phase 15's scope per the plan's own `<interfaces>` note).
- **Fix:** Added a `seasonal_period_for()` helper that passes an explicit `seasonal_period: 7, auto_detect_seasonality: false` for every `requires_seasonal_period` model in this specific test, since the test's purpose is naming-convention coverage, not detector accuracy.
- **Files modified:** `crates/anofox-fcst-core/src/forecast.rs`
- **Verification:** `test_all_model_names_match_enum` passes; `holt_winters_auto_detected_period_matches_explicit` (new, in `forecast_fallbacks.rs`) separately proves detection itself works correctly on a clean period-12 series.
- **Committed in:** `e01e87b` (Task 2 commit)

**3. [Rule 3 - Blocking] Panel `croston_variant` misuse SQL test needed a 3-series panel, not the single-series `air` table**
- **Found during:** Task 3, first native run of the new SQL assertions against the built extension
- **Issue:** `ts_forecast_panel_by` requires >= 3 usable series for cross-series global learning; the plan's example used the existing single-series `air` table, which fails with an unrelated "fewer than 3 usable series" error before ever reaching the `croston_variant` parse.
- **Fix:** Added a small 3-series `panel3` table and pointed the `croston_variant: 'sab'` misuse assertion at it.
- **Files modified:** `test/sql/ts_forecast_crate_delegation.test`
- **Verification:** `./build/release/test/unittest "test/sql/ts_forecast_crate_delegation.test"` — 25/25 assertions pass.
- **Committed in:** `30d74b6` (Task 3 commit)

---

**Total deviations:** 3 auto-fixed (1 Rule 1 production bug, 1 Rule 1 test-design adjustment, 1 Rule 3 blocking test-fixture fix)
**Impact on plan:** The SMA fix is a genuine pre-existing correctness bug this plan's own goal (ensemble-member parity) exists to catch — exactly the kind of divergence FCST-07 targets. The other two are test-fixture adjustments with no production-code impact. No scope creep.

## Issues Encountered

None beyond the deviations above.

## Known Stubs

None — all deliverables are fully wired; no placeholder/mock data paths were introduced.

## Threat Flags

None beyond what the plan's own `<threat_model>` already covers (T-10-08 through T-10-11, all mitigated per the plan's design: every former fallback site now returns `InvalidInput`/`InvalidParameter`; the `anofox_ts_forecast_ensemble` signature change was updated in the same commit as its only C++ caller, with the header regenerated by the same build that compiles both sides; `confidence_level` is validated in `(0,1)` before reaching crate quantile code).

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- Plan 10-04 (exogenous path native crate routing, D-10) can proceed: `forecast_with_exog`'s no-regressor branch now cleanly delegates to `forecast()`, and the regressor branch (`forecast_arima_with_exog`/`forecast_theta_with_exog`/`forecast_mfles_with_exog`) is explicitly untouched and ready for replacement.
- `cargo test --workspace`, `cargo fmt --all -- --check`, and `cargo clippy --all-targets --all-features -- -D warnings` are all green on `HEAD` (`30d74b6`).
- `RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release` succeeds; `./build/release/test/unittest "test/sql/ts_forecast_crate_delegation.test"` passes (25/25 assertions, up from 10-02's 14/14).
- Docs (`docs/api/07-forecasting.md`, `docs/reference/models/**`, the `anofox-forecast-models`/`anofox-forecast-backtest` skills) still describe the pre-10-03 "yhat_lower/yhat_upper are NULL in Phase 5" ensemble-interval behavior and the old AutoMSTL/AutoMFLES/Kalman naming in places — flagging for whichever later plan owns the docs-update pass (10-CONTEXT.md's D-13/docs note), since this plan's scope was the SQL surface and its regression tests, not prose documentation.

---
*Phase: 10-forecast-wrapper-delegates-to-crate-models*
*Completed: 2026-10-09*

## Self-Check: PASSED

All claimed files found on disk (`forecast.rs`, `forecast_fallbacks.rs`, `lib.rs`, `core_ffi_parity.rs`, `anofox_fcst_ffi.h`, `ts_forecast_ensemble_native.cpp`, `ts_macros.cpp`, `ts_forecast_crate_delegation.test`, `point_forecasts.json`, this SUMMARY) and all claimed commits (`e0e2ce3`, `e01e87b`, `30d74b6`) found in `git log --oneline --all`.
