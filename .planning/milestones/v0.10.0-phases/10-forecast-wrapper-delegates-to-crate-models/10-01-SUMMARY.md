---
phase: 10-forecast-wrapper-delegates-to-crate-models
plan: 01
subsystem: forecasting
tags: [rust, anofox-forecast, prediction-intervals, mse, forecast-wrapper, ffi-parity]

requires:
  - phase: 09
    provides: forecast_snapshot point-forecast regression fixture, reference_fixtures.rs R-fixture harness, core_ffi_parity.rs parity scaffolding
provides:
  - "forecast()/forecast_with_exog() return crate-native prediction intervals, fitted values, and residuals for every model (no fabricated interval/fitted-value formulas remain)"
  - "Explicit input policy: NaN ≡ NULL, ±Inf rejected, all-NULL/too-short rejected, leading/trailing NULLs trimmed with correct trailing-NULL forecast-step alignment (PreparedSeries/prepare_series/finalize_output/align_exog)"
  - "MSE computed from crate residuals excluding interpolated points, shared by every forecast surface (forecast, forecast_with_exog, forecast_explicit_ensemble)"
  - "Full FFI parity test coverage (point/lower/upper/fitted/residuals) for every forecast model including GARCH, Kalman, AutoEnsemble, Laplace"
affects: ["10-02", "10-03", "10-04"]

actuals:
  tokens: 40066
  tasks: 3
  commits: 4

tech-stack:
  added: []
  patterns:
    - "extract_bounds(forecast, point_len) — single bounds-extraction helper used by every forecast_* function; no z-value/interval arithmetic anywhere in forecast.rs"
    - "PreparedSeries/prepare_series/finalize_output — shared input-policy + trailing-NULL-alignment + MSE-over-non-interpolated-residuals pipeline used by forecast(), forecast_with_exog(), forecast_explicit_ensemble, forecast_inspect, forecast_explain, inspect_explicit_ensemble"
    - "align_exog — exog-path analogue of finalize_output's trailing-NULL alignment"

key-files:
  created:
    - crates/anofox-fcst-core/tests/forecast_intervals.rs
  modified:
    - crates/anofox-fcst-core/src/forecast.rs
    - crates/anofox-fcst-ffi/tests/core_ffi_parity.rs

key-decisions:
  - "Crate SeasonalNaive requires period >= 2 (unlike the old hand-rolled helper); forecast_seasonal_naive now delegates to crate Naive when period < 2, preserving point-forecast identity instead of erroring"
  - "MSE is computed from raw crate residuals directly (a scalar), not the length-filtered/position-aligned array returned to callers — so AutoARIMA (whose crate residuals drop ARIMA differencing warmup rows, a different length than the input) still gets a real MSE, while the EXPORTED residuals array keeps the strict length-match safety check (T-10-02)"
  - "A documented, pre-existing FFI correctness gap (ForecastResult shares one n_fitted length field between fitted_values and residuals, silently truncating residuals to 0 when fitted is None but residuals is Some) is left unfixed — reserved for plan 10-02, which owns the generated C header"

patterns-established:
  - "Deviation documentation inline in test files when a plan's literal numeric must-have turns out to be empirically false for a specific, identified reason (crate z-approximation, model coincidence, pre-existing stand-in value) — not silently loosened, not blocking"

requirements-completed: [FCST-01, FCST-02, FCST-03, FCST-08]

coverage:
  - id: D1
    description: "Every crate-backed model (SeasonalNaive, SMA, RandomWalkDrift, ETS-spec, AutoARIMA, AutoETS, AutoTheta, AutoTBATS, MFLES, Laplace, GARCH, Kalman, AutoEnsemble, and the models already crate-backed before this plan) returns prediction intervals, fitted values, and residuals from the crate's own predict_with_intervals/fitted_values/residuals — no fabricated formulas"
    requirement: FCST-01
    verification:
      - kind: integration
        ref: "crates/anofox-fcst-ffi/tests/core_ffi_parity.rs#naive_intervals_match_r_naive_through_ffi,parity_garch,parity_kalman,parity_auto_ensemble,parity_laplace (+26 other parity_* tests via assert_full_parity)"
        status: pass
      - kind: integration
        ref: "crates/anofox-fcst-core/tests/forecast_intervals.rs#no_model_reproduces_fabricated_width,laplace_intervals_pass_through"
        status: pass
    human_judgment: false
  - id: D2
    description: "No z-value/step-table arithmetic remains anywhere in forecast.rs; the confidence-level-to-quantile mapping comes exclusively from the crate's own quantile source"
    requirement: FCST-02
    verification:
      - kind: unit
        ref: "grep -cE '2\\.576|1\\.645' crates/anofox-fcst-core/src/forecast.rs (prints 0)"
        status: pass
      - kind: integration
        ref: "crates/anofox-fcst-core/tests/forecast_intervals.rs#z_is_exact_inverse_normal"
        status: pass
    human_judgment: false
  - id: D3
    description: "Fitted values, residuals, and MSE come from the crate model; calculate_fitted_values is deleted; MSE excludes interpolated points"
    requirement: FCST-03
    verification:
      - kind: unit
        ref: "grep -v '^\\s*//' crates/anofox-fcst-core/src/forecast.rs | grep -c 'fn calculate_fitted_values' (prints 0)"
        status: pass
      - kind: integration
        ref: "crates/anofox-fcst-core/tests/forecast_intervals.rs#mse_is_crate_residual_mse,interpolated_points_excluded"
        status: pass
    human_judgment: false
  - id: D4
    description: "Series input handling is explicit: all-NULL series errors, valid NaN is treated as missing, leading/trailing NULLs are trimmed with correct trailing-NULL forecast-step alignment"
    requirement: FCST-08
    verification:
      - kind: integration
        ref: "crates/anofox-fcst-core/tests/forecast_intervals.rs#all_null_errors,invalid_input_rejects_inf,nan_equals_null,trailing_null_alignment,confidence_level_outside_open_interval"
        status: pass
    human_judgment: false

duration: ~75min (continuation session)
completed: 2026-10-09
status: complete
---

# Phase 10 Plan 01: Forecast Wrapper Delegates to Crate Models Summary

**Every forecast model now returns the crate's own prediction intervals, fitted values, residuals, and MSE — no z-step-table or SES(0.3) fabrication remains anywhere in `forecast.rs`, and input handling (NaN/NULL/±Inf/trailing-NULL alignment) is explicit and shared across every forecast surface.**

## Performance

- **Duration:** ~75 min (this continuation session — Task 1 was committed by a prior session as `cdf4480`)
- **Tasks:** 3 (Task 1 inherited complete; Task 2 resumed from a stalled WIP and completed; Task 3 executed fresh)
- **Files modified:** 2 (`forecast.rs`, `core_ffi_parity.rs`) + 1 created (`forecast_intervals.rs`)

## Accomplishments

- Every model that reaches `anofox_ts_forecast` (SeasonalNaive, SMA, RandomWalkDrift, ETS-spec, AutoARIMA, AutoETS, AutoTheta, AutoTBATS, MFLES, Laplace, GARCH, Kalman, AutoEnsemble, plus the models already crate-backed from earlier phases) now returns `predict_with_intervals`/`fitted_values`/`residuals` straight from the crate — proven bit-identical through the FFI by 30 `parity_*` tests in `core_ffi_parity.rs`.
- Deleted `calculate_confidence_intervals` (z step-table) and `calculate_fitted_values` (SES(0.3) stand-in) entirely (Task 1); factored `extract_bounds` as the single bounds-extraction path (Task 2).
- Added an explicit input policy (`PreparedSeries`/`prepare_series`) shared by every forecast surface: `Some(NaN)` ≡ `None`, `±Inf` is a hard `InvalidInput` naming the index, an all-missing or <3-point series is `InsufficientData`, and leading/trailing missing values are trimmed before fitting.
- Added `finalize_output` so every forecast surface shares one trailing-NULL alignment policy: models are asked for `horizon + trail_trim` steps, the lead-in is dropped so step 1 lands on the period after the ORIGINAL last row, and MSE excludes residuals at interpolated positions.
- `confidence_level` outside the open interval `(0, 1)` now returns `InvalidParameter` in both `forecast()` and `forecast_with_exog()`.

## Task Commits

Each task was committed atomically (Task 1 by a prior, now-completed session):

1. **Task 1: Tracer — override removed, extract_forecast returns crate intervals; Naive crate-backed** — `cdf4480` (feat) — inherited from a prior session, verified against `git log`/disk, not re-done.
2. **Task 2: Every model returns crate intervals/fitted/residuals, full FFI parity** — `9a1ffe1` (feat) — resumed from a stalled mid-task WIP this session.
3. **Task 3: Explicit input policy and MSE over non-interpolated residuals** — `5ec2cac` (feat)

**Plan metadata:** (this commit, immediately following)

## Files Created/Modified

- `crates/anofox-fcst-core/src/forecast.rs` — every `forecast_*` function delegates to the crate's `predict_with_intervals`/`fitted_values`/`residuals`; `extract_bounds`, `PreparedSeries`, `prepare_series`, `finalize_output`, `align_exog` added; `forecast()`/`forecast_with_exog()`/`forecast_inspect`/`forecast_explain`/`forecast_explicit_ensemble`/`inspect_explicit_ensemble` share one input policy.
- `crates/anofox-fcst-ffi/tests/core_ffi_parity.rs` — `lib_predict_full`/`assert_full_parity` helpers; every `parity_*` test extended to compare point/lower/upper/fitted/residuals bit-identically; added `parity_garch`/`parity_kalman`/`parity_auto_ensemble`/`parity_laplace`.
- `crates/anofox-fcst-core/tests/forecast_intervals.rs` (new) — 12 tests proving the R-fixture interval values, the exact-quantile ratio, the absence of the old fabricated half-width, Laplace passthrough, crate-residual MSE, interpolated-point exclusion, NaN/NULL equivalence, all-NULL/±Inf rejection, trailing-NULL alignment, and confidence-level validation.

## Decisions Made

- **SeasonalNaive period < 2 delegates to crate Naive.** Crate `SeasonalNaive::new(p)` requires `p >= 2`; the old hand-rolled helper allowed `p=1` (equivalent to repeating the last value). Rather than changing the `.max(1)` fallback semantics (explicitly deferred to plan 10-03's D-07 auto-detect-or-error), `forecast_seasonal_naive` now fits crate `Naive` when `p < 2` — bit-identical point forecasts, no behavior change, model name stays `"SeasonalNaive"`.
- **MSE computed from raw crate residuals, independent of the exported-array length check.** AutoARIMA's crate residuals drop the ARIMA differencing warmup rows (verified: 144 input observations → 131 residuals at seasonal_period 12), a different length than the model-input series. `finalize_output` computes MSE from `raw.residuals` directly (interpolated-exclusion applied only when the length matches; falls back to all-finite-residuals otherwise), while the EXPORTED `residuals` array keeps the strict `len() == prep.values.len()` safety check (T-10-02) so a mismatched array is never mis-indexed into a position-aligned output.
- **Known FFI correctness gap documented, not fixed.** `ForecastResult` (in `crates/anofox-fcst-ffi/src/types.rs`) shares a single `n_fitted` length field between `fitted_values` and `residuals`. When a crate model's `fitted_values()` is `None` while `residuals()` is `Some` (GARCH always, by crate design; AutoARIMA on some series), the FFI/SQL surface silently truncates the real residuals array to length 0 — this is a genuine, pre-existing production bug (also present in `ts_forecast.cpp`/`ts_forecast_agg.cpp`, which read `fcst_result.n_fitted` for both arrays). Fixing it needs a new `n_residuals` field on `ForecastResult`, the regenerated C header, and the C++ call sites — out of scope for this plan (`src/include/anofox_fcst_ffi.h` is explicitly reserved for plan 10-02). `assert_full_parity` and `parity_garch` assert the current (truncated) behavior with an inline comment, so a future fix shows up as a test change, not a silent regression.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] SeasonalNaive crashed on the default seasonal_period=1 fallback**
- **Found during:** Task 2, first compile/test pass on the inherited WIP
- **Issue:** Crate `SeasonalNaive::new(p)` requires `p >= 2`; `forecast_seasonal_naive`'s `.max(1)` fallback could produce `p=1`, which the crate rejects (`"seasonal period must be >= 2, got 1"`). Broke `forecast_snapshot`'s `rw`/`inter` series and `test_all_model_names_match_enum`.
- **Fix:** Delegate to crate `Naive` when `p < 2` (bit-identical to the old hand-rolled `p=1` behavior — repeat the last value). `.max(1)` fallback itself untouched.
- **Files modified:** `crates/anofox-fcst-core/src/forecast.rs`
- **Verification:** `cargo test -p anofox-fcst-core --test forecast_snapshot` and the lib test suite both green.
- **Committed in:** `9a1ffe1` (Task 2 commit)

**2. [Rule 3 - Blocking] forecast_mfles_with_exog missing the new `level` argument**
- **Found during:** Task 2, resuming the stalled WIP (this was the exact compile error the prior session stalled on)
- **Issue:** `forecast_mfles` gained a `level: f64` parameter as part of Task 2's `predict_with_intervals` switch; `forecast_mfles_with_exog`'s call site wasn't updated — compile error.
- **Fix:** Pass `0.95` (mirrors `forecast_theta_with_exog`'s existing pattern for the same reason: this function's output always carries empty lower/upper, so the residual-forecast's own bounds are never read).
- **Files modified:** `crates/anofox-fcst-core/src/forecast.rs`
- **Verification:** `cargo check -p anofox-fcst-core --lib` clean.
- **Committed in:** `9a1ffe1` (Task 2 commit)

**3. [Rule 1 - Bug, test-design] Two of the plan's literal numeric must-haves were empirically false for identified, non-regression reasons**
- **Found during:** Task 3, writing `forecast_intervals.rs`
- **Issue (a):** The plan's `z_is_exact_inverse_normal` behavior specified the Naive half-width ratios to 1e-9 relative. Empirically, the crate's z comes from a private Abramowitz & Stegun 26.2.23 approximation (same root cause Task 1 already documented for the absolute interval bounds), so the ratio of two approximated z's is ~8e-5 relative off the exact ratio.
  **Issue (b):** The plan's `no_model_reproduces_fabricated_width` behavior checks every model's half-width against the old fabricated 234.32 constant. ADIDA and IMAPA — both already crate-native before this plan, never part of the fabrication — legitimately produce a 95% h=1 half-width of ~235.18 on AirPassengers, coincidentally within 1% of the unrelated constant.
  **Issue (c):** The plan's `mse_is_crate_residual_mse` "none equals 2090.28" check: SES genuinely is `SimpleExponentialSmoothing::new(0.3)`, so its own residual MSE legitimately equals ~2090.284 — 2090.28 was the OLD universal SES(0.3) stand-in every model funneled through pre-Phase-10, not a fabrication unique to SES.
- **Fix:** Relaxed (a) to 1e-3 relative tolerance; excluded ADIDA/IMAPA from (b); excluded SES from (c) — each with an inline comment in the test file explaining the verified, non-regression reason.
- **Files modified:** `crates/anofox-fcst-core/tests/forecast_intervals.rs`
- **Verification:** All 12 tests in `forecast_intervals.rs` pass; each deviation is independently reproducible (documented exact numeric values from direct crate measurement this session).
- **Committed in:** `5ec2cac` (Task 3 commit)

---

**Total deviations:** 3 auto-fixed (2 Rule 1 bugs, 1 Rule 3 blocking compile error, plus 3 identified non-regression test-tolerance adjustments folded into deviation 3)
**Impact on plan:** All auto-fixes were necessary for correctness (SeasonalNaive) or to unblock compilation (forecast_mfles_with_exog); the test-tolerance deviations are documented, verified-non-regression adjustments to literal numeric claims that turned out to be empirically inaccurate at planning time. No scope creep — no FFI/C++ files touched (left for plan 10-02 as instructed).

## Issues Encountered

- **FFI `ForecastResult.n_fitted` shared-length bug** discovered while writing `parity_garch`/extending `assert_full_parity`: GARCH (always) and AutoARIMA (on this plan's synthetic 60-point seasonal test series) have `fitted_values() == None` while `residuals() == Some(...)`, and the FFI struct's single `n_fitted` field (used for both arrays) truncates the real residuals to length 0 at the FFI/SQL boundary. This is a genuine, pre-existing production bug (also present in `ts_forecast.cpp`/`ts_forecast_agg.cpp`), not something this plan introduced. Documented inline in the test file and here; left unfixed per the explicit instruction that `src/include/anofox_fcst_ffi.h` (and by extension the `ForecastResult` struct it mirrors) is reserved for plan 10-02.

## Known Stubs

None — all deliverables are fully wired; no placeholder/mock data paths were introduced.

## Threat Flags

| Flag | File | Description |
|------|------|--------------|
| threat_flag: information-disclosure-adjacent | `crates/anofox-fcst-ffi/src/types.rs` (`ForecastResult`), `src/table_functions/ts_forecast.cpp`, `src/aggregate_functions/ts_forecast_agg.cpp` | Pre-existing FFI struct design bug: `n_fitted` is shared between `fitted_values` and `residuals`, silently truncating residuals to empty when a crate model's `fitted_values()` is `None` but `residuals()` is `Some` (GARCH always; AutoARIMA on some series). Not a security issue (no secret/cross-tenant data), but a real data-loss correctness gap at the SQL surface — flagging for plan 10-02's scope, which owns the FFI header. |

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- Plan 10-02 can proceed: the `forecast()`/`forecast_with_exog()` crate-passthrough foundation (D-01/D-02/D-03/D-11/D-12) is complete and fully tested; 10-02 inherits the documented `n_fitted`/`residuals` length-sharing gap as its own scope (the FFI header is reserved for it).
- `cargo test --workspace`, `cargo fmt --all -- --check`, and `cargo clippy --all-targets --all-features -- -D warnings` are all green on `HEAD` (`5ec2cac`).
- No blockers for 10-03 (ensemble intervals) or 10-04 (exog model replacement) — both plans were explicitly scoped around this plan's `extract_point_forecast`/wrapper-OLS exog boundaries, which are untouched.

---
*Phase: 10-forecast-wrapper-delegates-to-crate-models*
*Completed: 2026-10-09*

## Self-Check: PASSED

All claimed files found on disk (`forecast.rs`, `core_ffi_parity.rs`, `forecast_intervals.rs`, this SUMMARY) and all claimed commits (`cdf4480`, `9a1ffe1`, `5ec2cac`) found in `git log --oneline --all`.
