---
phase: 16-conformal-bootstrap-correctness
plan: 02
subsystem: forecasting
tags: [conformal-prediction, duckdb, rust-ffi, monte-carlo, statistical-correctness]

# Dependency graph
requires:
  - phase: 16-conformal-bootstrap-correctness
    plan: 01
    provides: "conformal_from_pairs/conformal_calibrate, _ts_conformal_from_pairs/_ts_conformal_calibrate, ts_conformal_by/calibrate/apply_by macros, conformal/split_cases.json fixture family, ts_conformal_reference.test"
provides:
  - "D-05: one finite-sample level helper (finite_sample_level) shared by every split-conformal surface; insufficient (ceil((n+1)(1-alpha))>n) reports an explicit unbounded result (conformity_score=+inf, bounds -inf/+inf, coverage=NaN, calibration_insufficient=true) instead of clamping"
  - "D-06: validate_alpha() rejects alpha outside (0,1) naming the value, on every conformal surface; non-finite residuals filtered via f64::total_cmp (no unwrapped partial_cmp panic path remains in conformal.rs)"
  - "D-07 (CONF-03): conformal_predict_asymmetric/conformal_learn's Asymmetric branch derive both margins from one sorted array of SIGNED residuals and its negation over the FULL finite count, replacing the old positive/negative-subset construction (which over-covered by 5-6pp and was 1.2x-1.7x wider per the new Monte Carlo fixture)"
  - "D-08 (CONF-04): conformal_predict_adaptive(residuals, difficulty_cal, forecasts, difficulty_pred, alpha) takes explicit per-row difficulty at calibration AND prediction with NO batch-mean normalisation; conformal_apply_score(forecasts, score, method, difficulty) applies a single pre-computed score; new FFI anofox_ts_conformal_apply_score; C++ difficulty overloads for _ts_conformal_from_pairs (7-arg), _ts_conformal_calibrate (5-arg), ts_conformal_intervals (4-arg), ts_conformal_learn (5-arg), ts_conformal_apply (3-arg)"
  - "difficulty_col named parameter (default NULL) on ts_conformal_by/ts_conformal_calibrate/ts_conformal_apply_by"
  - "Fixture families: conformal/asymmetric_cases.json, conformal/adaptive_cases.json, conformal/monte_carlo.json (seeded Monte Carlo coverage for symmetric/asymmetric/adaptive x normal/lognormal/heteroscedastic)"
affects: [16-03, 18-test-coverage-sweep]

# Actuals (#2632)
actuals:
  tokens: 78000
  tasks: 3
  commits: 3

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "One private finite_sample_level(n, alpha) helper backs every split-conformal quantile (symmetric, asymmetric x2, adaptive, jackknife+, per-step) -- insufficiency is a reported value (f64::INFINITY / calibration_insufficient flag), never a clamp"
    - "NaN is the missing-value sentinel (Phase 10 D-12 convention) for difficulty too: validate_difficulty_values() treats NaN as 'drop this row' / 'propagate NaN here', and only a PRESENT non-positive or infinite value is an InvalidInput error naming the 1-based position"
    - "FFI boundary converts every non-finite (NaN or +-inf) score/bound to NaN uniformly via nan_if_non_finite(); C++ turns NaN into SQL NULL via WriteDoubleListNanAsNull (lists) or WriteDoubleOrNull/SetNull (scalars) -- one convention, no per-function special-casing"
    - "C++ overload dispatch on args.ColumnCount() within ONE function body (has_difficulty = ColumnCount() == N) instead of duplicating near-identical TsXFunction bodies per overload"
    - "Independent Monte Carlo cross-validation: numpy (PCG64) and Rust (inline SplitMix64 + Box-Muller) run the SAME mathematical simulation with DIFFERENT RNG algorithms, compared via a coverage_abs/mean_width_rel TOLERANCE band, not bit-exact parity"

key-files:
  created:
    - validation/reference/fixtures/conformal/asymmetric_cases.json
    - validation/reference/fixtures/conformal/adaptive_cases.json
    - validation/reference/fixtures/conformal/monte_carlo.json
  modified:
    - crates/anofox-fcst-core/src/conformal.rs
    - crates/anofox-fcst-core/src/lib.rs
    - crates/anofox-fcst-core/tests/conformal_reference.rs
    - crates/anofox-fcst-ffi/src/lib.rs
    - crates/anofox-fcst-ffi/src/types.rs
    - crates/anofox-fcst-ffi/tests/null_policy_ffi.rs
    - examples/conformal_prediction/synthetic_conformal_examples.sql
    - src/include/anofox_fcst_ffi.h
    - src/macros/ts_macros.cpp
    - src/scalar_functions/conformal.cpp
    - test/sql/ts_conformal_null_policy.test
    - test/sql/ts_conformal_reference.test
    - validation/reference/fixtures/conformal/split_cases.json
    - validation/reference/python/gen_conformal_reference.py

key-decisions:
  - "Unbounded representation fixed once: core returns conformity_score=+inf, bounds -inf/+inf, coverage=NaN, calibration_insufficient=true; FFI writes every non-finite value as NaN; C++ writes NaN as SQL NULL. Every new conformal code path reuses this instead of inventing its own NULL convention."
  - "Asymmetric construction changed from positive/negative RESIDUAL SUBSETS (wrong: a model with few negative residuals got an unstable, narrow lower-margin estimate from a tiny subset) to the alpha/2 and 1-alpha/2 quantiles of the FULL signed-residual array and its negation (D-07) -- the textbook-correct construction, confirmed by the new Monte Carlo fixture to hit nominal coverage within 0.2pp vs the old construction's 5-6pp over-coverage."
  - "Adaptive difficulty validation treats NaN as a missing-data sentinel (silently dropped at calibration, propagated as NaN bounds at prediction) and reserves InvalidInput errors for PRESENT bad values (<=0 or infinite) -- this is the SAME Phase 10 D-12 convention the rest of the codebase already uses for missing data, applied consistently to difficulty for the first time."
  - "C++ overloads for the difficulty-aware conformal surfaces dispatch on args.ColumnCount() inside ONE function body rather than duplicating TsXFunction per overload; a length mismatch between a row's difficulty list and its forecast/residual list throws InvalidInputException in C++ BEFORE the FFI call, because the FFI signatures for anofox_ts_conformal_learn/_apply/_predict_adaptive have no independent difficulty-length parameter (they trust the caller for alignment with residuals_length/n_forecasts)."
  - "anofox_ts_conformal_learn/_apply now build (residual-or-forecast, difficulty) via Rust's complete_cases BEFORE dropping any residual, fixing a pre-existing silent-misalignment bug: the old code called drop_missing(residuals) first, then read the difficulty array at the ORIGINAL (pre-drop) indices, silently pairing wrong values after any missing residual."
  - "Rust's Monte Carlo tests use an inline SplitMix64 PRNG with Box-Muller normals (the plan forbids a new rand crate dependency) and are compared against numpy's independent PCG64 simulation via a tolerance band (coverage_abs 0.02, mean_width_rel 0.05), not RNG-exact parity -- the two implementations necessarily draw different sequences even from the 'same' seed number."

requirements-completed: [CONF-02, CONF-03, CONF-04]

coverage:
  - id: D1
    description: "Split conformal (symmetric) never clamps an insufficient calibration set to the max residual; reports conformity_score/coverage/bounds as NULL plus calibration_insufficient=true, verified against a numpy threshold table over n=0..60 x 7 alphas"
    requirement: "CONF-02"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/conformal_reference.rs#threshold_table_matches_numpy_for_every_n_and_alpha"
        status: pass
      - kind: integration
        ref: "test/sql/ts_conformal_reference.test#16-02 split insufficiency / alpha / non-finite"
        status: pass
    human_judgment: false
  - id: D2
    description: "alpha outside (0,1) raises an SQL error naming the value on every conformal surface (ts_conformal_by/calibrate/quantile/predict/predict_asymmetric/learn/evaluate/predict_per_step)"
    requirement: "CONF-02"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/conformal.rs#test_validate_alpha_rejects_boundary_and_nan"
        status: pass
      - kind: integration
        ref: "test/sql/ts_conformal_reference.test (statement error blocks, 'got 0')"
        status: pass
    human_judgment: false
  - id: D3
    description: "Non-finite (NaN/+-inf) residuals are filtered before every quantile; the unwrapped partial_cmp sort is gone from conformal.rs (grep gate: 0 occurrences)"
    requirement: "CONF-02"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/conformal.rs#test_conformal_quantile_filters_non_finite_without_panicking"
        status: pass
      - kind: other
        ref: "grep -c 'partial_cmp(b).unwrap()' crates/anofox-fcst-core/src/conformal.rs == 0"
        status: pass
    human_judgment: false
  - id: D4
    description: "Asymmetric conformal uses the alpha/2 and 1-alpha/2 finite-sample quantiles of the FULL signed-residual array (and its negation), not a positive/negative subset; Monte Carlo coverage within 0.2pp of nominal for normal and log-normal residuals at alpha 0.1/0.2"
    requirement: "CONF-03"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/conformal_reference.rs#monte_carlo_symmetric_and_asymmetric_coverage_matches_numpy"
        status: pass
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/conformal_reference.rs#asymmetric_hand_checked_cases_match_numpy"
        status: pass
    human_judgment: false
  - id: D5
    description: "Adaptive conformal takes explicit per-row difficulty at calibration AND prediction (scores |r|/d, bounds point +/- q*d) with NO batch-mean normalisation -- a forecast's interval is independent of its batch, verified by exact equality"
    requirement: "CONF-04"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/conformal_reference.rs#adaptive_batch_independence_exact_equality"
        status: pass
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/conformal_reference.rs#adaptive_n20_matches_numpy"
        status: pass
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/conformal_reference.rs#adaptive_heteroscedastic_monte_carlo_coverage_matches_numpy"
        status: pass
      - kind: integration
        ref: "test/sql/ts_conformal_reference.test#16-02 adaptive"
        status: pass
    human_judgment: false
  - id: D6
    description: "Adaptive is reachable from SQL end-to-end: ts_conformal_by/ts_conformal_calibrate/ts_conformal_apply_by with difficulty_col, plus the 4 list overloads (ts_conformal_intervals, ts_conformal_learn, ts_conformal_apply, and the internal from-pairs/calibrate functions)"
    requirement: "CONF-04"
    verification:
      - kind: integration
        ref: "test/sql/ts_conformal_reference.test#16-02 adaptive (ts_conformal_by, ts_conformal_calibrate, ts_conformal_apply_by batch independence, ts_conformal_intervals 4-arg, ts_conformal_learn/apply overloads)"
        status: pass
      - kind: e2e
        ref: "examples/conformal_prediction/synthetic_conformal_examples.sql Section 7 (duckdb -bail, exit 0)"
        status: pass
    human_judgment: false
  - id: D7
    description: "Edge cases are errors or NULL rows, never crashes: adaptive without difficulty errors naming 'difficulty'; a non-adaptive method with present difficulty errors 'only used by method adaptive'; a non-finite/non-positive difficulty value errors naming the value and position; a NULL calibration difficulty drops that row; a NULL prediction difficulty gives NULL bounds at that position only"
    requirement: "CONF-04"
    verification:
      - kind: integration
        ref: "test/sql/ts_conformal_reference.test (statement error blocks for adaptive/difficulty)"
        status: pass
      - kind: unit
        ref: "crates/anofox-fcst-core/src/conformal.rs#test_conformal_apply_score_adaptive_missing_difficulty_at_present_forecast_is_nan"
        status: pass
    human_judgment: false
  - id: D8
    description: "Full DoD: cargo fmt/clippy/test --workspace clean; make release; make test_release assertion count rises (2118 -> 2166); all 4 affected examples exit 0; regenerated FFI header committed"
    verification:
      - kind: unit
        ref: "cargo test --workspace (all green)"
        status: pass
      - kind: integration
        ref: "make test_release (2166 assertions vs 2118 Task-1 baseline)"
        status: pass
      - kind: e2e
        ref: "4 examples via duckdb -bail, all exit 0"
        status: pass
    human_judgment: false

duration: 69min
completed: 2026-10-14
status: complete
---

# Phase 16 Plan 02: Split/Asymmetric/Adaptive Conformal Correctness Summary

**Replaced the quantile-level clamp with an explicit unbounded result everywhere, rebuilt asymmetric conformal from signed-residual quantiles instead of a positive/negative subset (D-07), and made adaptive conformal take explicit per-row difficulty with no batch-mean normalisation (D-08) -- all three verified against independent numpy fixtures including seeded Monte Carlo coverage simulations.**

## Performance

- **Duration:** 69 min
- **Started:** 2026-10-14T10:22:33Z
- **Completed:** 2026-10-14T11:31:13Z
- **Tasks:** 3 completed
- **Files modified:** 17 (3 created, 14 modified)

## Accomplishments

- **D-05 (CONF-02):** one private `finite_sample_level(n, alpha)` helper now backs every split-conformal quantile in `conformal.rs` (symmetric, asymmetric x2, adaptive, jackknife+, per-step). When `ceil((n+1)(1-alpha)) > n`, the core reports `conformity_score = f64::INFINITY`, `lower`/`upper = -inf/+inf`, `coverage = NaN`, and a new `calibration_insufficient: bool` flag -- never the old clamp to the maximum observed residual. Verified against a numpy threshold table over `n = 0..60` x 7 alpha values, with every `(n, alpha)` combination agreeing between Rust and numpy.
- **D-06 (CONF-02):** `validate_alpha()` rejects alpha outside `(0, 1)` naming the value (`"alpha must be in (0, 1), got {alpha}"`) on all 8 conformal surfaces that take one (`ts_conformal_by/calibrate/quantile/predict/predict_asymmetric/learn/evaluate/predict_per_step`). Non-finite residuals are filtered via `f64::total_cmp` before any sort; the unwrapped `partial_cmp(b).unwrap()` panic path is gone from `conformal.rs` (grep gate: 0 occurrences).
- **D-07 (CONF-03):** `conformal_predict_asymmetric` and `conformal_learn`'s Asymmetric branch now derive BOTH margins from one sorted array of the FULL signed residuals and its negation (`upper = quantile(r, k/n)`, `lower = quantile(-r, k/n)`), replacing the old positive/negative-SUBSET construction. A 2000-trial seeded Monte Carlo simulation (independent numpy + an inline Rust SplitMix64/Box-Muller generator) confirms the new construction hits nominal coverage within ~0.2pp for both normal and log-normal residuals at alpha 0.1/0.2, while the OLD construction (also simulated, for CHANGELOG documentation) over-covered by 5-6pp and was 1.2x-1.7x wider.
- **D-08 (CONF-04):** `conformal_predict_adaptive(residuals, difficulty_cal, forecasts, difficulty_pred, alpha)` takes explicit per-row difficulty at BOTH calibration and prediction; scores are `|r_i| / d_i`; bounds are `f_j -/+ q * d_j` using the prediction difficulty EXACTLY AS GIVEN. The pre-16-02 batch-mean normalisation (G14: a forecast's interval depended on which other forecasts were in the same batch) is gone, proven by an exact-equality batch-independence test. A heteroscedastic Monte Carlo simulation (`r = d * N(0,1)`, `d ~ U(0.5, 3)`, seed 16103) confirms ~0.1pp coverage error at both alpha levels.
- Adaptive is now reachable end-to-end from SQL: `ts_conformal_by`/`ts_conformal_calibrate`/`ts_conformal_apply_by` gain a `difficulty_col` named parameter (default `NULL`); new C++ overloads for `_ts_conformal_from_pairs` (7-arg), `_ts_conformal_calibrate` (5-arg), `ts_conformal_intervals` (4-arg, backed by a new `conformal_apply_score` core function and `anofox_ts_conformal_apply_score` FFI entry point), `ts_conformal_learn` (5-arg), and `ts_conformal_apply` (3-arg).
- Fixed a pre-existing silent-misalignment bug in `anofox_ts_conformal_learn`/`anofox_ts_conformal_apply`: the old code called `drop_missing(residuals)` BEFORE reading the difficulty array, so any missing residual silently shifted every subsequent (residual, difficulty) pairing. Both now use Rust's `complete_cases` to filter residual AND difficulty together before anything is dropped.
- `test/sql/ts_conformal_reference.test` grew from 36 (Phase 16-01 baseline) to 83 assertions across this plan's 3 tasks; `make test_release` rose from 2118 (this plan's starting baseline) to 2166 assertions.

## Task Commits

Each task was committed atomically:

1. **Task 1: Tracer -- finite-sample insufficiency, alpha validation, non-finite filtering end-to-end** -- `784d71c` (feat)
2. **Task 2: Signed-residual asymmetric conformal with Monte Carlo coverage; level helper everywhere; per-step D-05+G16; null-policy tests kept intent-true** -- `3915f9e` (feat)
3. **Task 3: Adaptive conformal with explicit calibration/prediction difficulty, reachable from SQL; full DoD** -- `f2b4e25` (feat)

**Plan metadata:** this SUMMARY (orchestrator-owned STATE.md/ROADMAP.md update, not committed by this executor per its instructions).

## Files Created/Modified

- `crates/anofox-fcst-core/src/conformal.rs` -- `validate_alpha`, `finite_sample_level`, `validate_difficulty_values`; rewritten `conformal_predict_asymmetric`/`conformal_predict_adaptive`; new `conformal_apply_score`; `conformal_from_pairs`/`conformal_calibrate` gain difficulty params; `conformal_learn`/`conformal_apply`/per-step lose every clamp and the batch-mean normalisation
- `crates/anofox-fcst-core/src/lib.rs` -- re-export `conformal_apply_score`
- `crates/anofox-fcst-core/tests/conformal_reference.rs` -- threshold-table, insufficiency, asymmetric, adaptive parity tests; inline `SplitMix64` generator; 3 Monte Carlo coverage tests
- `crates/anofox-fcst-ffi/src/lib.rs` -- `calibration_insufficient` fields; `nan_if_non_finite`; every remaining conformal entry point through `ffi_error_code`; difficulty params on `_predict_adaptive`/`_learn`/`_apply`/`_from_pairs`/`_calibrate`; new `anofox_ts_conformal_apply_score`
- `crates/anofox-fcst-ffi/src/types.rs` -- `calibration_insufficient` on `ConformalResultFFI`/`ConformalCalibrationFFI`
- `crates/anofox-fcst-ffi/tests/null_policy_ffi.rs` -- 3 pre-existing alpha-0.1-now-insufficient assertions moved to a sufficient alpha, each gaining a "stays finite" check; 3 new alpha/insufficiency FFI tests
- `examples/conformal_prediction/synthetic_conformal_examples.sql` -- Section 7 (adaptive) and Section 8 (insufficiency)
- `src/include/anofox_fcst_ffi.h` -- regenerated by cbindgen across all 3 task commits
- `src/macros/ts_macros.cpp` -- `calibration_insufficient` + `difficulty_col` on all 3 conformal macros
- `src/scalar_functions/conformal.cpp` -- `InvalidInputException` on every conformal scalar; `WriteDoubleOrNull`/`FillNanAtInvalid` helpers; difficulty overloads dispatched via `args.ColumnCount()`
- `test/sql/ts_conformal_null_policy.test` -- 1 assertion's alpha moved to a sufficient level
- `test/sql/ts_conformal_reference.test` -- 3 new sections (split insufficiency, asymmetric/per-step/errors, adaptive); 83 assertions total
- `validation/reference/fixtures/conformal/{split_cases,asymmetric_cases (new),adaptive_cases (new),monte_carlo (new)}.json`
- `validation/reference/python/gen_conformal_reference.py` -- generators for all 4 fixture files

## Decisions Made

See `key-decisions` in frontmatter. Most notable: the asymmetric construction change (positive/negative subset -> full signed-residual quantiles) and the difficulty-as-missing-sentinel convention are both verified by independent Monte Carlo simulation, not just unit-level numeric spot checks.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Stray leftover doc comment broke `cargo clippy -D warnings`**
- **Found during:** Task 3's full-DoD `cargo clippy --all-targets --all-features -- -D warnings` run
- **Issue:** Replacing `conformal_predict_adaptive`'s doc comment left the OLD doc comment's trailing bulleted list (`- Residual variance estimates...` etc.) directly adjacent to the NEW `validate_difficulty_values` function's doc comment with no blank line between them, which `clippy::doc_lazy_continuation` flagged as an un-indented list continuation.
- **Fix:** Removed the stray 21-line leftover doc comment block.
- **Files modified:** `crates/anofox-fcst-core/src/conformal.rs`
- **Verification:** `cargo clippy --all-targets --all-features -- -D warnings` exits clean; `cargo test --workspace` stays green.
- **Committed in:** `f2b4e25` (Task 3 commit)

**2. [Rule 1 - Bug] NaN silently passed the pre-existing difficulty positivity check**
- **Found during:** Task 3, while auditing `conformal_learn`'s and `conformal_apply`'s existing `d.iter().any(|&x| x <= 0.0)` validation for the new NaN-as-missing convention
- **Issue:** `NaN <= 0.0` evaluates to `false` in Rust (all comparisons with NaN are false), so a NaN difficulty value silently passed this "must be positive" check instead of being rejected OR intentionally treated as missing -- a latent correctness gap unrelated to difficulty's new semantics but directly adjacent to the code this plan touches.
- **Fix:** Replaced with `!(x > 0.0)` (which correctly catches NaN) in the two pre-existing call sites, then immediately superseded by `validate_difficulty_values()` which explicitly treats NaN as the missing-value sentinel (skip) and anything else non-positive/infinite as an error.
- **Files modified:** `crates/anofox-fcst-core/src/conformal.rs`
- **Verification:** `cargo test --workspace` all green; new unit tests exercise NaN difficulty explicitly.
- **Committed in:** `f2b4e25` (Task 3 commit)

---

**Total deviations:** 2 auto-fixed (1 clippy-blocking, 1 latent-bug). Both discovered while directly implementing this plan's own scope; neither required touching unrelated files.

## Issues Encountered

None beyond the two deviations above.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Plan 16-03 (bootstrap sign, jackknife+ removal, docs/CHANGELOG/skill update) can build directly on this plan's `calibration_insufficient` convention, the `conformal/{asymmetric_cases,adaptive_cases,monte_carlo}.json` fixture families, and the "Phase 16 before-values" table recorded in `16-01-SUMMARY.md` -- the old/new asymmetric width ratio this plan's `monte_carlo.json` recorded (`old_over_new_width_ratio`, 1.2x-1.7x) is ready to cite in the CHANGELOG.
- `conformal_reference.rs` and `test/sql/ts_conformal_reference.test` are structured for 16-03 to extend further (bootstrap cases go in the SAME `conformal` family's sibling `bootstrap` fixtures per the phase's existing convention).
- No blockers.

## Self-Check: PASSED

- `crates/anofox-fcst-core/tests/conformal_reference.rs` exists: FOUND
- `validation/reference/fixtures/conformal/asymmetric_cases.json` exists: FOUND
- `validation/reference/fixtures/conformal/adaptive_cases.json` exists: FOUND
- `validation/reference/fixtures/conformal/monte_carlo.json` exists: FOUND
- Commit `784d71c` exists in git log: FOUND
- Commit `3915f9e` exists in git log: FOUND
- Commit `f2b4e25` exists in git log: FOUND
- `cargo fmt --all -- --check` / `cargo clippy --all-targets --all-features -- -D warnings` / `cargo test --workspace`: all clean (confirmed after Task 3's clippy fix)
- `make test_release` reports 2166 assertions (> 2118 Task-1 baseline): CONFIRMED
- `test/sql/ts_conformal_reference.test` reports 83 assertions, all passing: CONFIRMED
- All 4 examples (`synthetic_conformal_examples.sql`, `ensemble_intervals.sql`, `synthetic_metrics_examples.sql`, `null_handling_examples.sql`) exit 0 with `-bail`: CONFIRMED
- `src/include/anofox_fcst_ffi.h` committed (git diff clean against HEAD): CONFIRMED

---
*Phase: 16-conformal-bootstrap-correctness*
*Completed: 2026-10-14*
