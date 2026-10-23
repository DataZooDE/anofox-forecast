---
phase: 20-detection-metric-bug-fixes
plan: 01
subsystem: detection/seasonality
status: complete
tags: [bug-fix, seasonality, stl, classify, amplitude-modulation, "#286", BUG-04]
requires: []
provides:
  - classify_seasonality strength/is_seasonal/classification from crate::stats::stl_strengths (shared with ts_stats_by)
  - detect_amplitude_modulation seasonality gate on the same STL strength
  - R tsfeatures reference case issue286_weekly84
  - "#286 value pins in test/sql/correctness/detection_decomposition.test"
affects:
  - ts_classify_seasonality_by / ts_classify_seasonality_agg / ts_classify_seasonality (values only)
  - ts_detect_amplitude_modulation (values only)
tech-stack:
  added: []
  patterns:
    - "One shared Rust strength implementation (stl_strengths) for every SQL surface that reports a seasonal strength"
    - "NaN strength marshalled to SQL NULL in the classify aggregate and scalar"
key-files:
  created: []
  modified:
    - crates/anofox-fcst-core/src/seasonality.rs
    - crates/anofox-fcst-core/tests/stl_reference.rs
    - validation/reference/r/gen_stl_strength_reference.R
    - validation/reference/fixtures/r_stl_strength/cases.json
    - src/aggregate_functions/ts_classify_seasonality_agg.cpp
    - src/table_functions/ts_seasonality.cpp
    - test/sql/correctness/detection_decomposition.test
    - CHANGELOG.md
    - docs/api/05a-decomposition.md
    - plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md
decisions:
  - "classify_seasonality's seasonal_strength is the ts_stats_by STL strength at the rounded period; fdars still supplies cycle_strengths/weak_seasons/has_stable_timing/timing_variability, and fdars's classification rule (change.rs:579) is re-applied to the corrected strength"
  - "ts_detect_amplitude_modulation keeps NaN (not NULL) for a degenerate-series seasonal_strength; NULL marshalling is scoped to the classify functions as planned"
  - "Phase 20.1 additive-only API rule: 20-01 changes values only; no column, struct field, parameter or function name was added, renamed or removed"
metrics:
  duration: "~4h22m wall clock (10:09 to 14:31, 2026-10-23), including an API-usage-limit interruption mid-Task 3 and a continuation run"
  completed: 2026-10-23
  tasks: 3
  files: 10
actuals:
  tokens: 9700
  tasks: 3
  commits: 4
plan_head_before: 1295572efd29a6a1a7110ecfb87dd743bd2b27e8
---

# Phase 20 Plan 01: classify_seasonality on the shared STL strength (#286) Summary

`ts_classify_seasonality_by`, `_agg` and the scalar now take `seasonal_strength` from the same Rust `stl_strengths` function `ts_stats_by` uses (tsfeatures F_S on the R-matching STL fit at the rounded period). The amplitude-modulation gate uses that strength too. The #286 weekly series now classifies `stable_seasonal` with strength 0.986881091, matching `ts_stats_by(seasonal_period := 7)` to within 1e-12 and R tsfeatures within the fixture tolerance.

## Commits

| Task | Commit | Message |
|------|--------|---------|
| 1 (tracer) | 290c39f | fix(20-01): classify_seasonality strength comes from the shared STL rule (#286) |
| 2 (tdd) | 7b2c6f7 | fix(20-01): amplitude-modulation gate on the shared STL strength + R fixture (#286) |
| 3 | 7e72904 | test(20-01): pin #286 by value in the correctness suite; docs + CHANGELOG |

`git rev-list --count 1295572..HEAD` = 4. The fourth commit, fea14ea ("docs: insert Phase 20.1 API Compatibility Gate"), is a planning commit made between Task 2 and Task 3 by another workflow. It is not part of this plan.

## Verification (observed, build at 7e72904's code, DuckDB v2.0.0)

- Tracer query (Task 1): prints `true|stable_seasonal|true`.
- `cargo test -p anofox-fcst-core --lib seasonality`: `ok. 22 passed; 0 failed`.
- `cargo test -p anofox-fcst-core --test stl_reference`: `ok. 4 passed; 0 failed`.
- `issue286_weekly84` appears once in cases.json. All 9 cases present at base 9d11361 are unchanged (0 diffs), and 1 case was added.
- `detection_decomposition.test`: All tests passed (70 assertions).
- Full native suite `HOME=$(mktemp -d) ./build/release/test/unittest "$PWD/test/*"`: All tests passed (5701 assertions in 94 test cases).
- `make check-docs`: 345 PASS, 0 FAIL, 8 EXPECTED-ERROR, 154 SYNOPSIS, 4 SKIP-NETWORK, 0 STALE.
- `make audit` exit 0. `cargo fmt --all -- --check` exit 0. `cargo clippy --workspace --all-targets -- -D warnings` exit 0.

## Pre-existing assertions whose expected value changed

None. Every listed file passes unmodified on the fixed build: ts_classify_seasonality (39 assertions), ts_detection_reference (147), ts_seasonality (13), ts_detrend (41), ts_periods_specialized (74), ts_detection_null_policy (24). The existing sine120 and deterministic-noise pins in detection_decomposition.test are unchanged and still pass; the noise series still has is_seasonal = false. No seasonal expectation flipped to non-seasonal.

## API compatibility (Phase 20.1 additive-only rule)

20-01 changes values only, not schemas.
- `git diff 9d11361 HEAD -- src/ crates/anofox-fcst-ffi/` touches only the `seasonal_strength` finalize hunks in ts_classify_seasonality_agg.cpp and ts_seasonality.cpp. No `push_back`/`make_pair` type definitions, parameters or registrations changed, and the FFI crate is untouched.
- Observed `DESCRIBE ts_classify_seasonality_by(...)`: id, timing_classification, modulation_type, has_stable_timing, timing_variability, seasonal_strength DOUBLE, is_seasonal, cycle_strengths DOUBLE[], weak_seasons BIGINT[]. The `typeof(ts_classify_seasonality_agg(...))` struct has the same 8 fields.
- Value-semantics change to note: for a degenerate series (constant, or no usable period), classify's `seasonal_strength` is now SQL NULL where it used to be NaN. The column and its DOUBLE type are unchanged. Callers that test `isnan(seasonal_strength)` would now see NULL. This is recorded in the CHANGELOG under Fixed.

## Deviations from Plan

### Auto-fixed Issues (continuation run, Task 3)

**1. [Rule 1 - Bug] Duplicate `### Changed` heading in CHANGELOG [Unreleased]**
- **Found during:** Task 3 diff review
- **Issue:** The new amplitude-modulation entry had been inserted with its own `### Changed` heading directly above the existing one.
- **Fix:** Merged both into a single `### Changed` section.
- **Commit:** 7e72904

**2. [Rule 1 - Bug] CHANGELOG claimed `ts_detect_amplitude_modulation` returns NULL strength**
- **Found during:** Task 3 diff review
- **Issue:** ts_detrend.cpp:839 writes the FFI value through without NaN-to-NULL conversion. On the build, `ts_detect_amplitude_modulation(list(5.0), 7.0)` over 84 rows returned `seasonal_strength = nan` (isnan true, IS NULL false).
- **Fix:** Corrected the entry to say `NaN`. Behaviour was not changed, because NULL marshalling is planned only for the classify functions.
- **Commit:** 7e72904

**3. [Rule 3 - Blocking] Acceptance grep needed table name `w`**
- **Issue:** The #286 block used table `issue286`, so `grep -c "ts_classify_seasonality_by(.w., id, ds, y, 7.0)"` returned 0.
- **Fix:** Renamed the block's table to `w`, as the plan specifies (no collision in the file). Re-run: 70 assertions pass, and the grep count is 2.
- **Commit:** 7e72904

## Deferred / out of scope

- docs/api/05a-decomposition.md lists `weak_seasons` as `INTEGER[]`, but the observed type is `BIGINT[]`. The SKILL.md already says BIGINT[]. This predates the plan and was not changed.

## Self-Check: PASSED

- FOUND: 290c39f, 7b2c6f7, 7e72904 (git log)
- FOUND: crates/anofox-fcst-core/src/seasonality.rs (contains stl_strengths, change.rs:579)
- FOUND: validation/reference/fixtures/r_stl_strength/cases.json (issue286_weekly84)
- FOUND: test/sql/correctness/detection_decomposition.test (#286 block)
