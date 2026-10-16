---
phase: 14-tsfresh-feature-parity
plan: 01
subsystem: forecasting
tags: [tsfresh, rust, duckdb-extension, statistics, feature-extraction, pacf, autocorrelation]

requires:
  - phase: 13-statistical-correctness
    provides: reference-fixture harness pattern (fixture_io.py, tests/common/mod.rs, non-json-gated SQL test style) reused verbatim for the tsfresh_features family
provides:
  - "117-row tsfresh/numpy parity contract (mapping.json) covering every anofox_fcst_core::features::list_features() name, with an empirically-verified status/owner inventory"
  - "tsfresh-generated reference fixtures for 9 series (6 D-09 series + 3 edge cases) under validation/reference/fixtures/tsfresh_features/"
  - "Durbin-Levinson PACF (partial_autocorrelation_lag1..5) matching statsmodels pacf(method='ld') exactly"
  - "tsfresh-exact autocorrelation_lag1..10 with the (n-lag)*var(x) denominator"
  - "117-key output invariant: extract_features() always returns exactly the 117 list_features() names for any input length >= 1"
  - "autocorrelation_lag10 now reaches every SQL surface (ts_features_by/ts_features/ts_features_agg/ts_features_table)"
affects: [14-02-tsfresh-feature-parity, 14-03-tsfresh-feature-parity, 14-04-tsfresh-feature-parity]

actuals:
  tokens: 42770
  tasks: 3
  commits: 5

tech-stack:
  added: []
  patterns:
    - "Fixture generator calls tsfresh.feature_extraction.feature_calculators.<fn> directly per a hand-maintained mapping.json row, never tsfresh.extract_features()'s settings-driven convenience API"
    - "Mapping-driven Rust parity test: one #[test] per fixture series, asserting every status:\"match\" row and ratcheting every status:\"pending:<owner>\" row (fails if it now matches everywhere)"
    - "117-key output invariant enforced by a debug_assert_eq! inside extract_features() plus a dedicated unit test across input lengths 1,2,3,5,8,10"

key-files:
  created:
    - validation/reference/fixtures/tsfresh_features/mapping.json
    - validation/reference/fixtures/tsfresh_features/white_noise_n200.json
    - validation/reference/fixtures/tsfresh_features/random_walk_n200.json
    - validation/reference/fixtures/tsfresh_features/seasonal_ar12_n144.json
    - validation/reference/fixtures/tsfresh_features/trend_season_n120.json
    - validation/reference/fixtures/tsfresh_features/intermittent_n100.json
    - validation/reference/fixtures/tsfresh_features/short_n20.json
    - validation/reference/fixtures/tsfresh_features/constant_n30.json
    - validation/reference/fixtures/tsfresh_features/all_zero_n50.json
    - validation/reference/fixtures/tsfresh_features/tiny_n8.json
    - validation/reference/python/gen_tsfresh_features.py
    - crates/anofox-fcst-core/tests/tsfresh_features_reference.rs
    - test/sql/ts_features_reference.test
  modified:
    - crates/anofox-fcst-core/src/features.rs
    - src/macros/ts_macros.cpp
    - validation/reference/README.md
    - CHANGELOG.md
    - crates/anofox-fcst-ffi/src/types.rs

key-decisions:
  - "Status assignment in mapping.json is empirically run, not guessed: every row's Rust output was compared against its tsfresh/numpy reference on all 9 fixture series via a throwaway dump-and-diff pass before finalizing status -- 4 rows the research expected to already match (variation_coefficient, linear_trend_r_squared, agg_linear_trend_stderr, approximate_entropy) actually mismatched and were reclassified to pending with a documented reason"
  - "gsd_run check tdd-red-evidence cannot classify this project's RED phase (it parses node --test TAP output only; cargo's text output always yields zero_tests_discovered) -- RED was verified by direct inspection of cargo's exit code + failing-test output instead, documented in the RED commit message"
  - "117-key invariant values for not-yet-fixed features follow the plan's explicit interim spec (e.g. skewness/kurtosis NaN on zero std_dev) even where that differs from tsfresh's own edge-case value (e.g. tsfresh skew on a constant series is 0.0, not NaN) -- the full correctness fix for those rows is owned by plan 14-02"

requirements-completed: [FEAT-01, FEAT-05]

coverage:
  - id: D1
    description: "Durbin-Levinson PACF (partial_autocorrelation_lag1..5) matches statsmodels pacf(method='ld') / tsfresh exactly, including NaN edge cases (n<=1, lag>=n/2, zero variance)"
    requirement: "FEAT-01"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/tsfresh_features_reference.rs#tsfresh_parity_<series> (9 tests)"
        status: pass
      - kind: integration
        ref: "test/sql/ts_features_reference.test (PACF block, 15 assertions)"
        status: pass
    human_judgment: false
  - id: D2
    description: "117-row mapping.json covers every list_features() name with an empirically-verified status (match / pending:<owner>) and a per-owner inventory"
    requirement: "FEAT-05"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/tsfresh_features_reference.rs#mapping_covers_list_features_exactly"
        status: pass
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/tsfresh_features_reference.rs#pending_features_still_mismatch"
        status: pass
    human_judgment: false
  - id: D3
    description: "tsfresh-exact autocorrelation_lag1..10 ((n-lag)*var(x) denominator) and the 117-key output invariant across all input lengths"
    requirement: "FEAT-05"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/features.rs#test_autocorrelation_matches_tsfresh_denominator, test_autocorrelation_nan_on_zero_variance_or_lag_ge_n, test_117_key_invariant_across_lengths, test_n1_edge_case_values, test_time_reversal_and_c3_zero_when_2lag_ge_n, test_fft_coefficient_nan_beyond_available_bins"
        status: pass
    human_judgment: false
  - id: D4
    description: "autocorrelation_lag10 reaches every SQL surface (ts_features_by returns 118 columns, ts_features/ts_features_agg return 117 struct keys, ts_features_table returns 117 columns)"
    requirement: "FEAT-05"
    verification:
      - kind: integration
        ref: "test/sql/ts_features_reference.test (ACF/anchor + schema blocks, 20 assertions)"
        status: pass
    human_judgment: false
  - id: D5
    description: "Full project DoD: cargo fmt / clippy -D warnings / cargo test --workspace green; make release + make test_release green with a higher assertion count; FFI header byte-identical"
    verification:
      - kind: other
        ref: "make test_release: 1859 assertions in 31 test cases (baseline 1824/30); cargo fmt --all -- --check; cargo clippy --all-targets --all-features -- -D warnings; cargo test --workspace (324+ passed); git diff --quiet -- src/include/anofox_fcst_ffi.h"
        status: pass
    human_judgment: false

duration: ~45 min (approximate -- no explicit PLAN_START_TIME timestamp was captured at session start; derived from git commit span plus upfront research/setup time)
completed: 2026-10-11
status: complete
---

# Phase 14 Plan 1: Tracer Harness + PACF + Autocorrelation Summary

**Built the 117-row tsfresh/numpy parity harness (mapping.json + generator + 9 fixtures + ratcheted Rust test), then used it to ship Durbin-Levinson PACF and tsfresh-exact autocorrelation end-to-end through every SQL surface, closing the 117-key output invariant along the way.**

## Performance

- **Duration:** ~45 min (approximate)
- **Tasks:** 3 completed
- **Files modified:** 18 (13 created, 5 modified)
- **Commits:** 5

## Accomplishments

- Built the D-01 completeness contract: `mapping.json` maps all 117 `list_features()` names to an exact tsfresh calculator + params, or a numpy/scipy formula, with an empirically-verified `status` (`match` or `pending:<owner-plan>`) and a ratchet test that fails if a pending row silently starts matching.
- Generated 9 tsfresh reference fixtures (white noise, random walk, seasonal AR(12), trend+season, intermittent, the audit `short_n20` vector, plus constant/all-zero/tiny-n8 edge cases) by calling `tsfresh.feature_extraction.feature_calculators` directly — never the settings-driven convenience API, which silently uses the wrong default parameters for several of this project's exact choices.
- Replaced the ad-hoc Yule-Walker PACF shortcut with a Durbin-Levinson recursion matching statsmodels `pacf(method="ld")` exactly — lags 3-5 no longer reuse the lag-2 formula (the headline Phase 14 bug).
- Found and fixed a NEW correctness bug beyond the original 11-item audit: `autocorrelation_lag1..10`'s denominator was the whole-series sum of squared deviations instead of tsfresh's `(n-lag)*var(x)`.
- Closed the 117-key output invariant: `extract_features()` now always returns exactly 117 keys for any input length >= 1 (previously several keys — including `autocorrelation_lag10` on short series — were silently omitted). `autocorrelation_lag10` now reaches `ts_features_by`/`ts_features`/`ts_features_agg`/`ts_features_table` for the first time.
- Ran the full project DoD: `cargo fmt`/`clippy -D warnings`/`cargo test --workspace` all green; `make test_release` reports 1859 assertions in 31 test cases (up from the 1824/30 baseline); the FFI header stayed byte-identical.

## Task Commits

Each task was committed atomically (Task 3 followed RED-GREEN-REFACTOR per its `tdd="true"` attribute):

1. **Task 1: Tracer — PACF end-to-end** - `6ceae55` (feat)
2. **Task 2: Expand the harness to all 117 names** - `f068813` (feat)
3. **Task 3 RED: failing tests for autocorrelation + 117-key invariant** - `bb031e2` (test)
4. **Task 3 GREEN: implement autocorrelation + 117-key invariant** - `4b930ca` (feat)
5. **Task 3: autocorrelation_lag10 reaches SQL, SQL/CHANGELOG, DoD cleanup** - `0d51838` (feat)

_Note: Task 3 (`tdd="true"`) produced two commits for its core cycle (RED `bb031e2` → GREEN `4b930ca`); no separate REFACTOR commit was needed. The third commit (`0d51838`) covers the remaining non-TDD action items (macro, SQL test, CHANGELOG, DoD lint cleanup)._

## Files Created/Modified

- `validation/reference/fixtures/tsfresh_features/mapping.json` - 117-row parity contract (D-01/D-02): calculator/params or formula, status, definition per feature
- `validation/reference/fixtures/tsfresh_features/{9 series}.json` - tsfresh-generated expected values for all 117 features per series
- `validation/reference/python/gen_tsfresh_features.py` - fixture generator, calls `feature_calculators` directly per mapping row
- `crates/anofox-fcst-core/tests/tsfresh_features_reference.rs` - mapping-driven parity harness: schema guard, 9 per-series tests, ratchet test
- `test/sql/ts_features_reference.test` - non-json-gated SQL value + schema assertions (35 total) against `ts_features_by`/`ts_features`/`ts_features_agg`/`ts_features_table`
- `crates/anofox-fcst-core/src/features.rs` - `fn pacf_durbin_levinson`, rewritten `fn autocorr`, 117-key invariant fixes across 7 feature groups
- `src/macros/ts_macros.cpp` - `ts_features_table` macro gains the `autocorrelation_lag10` column
- `validation/reference/README.md` - `tsfresh_features` family row, explainer paragraph, regeneration command
- `CHANGELOG.md` - Phase 14 Breaking-changes bullet with an 8-row before/after table, plus the 117-feature-schema bullet
- `crates/anofox-fcst-ffi/src/types.rs` - pre-existing clippy fix (unrelated to this plan's feature work; see Deviations)

## Decisions Made

- Status assignment in `mapping.json` is empirically run on every row/series pair (via a throwaway Rust-dump + Python-diff pass), not derived from the research document's expected inventory alone — this caught 4 rows (`variation_coefficient`, `linear_trend_r_squared`, `agg_linear_trend_stderr`, `approximate_entropy`) that the research expected to already match but actually mismatched, each now documented with a concrete reason.
- `gsd_run check tdd-red-evidence` cannot classify this Rust project's RED phase — it parses `node --test`'s TAP summary format only, and cargo's native output always yields `zero_tests_discovered` regardless of whether the target test genuinely failed. RED was verified by direct inspection of `cargo test`'s exit code and per-test failure output instead (documented in the RED commit message, `bb031e2`).
- Interim 117-key-invariant values for not-yet-fixed features follow this plan's own spec literally (e.g. `skewness`/`kurtosis` NaN when `std_dev <= EPSILON`) even where that differs from tsfresh's true edge-case behavior (tsfresh's skew on a constant series is `0.0`, not NaN) — the full correctness fix for those rows is explicitly owned by plan 14-02, and the mapping's `pending:14-02` status reflects that.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Pre-existing clippy failures blocking the DoD gate**
- **Found during:** Task 3, running the DoD's `cargo clippy --all-targets --all-features -- -D warnings`
- **Issue:** Two findings blocked the mandatory clippy-clean gate: (a) `needless_range_loop` in this plan's own Task 1 PACF-insertion loop (`for lag in 1..=5 { ...pacf[lag]... }`), and (b) a pre-existing, unrelated `field_reassign_with_default` in `crates/anofox-fcst-ffi/src/types.rs` (a Phase 13 test, last touched in commit `15f7d63`, long before this plan started)
- **Fix:** (a) rewrote the PACF loop as `for (lag, &value) in pacf.iter().enumerate().take(6).skip(1)`; (b) rewrote the test's `TsStats::default()` + field-reassignment as a single struct-literal with `..Default::default()`
- **Files modified:** `crates/anofox-fcst-core/src/features.rs`, `crates/anofox-fcst-ffi/src/types.rs`
- **Verification:** `cargo clippy --all-targets --all-features -- -D warnings` exits clean; `cargo test --workspace` still green (324+ passed)
- **Committed in:** `0d51838`

**2. [Rule 1 - Bug] `if_same_then_else` clippy failure in this plan's own Task 2 ratchet test**
- **Found during:** Task 3, same clippy run
- **Issue:** `pending_features_still_mismatch`'s match predicate used an `if/else if/else` chain where the first two branches both returned `true` literally
- **Fix:** Flattened to a single boolean OR expression (`(nan_match) || (inf_match) || (tolerance_check)`), logically identical, clippy-clean
- **Files modified:** `crates/anofox-fcst-core/tests/tsfresh_features_reference.rs`
- **Verification:** `cargo clippy --all-targets --all-features -- -D warnings` exits clean; `pending_features_still_mismatch` still passes
- **Committed in:** `0d51838`

**3. [Rule 1 - Bug] `cargo fmt` drift in this plan's own Task 2 commit**
- **Found during:** Task 3, running the DoD's `cargo fmt --all -- --check`
- **Issue:** `tsfresh_features_reference.rs` (written in Task 2) had several lines exceeding rustfmt's preferred wrap width, not caught before that commit
- **Fix:** Ran `cargo fmt --all` and verified `--check` passes
- **Files modified:** `crates/anofox-fcst-core/tests/tsfresh_features_reference.rs`
- **Verification:** `cargo fmt --all -- --check` exits clean
- **Committed in:** `4b930ca`

---

**Total deviations:** 3 auto-fixed (2 blocking/clippy, 1 bug/fmt). **Impact:** All three were necessary to satisfy the project's own Definition of Done (`cargo fmt`/`clippy -D warnings` gates documented in this project's MEMORY/CLAUDE.md); none expanded scope beyond what the plan's own `<verify>` blocks required. One fix (`types.rs`) touched a pre-existing, unrelated file — narrowly scoped to the single clippy-flagged line, no behavior change.

## Issues Encountered

- `gsd_run check tdd-red-evidence` does not support Rust/cargo test output (TAP-parser is `node --test`-specific) — see Decisions Made above. This is a tooling gap outside this plan's scope; RED was verified manually and documented in the commit message instead of via the automated checker.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- The parity harness (`mapping.json`, `gen_tsfresh_features.py`, `tsfresh_features_reference.rs`) is in place and ready for plans 14-02/14-03/14-04 to consume: each flips its owned rows from `pending:14-0N` to `match` and the ratchet will catch any row that's fixed without the mapping being updated (or vice versa).
- Pending inventory for the next three plans (empirically verified, not estimated):
  - **14-02 (12 rows):** `skewness`, `kurtosis`, `variation_coefficient`, `mean_second_derivative_central`, `last_location_of_maximum`, `last_location_of_minimum`, `time_reversal_asymmetry_stat_1..3`, `agg_linear_trend_rvalue`, `agg_linear_trend_stderr`, `linear_trend_r_squared`
  - **14-03 (31 rows):** `spectral_centroid`, `spectral_variance`, `fft_coefficient_*` (29 of 30 — `fft_coefficient_0_imag` already matches, trivially 0 either way)
  - **14-04 (6 rows):** `lempel_ziv_complexity`, `benford_correlation`, `binned_entropy`, `sample_entropy`, `permutation_entropy`, `approximate_entropy`
- No blockers. `rustfft` (D-07) is not yet added as a direct dependency — that's 14-03's own task.

---
*Phase: 14-tsfresh-feature-parity*
*Completed: 2026-10-11*
