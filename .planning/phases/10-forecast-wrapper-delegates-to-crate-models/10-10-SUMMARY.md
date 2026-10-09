---
phase: 10-forecast-wrapper-delegates-to-crate-models
plan: 10
subsystem: database
tags: [duckdb-extension, forecasting, rust-ffi, cpp, sql-aggregate, params-validation]

# Dependency graph
requires:
  - phase: 10-forecast-wrapper-delegates-to-crate-models
    provides: "ts_forecast_native.cpp's ValidateParamKeys closed key set/message (plans 10-01..10-09), ts_forecast_scalar.cpp's per-row MAP parse helpers, ts_forecast_agg.cpp's WR-02 residuals/aic/bic/mse fields"
provides:
  - "src/include/ts_forecast_params.hpp — single shared params-MAP parser/validator (ValidateForecastParamKeys, ParseForecastParamSet) and ForecastOptions writer (ApplyForecastParamSet), used by both ts_forecast_by's scalar path and ts_forecast_agg"
  - "ts_forecast_agg now validates the full closed params key set and threads every ts_forecast_by param (kalman_model, garch_p/garch_q, model_pool, window, seasonal_periods, laplace_*, top_k, combination_method, confidence_level) instead of silently dropping everything but model/arima_order/seasonal_order/seasonal_period"
  - "ts_forecast_agg raises the same InvalidInputException as ts_forecast_by for invalid model/parameter values (INVALID_MODEL/INVALID_INPUT), instead of only ever populating error_message"
  - "ts_forecast_agg's confidence_level is folded at bind time from a constant params argument, correctly naming lower_<pct>/upper_<pct> fields, with a per-group guard that raises 'must be a constant expression' on disagreement"
  - "crates/anofox-fcst-core/tests/theta_croston_fitted.rs — pins the Theta/CrostonClassic fitted-array identity as genuine anofox-forecast 0.15.10 behaviour (not a wrapper bug), routed to Phase 11 UPST-04"
  - "docs/api/07-forecasting.md, CHANGELOG.md, two skills, four reference pages corrected so every ts_forecast_agg claim matches the built extension"
affects: [phase-11-upstream-crate-correctness, phase-12-agg-null-handling-parity]

actuals:
  tokens: 22941
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Shared header pattern for C++ DuckDB extension functions: ts_forecast_params.hpp centralizes params-MAP parsing/validation/ForecastOptions-writing so two call sites (scalar function + aggregate function) cannot drift — same pattern ts_forecast_native.cpp/ts_cv_forecast_native.cpp/ts_forecast.cpp keep their own copies of, intentionally out of this gap's scope."
    - "Bind-time constant fold for aggregate functions whose return type depends on a runtime argument: ExpressionExecutor::EvaluateScalar on a foldable argument at bind time, with a per-group Update-time guard that raises an SQL error if a non-constant argument's resolved value disagrees with the bound one."

key-files:
  created:
    - src/include/ts_forecast_params.hpp
    - crates/anofox-fcst-core/tests/theta_croston_fitted.rs
  modified:
    - src/scalar_functions/ts_forecast_scalar.cpp
    - src/aggregate_functions/ts_forecast_agg.cpp
    - test/sql/ts_forecast_crate_delegation.test
    - docs/api/07-forecasting.md
    - CHANGELOG.md
    - plugins/anofox-forecast/skills/anofox-forecast-models/SKILL.md
    - plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md
    - docs/reference/models/multi-seasonal/mstl.md
    - docs/reference/models/state-space/auto_ets.md
    - docs/reference/models/theta/auto_theta.md
    - docs/reference/models/state-space/auto_arima.md
    - .planning/phases/10-forecast-wrapper-delegates-to-crate-models/deferred-items.md

key-decisions:
  - "Theta/CrostonClassic fitted-array identity confirmed as genuine crate 0.15.10 behaviour (fixed alpha=0.1 SES recursion on both models, independently verified against model.fitted_values() with no shared wrapper helper) — kept as a pinning test, routed to Phase 11 UPST-04, no forecast.rs change made."
  - "Kept the pre-existing lenient numeric-params-parsing behavior (unparsable garch_p/confidence_level/etc. silently default) unchanged on both ts_forecast_by and ts_forecast_agg, to preserve exact parity between the two surfaces as the plan's primary contract — logged as deferred-items.md entry 3 rather than fixed."
  - "ts_forecast_agg's NULL-row-dropping input-handling divergence from ts_forecast_by (interior NULLs spliced out, not interpolated; trailing NULLs move the forecast origin) is explicitly out of this plan's scope (Phase 12, RUST-08) — documented as a known, now-accurate limitation rather than silently left unexplained."

requirements-completed: [FCST-06, FCST-03, FCST-01]

coverage:
  - id: D1
    description: "ts_forecast_agg validates params against the same closed 15-key set as ts_forecast_by and rejects unknown keys (including panel-only croston_variant) with an identical InvalidInputException"
    requirement: "FCST-06"
    verification:
      - kind: integration
        ref: "test/sql/ts_forecast_crate_delegation.test (unknown-key, alias, croston_variant statement-error assertions)"
        status: pass
    human_judgment: false
  - id: D2
    description: "ts_forecast_agg threads kalman_model/garch_p/garch_q/model_pool/window/laplace_*/top_k/combination_method/confidence_level through to the crate exactly like ts_forecast_by, and raises the same SQL error for invalid model/parameter values"
    requirement: "FCST-06"
    verification:
      - kind: integration
        ref: "test/sql/ts_forecast_crate_delegation.test (kalman_model/garch_p threading + error-raising assertions; three parity queries against ts_forecast_by for AutoETS/Kalman/HoltWinters)"
        status: pass
    human_judgment: false
  - id: D3
    description: "ts_forecast_agg honours confidence_level with correctly-named lower_<pct>/upper_<pct> fields; a non-constant confidence_level is an SQL error"
    requirement: "FCST-06"
    verification:
      - kind: integration
        ref: "test/sql/ts_forecast_crate_delegation.test (width comparison, half-width-vs-R-fixture, non-constant-confidence_level statement-error assertions)"
        status: pass
    human_judgment: false
  - id: D4
    description: "Theta/CrostonClassic identical in-sample fitted arrays confirmed as crate behaviour (not a wrapper bug), pinned by a Rust test, routed to Phase 11"
    requirement: "FCST-03"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/theta_croston_fitted.rs#theta_and_croston_classic_fitted_are_both_ses_alpha_0_1_on_positive_series, #wrapper_fitted_is_the_crate_models_own_fitted_values, #croston_fitted_diverges_from_theta_on_intermittent_series"
        status: pass
    human_judgment: false
  - id: D5
    description: "Every ts_forecast_agg claim in docs/api/07-forecasting.md, CHANGELOG.md, the two skills, and the four reference pages is true after the fix, and every changed SQL block executes against the built extension"
    requirement: "FCST-01"
    verification:
      - kind: other
        ref: ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/snippets/pass-list-10-10.tsv (snippet harness, 0 FAIL)"
        status: pass
    human_judgment: false

duration: ~50min
completed: 2026-10-09
status: complete
---

# Phase 10 Plan 10: ts_forecast_agg Parameter Parity Summary

**ts_forecast_agg now validates and threads the full params-MAP set through the same shared parser/writer as ts_forecast_by, closing the one BLOCKER from 10-VERIFICATION.md (FCST-06) — plus a pinning test confirming the Theta/CrostonClassic fitted-array coincidence is genuine crate behaviour, and doc/CHANGELOG/skills corrections to match.**

## Performance

- **Duration:** ~50 min
- **Completed:** 2026-10-09T18:51:54Z
- **Tasks:** 3 completed
- **Files modified:** 15 (2 created, 13 modified)

## Accomplishments

- Closed the FCST-06 BLOCKER: `ts_forecast_agg` previously read only `model`/`arima_order`/`seasonal_order`/`seasonal_period` from its params MAP and hardcoded `confidence_level` to 0.90; it now validates the full 15-key closed set, threads every `ts_forecast_by` param, raises the same SQL errors, and honours `confidence_level` with correctly-named bound fields — proven identical to `ts_forecast_by` by three exact-parity SQL assertions (AutoETS, Kalman, HoltWinters).
- Extracted a shared `src/include/ts_forecast_params.hpp` header so `ts_forecast_by`'s scalar path and `ts_forecast_agg` use the exact same parser/validator/writer — parity is now structural, not just tested.
- Added 15 new natively-run SQL assertions to `test/sql/ts_forecast_crate_delegation.test` (55 total, up from 40), all passing with no `require json` gate.
- Confirmed via a new Rust test file that Theta and CrostonClassic's identical in-sample fitted arrays on non-seasonal, no-zero series is genuine `anofox-forecast` 0.15.10 behaviour (both models fix alpha at 0.1; Theta's in-sample path has no drift term) — not a wrapper dispatch bug — and routed the finding to Phase 11 (UPST-04).
- Corrected every stale "`ts_forecast_agg` does not auto-detect" claim across `docs/api/07-forecasting.md`, `CHANGELOG.md`, two skills, and four reference pages; found and fixed one additional pre-existing doc regression along the way (an MSTL exog example whose synthetic seasonality became too weak to auto-detect once an earlier Phase 10 fix — CR-01 — started genuinely running period detection on the exog path).

## Task Commits

Each task was committed atomically:

1. **Task 1: Tracer — shared params header; ts_forecast_agg validates, threads and errors exactly like ts_forecast_by** - `b50e654` (fix)
2. **Task 2: Theta vs CrostonClassic identical fitted arrays — decide wrapper bug vs crate behaviour, pin it, record for Phase 11** - `b1d8735` (test)
3. **Task 3: Make every ts_forecast_agg claim in docs, CHANGELOG and skills true; execute changed snippets; full DoD** - `d4e60b3` (docs)

**Plan metadata:** (this commit) `docs: complete 10-10 plan`

## Files Created/Modified

- `src/include/ts_forecast_params.hpp` - New shared header: `ForecastParamSet` struct, `ForecastParamString/Int64/Double` parse helpers, `ValidateForecastParamKeys`, `ParseForecastParamSet`, `ApplyForecastParamSet`
- `src/scalar_functions/ts_forecast_scalar.cpp` - Refactored to use the shared header instead of private copies of the same parse/validate/write logic (behaviour-preserving)
- `src/aggregate_functions/ts_forecast_agg.cpp` - `TsForecastAggStateData` now carries one `ForecastParamSet params` instead of five bespoke fields; `TsForecastAggBind` folds `confidence_level` at bind time via `ExpressionExecutor::EvaluateScalar`; `TsForecastAggUpdate` validates+parses the full params set per group and guards constant `confidence_level`; `TsForecastAggFinalize` uses `ApplyForecastParamSet` and raises `InvalidInputException` for `INVALID_MODEL`/`INVALID_INPUT`; deleted the dead templated `Finalize` and the now-unused `GetParamFromMap` helper
- `test/sql/ts_forecast_crate_delegation.test` - 15 new SQL assertions covering key validation, param threading, confidence_level honoring, and ts_forecast_by parity
- `crates/anofox-fcst-core/tests/theta_croston_fitted.rs` - New Rust test file pinning the Theta/CrostonClassic fitted-array identity
- `.planning/phases/10-forecast-wrapper-delegates-to-crate-models/deferred-items.md` - Four new entries (Theta/Croston → Phase 11; lenient numeric parsing; stale `00-api-design.md` field names; json-gated agg test debt)
- `docs/api/07-forecasting.md` - Corrected seasonality note; new "`ts_forecast_agg` specifics" subsection; Theta/CrostonClassic fitted bullet
- `CHANGELOG.md` - New Breaking-changes bullet for `ts_forecast_agg`'s fix; qualified the NaN-equals-NULL bullet; Added entry for the full params set
- `plugins/anofox-forecast/skills/anofox-forecast-models/SKILL.md` - Corrected gotcha-1 and the `ts_forecast_agg` signature/section
- `plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md` - Corrected two stale auto-detection claims
- `docs/reference/models/multi-seasonal/mstl.md` - Corrected stale claim; fixed the exog example's weak-seasonality regression
- `docs/reference/models/state-space/auto_ets.md`, `docs/reference/models/theta/auto_theta.md`, `docs/reference/models/state-space/auto_arima.md` - Corrected the same stale claim
- `.planning/phases/10-forecast-wrapper-delegates-to-crate-models/snippets/pass-list-10-10.tsv` - Snippet harness run record (0 FAIL)

## Decisions Made

- Theta/CrostonClassic fitted-array identity is genuine crate behaviour (both models fix alpha=0.1; verified `model.fitted_values()` is read directly with no shared/aliased helper) — kept as a pin, no `forecast.rs` change, routed to Phase 11 UPST-04.
- Kept the pre-existing lenient numeric-params-parsing leniency unchanged on both surfaces to preserve exact `ts_forecast_by`/`ts_forecast_agg` parity (this plan's primary contract); logged as a separate deferred item rather than fixed here.
- `ts_forecast_agg`'s NULL-row-dropping divergence from `ts_forecast_by` is explicitly scoped to Phase 12 (RUST-08) and documented as a known, accurate limitation rather than silently left unexplained.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed a stale MSTL exog doc example broken by an earlier Phase 10 fix (CR-01)**
- **Found during:** Task 3, running the snippet harness over `docs/reference/models/multi-seasonal/mstl.md`
- **Issue:** The exog-path doc example (`sales_history`/`future_weather`, `MSTL` with a `temperature` regressor) relied on auto-detecting a seasonal period from a synthetic signal too weak to reliably detect. It had been passing only because, before this same phase's CR-01 fix (commit `e84bcd9`/`879fd51`, Plan 10-04/REVIEW-FIX), `forecast_with_exog` silently defaulted to `seasonal_period=1` instead of actually running detection. Once CR-01 made detection genuinely run on the exog path, this fixture started raising "no seasonal period could be detected for model 'MSTL'" — a pre-existing-but-latent regression this plan's snippet harness run was the first to surface.
- **Fix:** Passed `seasonal_period: '5'` explicitly in the example's `MAP{}` (matching the doc's own recommended best practice, and the regressor's actual period-5 synthetic cycle).
- **Files modified:** `docs/reference/models/multi-seasonal/mstl.md`
- **Verification:** Ran the exact SQL against the built extension (`model_name = 'MSTL with regressors'`, `yhat_lower`/`yhat_upper = NULL`); re-ran the snippet harness (0 FAIL, up from 2 FAIL).
- **Committed in:** `d4e60b3` (Task 3 commit)

---

**Total deviations:** 1 auto-fixed (1 bug fix)
**Impact on plan:** The fix was a one-line doc-example parameter addition with zero production-code changes; necessary for Task 3's own "every changed SQL block executes" requirement and CLAUDE.md's "verify SQL doc examples end-to-end" rule. No scope creep.

## Issues Encountered

None beyond the deviation above.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- FCST-06 is now fully satisfied across every in-scope forecast surface (`ts_forecast_by`, native, exog, scalar, and now `ts_forecast_agg`) — Phase 10's core promise ("no silent model/parameter fallback on any in-scope surface") holds.
- Phase 11 has two concrete, evidence-backed action items waiting: UPST-04 (Theta STM's fixed alpha=0.1, this plan's pinning test will need updating once that lands) and the R `thetaf`/statsforecast in-sample-fitted-convention confirmation this plan's doc bullet calls out.
- Phase 12 (RUST-08) has a concrete, now-documented target: aligning `ts_forecast_agg`'s NULL-row-dropping input handling with `ts_forecast_by`'s interpolate-and-anchor behaviour.
- `test/sql/ts_forecast_agg.test` and the other `require json`-gated agg test files remain test debt (deferred-items.md entry 5) — unaffected by this plan, never executed in this environment.

---
*Phase: 10-forecast-wrapper-delegates-to-crate-models*
*Completed: 2026-10-09*

## Self-Check: PASSED

All files created/modified (15) confirmed present on disk; all 3 task commit hashes (`b50e654`, `b1d8735`, `d4e60b3`) confirmed present in git history.
