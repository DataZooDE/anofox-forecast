---
phase: 15-detection-decomposition-on-fdars-0-47
plan: 02
subsystem: forecasting-core
tags: [scipy, rust, ffi, peak-detection, tdd, duckdb-extension]

requires:
  - phase: 15-detection-decomposition-on-fdars-0-47 (plan 01)
    provides: "fdars-core 0.47 bump, single_series_matrix() helper, Phase 15 fixture harness + non-json-gated test/sql/ts_detection_reference.test, docs-snippet harness fork"
provides:
  - "peaks::{local_maxima, peak_prominences, find_peaks, FindPeaksOptions, FoundPeak} -- a direct scipy.signal.find_peaks 1.18.1 port (plateau midpoints, O(n) monotonic-stack prominences, height/distance/prominence filters in scipy's own order)"
  - "detect_peaks rewritten on find_peaks: min_distance/min_prominence mapped onto FindPeaksOptions, smooth_first wired to fdars' Fourier-basis smoothing; fdars' own peak detector is no longer called anywhere in this crate"
  - "scipy_peaks fixture family (validation/reference/fixtures/scipy_peaks/cases.json) + gen_scipy_peaks_reference.py generator"
  - "crates/anofox-fcst-core/tests/peaks_reference.rs: scipy parity for every fixture case (no-filter and filter cases alike)"
  - "test/sql/ts_detection_reference.test 15-02 sections: rw150 n_peaks/indices/prominences, plateau midpoints, distance/prominence filters, NULL-vs-0.0 min_distance, smooth_first (41 assertions)"
affects: [15-03, 15-04, 15-05, 18]

actuals:
  tokens: 26500
  tasks: 3
  commits: 4
  plan_head_before: 4f93babbb9af83ec548ce4154dee2acabdde3105

tech-stack:
  added: []
  patterns:
    - "scipy-algorithm port pattern: local_maxima (plateau midpoint via the i_ahead-extends-while-equal loop) + O(n) two-pass monotonic-stack prominences (left_mins/right_mins, each index pushed/popped once) + filters applied in the paper's own stated order (height -> distance -> prominence), rather than re-deriving a heuristic"
    - "O(n) vs naive-scan cross-check test (peak_prominences vs a hand-written scipy-style linear scan on 300 deterministic pseudo-random series) as the correctness proof for an algorithmic-complexity optimization, not just a few hand-picked cases"

key-files:
  created:
    - validation/reference/python/gen_scipy_peaks_reference.py
    - validation/reference/fixtures/scipy_peaks/cases.json
    - crates/anofox-fcst-core/tests/peaks_reference.rs
  modified:
    - crates/anofox-fcst-core/src/peaks.rs
    - test/sql/ts_detection_reference.test
    - validation/reference/README.md
    - CHANGELOG.md
    - docs/api/05b-peak-detection.md
    - plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md
    - examples/peak_detection/README.md
    - .planning/phases/15-detection-decomposition-on-fdars-0-47/snippets/pass-list-15-02.tsv

key-decisions:
  - "Task 1 (tracer) wired find_peaks/detect_peaks with zero filtering (height/distance/prominence options accepted but ignored, parameters prefixed _) specifically so Task 2's TDD RED phase had real, unimplemented behavior to fail against -- confirmed via re-running the exact pre-existing tests (test_detect_peaks_with_prominence etc.) and finding they still passed, coincidentally, because the test fixtures' peaks all have prominence well above the old normalized-0-1-scale threshold once re-interpreted on the real data scale."
  - "detect_peaks maps min_distance/min_prominence onto FindPeaksOptions via `.filter(|&d| d > 1.0)` / `.filter(|&p| p > 0.0)` (None below the threshold) rather than passing the raw value into find_peaks's own stricter validation (distance < 1.0 -> InvalidParameter) -- this keeps the existing SQL macro default of min_distance=1.0 a true no-op without needing find_peaks itself to special-case small distances."
  - "A literal SQL `NULL` argument to the ts_detect_peaks scalar function short-circuits to a NULL struct via DuckDB's default null-propagation for scalar functions without SPECIAL_HANDLING -- discovered while writing the 'NULL min_distance behaves like 0.0' SQL assertion, which literal-NULL cannot demonstrate. Rewrote the assertion to source the NULL from a table column instead, where the per-row IsNull() check in ts_peaks.cpp's TsDetectPeaksFunction is actually reached. This is pre-existing DuckDB/C++ behavior, not a Rust change in this plan, so left unfixed and merely documented in the test's own comment."

patterns-established:
  - "TDD RED evidence recorded by hand in the commit message and SUMMARY (not the gsd-tools tdd-red-evidence checker, which cannot parse cargo test output) -- lists the exact failing assertion (got/want) for every target test, distinguishing them from pre-existing/sanity tests that passed immediately."

requirements-completed: [DET-01]

coverage:
  - id: D1
    description: "peaks::find_peaks reproduces scipy.signal.find_peaks 1.18.1 exactly: same indices (plateau midpoints, edges never peaks), same prominences (peak_prominences, wlen=None) within 1e-12, same survivors under height -> distance -> prominence filters in scipy's own order"
    requirement: "DET-01"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/peaks_reference.rs#scipy_find_peaks_parity (every scipy_peaks fixture case, incl. all filter/tie cases)"
        status: pass
      - kind: integration
        ref: "test/sql/ts_detection_reference.test (15-02 sections: rw150, plateau, sine240 distance/prominence filters)"
        status: pass
    human_judgment: false
  - id: D2
    description: "On the audited random walk rw150, ts_detect_peaks returns exactly scipy's 38 peaks at scipy's indices, with no reported index a local minimum (previously 25 found, 12 matching scipy's 38, 5 reported minima)"
    requirement: "DET-01"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/peaks_reference.rs#detect_peaks_matches_scipy_on_rw150"
        status: pass
    human_judgment: false
  - id: D3
    description: "min_distance -> distance in samples (<=1 disables it, matching the macro default 1.0), min_prominence -> minimum prominence (<=0 disables it), smooth_first applies fdars' Fourier-basis smoothing before the scipy port runs; every returned peak carries its scipy prominence; inter_peak_distances/mean_period defined as consecutive index differences / their mean (NaN under 2 peaks); height is on the Rust API without a new SQL parameter"
    requirement: "DET-01"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/peaks.rs#test_find_peaks_height_filter_keeps_only_tall_peaks, #test_detect_peaks_min_distance_macro_default_is_noop, #test_detect_peaks_min_distance_filters_close_peaks, #test_detect_peaks_min_prominence_filters_all_peaks, #test_detect_peaks_smooth_first_reports_smoothed_values"
        status: pass
      - kind: integration
        ref: "test/sql/ts_detection_reference.test (sine240 distance=8.0/prominence=2.0, NULL-vs-0.0 min_distance via peaks_null_check, smooth_first=true)"
        status: pass
    human_judgment: false
  - id: D4
    description: "Prominences computed in O(n) with monotonic stacks, equal a naive scipy-style scan on randomised plateau/tie inputs; a 200000-point trending sawtooth with ~100000 peaks finishes in under 5s in a debug build"
    requirement: "DET-01"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/peaks.rs#test_peak_prominences_matches_naive_scan_on_random_series, #test_prominence_performance_on_long_trending_sawtooth"
        status: pass
    human_judgment: false
  - id: D5
    description: "Edge cases: constant/monotonic series return 0 peaks; [1,2,1] returns index 1; n<3 stays InsufficientData (SQL NULL row); distance<1 or non-finite options are InvalidParameter; NaN input never panics"
    requirement: "DET-01"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/peaks.rs#test_local_maxima_edges_never_peaks, #test_local_maxima_rejects_short_series, #test_find_peaks_rejects_distance_below_one, #test_find_peaks_rejects_non_finite_options, #test_find_peaks_never_panics_on_nan_input"
        status: pass
      - kind: integration
        ref: "crates/anofox-fcst-core/tests/peaks_reference.rs#scipy_find_peaks_parity (constant20, monotonic20, n3, edge_plateaus_a/b cases)"
        status: pass
    human_judgment: false
  - id: D6
    description: "test/sql/ts_detection_reference.test gains non-gated ts_detect_peaks assertions; make test_release reports more assertions than after plan 15-01 (1963 -> 1976); cargo fmt/clippy/test --workspace clean; FFI header byte-identical"
    verification:
      - kind: other
        ref: "make test_release (1976 assertions); cargo fmt --all -- --check; cargo clippy --all-targets --all-features -- -D warnings; cargo test --workspace; git diff --quiet -- src/include/anofox_fcst_ffi.h"
        status: pass
    human_judgment: false
  - id: D7
    description: "Peak docs/skill/example README describe scipy semantics (prominence on the data's own scale, not 0-1 normalized; plateau midpoints; min_distance<=1.0 no-op); CHANGELOG before/after table; snippet harness 0 FAIL; no zero-crossing wording remains in the three user-facing files"
    verification:
      - kind: other
        ref: ".planning/phases/15-detection-decomposition-on-fdars-0-47/snippets/pass-list-15-02.tsv (0 FAIL); git grep zero-crossing across docs/api/05b-peak-detection.md, examples/peak_detection/README.md, plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md (0 hits)"
        status: pass
    human_judgment: false

duration: 32min
completed: 2026-10-13
status: complete
---

# Phase 15 Plan 2: scipy find_peaks port Summary

**`ts_detect_peaks`/`ts_detect_peaks_by` are now a faithful port of `scipy.signal.find_peaks`
1.18.1 (plateau midpoints, real-scale `O(n)` prominences, height/distance/prominence filters
in scipy's own order), replacing fdars-core's derivative-sign-change detector: on the audited
random walk, peak count went from 25 (12 matching scipy, 5 reported local minima) to exactly
scipy's 38/38, and the `[0,1,1,1,0]` plateau now reports index 2 (the midpoint) with
prominence 1.0 instead of index 1 with prominence 0.0.**

## Performance

- **Duration:** ~32 min
- **Started:** 2026-10-13T06:54:05Z (approx, per the plan's base commit timestamp)
- **Completed:** 2026-10-13T07:26:05Z
- **Tasks:** 3 (Task 1 tracer, Task 2 TDD RED+GREEN, Task 3 docs/CHANGELOG sweep)
- **Files modified:** 11 (across 4 commits)

## Accomplishments

- **Task 1 (tracer):** `peaks.rs` gained `local_maxima` (scipy's `_local_maxima_1d` port,
  including the plateau-midpoint and never-a-boundary-sample rules), `peak_prominences`
  (two monotonic-stack passes -- `left_mins`/`right_mins` -- each index pushed/popped at
  most once, giving `O(n)` total instead of scipy's own `O(n)`-per-peak linear scan),
  `FindPeaksOptions`/`FoundPeak`, and a `find_peaks` wired to the no-filter path. `detect_peaks`
  was rewritten to call `find_peaks` on the raw values; the fdars derivative detector import
  and call are gone entirely (`analyze_peak_timing` is untouched and still uses fdars). New
  `scipy_peaks` fixture family (`gen_scipy_peaks_reference.py`): the audited `rw150` random
  walk plus a seeded `sine240` (with distance/prominence/height/combined filter variants),
  plateau/edge/tie/degenerate series, scipy `find_peaks` + `peak_prominences` for every case.
  `peaks_reference.rs`'s `detect_peaks_matches_scipy_on_rw150` confirmed 38/38 exact-index
  parity and zero reported minima; the built extension's `ts_detect_peaks([0,1,1,1,0,...])`
  returned `[2, 5, 9]` exactly matching scipy.
- **Task 2 (TDD):** wrote 8 Rust unit tests + enabled the fixture's filter cases in
  `peaks_reference.rs` describing height/distance/prominence filtering, option validation
  (distance `< 1` or non-finite -> `InvalidParameter`), and `smooth_first` smoothing -- ran
  them and recorded the exact RED failures (got/want mismatches, e.g. height filter keeping
  all 3 candidates instead of the 1 above threshold) before writing any implementation.
  Implemented `validate_find_peaks_options`, `suppress_by_distance` (scipy's own stable-sort-
  by-height, suppress-from-highest-down algorithm, ties won by the rightmost), the
  height/distance/prominence filter chain in `find_peaks`, `detect_peaks`'s parameter mapping
  (`min_distance > 1.0 -> distance`, `min_prominence > 0.0 -> prominence`), and
  `smooth_for_peaks` (fdars' Fourier-basis fit with GCV-selected `nbasis` in `5..=25`,
  falling back to raw values on a failed fit, exactly mirroring the old detector's smoothing
  step). All 8 target tests + the fixture's filter cases went GREEN; `cargo fmt`/`clippy -D
  warnings`/`cargo test --workspace` all clean.
- **Task 3:** CHANGELOG gained a 6-row peak-detection before/after table (real "before" values
  captured live from the pre-port build in Task 1 step 1: `n_peaks` 25->38, matching-scipy
  12/38->38/38, reported minima 5->0, plateau index/prominence 1/0.0 -> 2/1.0). New "Algorithm"
  section in `docs/api/05b-peak-detection.md` documenting scipy semantics end-to-end. Corrected
  `anofox-forecast-detection` SKILL.md and `examples/peak_detection/README.md`, which had
  wrongly described prominence as a normalized `0..1` score -- it is now documented as being
  on the data's own scale everywhere. Phase 15 snippet harness run on all three files:
  `pass-list-15-02.tsv`, 0 FAIL. `make test_release`: 1976 assertions (up from 1963 after
  15-01). FFI header byte-identical (`header_changed=0`).

## Task Commits

Each task was committed atomically (Task 2's TDD cycle produced two commits):

1. **Task 1: Tracer -- scipy find_peaks port, default params, end-to-end** -- `8ce654f` (feat)
2. **Task 2 RED: failing tests for filters/validation/smoothing** -- `c5a9c7e` (test)
3. **Task 2 GREEN: filters, validation, Fourier smoothing implemented** -- `9c56130` (feat)
4. **Task 3: CHANGELOG, peak docs/skill/example README sweep** -- `a0c0812` (docs)

_Tracer feedback gate (after Task 1): `HUMAN_VERIFY_MODE=end-of-phase`, `<verify>` was
automated-only, auto mode was NOT active (`workflow.auto_advance: false`,
`_auto_chain_active: false`) -- per the tracer rule chain this re-ran Task 1's `<verify>`
end-to-end (already green from the initial run) and continued directly to Task 2 without a
checkpoint._

**Commits ledger (#3968):** `plan_head_before: 4f93babbb9af83ec548ce4154dee2acabdde3105`;
`git rev-list --count 4f93bab..HEAD` = 4, matching this plan's own 4 commits exactly (no
concurrent sibling-plan commits interleaved this time, unlike 15-01).

**Plan metadata:** (this commit, docs: complete 15-02 plan)

## Files Created/Modified

**Created:**
- `validation/reference/python/gen_scipy_peaks_reference.py` -- scipy `find_peaks`/
  `peak_prominences` fixture generator (rw150, sine240 + 4 filter variants, plateau/edge/
  tie/degenerate series)
- `validation/reference/fixtures/scipy_peaks/cases.json` -- fixture data (15 cases)
- `crates/anofox-fcst-core/tests/peaks_reference.rs` -- Rust scipy-parity integration test

**Modified:**
- `crates/anofox-fcst-core/src/peaks.rs` -- `local_maxima`, `peak_prominences` (+
  `left_mins`/`right_mins` helpers), `FindPeaksOptions`, `FoundPeak`, `find_peaks`,
  `validate_find_peaks_options`, `suppress_by_distance`, `smooth_for_peaks`, rewritten
  `detect_peaks`; 19 new/changed unit tests
- `test/sql/ts_detection_reference.test` -- 15-02 sections: rw150 (4 assertions), plateau
  midpoints, sine240 distance/prominence filters, NULL-vs-0.0 `min_distance`, `smooth_first`
  (41 assertions total for this file, up from 28 after 15-01)
- `validation/reference/README.md` -- `scipy_peaks` family row, tolerance rationale,
  regeneration command
- `CHANGELOG.md` -- peak-detection before/after table (6 rows) appended to the Phase 15 bullet
- `docs/api/05b-peak-detection.md` -- new Algorithm section (scipy semantics, parameter
  mapping, plateau midpoints, prominence definition)
- `plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md` -- peak section
  algorithm note
- `examples/peak_detection/README.md` -- corrected prominence-scale description + parameter
  table
- `.planning/phases/15-detection-decomposition-on-fdars-0-47/snippets/pass-list-15-02.tsv` --
  snippet harness output (0 FAIL)

## Decisions Made

See `key-decisions` in frontmatter.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocker] "NULL min_distance behaves like 0.0" SQL assertion rewritten to use a
column-sourced NULL, not a literal constant**
- **Found during:** Task 2, writing the `test/sql/ts_detection_reference.test` assertions
- **Issue:** `ts_detect_peaks(LIST(...), NULL)` with a literal SQL `NULL` constant returns a
  NULL struct outright -- DuckDB's default null-propagation policy for scalar functions
  without `SPECIAL_HANDLING` short-circuits before the C++ function body (and its own
  per-row `IsNull()` check) ever runs. The planned assertion comparing this against the
  `0.0` call would always compare `NULL = <value>`, which is `NULL`, not `true`.
- **Fix:** Built a 2-row table (`peaks_null_check`) with `min_distance` as an actual column
  (one row `NULL`, one row `0.0`) and compared `n_peaks` across the two `WHERE` branches --
  this reaches the C++ reader's real per-row null handling, which does default to `0.0`
  exactly as documented.
- **Files modified:** `test/sql/ts_detection_reference.test`
- **Verification:** `./build/release/test/unittest test/sql/ts_detection_reference.test` --
  41/41 assertions pass.
- **Committed in:** `9c56130` (Task 2 GREEN commit)

---

**Total deviations:** 1 auto-fixed (1x Rule 3 - blocker).
**Impact:** Mechanical SQL-test-authoring fix; the underlying NULL-default behavior itself
was already correct and unchanged by this plan -- only the assertion's own SQL needed
rewriting to actually exercise it.

## Known Stubs

None.

## Issues Encountered

None beyond the deviation documented above.

## User Setup Required

None -- no external service configuration required.

## Next Phase Readiness

- Plan 15-03 (BOCPD + PELT, DET-02/DET-03) is unaffected by this plan's changes --
  `changepoint.rs` was not touched.
- `peaks.rs`'s `find_peaks`/`FindPeaksOptions`/`FoundPeak` are stable, reusable building
  blocks if a later plan needs scipy-style peak detection elsewhere (e.g. changepoint
  post-processing), though none currently plan to.
- The Phase 15 snippet harness and `test/sql/ts_detection_reference.test` remain in place
  for 15-03/15-04/15-05 to append their own sections.
- No blockers identified for 15-03.

## Self-Check: PASSED

- `crates/anofox-fcst-core/tests/peaks_reference.rs` exists on disk: FOUND
- `validation/reference/fixtures/scipy_peaks/cases.json` exists on disk: FOUND
- `validation/reference/python/gen_scipy_peaks_reference.py` exists on disk: FOUND
- `.planning/phases/15-detection-decomposition-on-fdars-0-47/snippets/pass-list-15-02.tsv` exists on disk: FOUND
- `git log --oneline --all | grep 8ce654f`: FOUND
- `git log --oneline --all | grep c5a9c7e`: FOUND
- `git log --oneline --all | grep 9c56130`: FOUND
- `git log --oneline --all | grep a0c0812`: FOUND
- Re-ran plan-level `<verification>`: `scipy_peaks` fixture regenerates deterministically;
  `peaks_reference` (2 tests) and `peaks::tests` (22 tests) pass; O(n)-vs-naive cross-check
  (300 series) and 200000-point performance test (< 5s) pass (PASS); `cargo fmt`/`clippy -D
  warnings`/`cargo test --workspace` green (PASS); `make release` + `make test_release`
  green with 1976 assertions, up from 1963 (PASS); peak docs/skill/example README describe
  scipy semantics, harness 0 FAIL, `synthetic_peak_examples.sql` exits 0, FFI header
  unchanged (PASS).

---
*Phase: 15-detection-decomposition-on-fdars-0-47*
*Completed: 2026-10-13*
