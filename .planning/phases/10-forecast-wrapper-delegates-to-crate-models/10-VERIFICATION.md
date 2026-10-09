---
phase: 10-forecast-wrapper-delegates-to-crate-models
verified: 2026-10-09T18:00:40Z
status: gaps_found
score: 11/13 must-haves verified
covered_files: [".planning/REQUIREMENTS.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-01-PLAN.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-01-SUMMARY.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-02-PLAN.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-02-SUMMARY.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-03-PLAN.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-03-SUMMARY.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-04-PLAN.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-04-SUMMARY.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-05-PLAN.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-05-SUMMARY.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-06-PLAN.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-06-SUMMARY.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-07-PLAN.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-07-SUMMARY.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-08-PLAN.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-08-SUMMARY.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-09-PLAN.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-09-SUMMARY.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-CONTEXT.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-REVIEW-FIX.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-REVIEW.md", ".planning/phases/10-forecast-wrapper-delegates-to-crate-models/deferred-items.md", "CHANGELOG.md", "crates/anofox-fcst-core/src/forecast.rs", "crates/anofox-fcst-ffi/src/lib.rs", "crates/anofox-fcst-ffi/src/types.rs", "docs/api/07-forecasting.md", "docs/reference/models/state-space/kalman.md", "examples/forecasting/autoensemble.sql", "examples/forecasting/crate_delegation_examples.sql", "examples/forecasting/ensemble_explicit.sql", "examples/forecasting/ensemble_intervals.sql", "src/aggregate_functions/ts_forecast_agg.cpp", "src/include/anofox_fcst_ffi.h", "src/macros/ts_macros.cpp", "src/scalar_functions/ts_forecast_scalar.cpp", "src/table_functions/ts_cv_forecast_native.cpp", "src/table_functions/ts_forecast.cpp", "src/table_functions/ts_forecast_ensemble_native.cpp", "src/table_functions/ts_forecast_native.cpp", "test/sql/ts_backtest_equivalence.test", "test/sql/ts_forecast_crate_delegation.test", "test/sql/ts_forecast_exog.test", "test/sql/ts_model_distinctness.test", "test/sql/ts_native_model_names.test"]
covered_digest: "v1:sha256:b792ec64beab122750945694cdc9830ccc6ed4ea9d0db16e8d5c4968b8b2b7f3"
behavior_unverified: 0
overrides_applied: 0
gaps:
  - truth: "Every former silent fallback is an SQL error or explicitly reported — no silent model/parameter drop on any in-scope forecast surface (criterion 4 / FCST-06 / D-04 / D-07)"
    status: failed
    reason: >
      `ts_forecast_agg` (the aggregate surface, explicitly in scope per 10-CONTEXT.md's
      domain statement "ts_forecast_by, ts_forecast_exog_by, the scalar/agg forecast
      paths and the ensemble surfaces") has NO params-MAP key validation at all. Every
      other forecast entry point (`ts_forecast_native.cpp`'s `ValidateParamKeys`,
      `ts_cv_forecast_native.cpp`'s `ValidateParamKeys`, `ts_forecast.cpp`'s
      `ExogValidateParamKeys`, `ts_forecast_scalar.cpp`'s `valid_keys`) rejects an
      unrecognized MAP key with `InvalidInputException`. `ts_forecast_agg.cpp` only ever
      reads `model`/`arima_order`/`seasonal_order`/`seasonal_period` via
      `GetParamFromMap` and silently drops every other key — `kalman_model`, `garch_p`,
      `garch_q`, `croston_variant`, `model_pool`, `window`, `laplace_variant`,
      `laplace_seasonal_batch_init`, `top_k`, `combination_method`, and even
      `confidence_level` (hardcoded to 0.90 at bind time; a comment at line 108 admits
      "we can't easily extract the confidence_level from params MAP at bind time").
      Confirmed live against the rebuilt extension: `ts_forecast_agg(ds, y, 'Kalman', 3,
      MAP{'kalman_model':'bogus'})` returns `model_name = 'Kalman(local_level)'` with no
      error (the equivalent `ts_forecast_by(..., 'Kalman', ..., params :=
      MAP{'kalman_model':'bogus'})` call correctly raises `Invalid Input Error: Invalid
      parameter 'kalman_model' = 'bogus': expected local_level or local_linear_trend`).
      Also confirmed for `garch_p` and `croston_variant` — both silently ignored on the
      agg surface. This gap is pre-existing (no `ValidateParamKeys` was ever added to
      `ts_forecast_agg.cpp` in its git history), but it is NOT out of this phase's scope:
      10-CONTEXT.md's domain statement explicitly includes the agg path, WR-02 in this
      phase's own review touched this exact file for a related issue (missing
      residuals/aic/bic/mse), and — critically — this phase's OWN new documentation
      overclaims the guarantee for this surface. `docs/api/07-forecasting.md` line 296
      states "This section applies to every forecast surface (`ts_forecast_by`,
      `ts_forecast_exog_by`, `ts_forecast_agg`, the scalar paths, `ts_cv_forecast_by`)
      unless a surface's own section says otherwise" and then (no agg-specific
      exception given) lists "An unrecognized `kalman_model` value" and "An unrecognized
      panel `croston_variant` or `model_pool` value" under "Former silent fallbacks — now
      hard errors". `CHANGELOG.md`'s "Breaking changes" section repeats the same
      unqualified claim. Both are factually false for `ts_forecast_agg`.
    artifacts:
      - path: "src/aggregate_functions/ts_forecast_agg.cpp"
        issue: "No ValidateParamKeys/valid_keys set exists; unrecognized MAP keys (kalman_model, garch_p, garch_q, croston_variant, model_pool, window, laplace_variant, laplace_seasonal_batch_init, top_k, combination_method) are silently dropped; confidence_level is hardcoded to 0.90 regardless of the params MAP"
      - path: "docs/api/07-forecasting.md"
        issue: "Line 296 claims the 'former silent fallbacks — now hard errors' guarantee (including unrecognized kalman_model / croston_variant / model_pool) applies to ts_forecast_agg without exception; this is false"
      - path: "CHANGELOG.md"
        issue: "Breaking-changes bullet repeats the same unqualified claim for 'every former silent fallback'"
    missing:
      - "Add a ValidateParamKeys-equivalent to ts_forecast_agg.cpp covering the same key set as ts_forecast_native.cpp (model, seasonal_period, seasonal_periods, confidence_level, window, model_pool, laplace_variant, laplace_seasonal_batch_init, garch_p, garch_q, kalman_model, top_k, combination_method, arima_order, seasonal_order), or explicitly extend the params the agg surface reads/validates (kalman_model, garch_p/garch_q, croston_variant, model_pool, confidence_level at minimum) so the same model-specific parameters work and are validated identically to ts_forecast_by"
      - "Either make confidence_level actually honour the params MAP value on the agg surface, or document the current hardcoded-0.90 behavior explicitly as a known surface limitation"
      - "Correct docs/api/07-forecasting.md and CHANGELOG.md to scope the 'former silent fallbacks' guarantee away from ts_forecast_agg until it is actually true, or close the code gap so the existing docs become accurate"
deferred: []
coincidental_reliance_items:
  - truth: "Fitted values, residuals and MSE for each model come from the crate model (`fitted_values()`/`residuals()`), and no longer share the old stub MSE value 2090.28 (criterion 2 / FCST-03)"
    reason: incidental-ordering
    harden: >
      Verified: `_ts_forecast` on the 24-point AirPassengers-prefix series returns 8
      genuinely distinct MSE values across SES/Holt/AutoARIMA/AutoETS/Theta/HoltWinters/
      MSTL/CrostonClassic (289.6671 / 217.6317 / 54.7084 / 201.5916 / 364.8022 / 16.7691 /
      0.0 / 364.8022) — none equal the old bug value 2090.28, and the wiring (per-model
      dispatch to the crate's own fitted_values()/residuals()) is confirmed correct by
      code inspection (forecast_croston_classic / forecast_theta call distinct crate
      model types, and point forecasts differ: Croston flat 135.39, Theta trending
      140.24→141.21). However, Theta and CrostonClassic's in-sample `fitted` arrays are
      byte-identical to 15 significant digits on this fixture even though their point
      forecasts diverge — this reads as an upstream crate numerical coincidence (Theta's
      fitted path may reduce to its internal SES component on this series) rather than a
      wrapper dispatch bug, and is the kind of upstream calibration question CONTEXT.md
      explicitly defers to Phase 11 ("Upstream crate correctness ... Theta ...").
      Flagging as coincidental-reliance rather than a gap because the written criterion
      ("no longer share MSE 2090.28") is satisfied and the dispatch code is verifiably
      distinct; a future verifier/phase should confirm this Theta/Croston fitted-value
      match is intentional crate behavior, not a latent aliasing bug, before relying on
      Theta's `fitted`/`residuals` output for anything calibration-sensitive.
---

# Phase 10: Forecast Wrapper Delegates to Crate Models Verification Report

**Phase Goal:** `ts_forecast_by`, `ts_forecast_exog_by` and the ensemble surfaces return the
prediction intervals, fitted values, residuals and model identity the crate model actually
produced — no fabricated point ± z·sd(series)·√h intervals, no SES(α=0.3) stand-in residuals,
no fixed-coefficient "ARIMA", no silent model/parameter fallbacks.

**Verified:** 2026-10-09T18:00:40Z
**Status:** gaps_found
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | AirPassengers 95% h=1 Naive half-width ≈ R naive() ±66 (not 234); models without native intervals return NULL bounds; Laplace passes through; z uses exact Φ⁻¹ | ✓ VERIFIED | `examples/forecasting/crate_delegation_examples.sql` SECTION 1 run against the built extension: Naive 95% half-width = 66.0857125158351, 80%/95% ratio check `PASS naive_80_95_ratio`; cargo test `naive_bounds_match_r_fixture`, `z_is_exact_inverse_normal`, `laplace_intervals_pass_through`, `no_model_reproduces_fabricated_width` all pass; manually confirmed `ts_forecast_exog_by(..., 'MSTL', ..., seasonal_period:=12)` on a regressor path returns `yhat_lower=NULL, yhat_upper=NULL` (D-02) |
| 2 | Fitted/residuals/MSE from the crate: SES, Holt, AutoARIMA, AutoETS, Theta, HW, MSTL, Croston no longer share MSE 2090.28; each MSE = mean of crate residuals² over non-interpolated points | ✓ VERIFIED (coincidental-reliance) | 8 distinct MSE values confirmed live (289.6671/217.6317/54.7084/201.5916/364.8022/16.7691/0.0/364.8022), none = 2090.28; cargo test `mse_is_crate_residual_mse`, `interpolated_points_excluded` pass. Theta/CrostonClassic fitted-array coincidence noted — see `coincidental_reliance_items` |
| 3 | 'ARIMA' with a user order matches R Arima() within 1%; exog AutoARIMA runs its own order search; Theta variants stay themselves; ETS/GARCH/Kalman options honoured; constant-in-history regressor → finite forecast, 0 effect | ✓ VERIFIED | `crate_delegation_examples.sql` SECTION 2 (`PASS arima_step1_within_1pct_of_r`), SECTION 3 (`ETS(A,N,M)` AutoETS selection), SECTION 4 (`AutoARIMA(3,1,1)(1,0,1)[15] with regressors`, `PASS exog_all_finite`); cargo test `arima_airline_matches_r_through_ffi`, `exog_autoarima_parity_through_ffi`, `parity_kalman`, `parity_garch` all pass |
| 4 | Every former silent fallback is an SQL error or explicitly reported; model name always identifies what ran | ✗ FAILED | `ts_forecast_by`/native/exog/scalar surfaces all correctly error (confirmed live: bad `kalman_model` → `Invalid Input Error`); AutoMFLES→MFLES, AutoMSTL→MSTL, missing period for HoltWinters → error, 'ETS' w/o spec → `ETS(A,N,N)` all confirmed. **But `ts_forecast_agg` silently drops `kalman_model`/`garch_p`/`garch_q`/`croston_variant`/`model_pool`/`confidence_level` with no validation at all** — confirmed live, contradicts this phase's own `docs/api/07-forecasting.md` and `CHANGELOG.md` claims. See Gaps. |
| 5 | Ensemble member via build_forecaster == standalone with same params; all-NULL series errors; valid NaN treated as missing; regressor NULLs via validity — documented | ✓ VERIFIED | `crate_delegation_examples.sql` SECTION 3 `PASS ets_ensemble_equals_standalone`, `PASS ets_ensemble_bounds_non_null`; cargo test `parity_auto_ensemble`, `all_null_errors`, `nan_equals_null`, `exog_regressor_validity_bit_cleared_behaves_like_nan`, `exog_future_validity_bit_cleared_is_invalid_input` all pass; documented in `docs/api/07-forecasting.md` |
| 6 | FCST-01: crate `predict_with_intervals` for every model, `calculate_confidence_intervals` deleted | ✓ VERIFIED | `grep -r calculate_confidence_intervals crates/` → no matches; cargo test suite above |
| 7 | FCST-02: exact normal inverse CDF for z, no step table | ✓ VERIFIED | cargo test `z_is_exact_inverse_normal` passes; statrs-based implementation confirmed in forecast.rs |
| 8 | FCST-03: `calculate_fitted_values` deleted, fitted/residuals/MSE from crate | ✓ VERIFIED | `grep -r calculate_fitted_values crates/` → no matches |
| 9 | FCST-04: 'ARIMA' is a real crate fit, never fixed-coefficient AR(1) | ✓ VERIFIED | see truth 3 |
| 10 | FCST-05: exog routes through crate exog support | ✓ VERIFIED | see truth 3 |
| 11 | FCST-06: silent fallbacks → errors or reported identity | ✗ FAILED | see truth 4 — fails specifically for `ts_forecast_agg` |
| 12 | FCST-07: ensemble member == standalone | ✓ VERIFIED | see truth 5 |
| 13 | FCST-08: explicit NULL/NaN input handling | ✓ VERIFIED | see truth 5 |

**Score:** 11/13 must-haves verified (0 present-but-behavior-unverified)

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `crates/anofox-fcst-core/src/forecast.rs` | crate-native intervals/fitted/residuals dispatch, `resolve_period`, `arima_order`/`seasonal_order` | ✓ VERIFIED | `calculate_confidence_intervals`/`calculate_fitted_values` deleted; `resolve_period` used by both `forecast()` and (post CR-01 fix) `forecast_with_exog()` |
| `crates/anofox-fcst-ffi/src/lib.rs`, `types.rs` | additive ABI fields, strict option parsing, `n_residuals` | ✓ VERIFIED | `c_field_str`, `parse_order_triplet`, `n_residuals` field present; cargo test `core_ffi_parity` 47/47 pass |
| `src/table_functions/ts_forecast_native.cpp` | `ValidateParamKeys` incl. `kalman_model`/`garch_p`/`garch_q`/`arima_order`/`seasonal_order` | ✓ VERIFIED | confirmed via grep + live SQL error on bad `kalman_model` |
| `src/table_functions/ts_cv_forecast_native.cpp` | same key validation, NULL-safe bounds | ✓ VERIFIED | `ValidateParamKeys` present |
| `src/table_functions/ts_forecast.cpp` (exog) | `ExogValidateParamKeys`, exog dispatch | ✓ VERIFIED | present and wired |
| `src/scalar_functions/ts_forecast_scalar.cpp` | `valid_keys` validation | ✓ VERIFIED | present and wired |
| `src/aggregate_functions/ts_forecast_agg.cpp` | residuals/aic/bic/mse in STRUCT (WR-02), arima_order/seasonal_order, **param key validation** | ⚠️ PARTIAL | WR-02 fields present and correct (confirmed live: `residuals` len=24, real `mse`); **no `ValidateParamKeys` equivalent exists — unknown/model-specific keys silently dropped** |
| `examples/forecasting/crate_delegation_examples.sql` | runnable end-to-end proof of FCST-01/04/05/06/07/08 | ✓ VERIFIED | ran against built extension, "ALL SECTIONS PASSED" |
| `CHANGELOG.md` Breaking changes | D-13 changelog entry | ⚠️ PARTIAL | section exists and is substantially accurate, but overclaims the silent-fallback guarantee for `ts_forecast_agg` (see gap) |
| `docs/api/07-forecasting.md` | updated docs, no "(being corrected in v0.10.0, Phase 10)" notes | ⚠️ PARTIAL | notes removed (confirmed via grep, 0 matches); but line 296's "applies to every forecast surface" claim is false for `ts_forecast_agg`'s silent-fallback section |

### Key Link Verification

| From | To | Via | Status | Details |
|------|-----|-----|--------|---------|
| `forecast_with_exog` | `resolve_period` | CR-01 fix (commits `e84bcd9`/`879fd51`) | ✓ WIRED | confirmed via cargo test `mstl_with_regressors_and_no_detectable_period_errors` passing |
| `forecast_crate_exog` | ARIMA-order non-ARIMA-model guard | WR-01 fix (commit `12f6a1e`) | ✓ WIRED | confirmed via cargo test `arima_order_on_non_arima_model_errors_on_exog_path` |
| `ts_forecast_agg.cpp` Finalize | crate `ForecastResult.residuals/aic/bic/mse` | WR-02 fix (commit `9b27901`) | ✓ WIRED | confirmed live: `ts_forecast_agg(..., 'AutoETS', ...)` returns real residuals/mse |
| `ts_forecast_agg.cpp` params MAP | `kalman_model`/`garch_p`/`garch_q`/`croston_variant`/`model_pool`/`confidence_level` | (none — not read at all) | ✗ NOT_WIRED | these keys are never extracted from the params MAP on this surface; confirmed live |
| `ts_forecast_by`/native/exog/scalar | `ValidateParamKeys` → `InvalidInputException` | — | ✓ WIRED | confirmed live on all four surfaces |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Naive 95% half-width ≈ R ±66 | `crate_delegation_examples.sql` SECTION 1 | 66.0857 | ✓ PASS |
| ARIMA(0,1,1)(0,1,1)[12] matches R within 1% | `crate_delegation_examples.sql` SECTION 2 | PASS | ✓ PASS |
| 'ETS' auto-selects, ensemble==standalone | `crate_delegation_examples.sql` SECTION 3 | `ETS(A,N,M)`, PASS | ✓ PASS |
| AutoARIMA exog runs own order search | `crate_delegation_examples.sql` SECTION 4 | `AutoARIMA(3,1,1)(1,0,1)[15] with regressors` | ✓ PASS |
| Trailing-NULL realignment | `crate_delegation_examples.sql` SECTION 5 | PASS | ✓ PASS |
| Bad `kalman_model` on `ts_forecast_by` errors | manual SQL | `Invalid Input Error` | ✓ PASS |
| Bad `kalman_model` on `ts_forecast_agg` errors | manual SQL | **silently returns `Kalman(local_level)`, no error** | ✗ FAIL |
| Bad `garch_p`/`croston_variant` on `ts_forecast_agg` errors | manual SQL | **silently ignored, no error** | ✗ FAIL |
| MSTL+regressors, no detectable period → error | cargo test `mstl_with_regressors_and_no_detectable_period_errors` | pass | ✓ PASS |
| `ts_forecast_exog_by('MSTL', ...)` with regressors → NULL bounds | manual SQL (`LOAD json`) | `yhat_lower=NULL, yhat_upper=NULL` | ✓ PASS |

### Probe Execution

No `scripts/*/tests/probe-*.sh` convention used by this phase; none declared in PLAN/SUMMARY files. Skipped.

### Requirements Coverage

| Requirement | Source Plan(s) | Status | Evidence |
|--------------|-----------------|--------|----------|
| FCST-01 | 10-01, 10-02, 10-03, 10-04, 10-05, 10-06, 10-07, 10-08, 10-09 | ✓ SATISFIED | truth 6 |
| FCST-02 | 10-01, 10-02, 10-06 | ✓ SATISFIED | truth 7 |
| FCST-03 | 10-01, 10-02, 10-04, 10-06, 10-07 | ✓ SATISFIED | truth 8 |
| FCST-04 | 10-02, 10-05, 10-06, 10-08 | ✓ SATISFIED | truth 9 |
| FCST-05 | 10-03, 10-04, 10-05, 10-06, 10-08 | ✓ SATISFIED | truth 10 |
| FCST-06 | 10-02, 10-03, 10-05, 10-06, 10-07, 10-08, 10-09 | ✗ BLOCKED | truth 11 — `ts_forecast_agg` silent-fallback gap |
| FCST-07 | 10-03, 10-05, 10-06, 10-08, 10-09 | ✓ SATISFIED | truth 12 |
| FCST-08 | 10-01, 10-04, 10-05, 10-06 | ✓ SATISFIED | truth 13 |
| FCST-09 | (Phase 17, per REQUIREMENTS.md) | N/A — out of this phase's scope | explicitly deferred, not orphaned |

No orphaned requirements: all FCST-01..08 IDs are claimed by at least one plan's frontmatter; FCST-09 is explicitly mapped to Phase 17 in REQUIREMENTS.md.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| `src/aggregate_functions/ts_forecast_agg.cpp` | 101-115 | Comment admits confidence_level "can't easily [be] extract[ed] ... from the params MAP at bind time" — hardcoded 0.90, never varies | 🛑 Blocker | Silent fallback — `confidence_level` param is accepted syntactically but has zero effect on this surface |
| `src/aggregate_functions/ts_forecast_agg.cpp` | whole file | No `ValidateParamKeys`/`valid_keys` equivalent | 🛑 Blocker | Any unrecognized or model-specific key (`kalman_model`, `garch_p`, `garch_q`, `croston_variant`, `model_pool`, `window`, `laplace_variant`, `top_k`, `combination_method`) is silently dropped, contradicting FCST-06/D-04/D-07 and this phase's own new docs |

No `TBD`/`FIXME`/`XXX` debt markers found in phase-10-modified files.

### Build / Test Verification (commands run by this verifier, not taken from SUMMARY claims)

- `cargo fmt --all -- --check`: clean
- `cargo clippy --all-targets --all-features --workspace -- -D warnings`: clean (0 warnings)
- `cargo test --workspace`: **all pass** — 242 unit (anofox-fcst-core) + 1+1+11+10+17+12+1+6 integration + 30 unit (anofox-fcst-ffi) + 47 integration (`core_ffi_parity`) + 12+0 doc tests; 0 failures
- `./build/release/test/unittest "test/sql/ts_forecast_crate_delegation.test"`: 40/40 assertions pass
- `./build/release/test/unittest "test/sql/ts_native_model_names.test"`: 63/63 assertions pass
- `./build/release/test/unittest "test/sql/ts_backtest_equivalence.test"`: 44/44 assertions pass
- `./build/release/test/unittest "test/sql/ts_model_distinctness.test"`: 34/34 assertions pass
- `./build/release/test/unittest "test/sql/ts_forecast_exog.test"`: skipped (`require json` — pre-existing environment gap confirmed via `git log -p` to predate Phase 10 by 9 months, commit `705a7e0`, Jan 2026; the whole file is skipped because DuckDB's sqllogic runner pre-scans every `require` line before executing any statement)
- `make test_release`: **All tests passed (50 skipped tests, 1397 assertions in 18 test cases)** — the 50 skipped are all `require json` (same pre-existing gap); no regressions
- `git status --short | grep -v '^??'`: clean — working tree matches the last commit exactly, so the built extension reflects the final committed state (including all 4 REVIEW-FIX commits)
- All 6 commits named in SUMMARY/REVIEW-FIX (`e84bcd9`, `879fd51`, `12f6a1e`, `9b27901`, `f406e6b`, `edf3251`) confirmed present via `git show`
- `grep -rn "being corrected in v0.10.0"`: 0 matches — all notes removed as required
- `examples/forecasting/crate_delegation_examples.sql` run against `build/release/duckdb -unsigned -bail`: "ALL SECTIONS PASSED" (5/5 sections, all inline assertions PASS)

## Gaps Summary

This phase's core delivery — crate-native intervals, fitted values, residuals, MSE, real
ARIMA, crate-driven exog forecasting, ensemble parity, explicit NULL/NaN handling — is
genuinely implemented, extensively value-tested against R fixtures, and independently
reproduced by this verifier end-to-end against the rebuilt extension (all 5 example
sections pass; 40/40, 63/63, 44/44, 34/34 SQL assertions pass across four migrated test
files; 1397/1397 assertions pass workspace-wide via `make test_release`; `cargo test
--workspace`/`fmt`/`clippy` all clean). The two review findings from `10-REVIEW.md`
(CR-01, WR-01/02/03) were genuinely fixed and are independently confirmed here, not just
claimed.

However, one gap survived both the executor and the code reviewer: **`ts_forecast_agg`
has no params-MAP key validation at all**, unlike every other forecast surface this phase
touches. A caller can pass `kalman_model: 'nonsense'`, `garch_p: 'nonsense'`, or
`croston_variant: 'nonsense'` to `ts_forecast_agg` and get a silently-defaulted result
instead of the hard SQL error every other surface (`ts_forecast_by`, `_ts_forecast_native`,
`ts_cv_forecast_by`, the exog path, the scalar path) correctly raises for the exact same
input. `confidence_level` is also silently ignored on this surface (hardcoded to 0.90).
This directly contradicts FCST-06 / locked decisions D-04 and D-07 ("unknown kalman_model
... → errors", "every former silent fallback becomes an error"), and — more pointedly —
it contradicts this phase's own new documentation: `docs/api/07-forecasting.md` explicitly
claims the "former silent fallbacks — now hard errors" list (which names `kalman_model`
and `croston_variant`/`model_pool` by name) "applies to every forecast surface ...
`ts_forecast_agg` ... unless a surface's own section says otherwise" — and no such override
exists for `ts_forecast_agg`. `CHANGELOG.md`'s "Breaking changes" section repeats the
unqualified claim. Both are factually false as written.

This gap is narrow in scope (one aggregate function's parameter plumbing) but it sits
exactly on the phase's central promise ("no silent model/parameter fallbacks") and the
phase's own documentation accuracy requirement (D-13 / CLAUDE.md's "verify SQL doc
examples end-to-end" convention) — so it blocks a clean pass rather than being advisory.

**This looks like an oversight, not an intentional deviation** — there is no CONTEXT.md
carve-out scoping `ts_forecast_agg` out of the silent-fallback guarantee, and the gap was
never raised in `10-REVIEW.md`/`10-REVIEW-FIX.md`. No override is suggested; recommend
routing this to a closure plan (either extend `ts_forecast_agg.cpp`'s key validation/param
support to match `ts_forecast_native.cpp`, or narrow the docs/CHANGELOG claim to exclude
`ts_forecast_agg` until the code gap is closed).

---

_Verified: 2026-10-09T18:00:40Z_
_Verifier: Claude (gsd-verifier)_
