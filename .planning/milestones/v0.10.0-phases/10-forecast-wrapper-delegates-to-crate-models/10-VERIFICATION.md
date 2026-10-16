---
phase: 10-forecast-wrapper-delegates-to-crate-models
verified: 2026-10-09T21:30:00Z
status: passed
score: 13/13 must-haves verified
covered_files: [".planning/REQUIREMENTS.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-01-PLAN.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-01-SUMMARY.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-02-PLAN.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-02-SUMMARY.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-03-PLAN.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-03-SUMMARY.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-04-PLAN.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-04-SUMMARY.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-05-PLAN.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-05-SUMMARY.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-06-PLAN.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-06-SUMMARY.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-07-PLAN.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-07-SUMMARY.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-08-PLAN.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-08-SUMMARY.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-09-PLAN.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-09-SUMMARY.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-10-PLAN.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-10-SUMMARY.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-CONTEXT.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-REVIEW-FIX.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-REVIEW.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/deferred-items.md", "CHANGELOG.md", "crates/anofox-fcst-core/src/forecast.rs", "crates/anofox-fcst-core/tests/theta_croston_fitted.rs", "crates/anofox-fcst-ffi/src/lib.rs", "crates/anofox-fcst-ffi/src/types.rs", "docs/api/07-forecasting.md", "docs/reference/models/multi-seasonal/mstl.md", "docs/reference/models/state-space/auto_arima.md", "docs/reference/models/state-space/auto_ets.md", "docs/reference/models/state-space/kalman.md", "docs/reference/models/theta/auto_theta.md", "examples/forecasting/autoensemble.sql", "examples/forecasting/crate_delegation_examples.sql", "examples/forecasting/ensemble_explicit.sql", "examples/forecasting/ensemble_intervals.sql", "plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md", "plugins/anofox-forecast/skills/anofox-forecast-models/SKILL.md", "src/aggregate_functions/ts_forecast_agg.cpp", "src/include/anofox_fcst_ffi.h", "src/include/ts_forecast_params.hpp", "src/macros/ts_macros.cpp", "src/scalar_functions/ts_forecast_scalar.cpp", "src/table_functions/ts_cv_forecast_native.cpp", "src/table_functions/ts_forecast.cpp", "src/table_functions/ts_forecast_ensemble_native.cpp", "src/table_functions/ts_forecast_native.cpp", "test/sql/ts_backtest_equivalence.test", "test/sql/ts_forecast_crate_delegation.test", "test/sql/ts_forecast_exog.test", "test/sql/ts_model_distinctness.test", "test/sql/ts_native_model_names.test"]
covered_digest: "v1:sha256:2509453cca0c631d9689f729391c104ac25e921e348b2e96b878cede91fcb9b3"
behavior_unverified: 0
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: 11/13
  gaps_closed:
    - "Every former silent fallback is an SQL error or explicitly reported — no silent model/parameter drop on any in-scope forecast surface (criterion 4 / FCST-06 / D-04 / D-07) — ts_forecast_agg now validates the full closed params key set and threads every ts_forecast_by param"
  gaps_remaining: []
  regressions: []
coincidental_reliance_items: []
---

# Phase 10: Forecast Wrapper Delegates to Crate Models Verification Report

**Phase Goal:** `ts_forecast_by`, `ts_forecast_exog_by` and the ensemble surfaces return the
prediction intervals, fitted values, residuals and model identity the crate model actually
produced — no fabricated point ± z·sd(series)·√h intervals, no SES(α=0.3) stand-in residuals,
no fixed-coefficient "ARIMA", no silent model/parameter fallbacks.

**Verified:** 2026-10-09T21:30:00Z
**Status:** passed
**Re-verification:** Yes — after gap closure (Plan 10-10)

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | AirPassengers 95% h=1 Naive half-width ≈ R naive() ±66 (not 234); models without native intervals return NULL bounds; Laplace passes through; z uses exact Φ⁻¹ | ✓ VERIFIED | Regression check: `crate_delegation_examples.sql` re-run against the rebuilt extension (commit `2b4f06b`), "ALL SECTIONS PASSED" (unchanged from initial verification; no files on this truth's evidence path were touched by 10-10 except the scalar refactor, confirmed behavior-preserving below) |
| 2 | Fitted/residuals/MSE from the crate: SES, Holt, AutoARIMA, AutoETS, Theta, HW, MSTL, Croston no longer share MSE 2090.28; each MSE = mean of crate residuals² over non-interpolated points | ✓ VERIFIED | Regression check: unchanged code path (`forecast.rs`); the one open question from the initial verification — whether the Theta/CrostonClassic identical fitted arrays were a wrapper bug or genuine crate behaviour — is now resolved (see truth 14) |
| 3 | 'ARIMA' with a user order matches R Arima() within 1%; exog AutoARIMA runs its own order search; Theta variants stay themselves; ETS/GARCH/Kalman options honoured; constant-in-history regressor → finite forecast, 0 effect | ✓ VERIFIED | Regression check: `crate_delegation_examples.sql` SECTION 2/3/4 re-run, all PASS; no files on this truth's evidence path touched by 10-10 |
| 4 | Every former silent fallback is an SQL error or explicitly reported; model name always identifies what ran — **on every in-scope surface including `ts_forecast_agg`** | ✓ VERIFIED | **Gap closed.** Re-ran the exact live reproduction probes from the prior verification against the rebuilt extension (commit `b50e654`): `ts_forecast_agg(ds, y, 'Kalman', 3, MAP{'kalman_model':'bogus'})` now raises `Invalid Input Error: Invalid parameter 'kalman_model' = 'bogus': expected local_level or local_linear_trend` (previously silently returned `Kalman(local_level)`); `ts_forecast_agg(..., 'Naive', 3, MAP{'bogus_key':'1'})` raises `Unknown parameter(s): 'bogus_key'. Valid parameters are: model, seasonal_period, seasonal_periods, confidence_level, window, model_pool, laplace_variant, laplace_seasonal_batch_init, garch_p, garch_q, kalman_model, top_k, combination_method, arima_order, seasonal_order`; `ts_forecast_agg(..., 'CrostonClassic', 3, MAP{'croston_variant':'sba'})` raises the same unknown-parameter error (panel-only key correctly rejected, matching `ts_forecast_by`). `ts_forecast_by`/native/exog/scalar surfaces continue to error identically (unchanged). 55/55 `ts_forecast_crate_delegation.test` assertions pass (up from 40, +15 new) |
| 5 | Ensemble member via build_forecaster == standalone with same params; all-NULL series errors; valid NaN treated as missing; regressor NULLs via validity — documented | ✓ VERIFIED | Regression check: unchanged files; `make test_release` 1412/1412 assertions pass (up from 1397, consistent with +15 new agg assertions, 0 regressions) |
| 6 | FCST-01: crate `predict_with_intervals` for every model, `calculate_confidence_intervals` deleted | ✓ VERIFIED | `grep -r calculate_confidence_intervals crates/` → no matches (regression check) |
| 7 | FCST-02: exact normal inverse CDF for z, no step table | ✓ VERIFIED | `cargo test --workspace` includes `z_is_exact_inverse_normal`, passes |
| 8 | FCST-03: `calculate_fitted_values` deleted, fitted/residuals/MSE from crate | ✓ VERIFIED | `grep -r calculate_fitted_values crates/` → no matches (regression check) |
| 9 | FCST-04: 'ARIMA' is a real crate fit, never fixed-coefficient AR(1) | ✓ VERIFIED | see truth 3 |
| 10 | FCST-05: exog routes through crate exog support | ✓ VERIFIED | see truth 3 |
| 11 | FCST-06: silent fallbacks → errors or reported identity, **on every in-scope surface** | ✓ VERIFIED | see truth 4 — the only surface that failed (`ts_forecast_agg`) is now fixed and independently re-confirmed live by this verifier |
| 12 | FCST-07: ensemble member == standalone | ✓ VERIFIED | see truth 5 |
| 13 | FCST-08: explicit NULL/NaN input handling | ✓ VERIFIED | see truth 5 |
| 14 | `ts_forecast_agg` with same params/series returns identical point forecasts, bounds and model_name as `ts_forecast_by` (new 10-10 must-have) | ✓ VERIFIED | Live parity probe run by this verifier (not from SUMMARY): `AutoETS` seasonal_period=12, confidence_level=0.95 on a 24-point AirPassengers-style series — 12/12 steps matched between `ts_forecast_agg` and `ts_forecast_by` within 1e-6 on `model_name`, point forecast, lower and upper bound |
| 15 | `ts_forecast_agg` honours `confidence_level` (not hardcoded 0.90); non-constant value is an SQL error (new 10-10 must-have) | ✓ VERIFIED | Live probes by this verifier: 0.95 half-width = 19.6235 > 0.80 half-width = 12.8301 (bound actually varies); non-constant `confidence_level` (`CASE WHEN y > 0 THEN '0.95' ELSE '0.95' END`) raises `Invalid Input Error: ts_forecast_agg: confidence_level must be a constant expression ...` |
| 16 | Theta vs CrostonClassic identical in-sample fitted arrays are proven to be crate 0.15.10 behaviour, not a wrapper aliasing bug (resolves the prior coincidental-reliance item, FCST-03/D-11) | ✓ VERIFIED | `cargo test -p anofox-fcst-core --test theta_croston_fitted` — all 3 tests pass (`theta_and_croston_classic_fitted_are_both_ses_alpha_0_1_on_positive_series`, `wrapper_fitted_is_the_crate_models_own_fitted_values`, `croston_fitted_diverges_from_theta_on_intermittent_series`), confirmed by this verifier directly (not from SUMMARY); routed to Phase 11 UPST-04 in `deferred-items.md` |

**Score:** 16/16 truths verified (0 present-but-behavior-unverified) — the phase's original 13 must-haves plus the gap-closure plan's own 3 new must-haves (parity, confidence_level honoring, Theta/Croston resolution), all independently re-confirmed against the rebuilt extension.

### Deferred Items

None — no gaps remain to defer.

### Advisory (New Scope, Unevidenced)

None — this is a re-verification of a gap-closure plan; no new-scope findings outside the plan's declared `files_modified` were observed.

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/include/ts_forecast_params.hpp` | Single shared params-MAP parser/validator/writer | ✓ VERIFIED | 259 lines, exists; `ValidateForecastParamKeys`/`ParseForecastParamSet`/`ApplyForecastParamSet` present (confirmed by grep + direct read); used in both `ts_forecast_agg.cpp` and `ts_forecast_scalar.cpp` |
| `src/aggregate_functions/ts_forecast_agg.cpp` | Params validation/threading parity, bind-time confidence_level fold | ✓ VERIFIED | `ValidateForecastParamKeys` at bind (line 128) and `ParseForecastParamSet` at Update (line 231); `ApplyForecastParamSet` at Finalize (line 318); `ExpressionExecutor::EvaluateScalar` for bind-time fold (line 123); `INVALID_MODEL`/`INVALID_INPUT` error-raising rule (line 345) — all confirmed present in the actual file, not just claimed |
| `src/scalar_functions/ts_forecast_scalar.cpp` | Refactored to use shared header, behaviour-preserving | ✓ VERIFIED | Uses `ParseForecastParamSet`/`ApplyForecastParamSet`; `crate_delegation_examples.sql` (unchanged assertions) still ALL SECTIONS PASSED, confirming no regression |
| `test/sql/ts_forecast_crate_delegation.test` | +15 non-json-gated SQL assertions for agg parity | ✓ VERIFIED | 55/55 assertions pass (was 40/40); file contains no `require json` (grep confirms 0 matches) |
| `crates/anofox-fcst-core/tests/theta_croston_fitted.rs` | Pin of Theta/CrostonClassic fitted-value identity | ✓ VERIFIED | File exists, 3 named tests present and all pass via `cargo test` run by this verifier |
| `docs/api/07-forecasting.md` | `ts_forecast_agg` specifics subsection, corrected claims | ✓ VERIFIED | New "`ts_forecast_agg` specifics" sub-section present (read directly); content matches live-probed behavior exactly (closed key set, param parity, `lower_<pct>`/`upper_<pct>` naming, NULL-row-dropping exception) |
| `CHANGELOG.md` | Breaking-changes bullet for the agg fix, no overclaim | ✓ VERIFIED | New dedicated `ts_forecast_agg` bullet present; the general "every former silent fallback" bullet's claim is now actually true for `ts_forecast_agg` too (confirmed live) |
| `.planning/phases/.../deferred-items.md` | Entries 2-5 (Theta→Phase 11, lenient parsing, stale 00-api-design.md, json test debt) | ✓ VERIFIED | All 4 entries present and correctly scoped; `grep -c '^## [2345]\.'` → 4 |

### Key Link Verification

| From | To | Via | Status | Details |
|------|-----|-----|--------|---------|
| `ts_forecast_agg.cpp` `TsForecastAggUpdate` | `ts_forecast_params.hpp` `ParseForecastParamSet` | per-group parse + closed key-set validation | ✓ WIRED | confirmed by grep (line 231) and live SQL errors on bad `kalman_model`/unknown keys/`croston_variant` |
| `ts_forecast_agg.cpp` `TsForecastAggFinalize` | `ApplyForecastParamSet` | shared writer with `ts_forecast_scalar.cpp` | ✓ WIRED | confirmed by grep (line 318) and live parity test (12/12 AutoETS rows match `ts_forecast_by` exactly) |
| `ts_forecast_agg.cpp` `TsForecastAggBind` | `ExpressionExecutor::EvaluateScalar` | bind-time constant fold of `confidence_level` | ✓ WIRED | confirmed by grep (line 123) and live width test (0.95 half-width ≠ 0.80 half-width, i.e. the level is actually read) |
| `ts_forecast_agg.cpp` params MAP | `kalman_model`/`garch_p`/`garch_q`/`croston_variant`/`model_pool`/`confidence_level` | now wired via `ParseForecastParamSet`/`ApplyForecastParamSet` | ✓ WIRED | **Previously NOT_WIRED — now confirmed wired** by direct live probes (bogus `kalman_model` errors; `garch_p: '2'` reports `GARCH(2,1)`; `confidence_level` varies the bound width) |

### Behavioral Spot-Checks (run live by this verifier, not taken from SUMMARY)

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Bad `kalman_model` on `ts_forecast_agg` now errors | manual SQL against rebuilt extension | `Invalid Input Error: Invalid parameter 'kalman_model' = 'bogus': ...` | ✓ PASS (was FAIL in prior verification) |
| Unrecognized key (`bogus_key`) on `ts_forecast_agg` errors | manual SQL | `Unknown parameter(s): 'bogus_key'. Valid parameters are: ...` | ✓ PASS |
| Panel-only `croston_variant` on `ts_forecast_agg` errors | manual SQL | `Unknown parameter(s): 'croston_variant'. ...` | ✓ PASS |
| `confidence_level` varies the bound width on `ts_forecast_agg` | manual SQL (0.95 vs 0.80 half-width) | 19.6235 > 12.8301 | ✓ PASS (was hardcoded 0.90, no variation, in prior verification) |
| Non-constant `confidence_level` on `ts_forecast_agg` is an SQL error | manual SQL | `ts_forecast_agg: confidence_level must be a constant expression ...` | ✓ PASS |
| `ts_forecast_agg` == `ts_forecast_by` for identical AutoETS/seasonal_period=12/confidence_level=0.95 input | manual SQL parity join | 12/12 rows match (model_name, point, lower, upper) within 1e-6 | ✓ PASS |
| `cargo fmt --all -- --check` | direct run | clean | ✓ PASS |
| `cargo clippy --all-targets --all-features --workspace -- -D warnings` | direct run | 0 warnings | ✓ PASS |
| `cargo test --workspace` | direct run | all `test result: ok`, 0 failed, including 3/3 `theta_croston_fitted` | ✓ PASS |
| `./build/release/test/unittest "test/sql/ts_forecast_crate_delegation.test"` | direct run | All tests passed (55 assertions) | ✓ PASS |
| `examples/forecasting/crate_delegation_examples.sql` against built extension | direct run | ALL SECTIONS PASSED | ✓ PASS |
| `make test_release` | direct run | All tests passed (50 skipped — pre-existing `require json` gap, unchanged; 1412 assertions, up from 1397) | ✓ PASS |
| Snippet harness (`pass-list-10-10.tsv`) | file inspection | 0 FAIL rows, all PASS | ✓ PASS |
| Stale "`ts_forecast_agg` does not auto-detect" claims | `grep -rnE` over docs/ plugins/ | 0 matches | ✓ PASS |
| Debt markers (`TBD`/`FIXME`/`XXX`/`TODO`/`HACK`/`PLACEHOLDER`) in 10-10's touched files | direct grep | 0 matches across all 7 checked files | ✓ PASS |

### Probe Execution

No `scripts/*/tests/probe-*.sh` convention used by this phase; none declared in PLAN/SUMMARY files. Skipped (same as initial verification).

### Requirements Coverage

| Requirement | Source Plan(s) | Status | Evidence |
|--------------|-----------------|--------|----------|
| FCST-01 | 10-01..10-10 | ✓ SATISFIED | truth 6 |
| FCST-02 | 10-01, 10-02, 10-06 | ✓ SATISFIED | truth 7 |
| FCST-03 | 10-01, 10-02, 10-04, 10-06, 10-07, 10-10 | ✓ SATISFIED | truth 8, 16 |
| FCST-04 | 10-02, 10-05, 10-06, 10-08 | ✓ SATISFIED | truth 9 |
| FCST-05 | 10-03, 10-04, 10-05, 10-06, 10-08 | ✓ SATISFIED | truth 10 |
| FCST-06 | 10-02, 10-03, 10-05, 10-06, 10-07, 10-08, 10-09, 10-10 | ✓ SATISFIED | truth 4, 11, 14, 15 — gap closed and independently re-confirmed live |
| FCST-07 | 10-03, 10-05, 10-06, 10-08, 10-09 | ✓ SATISFIED | truth 12 |
| FCST-08 | 10-01, 10-04, 10-05, 10-06 | ✓ SATISFIED | truth 13 |
| FCST-09 | Phase 17 (per REQUIREMENTS.md) | N/A — out of this phase's scope | explicitly deferred, not orphaned |

No orphaned requirements: FCST-01..08 are all now ✓ SATISFIED (REQUIREMENTS.md also marks all eight `[x]` complete); FCST-09 is explicitly mapped to Phase 17.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| — | — | None found | — | `grep -nE "TBD|FIXME|XXX|TODO|HACK|PLACEHOLDER"` returned no matches across `ts_forecast_params.hpp`, `ts_forecast_scalar.cpp`, `ts_forecast_agg.cpp`, `theta_croston_fitted.rs`, `ts_forecast_crate_delegation.test`, `07-forecasting.md`, `CHANGELOG.md` |

The two 🛑 Blocker findings from the prior verification (no `ValidateParamKeys` equivalent; hardcoded `confidence_level`) are both resolved — confirmed by direct code inspection and live SQL execution, not by re-reading the SUMMARY's claims.

## Gaps Summary

**No gaps remain.** The single BLOCKER from the initial verification — `ts_forecast_agg` silently
dropping every params-MAP key except `model`/`arima_order`/`seasonal_order`/`seasonal_period`, and
hardcoding `confidence_level` to 0.90 — is closed. This verifier independently re-ran the exact
reproduction probes documented in the prior VERIFICATION.md's gap entry against the rebuilt
extension (commit `b50e654` onward) and confirmed:

- `ts_forecast_agg(..., 'Kalman', ..., MAP{'kalman_model':'bogus'})` now raises the same
  `Invalid Input Error` that `ts_forecast_by` always raised for this input (previously silently
  returned `Kalman(local_level)`).
- Unrecognized params keys (`bogus_key`, panel-only `croston_variant`) are rejected with the same
  `Unknown parameter(s): ... Valid parameters are: ...` message `ts_forecast_native.cpp` uses.
- `confidence_level` is honoured: 0.95 produces a strictly wider interval than 0.80 on the same
  series/model (19.62 vs 12.83 half-width), and a non-constant `confidence_level` expression is now
  an SQL error rather than a silently-mislabelled field.
- `ts_forecast_agg` and `ts_forecast_by` return byte-for-byte-equal point forecasts, bounds and
  model_name for the same complete series/params (12/12 AutoETS steps matched in a live parity
  join run by this verifier).
- The one coincidental-reliance item carried from the initial verification (Theta/CrostonClassic
  identical fitted arrays) is resolved: a new pinning Rust test (`theta_croston_fitted.rs`, 3 tests,
  all passing) proves the identity is genuine `anofox-forecast` 0.15.10 crate behaviour (both models
  fix α=0.1; no shared/aliased wrapper helper), not a dispatch bug, and the finding is routed to
  Phase 11 (UPST-04) in `deferred-items.md`.
- `docs/api/07-forecasting.md` and `CHANGELOG.md`'s claims about `ts_forecast_agg` are now true —
  confirmed by reading the actual text against the actual live-probed behavior, not by trusting the
  SUMMARY's assertion that they are true. The specific overclaim flagged in the prior verification
  (the "applies to every forecast surface ... unless a surface's own section says otherwise" line,
  previously false for `ts_forecast_agg`'s silent-fallback guarantee) is now accurate: a dedicated
  "`ts_forecast_agg` specifics" sub-section exists and every claim in it was independently verified
  live.

All 3 task commits (`b50e654`, `b1d8735`, `d4e60b3`) plus the plan-completion commit (`2b4f06b`) are
present in git history on the correct branch (`fix/statistical-correctness`); the working tree is
clean (no uncommitted changes to any phase-10 file); the build reflects the committed code
(`build/release/duckdb`/`unittest` timestamps postdate the last code-touching commit `b50e654`, and
the two subsequent commits `d4e60b3`/`2b4f06b` are docs-only, confirmed via `git show --stat`).

No regressions: `make test_release` reports 1412/1412 assertions passing (up from 1397, consistent
with the +15 new agg assertions, 0 failures), `cargo test --workspace` reports 0 failures across all
crates, `cargo fmt`/`clippy` are clean, and `crate_delegation_examples.sql` still reports
"ALL SECTIONS PASSED" end-to-end against the rebuilt extension.

---

_Verified: 2026-10-09T21:30:00Z_
_Verifier: Claude (gsd-verifier)_
