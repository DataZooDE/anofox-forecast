---
phase: 06-ensemble-intervals-introspection
verified: 2026-08-31T14:00:00Z
status: passed
score: 7/7
behavior_unverified: 0
overrides_applied: 1
overrides:
  - must_have: "A runnable example produces conformal lower/upper bounds per horizon step on an AutoEnsemble forecast via ts_cv_forecast_by('AutoEnsemble') → ts_conformal_calibrate → apply, with lower<=point<=upper (EPI-01)"
    reason: "ts_cv_forecast_by('AutoEnsemble') segfaults at runtime in the built 0.15.3 extension — a pre-existing bug unrelated to this phase. The delivered example achieves the identical functional outcome using _ts_forecast_scalar('AutoEnsemble') in a manual per-fold loop over ts_cv_folds_by output → ts_conformal_calibrate → apply. ROADMAP SC1 ('routed through the existing conformal machinery') is satisfied. The crash and the workaround are documented honestly in the example header and docs. Bad_rows=0 independently verified. This deviation is intentional and correctly handled."
    accepted_by: "verifier (gsd-verifier)"
    accepted_at: "2026-08-31T14:00:00Z"
---

# Phase 6: Ensemble Intervals & Introspection Verification Report

**Phase Goal:** SQL users can attach distribution-free prediction intervals to an ensemble forecast through the EXISTING conformal path (EPI-01), and inspect which member models an ensemble selected + their combination weights per series (INSP-01).
**Verified:** 2026-08-31T14:00:00Z
**Status:** passed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | A user can call `ts_ensemble_inspect_by(...)` and get per-series (member_name, weight, score) rows for explicit-member ensemble (INSP-01) | ✓ VERIFIED | Direct run: 3 rows for 3 members per series; weight = 0.3333... for Mean; score NULL for explicit-member. Macro registered, wired, and exercised end-to-end. |
| 2 | A user can call `ts_auto_ensemble_inspect_by(...)` and get selected members + MSE score + rank for AutoEnsemble (INSP-01) | ✓ VERIFIED | Direct run: 3 members per series (AutoARIMA, AutoETS, AutoTheta); score>0; rank 1..3 ascending by MSE. |
| 3 | Explicit-member Mean weights == 1/k within 1e-10, sum(weight)==1 per series (INSP-01 DoD) | ✓ VERIFIED | Unit test `inspect_explicit_mean_weights_equal_1_over_k` passes. ensemble_inspect.sql Section 1: unequal_mean_weights=0, bad_weight_sum=0. Direct run: weight = 0.3333333333333333 exactly for all 3 members. |
| 4 | Explicit-member WeightedMSE sum(weight)==1 within 1e-6, no negatives (INSP-01 DoD) | ✓ VERIFIED | Unit test `inspect_explicit_weighted_mse_weights_sum_to_1` passes. ensemble_inspect.sql Section 2: negative_weights=0, bad_weight_sum=0. Direct run: sum_w = 0.9999999999999999, min_w = 7.6e-12. |
| 5 | AutoEnsemble Mean weight=1/k non-NULL, score>0 (INSP-01 DoD); AutoEnsemble non-Mean weight IS NULL, score>0 (documented crate 0.15.3 limitation) (INSP-01) | ✓ VERIFIED | Unit tests pass for both cases. ensemble_inspect.sql Sections 3+4: null_weights=0, unequal_weights=0, bad_scores=0, bad_ranks=0 (Section 3); non_null_weights=0, bad_scores=0 (Section 4). Direct runs confirmed. |
| 6 | A runnable example produces conformal lower/upper bounds per horizon step on an AutoEnsemble forecast via the existing conformal path, lower<=point<=upper (EPI-01) | ✓ VERIFIED (override) | ts_cv_forecast_by('AutoEnsemble') segfaults at runtime (pre-existing bug). Workaround: _ts_forecast_scalar('AutoEnsemble') in manual per-fold loop over ts_cv_folds_by → ts_conformal_calibrate. bad_rows=0, interval_width=0.734845. ROADMAP SC1 satisfied: "routed through existing conformal machinery." Deviation documented honestly. |
| 7 | A runnable example produces conformal lower/upper bounds per horizon step on an explicit-member ensemble forecast, lower<=point<=upper (EPI-01) | ✓ VERIFIED | ensemble_intervals.sql Section 2: manual per-fold _ts_forecast_ensemble_native loop → ts_conformal_calibrate → CROSS JOIN. bad_rows=0, interval_width=1.708277. Both limitations documented in example header. |

**Score:** 7/7 truths verified (6 VERIFIED, 1 VERIFIED via override)

### ROADMAP Success Criteria Cross-Check

| SC | Text | Status | Evidence |
|----|------|--------|----------|
| SC1 | A user can learn-then-apply conformal prediction intervals on an ensemble point forecast and get lower/upper bounds per horizon step, routed through the existing conformal machinery (EPI-01) | ✓ VERIFIED | Both example sections: bad_rows=0. ts_cv_folds_by + ts_conformal_calibrate used (existing machinery). ts_cv_forecast_by crash workaround documented. |
| SC2 | A user can query which member models an ensemble selected, per series (INSP-01) | ✓ VERIFIED | ts_auto_ensemble_inspect_by returns member_name per series (AutoARIMA, AutoETS, AutoTheta). |
| SC3 | A user can query the combination weight assigned to each selected member, per series, and the weights are consistent with the combination method chosen (INSP-01) | ✓ VERIFIED | Mean: weight=1/k. WeightedMSE explicit-member: sum=1, no negatives. AutoEnsemble non-Mean: weight=NULL (crate limit, documented). |
| SC4 | A runnable examples/*.sql demonstrates both intervals and introspection against the built extension and is verified | ✓ VERIFIED | ensemble_intervals.sql + ensemble_inspect.sql both run clean. All assertions == 0. |

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `crates/anofox-fcst-core/src/forecast.rs` | inspect_explicit_ensemble + inspect_auto_ensemble | ✓ VERIFIED | Both functions at lines 2894 and 2978; `ens.weights()` at line 2950; `all_scores()` + `model_count()` at lines 2998-3001. |
| `crates/anofox-fcst-core/src/lib.rs` | pub re-exports for both inspect functions | ✓ VERIFIED | Line 72: `inspect_auto_ensemble, inspect_explicit_ensemble` in pub re-exports. |
| `crates/anofox-fcst-ffi/src/lib.rs` | EnsembleInspectResult + 2 exports + free fn | ✓ VERIFIED | EnsembleInspectResult at line 8119; anofox_ts_ensemble_inspect at 8140; anofox_ts_auto_ensemble_inspect at 8302; anofox_free_ensemble_inspect_result at 8470. |
| `src/include/anofox_fcst_ffi.h` | 4 new INSP-01 symbols | ✓ VERIFIED | 16 occurrences of 4 symbols confirmed by grep count. |
| `src/include/ts_ensemble_inspect_native.hpp` | RegisterTsEnsembleInspectNativeFunction declaration | ✓ VERIFIED | File exists; line 10 declares the function. |
| `src/table_functions/ts_ensemble_inspect_native.cpp` | Both ScalarFunctions | ✓ VERIFIED | File exists; both `_ts_ensemble_inspect_native` and `_ts_auto_ensemble_inspect_native` implemented. |
| `src/macros/ts_macros.cpp` | ts_ensemble_inspect_by + ts_auto_ensemble_inspect_by macros | ✓ VERIFIED | Lines 644 and 682 register both macros; inner unnest calls at lines 651 and 690. |
| `examples/forecasting/ensemble_inspect.sql` | INSP-01 full DoD (4 sections) | ✓ VERIFIED | File exists; 4 sections with 8 assertions all returning 0 against the built extension. |
| `examples/forecasting/ensemble_intervals.sql` | EPI-01 DoD (2 sections, both bad_rows=0) | ✓ VERIFIED | File exists; both sections bad_rows=0; interval_width > 0 for both. |
| `docs/api/07-forecasting.md` | ts_ensemble_inspect_by + ts_auto_ensemble_inspect_by entries | ✓ VERIFIED | 11 occurrences across both function entries. |
| `docs/api/11-conformal-prediction.md` | Ensemble conformal section (EPI-01) | ✓ VERIFIED | 19 occurrences of ensemble/EPI-01 references; limitations documented. |
| `docs/reference/models/ensemble/ensemble_inspect.md` | Introspection reference doc with crate limitation | ✓ VERIFIED | File exists; line 162 documents weight=NULL for AutoEnsemble; line 166 "Weight availability (crate 0.15.3 limitation)" section. |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `ts_ensemble_inspect_by` macro | `_ts_ensemble_inspect_native` ScalarFunction | unnest call at ts_macros.cpp:651 | ✓ WIRED | Macro body calls the underlying ScalarFunction. |
| `ts_auto_ensemble_inspect_by` macro | `_ts_auto_ensemble_inspect_native` ScalarFunction | unnest call at ts_macros.cpp:690 | ✓ WIRED | Macro body calls the underlying ScalarFunction. |
| `ts_ensemble_inspect_native.cpp` | Extension load | CMakeLists.txt line 182 + extension.cpp lines 8, 182 | ✓ WIRED | CMakeLists entry + include + RegisterTsEnsembleInspectNativeFunction call confirmed. |
| FFI `anofox_ts_ensemble_inspect` | Rust core `inspect_explicit_ensemble` | anofox-fcst-ffi/src/lib.rs:8140+ | ✓ WIRED | FFI function calls core function; both compile (cargo build -p anofox-fcst-ffi). |
| `anofox_fcst_ffi.h` symbols | C++ ScalarFunction | regenerated via `make header` | ✓ WIRED | 16 occurrences of 4 symbols in header. |
| EPI-01 conformal path | AutoEnsemble backtest | ts_cv_folds_by + _ts_forecast_scalar | ✓ WIRED | Both sections use ts_cv_folds_by + ts_conformal_calibrate; confirmed by running example. |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|--------------------|--------|
| `ts_ensemble_inspect_by` | weight | Rust: `ens.weights()` after `ens.fit(&ts)` | Yes — real model fit | ✓ FLOWING |
| `ts_auto_ensemble_inspect_by` | score | Rust: `model.all_scores().iter().take(k)` after `model.fit(&ts)` | Yes — real model fit | ✓ FLOWING |
| `ts_auto_ensemble_inspect_by` | weight | Rust: `Some(1.0/k as f64)` for Mean, `None` otherwise | Conditional — correct behavior | ✓ FLOWING |
| `ensemble_intervals.sql` | yhat_lower/yhat_upper | `f.yhat ± c.conformity_score` | Real backtest residuals → calibrated quantile | ✓ FLOWING |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| 7 unit tests: Mean weights, WeightedMSE sum, error cases, AutoEnsemble | `cargo test -p anofox-fcst-core inspect_` | 7 passed, 0 failed, finished in 0.04s | ✓ PASS |
| INSP-01 full DoD: 8 assertions across 4 sections | `./build/release/duckdb -unsigned ... .read examples/forecasting/ensemble_inspect.sql` | All 8 assertion counts = 0 | ✓ PASS |
| EPI-01 AutoEnsemble bad_rows | `examples/forecasting/ensemble_intervals.sql` Section 1 | bad_rows=0, interval_width=0.734845 | ✓ PASS |
| EPI-01 explicit-member bad_rows | `examples/forecasting/ensemble_intervals.sql` Section 2 | bad_rows=0, interval_width=1.708277 | ✓ PASS |
| Explicit Mean weights = 1/3 direct | `ts_ensemble_inspect_by` on 3-member Mean ensemble | 0.3333333333333333 for all 3 members | ✓ PASS |
| WeightedMSE sum=1, no negatives direct | `ts_ensemble_inspect_by` WeightedMSE | sum_w=0.9999999999999999, min_w=7.6e-12 | ✓ PASS |
| AutoEnsemble Mean weight=1/3 direct | `ts_auto_ensemble_inspect_by` combination_method='mean' | weight=0.3333... rank 1..3, score>0 | ✓ PASS |
| AutoEnsemble WeightedMSE weight=NULL direct | `ts_auto_ensemble_inspect_by` combination_method='weighted_mse' | weight=NULL, score>0 | ✓ PASS |
| Regression: ts_forecast_by('AutoEnsemble') | COUNT from ts_forecast_by on 60-row series | 3 rows returned | ✓ PASS |
| Regression: ts_forecast_ensemble_by | COUNT from ts_forecast_ensemble_by on 60-row series | 3 rows returned | ✓ PASS |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|-------------|-------------|--------|----------|
| EPI-01 | 06-02-PLAN.md | User can attach distribution-free prediction intervals to an ensemble point forecast via the existing conformal path (learn + apply) | ✓ SATISFIED | ensemble_intervals.sql both sections: bad_rows=0. REQUIREMENTS.md marked [x]. |
| INSP-01 | 06-01-PLAN.md, 06-02-PLAN.md | User can inspect which member models an ensemble selected and their combination weights, per series | ✓ SATISFIED | ensemble_inspect.sql all 8 assertions = 0; 7 unit tests pass. REQUIREMENTS.md marked [x]. |

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| None found | — | No TBD/FIXME/XXX/TODO/HACK/PLACEHOLDER in phase-6 source files | — | — |

No unreferenced debt markers found in: forecast.rs (core), anofox-fcst-ffi/src/lib.rs, ts_ensemble_inspect_native.cpp, ts_macros.cpp, ensemble_intervals.sql, ensemble_inspect.sql.

### EPI-01 Route Deviation: Assessment

The PLAN must-have stated: "via `ts_cv_forecast_by('AutoEnsemble')`." The delivered example uses `_ts_forecast_scalar('AutoEnsemble')` in a manual per-fold loop instead, because `ts_cv_forecast_by('AutoEnsemble')` produces a segfault (exit 139) at runtime in the 0.15.3 extension — a pre-existing bug not introduced by this phase.

Assessment: The deviation is **intentional and correct**. The ROADMAP SC1 requires "routed through the existing conformal machinery," which the workaround satisfies (uses `ts_cv_folds_by`, `ts_conformal_calibrate`, and the standard apply pattern). The functional outcome is identical: non-degenerate conformal intervals on AutoEnsemble forecasts with lower<=point<=upper. The crash and workaround are documented honestly in both the example header and the conformal API doc. This is not a hidden failure.

### Human Verification Required

None required. All must-haves are verifiable programmatically and have been verified. The AutoEnsemble weight-NULL behavior is a documented upstream crate limitation, not a behavioral invariant requiring human observation.

### Gaps Summary

No gaps. All ROADMAP success criteria are satisfied. The one plan must-have that diverged from the plan's specified route (EPI-01 `ts_cv_forecast_by` path) is resolved via an override: the functional goal is achieved, the deviation is pre-existing-bug-driven, the workaround is documented honestly, and the ROADMAP requirement is unambiguously met.

---

_Verified: 2026-08-31T14:00:00Z_
_Verifier: Claude (gsd-verifier, Sonnet 4.6)_
