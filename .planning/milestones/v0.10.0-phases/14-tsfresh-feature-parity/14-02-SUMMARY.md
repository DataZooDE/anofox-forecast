---
phase: 14-tsfresh-feature-parity
plan: 02
subsystem: forecasting
tags: [tsfresh, rust, duckdb-extension, statistics, feature-extraction, moments, linregress]

requires:
  - phase: 14-tsfresh-feature-parity
    provides: "14-01's mapping.json parity harness (tsfresh_features_reference.rs, 9 fixtures) and its pending:14-02 inventory (12 rows)"
provides:
  - "last_location_of_maximum/minimum tsfresh-exact ((last_idx+1)/n, exact-equality max/min location, D-03)"
  - "skewness/kurtosis shared between ts_stats and ts_features via one pair of helpers (sample_skewness_g1/sample_excess_kurtosis_g2, D-05), with a documented zero-variance NaN deviation from tsfresh/pandas's 0-by-convention"
  - "signed variation_coefficient (std(x)/mean(x)), mean_second_derivative_central 1/2 factor, and the time_reversal_asymmetry term-order fix"
  - "agg_linear_trend and linear_trend_r_squared on scipy 1.18 linregress semantics (signed/clipped rvalue, IEEE-754 NaN/inf degenerate handling)"
  - "the full FEAT-02 family (12 of 12 previously-pending rows) now at mapping.json status match -- no pending:14-02 rows remain"
affects: [14-03-tsfresh-feature-parity, 14-04-tsfresh-feature-parity]

actuals:
  tokens: 11363
  tasks: 3
  commits: 6
  plan_head_before: 272a890377f6e4044ea6ddc21bb86a3a1d2157d1

tech-stack:
  added: []
  patterns:
    - "Shared statistical helpers live in the lower-level module (stats.rs) as pub(crate) fns, imported by the higher-level feature-extraction module (features.rs) -- one implementation, two SQL-facing consumers (ts_stats, ts_features) that can never drift apart again"
    - "A dedicated scipy_linregress() helper reproduces scipy's exact degenerate-case branching (ss_xx==0/ss_yy==0, IEEE-754 0/0->NaN vs x/0->inf) rather than reusing the project's older, looser linear_trend() helper -- correctness-by-construction instead of post-hoc NaN patching"
    - "TDD RED tests assert through the public extract_features() API (or directly against a private helper in the same test module) using hand-verified clean-number fixtures (e.g. chunk means [10,6,2] giving an exact r=-1.0), avoiding both compile-time INVALID_RED (referencing not-yet-existing symbols) and floating-point rounding noise that could mask a genuine assertion"

key-files:
  modified:
    - crates/anofox-fcst-core/src/features.rs
    - crates/anofox-fcst-core/src/stats.rs
    - validation/reference/fixtures/tsfresh_features/mapping.json
    - test/sql/ts_features_reference.test
    - test/sql/ts_agg_duplicate_timestamp_tiebreak.test
    - CHANGELOG.md

key-decisions:
  - "sample_skewness_g1/sample_excess_kurtosis_g2 extracted into stats.rs (not features.rs) as pub(crate) fns, matching D-05's framing that ts_stats and ts_features share one moment implementation -- stats.rs is the lower layer both ts_stats::compute_ts_stats and features::extract_features depend on"
  - "scipy_linregress() is a new, separate helper from the pre-existing linear_trend() -- reusing linear_trend() would have required either changing its (unsigned) r_squared-only return contract (breaking linear_trend_slope/intercept's untouched-by-design values) or bolting scipy's signed/degenerate branching onto a function with a different purpose; a dedicated helper keeps both call sites correct without cross-contamination"
  - "linear_trend_r_squared's early-return branch for n<2 was left returning 0.0 unchanged (not NaN) -- no fixture series has length 1 for this feature and the degenerate ss_xx==0 case the plan's NaN rule targets is only reachable via ss_yy==0 (constant y) for n>=2, since x is always-varying time index; changing the n<2 branch was out of scope and untested by any fixture"

requirements-completed: [FEAT-02]

coverage:
  - id: D1
    description: "last_location_of_maximum/minimum tsfresh-exact ((idx+1)/n), exact-equality location/has_duplicate_max/min helpers, duplicate-timestamp regression re-pinned to 1.0"
    requirement: "FEAT-02"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/features.rs#test_last_location_of_max_min_tsfresh_exact, test_last_location_constant_series_is_one, test_last_location_of_minimum_two_point"
        status: pass
      - kind: integration
        ref: "test/sql/ts_features_reference.test (last_location block, 6 assertions); test/sql/ts_agg_duplicate_timestamp_tiebreak.test (re-pinned to 1.0)"
        status: pass
    human_judgment: false
  - id: D2
    description: "skewness/kurtosis bias-corrected (G1/G2) via helpers shared with ts_stats (D-05); zero-variance deviation documented; signed variation_coefficient; mean_second_derivative_central 1/2 factor; time_reversal_asymmetry term order"
    requirement: "FEAT-02"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/features.rs#test_skewness_kurtosis_bias_corrected_matches_tsfresh, test_variation_coefficient_is_signed, test_mean_second_derivative_central_half_factor, test_time_reversal_asymmetry_term_order"
        status: pass
      - kind: integration
        ref: "test/sql/ts_features_reference.test (moments/time-reversal block + D-05 cross-surface check, 8 assertions)"
        status: pass
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/stats_reference.rs (unchanged, still green -- bit-identical ts_stats output)"
        status: pass
    human_judgment: false
  - id: D3
    description: "agg_linear_trend (signed rvalue, scipy-degenerate NaN/0.0, chunk_len>=n NaN) and linear_trend_r_squared (NaN for a constant series) on scipy 1.18 linregress semantics"
    requirement: "FEAT-02"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/features.rs#test_agg_linear_trend_nan_when_chunk_len_ge_n, test_agg_linear_trend_signed_rvalue, test_agg_linear_trend_degenerate_constant_chunk_means, test_agg_linear_trend_two_chunk_means_stderr_nan, test_linear_trend_r_squared_nan_for_constant_series"
        status: pass
      - kind: integration
        ref: "test/sql/ts_features_reference.test (rw200 table: agg_linear_trend_rvalue/variation_coefficient negative, 4 assertions)"
        status: pass
    human_judgment: false
  - id: D4
    description: "Full project DoD: cargo fmt/clippy -D warnings/cargo test --workspace green; make release + make test_release green with a higher assertion count than 14-01's baseline; FFI header byte-identical; no mapping.json row carries pending:14-02"
    verification:
      - kind: other
        ref: "make test_release: 1884 assertions in 31 test cases (14-01 baseline 1859/31); cargo fmt --all -- --check; cargo clippy --all-targets --all-features -- -D warnings; cargo test --workspace (336+ lib tests, all integration suites green); git diff --quiet -- src/include/anofox_fcst_ffi.h; mapping.json pending:14-02 count == 0"
        status: pass
    human_judgment: false

duration: ~55 min
completed: 2026-10-11
status: complete
---

# Phase 14 Plan 2: Moments, Locations, Time Reversal & agg_linear_trend Summary

**All 12 FEAT-02 features (locations, moments, time reversal, agg_linear_trend) are now tsfresh/scipy-exact, with skewness/kurtosis sharing one implementation between ts_stats and ts_features.**

## Performance

- **Duration:** ~55 min
- **Tasks:** 3 completed
- **Files modified:** 6
- **Commits:** 6

## Accomplishments

- `last_location_of_maximum`/`last_location_of_minimum` now use tsfresh's `(last_idx+1)/n` definition (previously the raw 0-based `last_idx/n`); the `first_last_location_of_value`/`has_duplicate_value` helpers switched from an `EPSILON` window to exact equality, matching tsfresh's `x == max`/`x == min` comparisons. The long-standing duplicate-timestamp SQL regression is re-pinned from `0.8333333333333334` to `1.0` under the corrected definition.
- Extracted `sample_skewness_g1`/`sample_excess_kurtosis_g2` out of `ts_stats`'s `compute_ts_stats` into shared `pub(crate)` helpers in `stats.rs` (bit-identical to the previous inline computation — `stats_reference` stays green unchanged), and wired `ts_features`'s skewness/kurtosis to call the same helpers instead of its own biased population moment ratio. A zero-variance series (`constant_n30`, `all_zero_n50`) now reports `NaN`, documented in `mapping.json` as an intentional deviation from tsfresh/pandas's `0`-by-convention (D-03).
- Fixed three more FEAT-02 bugs: `variation_coefficient` is now signed (`std(x)/mean(x)`, not `std(x)/|mean(x)|`); `mean_second_derivative_central` gained its missing `1/2` denominator factor; `time_reversal_asymmetry`'s second term had its operands transposed and is now `x * x_lag^2` (not `x_lag * x^2`).
- Rewrote `agg_linear_trend` to port scipy 1.18's `linregress` exactly via a new `scipy_linregress()` helper: signed Pearson r clipped to `[-1, 1]`, scipy's degenerate-case branching (`ss_xx == 0`/`ss_yy == 0` → `NaN` when `ss_xy == 0` too, else `0.0`), and plain IEEE-754 division for `stderr` (`0.0/0.0 → NaN`, nonzero `/0.0 → inf`) so a 2-chunk fit (`df = 0`) correctly reports `NaN`. `chunk_len >= n` now returns `NaN` for all four outputs (previously `0.0`). `linear_trend_r_squared` is now `NaN` for a constant series (scipy's degenerate rvalue is `NaN`, squared is still `NaN`), previously hard-coded `0.0`.
- **No `mapping.json` row carries `pending:14-02` anymore** — all 12 rows from the 14-01 inventory (`skewness`, `kurtosis`, `variation_coefficient`, `mean_second_derivative_central`, `last_location_of_maximum`, `last_location_of_minimum`, `time_reversal_asymmetry_stat_1..3`, `agg_linear_trend_rvalue`, `agg_linear_trend_stderr`, `linear_trend_r_squared`) are now `status: match`, each confirmed via the `pending_features_still_mismatch` ratchet (0 mismatches across all 9 fixture series) before flipping.
- `test/sql/ts_features_reference.test` gained 19 new non-json-gated assertions: `last_location_of_maximum`/`minimum` on `x20` (Task 1), `mean_second_derivative_central`/`time_reversal_asymmetry_stat_1`/`skewness`/`kurtosis` on `x20` plus a D-05 cross-surface check against `ts_stats_by` (Task 3), and a new `rw200` table (the `random_walk_n200` fixture values) asserting `agg_linear_trend_rvalue` and `variation_coefficient` are negative through `ts_features_by`/`ts_features_agg` (Task 3). `make test_release` now reports 1884 assertions (up from the 1859 baseline after 14-01).
- CHANGELOG Phase 14 before/after table gained 11 rows for the full FEAT-02 family, plus a note on the documented zero-variance skewness/kurtosis deviation and the now-shared `ts_stats`/`ts_features` moment implementation.

## Task Commits

Each task was committed atomically (Tasks 2 and 3, `tdd="true"`, followed RED-GREEN; no REFACTOR commit was needed for either — the GREEN implementations were already minimal):

1. **Task 1: Tracer — last_location_of_max/min end-to-end** - `5b404c0` (feat)
2. **Task 2 RED: failing tests for moments/variation/derivative/time-reversal** - `941984c` (test)
3. **Task 2 GREEN: bias-corrected moments shared with ts_stats + signed variation_coefficient** - `2668eec` (feat)
4. **Task 3 RED: failing tests for agg_linear_trend/linear_trend_r_squared** - `adb36a1` (test)
5. **Task 3 GREEN: agg_linear_trend + linear_trend_r_squared on scipy linregress semantics** - `6c36115` (feat)
6. **Task 3: CHANGELOG before/after rows** - `dc05743` (docs)

_Note: Task 1 (`type="tracer"`) is production-quality, committed like `type="auto"` — no RED/GREEN split. Its tracer feedback gate (re-running `<verify>` end-to-end) passed on the first attempt, so execution continued straight to Task 2 without a checkpoint (interactive, `human_verify_mode: end-of-phase`, the tracer's `<verify>` carries only `<automated>` — row 3 of the checkpoints.md precedence table)._

## Files Created/Modified

- `crates/anofox-fcst-core/src/features.rs` - location/duplicate helpers (exact equality), skewness/kurtosis (shared helpers), signed `variation_coefficient`, `mean_second_derivative_central` 1/2 factor, `time_reversal_asymmetry` term-order fix, new `scipy_linregress`/rewritten `aggregated_linear_trend`, `linear_trend_r_squared` NaN branch, 12 new unit tests
- `crates/anofox-fcst-core/src/stats.rs` - new `pub(crate) sample_skewness_g1`/`sample_excess_kurtosis_g2`, `compute_ts_stats` now calls them (bit-identical output)
- `validation/reference/fixtures/tsfresh_features/mapping.json` - 12 rows flipped `pending:14-02` → `match`; `skewness`/`kurtosis` carry a documented zero-variance `deviation` entry
- `test/sql/ts_features_reference.test` - 19 new assertions (locations, moments/time-reversal, D-05 cross-surface check, `rw200` table)
- `test/sql/ts_agg_duplicate_timestamp_tiebreak.test` - re-pinned expected value from `0.8333333333333334` to `1.0`
- `CHANGELOG.md` - 11 new before/after rows + a deviation/shared-implementation note

## Decisions Made

- `sample_skewness_g1`/`sample_excess_kurtosis_g2` live in `stats.rs` (the lower layer), not `features.rs` — matches D-05's framing that `ts_stats` and `ts_features` share one implementation, with `stats.rs` as the natural shared home both already depend on.
- `agg_linear_trend` got a dedicated `scipy_linregress()` helper rather than reusing the pre-existing `linear_trend()` — reuse would have required changing `linear_trend()`'s unsigned `r_squared`-only contract (breaking `linear_trend_slope`/`intercept`'s untouched values) or bolting scipy's signed/degenerate branching onto a function serving a different purpose.
- `linear_trend_r_squared`'s `n < 2` early-return branch was left at `0.0` (not changed to `NaN`) — no fixture series exercises `n = 1` for this feature, and the plan's degenerate-NaN rule is only reachable via `ss_yy == 0` (a constant series) for `n >= 2` since `x` is the always-varying time index; changing the untested early-return branch was out of scope.

## Deviations from Plan

None - plan executed exactly as written. The documented skewness/kurtosis zero-variance NaN behavior (deviating from tsfresh/pandas's `0`) was explicitly planned (D-03, D-05), not an unplanned deviation.

## Issues Encountered

- Same tooling gap as 14-01: `gsd_run check tdd-red-evidence` does not support Rust/cargo test output (it parses `node --test`'s TAP summary format only). RED was verified manually for both TDD tasks by inspecting `cargo test`'s per-test panic output and confirming each failure was a genuine assertion mismatch on the planned value (not a crash or compile error), documented in each RED commit message.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- The FEAT-02 family (12 of 12 rows from the 14-01 inventory) is fully closed: `mapping.json` has zero `pending:14-02` rows, confirmed by `tsfresh_features_reference`'s ratchet test.
- 14-03's pending inventory (31 rows: `spectral_centroid`, `spectral_variance`, `fft_coefficient_*`) and 14-04's (6 rows: `lempel_ziv_complexity`, `benford_correlation`, `binned_entropy`, `sample_entropy`, `permutation_entropy`, `approximate_entropy`) are untouched by this plan and ready to proceed as planned — confirmed unchanged in this run's `pending_features_still_mismatch` output.
- No blockers.

## Self-Check: PASSED

- Files exist on disk: `crates/anofox-fcst-core/src/features.rs`, `crates/anofox-fcst-core/src/stats.rs`, `validation/reference/fixtures/tsfresh_features/mapping.json`, `test/sql/ts_features_reference.test`, `test/sql/ts_agg_duplicate_timestamp_tiebreak.test`, `CHANGELOG.md` — all FOUND.
- Commits exist in `git log`: `5b404c0`, `941984c`, `2668eec`, `adb36a1`, `6c36115`, `dc05743` — all FOUND.
- Re-ran every task's `<acceptance_criteria>` and the plan-level `<verification>` block: all pass (tsfresh_features_reference 12/12 green with 0 `pending:14-02` rows; `cargo fmt`/`clippy -D warnings`/`cargo test --workspace` green; `make release` + `make test_release` green at 1884 assertions, up from 1859; FFI header byte-identical; duplicate-timestamp regression re-pinned to `1.0`).

---
*Phase: 14-tsfresh-feature-parity*
*Completed: 2026-10-11*
