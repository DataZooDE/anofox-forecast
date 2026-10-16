---
phase: 12-statistics-marshalling-moves-to-rust
plan: 04
subsystem: api
tags: [rust, ffi, duckdb, conformal, bootstrap, null-handling, cpp]

requires:
  - phase: 12-statistics-marshalling-moves-to-rust
    plan: 01
    provides: "src/include/ts_list_marshal.hpp's ListDoubleReader/ScalarArgReader/
      WriteDoubleList(NanAsNull) and crates/anofox-fcst-core/src/missing.rs's
      complete_cases/drop_missing — this plan adds propagate_missing/fill_missing
      and is the first consumer of the full D-01 reader on the conformal/bootstrap
      scalar surface"

provides:
  - "crates/anofox-fcst-core/src/missing.rs: propagate_missing (position-preserving
    NaN punch-back) and fill_missing (0.0-placeholder vector) — the forecast-position
    policy class, reused by conformal_predict/predict_asymmetric/apply/intervals and
    both bootstrap functions"
  - "crates/anofox-fcst-core/src/conformal.rs: conformal_predict_per_step_validated —
    Rust validates horizon>0, equal lengths, and residual-count % horizon == 0 (D-10,
    no silent n_folds truncation), computes per-step half-widths pairwise-complete
    (full steps bitwise-identical to the existing crate-backed function; steps with a
    missing pair use the crate's public fit().quantile_value() with a pooled-fallback
    below 2 remaining pairs), and is position-preserving for point forecasts"
  - "Every conformal/bootstrap FFI (quantile, intervals, predict, predict_asymmetric,
    learn, apply, coverage, evaluate, mean_interval_width, predict_per_step,
    bootstrap_intervals, bootstrap_quantiles) takes validity bitmasks and applies the
    policy class from the objective (drop_missing for residual pools, complete_cases
    for paired evaluation inputs, fill_missing+propagate_missing for forecast
    positions)"
  - "conformal.cpp/bootstrap.cpp: every function reads through the shared
    ListDoubleReader/ScalarArgReader (D-01) instead of the private
    ExtractListAsDouble/WriteListFromArray copies (both deleted); parameter lists
    (ts_conformal_learn alphas, ts_bootstrap_quantiles quantile_levels) use
    ReadNoNulls; ts_conformal_apply flattens its STRUCT profile argument before
    reading child vectors (T-12-15)"
  - "test/sql/ts_conformal_null_policy.test (14 non-json-gated assertions) + phase
    example SECTION 4 proving D-10 and the conformal/bootstrap NULL policy end-to-end
    against the built extension"
affects: [12-05, 12-06, 12-07, 16]

actuals:
  tokens: 30800
  tasks: 3
  commits: 2
  plan_head_before: 96c1c5b6ac43a935973b338a6ed0fcda43bfd76a

tech-stack:
  added: []
  patterns:
    - "missing::propagate_missing / missing::fill_missing: the forecast-position
      policy class — compute on a 0.0-placeholder vector, then punch NaN back into
      every output array at exactly the positions missing in the original input.
      Mirrors complete_cases (paired) and drop_missing (residual pool) as the third
      Rust-owned missing-data policy."
    - "Local replica of a crate-private quantile formula (split_quantile in
      conformal.rs) used only on FULL (no-missing) per-step data so the result is
      bitwise identical to the existing crate-backed function; the crate's public
      fit().quantile_value() is used only when a step actually has a missing pair —
      the two paths produce different numbers by design (fit_split subsets to a
      cal_fraction calibration window; the full-step replica does not), and only the
      full-step path is required to match the pre-change behavior."

key-files:
  created:
    - test/sql/ts_conformal_null_policy.test
  modified:
    - crates/anofox-fcst-core/src/missing.rs
    - crates/anofox-fcst-core/src/lib.rs
    - crates/anofox-fcst-core/src/conformal.rs
    - crates/anofox-fcst-ffi/src/lib.rs
    - crates/anofox-fcst-ffi/tests/null_policy_ffi.rs
    - src/include/anofox_fcst_ffi.h
    - src/scalar_functions/conformal.cpp
    - src/scalar_functions/bootstrap.cpp
    - examples/data_preparation/null_handling_examples.sql
    - docs/api/11-conformal-prediction.md
    - plugins/anofox-forecast/skills/anofox-forecast-backtest/SKILL.md
    - CHANGELOG.md
    - .planning/phases/12-statistics-marshalling-moves-to-rust/snippets/expected_errors.txt
    - .planning/phases/12-statistics-marshalling-moves-to-rust/snippets/pass-list-12-04.tsv

key-decisions:
  - "Merged Tasks 1 and 2 into a single commit (1c535c1) instead of two separate task
    commits. conformal.cpp's private ExtractListAsDouble helper (deleted in this
    plan) is used by every function in the file, not just the per-step scalar Task 1
    targets — removing it without updating every call site in the same file would
    leave the file non-compiling between commits. Splitting the single-pass port
    into a Task-1-shaped commit (per-step only) followed by a Task-2-shaped commit
    (everything else) would have required either re-adding ExtractListAsDouble
    temporarily or leaving the Task 1 commit non-compiling — both worse than one
    clearly-scoped commit covering the full marshalling-layer port. conformal.rs,
    missing.rs, lib.rs, the header, and null_policy_ffi.rs are the same files either
    way. Task 3 (build + SQL proof + docs) remains its own commit (050fd29)."
  - "Per-step pairwise calibration uses TWO different quantile computations by
    design, chosen per-step rather than globally: a step with NO missing pair (full
    n_folds) replicates the crate's private compute_quantile formula locally
    (split_quantile) so it is bitwise identical to the pre-change result for that
    step; a step WITH a missing pair uses the crate's public
    ConformalPredictor::fit(...).quantile_value() over the remaining complete pairs
    (and falls back to a pooled-complete-pairs fit() when fewer than 2 remain). This
    was verified empirically: fit()'s Split method subsets to a cal_fraction=0.2
    calibration window internally, which does NOT reduce to the full-data formula
    even when given the complete step's own data — so routing ALL steps through
    fit() once ANY pair is missing anywhere would have broken the plan's own
    required invariant that complete steps stay identical to the pre-change result."
  - "A non-positive horizon argument (<=0) is folded to 0 in C++ before the FFI call
    (not just horizon==0) so it reaches Rust's single 'horizon must be positive'
    error uniformly — casting a negative int32 directly to size_t would otherwise
    produce a huge unsigned value and a confusing 'not a multiple of horizon'
    message instead."

requirements-completed: [RUST-01, RUST-02, RUST-09]

coverage:
  - id: D1
    description: "ts_conformal_predict_per_step given a fold residual count that is
      not a multiple of horizon raises an SQL error naming both counts, decided in
      Rust — no silent truncation of n_folds (D-10)"
    requirement: RUST-09
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/conformal.rs#per_step_pairwise_tests::residual_count_not_multiple_of_horizon_errors_naming_both"
        status: pass
      - kind: unit
        ref: "crates/anofox-fcst-ffi/tests/null_policy_ffi.rs#per_step_residual_count_not_multiple_of_horizon_is_invalid_input"
        status: pass
      - kind: integration
        ref: "test/sql/ts_conformal_null_policy.test (D-10 statement error assertion, '7 is not a multiple of horizon 3')"
        status: pass
      - kind: e2e
        ref: "examples/data_preparation/null_handling_examples.sql (SECTION 4, PASS per_step_residual_count_not_multiple_of_horizon_errors)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Paired conformal inputs (ts_conformal_coverage, ts_mean_interval_width)
      use pairwise-complete observations in Rust; per-step calibration drops a
      (forecast, actual) pair only from its own step"
    requirement: RUST-02
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-ffi/tests/null_policy_ffi.rs#conformal_coverage_drops_incomplete_pairs, #mean_interval_width_drops_incomplete_pairs"
        status: pass
      - kind: unit
        ref: "crates/anofox-fcst-core/src/conformal.rs#per_step_pairwise_tests::one_missing_pair_leaves_other_steps_identical_to_complete_result"
        status: pass
      - kind: integration
        ref: "test/sql/ts_conformal_null_policy.test (coverage=0.5, mean_interval_width=1.5, per-step pairwise assertions)"
        status: pass
    human_judgment: false
  - id: D3
    description: "Residual pools (conformal quantile/predict/asymmetric/learn,
      bootstrap) drop NULL residuals in Rust; a NULL residual gives the same result
      as the complete list, and bootstrap with the same seed gives identical
      intervals with or without the NULL residual"
    requirement: RUST-02
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-ffi/tests/null_policy_ffi.rs#conformal_quantile_with_null_matches_complete_data, #conformal_predict_residual_null_matches_complete_residuals, #bootstrap_intervals_null_forecast_is_position_preserving_and_null_residual_matches"
        status: pass
      - kind: integration
        ref: "test/sql/ts_conformal_null_policy.test (ts_conformal_quantile NULL==complete assertion)"
        status: pass
    human_judgment: false
  - id: D4
    description: "A NULL point forecast yields NULL point/lower/upper at that
      position only (position-preserving) for ts_conformal_intervals,
      ts_conformal_predict(_asymmetric), ts_conformal_apply,
      ts_conformal_predict_per_step and both bootstrap functions — no splicing that
      shifts later forecasts onto earlier horizon steps"
    requirement: RUST-02
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/conformal.rs#per_step_pairwise_tests::null_point_forecast_is_position_preserving"
        status: pass
      - kind: unit
        ref: "crates/anofox-fcst-ffi/tests/null_policy_ffi.rs#per_step_null_point_forecast_yields_nan_at_that_position_only, #conformal_intervals_null_forecast_is_position_preserving"
        status: pass
      - kind: integration
        ref: "test/sql/ts_conformal_null_policy.test (ts_conformal_predict point[2]/lower[2]/upper[2] IS NULL; per-step point[2] IS NULL; bootstrap lower[2] IS NULL assertions)"
        status: pass
    human_judgment: false
  - id: D5
    description: "Every list and scalar argument of the conformal/bootstrap scalars
      is read per row through the shared readers (constant/dictionary safe); NULL
      entries in parameter lists (alphas, quantile levels, calibration-profile
      arrays) are an error naming the parameter"
    requirement: RUST-01
    verification:
      - kind: unit
        ref: "grep -c 'static void ExtractListAsDouble' src/scalar_functions/conformal.cpp src/scalar_functions/bootstrap.cpp == 0"
        status: pass
      - kind: integration
        ref: "test/sql/ts_conformal_null_policy.test (constant-list-per-row count(*)=4 assertion; ts_bootstrap_quantiles NULL-in-quantile-levels statement error)"
        status: pass
    human_judgment: false

duration: ~75min
completed: 2026-10-10
status: complete
---

# Phase 12 Plan 04: Conformal/Bootstrap Scalars on the Shared Reader with Rust-Owned NULL Policy Summary

**Every conformal/bootstrap scalar function now marshals through the shared ListDoubleReader/ScalarArgReader instead of a private per-file list helper, and per-step conformal validates its residual count against horizon in Rust instead of silently truncating the fold count.**

## Performance

- **Duration:** ~75 min
- **Tasks:** 3 planned, delivered as 2 commits (Tasks 1+2 merged — see Deviations)
- **Files modified/created:** 15 (across 2 task commits; no separate metadata commit per this run's instructions)

## Accomplishments

- Added `propagate_missing`/`fill_missing` to `crates/anofox-fcst-core/src/missing.rs`
  — the third Rust-owned missing-data policy class (forecast-position,
  position-preserving) alongside `complete_cases` (paired) and `drop_missing`
  (residual pool) from plan 12-01.
- Built `conformal_predict_per_step_validated` in `conformal.rs`: Rust validates
  `horizon > 0`, equal fold_forecasts/fold_actuals lengths, and
  `residual_count % horizon == 0` (D-10) before any slicing — the per-step scalar's
  `n_folds = fold_forecasts.size() / horizon` C++ computation (the source of the
  silent-truncation bug) is gone. Per-step half-widths are pairwise-complete: a step
  with no missing pair reproduces the existing crate-backed function's result
  bitwise; a step with a missing pair uses the crate's public
  `ConformalPredictor::fit(...).quantile_value()` over the remaining pairs, falling
  back to a pooled-complete-pairs fit when fewer than 2 remain.
- Extended all 12 conformal/bootstrap FFIs (`anofox_ts_conformal_quantile/intervals/
  predict/predict_asymmetric/learn/apply/coverage/evaluate`,
  `anofox_ts_mean_interval_width`, `anofox_ts_conformal_predict_per_step`,
  `anofox_ts_bootstrap_intervals/quantiles`) with validity bitmasks and the correct
  policy class per the objective's four-class taxonomy.
- Ported every function in `conformal.cpp` (9 scalars) and `bootstrap.cpp` (2
  scalars) to the shared `ListDoubleReader`/`ScalarArgReader`/
  `WriteDoubleList(NanAsNull)` header, deleting both files' private
  `ExtractListAsDouble`/`WriteListFromArray` copies; `ts_conformal_apply` now calls
  `profile_vec.Flatten(count)` before reading its STRUCT profile's child vectors
  (T-12-15); `ts_conformal_learn`'s `alphas` and `ts_bootstrap_quantiles`'s
  `quantile_levels` reject a `NULL` entry via `ReadNoNulls`.
- Proved the whole chain natively: `test/sql/ts_conformal_null_policy.test` (14
  assertions, no `require json` gate) covers residual-pool NULL-drop, paired
  coverage/width, forecast-position preservation (intervals, predict, per-step,
  bootstrap), the D-10 per-step horizon-mismatch error, parameter-list NULL
  rejection, and constant-list-per-row correctness. The phase example's new
  SECTION 4 proves three of these end-to-end against the built extension via
  `duckdb -unsigned`.
- Updated `docs/api/11-conformal-prediction.md` with a "NULL handling" subsection
  describing the four policy classes and the per-step residual-count rule (two
  runnable examples), the backtest skill's conformal section with a NULL-policy
  gotcha bullet, and the CHANGELOG (two Breaking-changes bullets: D-10 validation,
  position-preserving/pairwise-complete NULL handling).

## Task Commits

1. **Tasks 1+2 (merged): per-step D-10 validation + every conformal/bootstrap FFI and
   C++ scalar on the shared reader** - `1c535c1` (feat)
2. **Task 3: build once, SQL proof, example section, conformal docs + backtest skill
   + CHANGELOG** - `050fd29` (docs)

## Files Created/Modified

- `crates/anofox-fcst-core/src/missing.rs` - `propagate_missing`/`fill_missing` + tests
- `crates/anofox-fcst-core/src/lib.rs` - re-export the two new functions
- `crates/anofox-fcst-core/src/conformal.rs` - `conformal_predict_per_step_validated`,
  `split_quantile` local replica, 5 new unit tests
- `crates/anofox-fcst-ffi/src/lib.rs` - 12 FFIs extended with validity + policy class
- `crates/anofox-fcst-ffi/tests/null_policy_ffi.rs` - 10 new FFI-ABI tests (23 total)
- `src/include/anofox_fcst_ffi.h` - regenerated header (cbindgen)
- `src/scalar_functions/conformal.cpp` - all 9 scalars ported, helper deleted
- `src/scalar_functions/bootstrap.cpp` - both scalars ported, helpers deleted
- `test/sql/ts_conformal_null_policy.test` - 14 non-json-gated value assertions
- `examples/data_preparation/null_handling_examples.sql` - SECTION 4 appended
- `docs/api/11-conformal-prediction.md` - "NULL handling" subsection
- `plugins/anofox-forecast/skills/anofox-forecast-backtest/SKILL.md` - gotcha bullet
- `CHANGELOG.md` - 2 Breaking-changes bullets
- `.planning/phases/12-.../snippets/expected_errors.txt`, `pass-list-12-04.tsv` -
  snippet harness bookkeeping

## Decisions Made

- Merged Tasks 1 and 2 into one commit (see Deviations) — the shared private helper
  in `conformal.cpp` required a single-pass port of every function in the file.
- Per-step pairwise calibration deliberately uses two different quantile formulas
  depending on whether a given step has a missing pair (see Deviations/key-decisions
  in frontmatter for the empirical reasoning).
- A non-positive horizon is folded to 0 in C++ before the FFI call so both `0` and
  negative inputs hit the same Rust error message.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] conformal.cpp/bootstrap.cpp's shared private `ExtractListAsDouble` forced a whole-file port, not just the per-step function**
- **Found during:** Task 1 (deleting the per-step scalar's private helper usage)
- **Issue:** The plan's Task 1 file list named `src/scalar_functions/conformal.cpp`
  for the per-step function only, with the remaining 8 functions in that file
  slated for Task 2. `ExtractListAsDouble` is a single private function used by
  all 9 functions in the file (and `bootstrap.cpp`'s equivalent by both of its
  functions) — removing/updating it for only the per-step call site while leaving
  the other 8 still calling it would either require keeping the old helper
  temporarily (contradicting the plan's explicit "delete the private list helper"
  instruction) or leave the file non-compiling between two commits.
- **Fix:** Ported every function in both files to the shared `ListDoubleReader`/
  `ScalarArgReader` reader in one pass, matching Task 2's action items, and
  committed the full Rust FFI + C++ marshalling-layer change (Tasks 1+2's combined
  scope) as a single commit.
- **Files modified:** `src/scalar_functions/conformal.cpp`,
  `src/scalar_functions/bootstrap.cpp`, `crates/anofox-fcst-ffi/src/lib.rs`,
  `crates/anofox-fcst-core/src/{missing,conformal}.rs`,
  `crates/anofox-fcst-ffi/tests/null_policy_ffi.rs`, `src/include/anofox_fcst_ffi.h`.
- **Verification:** `cargo test --workspace` (260+ tests), `cargo clippy -D warnings`,
  `cargo fmt --check` all green; both `.cpp` files compile clean
  (`ninja ... conformal.cpp.o bootstrap.cpp.o`); all Task 1/2 acceptance-criteria
  greps pass (0 `ExtractListAsDouble`, 0 `FlatVector::IsNull` on the touched
  variable names, `Flatten(count)` present, validity-param counts match).
- **Committed in:** `1c535c1`.

---

**Total deviations:** 1 auto-fixed (Rule 3, blocking — a file-structure constraint,
not a correctness bug). No scope creep: every change was already required by the
plan's own Task 2 action items; only the task-boundary commit split changed.

## Issues Encountered

- Disk usage peaked at 99% (5.9-7.8 GB free) during the full `make release` build;
  the build and `make test_release` both completed successfully without running out
  of space, and `build/release` was reused throughout (no `make clean`).
- Verified empirically (not assumed) that `ConformalPredictor::fit()`'s `Split`
  method does NOT reduce to the crate's private `compute_quantile` formula when
  given the same data — `fit_split` subsets to the last `ceil(n * 0.2)` scores as a
  calibration window, while `compute_quantile` (used internally by
  `fit_per_step` for complete per-step data) uses the full score set directly. This
  is why the pairwise-missing path and the full-step path in
  `conformal_predict_per_step_validated` deliberately use two different quantile
  computations — confirmed via the `complete_data_matches_crate_backed_function` and
  `one_missing_pair_leaves_other_steps_identical_to_complete_result` unit tests,
  both passing.
- No concurrent-executor interference this run (unlike 12-01/12-02/12-03): the
  plan-commit ledger measured exactly 2 commits between
  `96c1c5b6ac43a935973b338a6ed0fcda43bfd76a` and `HEAD`, both this plan's own.

## Next Phase Readiness

- `missing::propagate_missing`/`fill_missing` are ready for reuse by any later plan
  needing position-preserving forecast-array NULL handling.
- Phase 16 (CONF-01..05, RUST-04) inherits a marshalling-correct conformal scalar
  surface: shared list reader and pairwise-complete validity are already in place
  for every conformal/bootstrap function — Phase 16 can focus purely on interval
  formulas, quantile definitions, and horizon ordering in the macros (explicitly out
  of scope here per the plan's objective).
- Per this run's instructions, `.planning/STATE.md` / `.planning/ROADMAP.md` /
  `.planning/REQUIREMENTS.md` were intentionally NOT touched (concurrent Phase 11
  executor owns those files in this shared working tree) — the orchestrator is
  responsible for folding `requirements-completed: [RUST-01, RUST-02, RUST-09]` into
  those files.
- No blockers for plan 12-05.

---
*Phase: 12-statistics-marshalling-moves-to-rust*
*Completed: 2026-10-10*

## Self-Check: PASSED

Confirmed on disk: `test/sql/ts_conformal_null_policy.test`,
`crates/anofox-fcst-core/src/missing.rs` (propagate_missing/fill_missing present),
`crates/anofox-fcst-core/src/conformal.rs` (conformal_predict_per_step_validated
present). Both commits confirmed in git history: `1c535c1`, `050fd29`.
