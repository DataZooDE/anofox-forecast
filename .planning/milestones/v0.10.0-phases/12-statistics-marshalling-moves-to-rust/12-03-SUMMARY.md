---
phase: 12-statistics-marshalling-moves-to-rust
plan: 03
subsystem: api
tags: [rust, ffi, duckdb, decomposition, peaks, changepoints, features, null-handling, cpp]

requires:
  - phase: 12-statistics-marshalling-moves-to-rust
    plan: 02
    provides: "ApplySingleSeriesPolicy, ListDoubleReader::ReadSingleSeries, ScalarArgReader<T>
      in src/include/ts_list_marshal.hpp — the Rust single-series gate
      (anofox_ts_series_gate) this plan reuses for every remaining detection/
      decomposition scalar and for the features aggregate's own validity vector"

provides:
  - "ts_features_agg / ts_features_by share one Rust single-series NULL policy (D-11):
    the aggregate no longer splices NULL-valued rows — it carries a validity vector
    through Update/Combine/Finalize and calls ApplySingleSeriesPolicy before
    anofox_ts_features, agreeing with ts_features_by on the same series"
  - "Every remaining single-series detection/decomposition scalar
    (ts_detrend, ts_decompose_seasonal, ts_seasonal_strength,
    ts_seasonal_strength_windowed, ts_detect_seasonality_changes,
    ts_instantaneous_period, ts_detect_amplitude_modulation, ts_detect_peaks,
    ts_analyze_peak_timing, _ts_mstl_decomposition, _ts_detect_changepoints_bocpd)
    reads its LIST(DOUBLE) via ListDoubleReader::ReadSingleSeries and every scalar
    parameter per row via ScalarArgReader<T> instead of row-0-only FlatVector reads"
  - "The single-series ts_detect_changepoints macro stays row-aligned under edge
    trimming via a `_lead` CTE shift guarded against DuckDB's negative-index-from-end
    list semantics"
  - "Dead, never-registered scalar bodies deleted (ts_features.cpp's TsFeaturesFunction,
    ts_changepoints.cpp's TsDetectChangepointsFunction/TsDetectChangepointsWithParamsFunction)
    with their private ExtractListAsDouble copies — zero SQL surface change"
  - "test/sql/ts_detection_null_policy.test (24 non-json-gated assertions) + phase
    example SECTION 3 proving D-11 and the detection NULL policy end-to-end against
    the built extension"
affects: [12-04, 12-05, 12-06, 12-07, 12-08]

actuals:
  tokens: 19700
  tasks: 3
  commits: 5
  plan_head_before: 535589d7fb55ac9d161fb162b6241cfcc8814ce3

tech-stack:
  added: []
  patterns:
    - "Aggregate validity vector: TsFeaturesAggStateData/TsFeaturesGroupData carry a
      per-row `vector<bool> valid` alongside timestamps/values; Finalize sorts
      (timestamp, value, valid) triples together before building the bitmask handed
      to ApplySingleSeriesPolicy — mirrors the scalar ListDoubleReader pattern for
      aggregate/table-function state that isn't built from a LIST(DOUBLE) argument"
    - "ReadSingleSeries + ScalarArgReader<T> reused verbatim across 11 scalar
      functions in 4 files — the second consumer wave after plan 12-02's diagnostics/
      periods/seasonality, confirming the shared helper's reusability"

key-files:
  created:
    - test/sql/ts_detection_null_policy.test
  modified:
    - src/aggregate_functions/ts_features_agg.cpp
    - src/table_functions/ts_features_native.cpp
    - src/table_functions/ts_features.cpp
    - src/table_functions/ts_detrend.cpp
    - src/table_functions/ts_peaks.cpp
    - src/table_functions/ts_decomposition.cpp
    - src/table_functions/ts_changepoints.cpp
    - src/macros/ts_macros.cpp
    - examples/data_preparation/null_handling_examples.sql
    - docs/api/05a-decomposition.md
    - docs/api/05b-peak-detection.md
    - docs/api/06-changepoint-detection.md
    - docs/api/20-feature-extraction.md
    - plugins/anofox-forecast/skills/anofox-forecast-eda/SKILL.md
    - plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md
    - CHANGELOG.md
    - .planning/phases/12-statistics-marshalling-moves-to-rust/snippets/pass-list-12-03.tsv

key-decisions:
  - "ts_features_native.cpp's TsFeaturesFeatureResult gained a new `all_null` bool
    field (not in the plan's interfaces block) so the output-emission loop can write
    SQL NULL instead of the pre-existing NaN-default for an all-NULL group (D-04) —
    the plan's action text described the behavior but the exact struct field was
    Claude's discretion."
  - "The plan's own acceptance grep for ts_changepoints.cpp
    (`grep -c \"TsDetectChangepointsFunction|TsDetectChangepointsWithParamsFunction\"`
    expecting 0) is unsatisfiable while also honoring the plan's explicit instruction
    to keep `RegisterTsDetectChangepointsFunction` as a no-op — that identifier's name
    contains the banned substring. Resolved by deleting both dead function bodies and
    minimizing comment text; the grep now matches only the one line the plan itself
    requires to be kept (count 1, not 0) — documented here rather than silently
    renaming the kept no-op or leaving noisy duplicate comment text."
  - "The single-series ts_detect_changepoints macro's `_lead` CTE uses
    `COALESCE(MIN(_idx), (SELECT COUNT(*) FROM ordered_data) + 1) - 1` so an
    all-NULL input also produces a safe (if degenerate) _lead value instead of
    NULL propagating through the downstream BETWEEN guard."

requirements-completed: [RUST-01, RUST-02, RUST-08]

coverage:
  - id: D1
    description: "ts_features_agg no longer splices NULL-valued rows: on a series
      with leading/trailing NULLs it equals ts_features_agg on the trimmed series
      AND ts_features_by on the same table (D-11)"
    requirement: RUST-02
    verification:
      - kind: integration
        ref: "test/sql/ts_detection_null_policy.test (three-way mean + variance parity assertions)"
        status: pass
      - kind: e2e
        ref: "examples/data_preparation/null_handling_examples.sql (SECTION 3, PASS features_agg_matches_trimmed_and_features_by)"
        status: pass
    human_judgment: false
  - id: D2
    description: "On a series with an interior NULL, both ts_features_agg and
      ts_features_by raise 'interior NULL at position k' (D-03)"
    requirement: RUST-02
    verification:
      - kind: integration
        ref: "test/sql/ts_detection_null_policy.test (interior-NULL statement error assertions for both surfaces)"
        status: pass
    human_judgment: false
  - id: D3
    description: "An all-NULL group gives a NULL features struct from ts_features_agg
      and NULL feature values from ts_features_by (D-04)"
    requirement: RUST-02
    verification:
      - kind: integration
        ref: "test/sql/ts_detection_null_policy.test (all-NULL -> NULL assertions for both surfaces)"
        status: pass
    human_judgment: false
  - id: D4
    description: "ts_detrend, ts_decompose_seasonal, ts_seasonal_strength,
      ts_seasonal_strength_windowed, ts_detect_seasonality_changes,
      ts_instantaneous_period, ts_detect_amplitude_modulation, ts_detect_peaks,
      ts_analyze_peak_timing, _ts_mstl_decomposition and
      _ts_detect_changepoints_bocpd trim edge NULLs, error on interior NULLs naming
      the position, return NULL for all-NULL input, and read every argument per row
      (constant-vector safe) (D-01, D-03, D-04)"
    requirement: RUST-01
    verification:
      - kind: unit
        ref: "ninja compile-check + grep counters: 0 private ExtractListAsDouble helpers, 0 row-indexed/row-0 FlatVector parameter reads, 11 ReadSingleSeries call sites across the four files"
        status: pass
      - kind: integration
        ref: "test/sql/ts_detection_null_policy.test (edge-trim parity + interior-NULL error assertions for 8 functions; constant-period-per-row assertion)"
        status: pass
    human_judgment: false
  - id: D5
    description: "The single-series ts_detect_changepoints macro maps BOCPD flags
      back to the right input rows when leading NULL rows exist"
    requirement: RUST-08
    verification:
      - kind: e2e
        ref: "examples/data_preparation/null_handling_examples.sql (SECTION 3, PASS changepoints_macro_row_aligned_under_leading_nulls — json-gated, run via duckdb -unsigned CLI since the unittest harness has no json autoload)"
        status: pass
    human_judgment: false
  - id: D6
    description: "The dead, never-registered list-taking bodies in ts_features.cpp
      and ts_changepoints.cpp are deleted together with their private list helpers"
    verification:
      - kind: unit
        ref: "grep -c 'static void ExtractListAsDouble' ts_features.cpp == 0; TsDetectChangepointsFunction/TsDetectChangepointsWithParamsFunction bodies absent from ts_changepoints.cpp (only the required no-op RegisterTsDetectChangepointsFunction identifier remains)"
        status: pass
    human_judgment: false

duration: ~24min
completed: 2026-10-09
status: complete
---

# Phase 12 Plan 03: Features Aggregate Parity + Remaining Detection/Decomposition NULL Policy Summary

**ts_features_agg/ts_features_by now agree on the same series under the Rust single-series NULL policy (D-11), and every remaining detrend/decomposition/peak/MSTL/BOCPD scalar reads its list and scalar arguments through the same shared gate plan 12-02 built, with the single-series changepoints macro re-aligned under edge trimming.**

## Performance

- **Duration:** ~24 min
- **Tasks:** 3 (all completed)
- **Files modified/created:** 17 (across 3 task commits + this metadata commit)

## Accomplishments

- `ts_features_agg.cpp`: `TsFeaturesAggStateData` now carries a `vector<bool> valid`
  alongside timestamps/values; `Update` only skips a row when its TIMESTAMP is NULL
  (a NULL VALUE is kept with `valid=false`); `Combine` (both the template operation's
  and the free-function's) appends validity; `Finalize` sorts `(timestamp, value,
  valid)` triples together, builds the bitmask, and calls `ApplySingleSeriesPolicy`
  before `anofox_ts_features` — the old `continue`-on-NULL-value splice is gone.
- `ts_features_native.cpp`: `TsFeaturesGroupData` carries a per-row valid flag instead
  of writing `NaN` for a NULL input value; `Finalize` applies the identical
  `ApplySingleSeriesPolicy("ts_features_by")` gate on the sorted series. An interior
  NULL now propagates as an SQL error (previously silently spliced); an all-NULL
  group now emits NULL feature values via a new `TsFeaturesFeatureResult::all_null`
  flag (previously NaN).
- `ts_features.cpp`: deleted the never-registered `TsFeaturesFunction` scalar body and
  its private `ExtractListAsDouble`/`GetScalarFeatureNames`/`GetScalarFeaturesResultType`
  helpers; `RegisterTsFeaturesFunction` stays the existing no-op.
- `ts_detrend.cpp` (7 functions), `ts_peaks.cpp` (2 functions), `ts_decomposition.cpp`
  (1 function) ported every list read to `ListDoubleReader::ReadSingleSeries` and every
  scalar/option parameter (period, method, window_size, thresholds, min_duration,
  modulation thresholds, min_distance/min_prominence/smooth_first, insufficient_data
  mode) to per-row `ScalarArgReader<T>` — fixing both the NULL-splice bug and the
  constant-vector-only-row-0 bug in the same pass. Method strings now go through
  `GetString()` instead of treating `string_t::GetData()` as a NUL-terminated C string.
- `ts_changepoints.cpp`: `_ts_detect_changepoints_bocpd` ported the same way
  (hazard_lambda/include_probabilities per row); deleted the two never-registered
  scalar bodies (`TsDetectChangepointsFunction`, `TsDetectChangepointsWithParamsFunction`)
  and their shared private list helper. The native table path
  (`ts_detect_changepoints_by`) is untouched, out of scope per plan (deferred to 12-08).
- `ts_macros.cpp`: the single-series `ts_detect_changepoints` macro now computes a
  `_lead` CTE (count of leading rows before the first non-NULL value) and shifts the
  BOCPD list index by it, guarded with `BETWEEN 1 AND len(...)` so leading-NULL rows
  get NULL flags and DuckDB's negative-index-from-end semantics can never fire.
- Proved the whole chain natively: `test/sql/ts_detection_null_policy.test` (24
  assertions, no `require json` gate) covers the three-way features parity, both
  interior-NULL errors, both all-NULL-to-NULL cases, edge-trim parity + interior-NULL
  errors for 8 detection/decomposition functions, and constant-period-per-row
  correctness. The phase example's new SECTION 3 proves the features parity and (via
  `LOAD json` + the `duckdb -unsigned` CLI, since the `unittest` harness has no `json`
  autoload) the changepoints macro's row-alignment under leading NULLs.
- Updated `docs/api/05a-decomposition.md`, `05b-peak-detection.md`,
  `06-changepoint-detection.md`, `20-feature-extraction.md`, the eda + detection
  skills, and the CHANGELOG (Breaking changes + Fixed) — every new SQL snippet
  verified against the built extension.

## Task Commits

1. **Task 1: Tracer — ts_features_agg keeps validity and goes through the Rust gate; ts_features_by native path gets the identical policy; dead features scalar body deleted** - `06aa1ae` (feat)
2. **Task 2: Detrend/decomposition, peaks, MSTL and BOCPD through the gate with per-row argument reads; dead changepoint bodies deleted; changepoints macro re-aligned** - `69a2ff6` (feat)
3. **Task 3: Build once, SQL proof incl. D-11 features parity, example section, detection/decomposition/feature docs + skills + CHANGELOG** - `9fa7764` (docs)

**Plan metadata:** (this commit, below)

## Files Created/Modified

- `src/aggregate_functions/ts_features_agg.cpp` - validity-tracking aggregate state + Rust gate
- `src/table_functions/ts_features_native.cpp` - `ts_features_by` native path, same policy
- `src/table_functions/ts_features.cpp` - dead scalar body + helpers deleted
- `src/table_functions/ts_detrend.cpp` - 7 functions ported to the shared reader + gate
- `src/table_functions/ts_peaks.cpp` - 2 functions ported
- `src/table_functions/ts_decomposition.cpp` - MSTL scalar ported
- `src/table_functions/ts_changepoints.cpp` - BOCPD scalar ported, dead bodies deleted
- `src/macros/ts_macros.cpp` - `ts_detect_changepoints` macro re-aligned (`_lead` CTE)
- `test/sql/ts_detection_null_policy.test` - 24 non-json-gated value assertions
- `examples/data_preparation/null_handling_examples.sql` - SECTION 3 appended
- `docs/api/05a-decomposition.md`, `05b-peak-detection.md`,
  `06-changepoint-detection.md`, `20-feature-extraction.md` - NULL-handling rule +
  runnable examples
- `plugins/anofox-forecast/skills/anofox-forecast-eda/SKILL.md` - features gotcha bullet
- `plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md` - extended gotcha bullet
- `CHANGELOG.md` - Breaking changes + Fixed entries
- `.planning/phases/12-.../snippets/pass-list-12-03.tsv` - snippet harness output

## Decisions Made

- `TsFeaturesFeatureResult` gained a new `all_null` bool field so the table path can
  emit SQL NULL (not NaN) for an all-NULL group, per D-04 — exact struct shape was
  Claude's discretion within the plan's described behavior.
- Kept `RegisterTsDetectChangepointsFunction`'s identifier as-is (plan's explicit
  instruction), which means the plan's own "grep for
  TsDetectChangepointsFunction|TsDetectChangepointsWithParamsFunction prints 0"
  acceptance criterion is structurally unsatisfiable (that identifier's name contains
  the banned substring as a prefix match) — documented rather than silently working
  around it by renaming the kept no-op.
- `_lead` CTE uses `COALESCE(MIN(_idx), (SELECT COUNT(*) FROM ordered_data) + 1) - 1`
  so an all-NULL series produces a safe large `_lead` (every row falls outside the
  trimmed range) instead of a NULL propagating through the `BETWEEN` guard.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Example script SQL comments used `--` instead of sqllogictest's `#` comment syntax**
- **Found during:** Task 3, first run of `test/sql/ts_detection_null_policy.test`
- **Issue:** `-- fe: ...` style line comments in a new `.test` file are parsed by
  DuckDB's sqllogictest harness as an (invalid) directive, not a comment, causing
  "Unrecognized parameter --".
- **Fix:** Changed all four `-- ...` fixture comments to `# ...`.
- **Files modified:** `test/sql/ts_detection_null_policy.test`
- **Verification:** file re-ran clean, 24/24 assertions pass.
- **Committed in:** `9fa7764` (Task 3 commit)

**2. [Rule 1 - Bug] `ts_detect_changepoints` macro call in the phase example used the wrong arg count / wrong output column names**
- **Found during:** Task 3, running the phase example end-to-end
- **Issue:** The macro's signature is `(source, date_col, value_col, params)` — a
  required 4th `params` positional argument (no default) — and its output columns are
  `date_col`/`value_col` (not the original column names, unlike the `_by` grouped
  macros). The first draft of SECTION 3 omitted `params` and referenced `ds`/`val`.
- **Fix:** Added `NULL` as the 4th argument and referenced `date_col`/`value_col` in
  the `WHERE` clauses.
- **Files modified:** `examples/data_preparation/null_handling_examples.sql`
- **Verification:** `./build/release/duckdb -unsigned -bail < examples/...sql` prints
  `ALL SECTIONS PASSED`, exit 0, 0 FAIL lines.
- **Committed in:** `9fa7764` (Task 3 commit)

---

**Total deviations:** 2 auto-fixed (both Rule 1, both caught before commit by actually
running the new test/example files against the built extension).
**Impact on plan:** No scope creep — both are mechanical fixes required to make the
plan's own verification artifacts actually run.

## Issues Encountered

- Another executor (Phase 11, concurrent) committed twice (`641222e`, `535eac6`)
  directly between this plan's Task 2 and Task 3 commits in the shared working tree.
  Every commit in this plan used `git commit --only <exact paths>` (never `git add .`
  / `-A`), so no Phase 11 files were ever staged or touched by this plan's commits.
  The plan-commit ledger's measured count (`git rev-list --count
  535589d7fb55ac9d161fb162b6241cfcc8814ce3..HEAD` = 5) includes those 2 concurrent
  commits; this plan's own commits are exactly the 3 listed above (`06aa1ae`,
  `69a2ff6`, `9fa7764`). Recorded as `commits: 5` per the ledger-measurement contract —
  the discrepancy is attributable to the documented concurrent-executor setup, not an
  uncommitted-change gap (working tree was clean of this plan's tracked changes at
  every commit boundary).
- The `json` DuckDB extension is not bundled in `build/release`'s `unittest` binary in
  this environment (confirmed again, consistent with 12-01/12-02's finding): `make
  test_release` reports "All tests passed (50 skipped tests, 1496 assertions in 21 test
  cases)" with "require json: 50" — `ts_detrend.test`, `ts_decomposition.test`,
  `ts_peaks.test`, `ts_features.test` are among the skipped files, so this plan's
  changes to those live code paths are proven via `test/sql/ts_detection_null_policy.test`
  (non-gated) and the `duckdb -unsigned` CLI (which DOES autoload `json` over network)
  rather than via `unittest` running those specific gated files. Checked every one of
  those four gated files by hand for NULL-data list literals that this plan's
  edge-trim/interior-NULL policy change could have invalidated: every existing NULL
  assertion in them tests whole-list-NULL input (`ts_detrend(NULL)`,
  `_ts_mstl_decomposition(NULL)`, etc.), which is unaffected — none needed updated
  expectations.
- The plan's Task 2 acceptance criterion `grep -c
  "TsDetectChangepointsFunction|TsDetectChangepointsWithParamsFunction"
  src/table_functions/ts_changepoints.cpp` expecting `0` cannot be satisfied while also
  honoring the plan's own instruction to keep `RegisterTsDetectChangepointsFunction` as
  a no-op (that identifier's name contains `TsDetectChangepointsFunction` as a
  substring). Resolved pragmatically: both dead bodies are deleted, the explanatory
  comment was trimmed to avoid restating the banned substrings, and the grep now
  returns `1` (only the required kept identifier) rather than `0` — a plan-text
  self-contradiction, not a code defect.

## Next Phase Readiness

- `ApplySingleSeriesPolicy`/`ReadSingleSeries`/`ScalarArgReader<T>` have now been
  reused across two plans (12-02: 7 diagnostics + 13 period-estimator call sites + 4
  seasonality functions; 12-03: 11 more call sites across 4 files) with zero changes
  needed to the shared header — confirms the abstraction is stable for any remaining
  Phase 12 consumers.
- `ts_detect_changepoints_by` (the native grouped table path) was explicitly left
  untouched per plan — still reads lists via its own direct-collection logic (not
  `ListDoubleReader`) and still writes `row.value = value_val.IsNull() ? 0.0 : ...`
  without a validity concept. Recorded by the plan as deferred to 12-08; not
  rediscovered as new scope here.
- No blockers for plan 12-04.

---
*Phase: 12-statistics-marshalling-moves-to-rust*
*Completed: 2026-10-09*

## Self-Check: PASSED

All created/modified files confirmed on disk. All three task commits confirmed in git
history (`06aa1ae`, `69a2ff6`, `9fa7764`).
