---
phase: 12-statistics-marshalling-moves-to-rust
plan: 02
subsystem: api
tags: [rust, ffi, duckdb, diagnostics, periods, seasonality, null-handling, cpp]

requires:
  - phase: 12-statistics-marshalling-moves-to-rust
    plan: 01
    provides: "src/include/ts_list_marshal.hpp's ListDoubleReader/NestedListDoubleReader
      and crates/anofox-fcst-core/src/missing.rs's trim_edges_reject_interior (written
      but not yet wired to any caller) — this plan is the first consumer"

provides:
  - "anofox_ts_series_gate FFI entry point: the one Rust-owned single-series
    missing-data policy (trim leading/trailing NULL/NaN, error on interior NULL
    naming its 1-based position, empty -> caller emits NULL) — D-03/D-04"
  - "ApplySingleSeriesPolicy, ListDoubleReader::ReadSingleSeries, ScalarArgReader<T>
    in src/include/ts_list_marshal.hpp — reusable by every remaining Phase 12 plan
    (changepoints, peaks, decomposition, detrend, features)"
  - "Every diagnostic (ts_adf/ts_kpss/ts_stationarity/ts_ljung_box/ts_durbin_watson/
    ts_jarque_bera/ts_residual_diagnostics), period estimator (12 SQL names across
    13 call sites) and seasonality function (ts_detect_seasonality/
    ts_analyze_seasonality x2/ts_classify_seasonality) routed through the gate"
  - "Every scalar parameter of the touched functions (method strings, thresholds,
    window/candidate counts, period) read per row via ScalarArgReader instead of
    row-0/FlatVector indexing — fixes constant-argument correctness for row > 0"
  - "test/sql/ts_series_null_policy.test (29 non-json-gated assertions) + phase
    example SECTION 2 proving the policy end-to-end against the built extension"
affects: [12-03, 12-04, 12-05, 12-06, 12-07]

actuals:
  tokens: 21600
  tasks: 3
  commits: 5
  plan_head_before: 8fbfd1263cb8ef7b7fa80669498cb67dc58afcb4

tech-stack:
  added: []
  patterns:
    - "ApplySingleSeriesPolicy/ReadSingleSeries: one Rust FFI call
      (anofox_ts_series_gate) owns the entire single-series missing-data decision;
      C++ only marshals values+validity and slices the returned [start,start+len)
      range — mirrors Phase 12-01's complete_cases pattern for the single-series
      (rather than paired) case"
    - "ScalarArgReader<T>: ToUnifiedFormat-based per-row scalar parameter read,
      constructed once per chunk; replaces every row-0-only
      FlatVector::GetData(...)[0] / [row_idx] read on a potentially-constant
      parameter vector (T-12-07)"

key-files:
  created:
    - test/sql/ts_series_null_policy.test
  modified:
    - crates/anofox-fcst-ffi/src/lib.rs
    - crates/anofox-fcst-ffi/tests/null_policy_ffi.rs
    - src/include/anofox_fcst_ffi.h
    - src/include/ts_list_marshal.hpp
    - src/scalar_functions/diagnostics.cpp
    - src/table_functions/ts_periods.cpp
    - src/table_functions/ts_seasonality.cpp
    - examples/data_preparation/null_handling_examples.sql
    - docs/api/10-diagnostics.md
    - docs/api/05-period-detection.md
    - plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md
    - CHANGELOG.md
    - .planning/phases/12-statistics-marshalling-moves-to-rust/snippets/expected_errors.txt
    - .planning/phases/12-statistics-marshalling-moves-to-rust/snippets/pass-list-12-02.tsv

key-decisions:
  - "ListDoubleReader::ReadSingleSeries folds the IsNull(row) + ApplySingleSeriesPolicy
    call into one method (plan's action text showed them as two explicit checks);
    simplified since ReadSingleSeries already checks IsNull internally — same
    behavior, less duplicated code at each of the 20+ call sites."
  - "TsDetectPeriodsFunction's method string argument now reads via
    Get(row).GetString() into a std::string, not string_t::GetData() as a raw
    C pointer — the interfaces block flagged this as the correct pattern; the
    original code's raw GetData() pointer was not guaranteed NUL-terminated for
    longer strings (Rule 1, latent bug fixed while already touching the line)."

requirements-completed: [RUST-01, RUST-02]

coverage:
  - id: D1
    description: "A single-series analysis given a list with an interior NULL raises
      an SQL error naming the position (e.g. ts_adf on a list whose 6th element is
      NULL -> 'interior NULL at position 6') instead of splicing the remaining
      values together"
    requirement: RUST-02
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-ffi/tests/null_policy_ffi.rs#gate_interior_null_errors_with_one_based_position"
        status: pass
      - kind: integration
        ref: "test/sql/ts_series_null_policy.test (5 'Interior NULL' assertions: ts_adf/ts_kpss/ts_ljung_box/ts_estimate_period_fft/ts_detect_seasonality)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Leading/trailing NULLs are trimmed in Rust before the analysis:
      every diagnostic, every ts_periods.cpp estimator and every ts_seasonality.cpp
      function returns the same result for [NULL] || v || [NULL] as for v"
    requirement: RUST-02
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-ffi/tests/null_policy_ffi.rs#gate_trims_leading_and_trailing_missing"
        status: pass
      - kind: integration
        ref: "test/sql/ts_series_null_policy.test (12 edge-trim-parity assertions covering all 7 diagnostics, both period estimators, 3 seasonality functions)"
        status: pass
    human_judgment: false
  - id: D3
    description: "A list with no non-NULL value returns NULL, not an error"
    requirement: RUST-02
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-ffi/tests/null_policy_ffi.rs#gate_all_invalid_or_zero_length_returns_empty_not_error"
        status: pass
      - kind: integration
        ref: "test/sql/ts_series_null_policy.test (all-NULL -> NULL section: ts_adf, ts_estimate_period_fft)"
        status: pass
    human_judgment: false
  - id: D4
    description: "Constant/literal list arguments AND constant scalar parameters
      (e.g. ts_classify_seasonality(v, 12.0) over several groups) give the correct
      per-row result for every row of a chunk"
    requirement: RUST-01
    verification:
      - kind: integration
        ref: "test/sql/ts_series_null_policy.test (constant list: ts_kpss over 4 cross-joined rows; constant scalar: ts_classify_seasonality period=12.0 over 3 groups)"
        status: pass
    human_judgment: false
  - id: D5
    description: "The interior-NULL / trim / empty decision is made by Rust
      (anofox_ts_series_gate -> missing::trim_edges_reject_interior); C++ only reads
      lists with the shared reader and slices"
    requirement: RUST-02
    verification:
      - kind: unit
        ref: "grep -c 'fn anofox_ts_series_gate' crates/anofox-fcst-ffi/src/lib.rs == 1, calls trim_edges_reject_interior"
        status: pass
      - kind: unit
        ref: "grep -c 'ExtractListAsDoubleLocal\\|ExtractListAsDouble\\|IsValueNull' across diagnostics.cpp/ts_periods.cpp/ts_seasonality.cpp == 0"
        status: pass
    human_judgment: false
  - id: D6
    description: "ts_adf_by (grouped macro path) on a table with a leading-NULL
      group matches the per-group scalar ts_adf on the trimmed values"
    requirement: RUST-02
    verification:
      - kind: integration
        ref: "test/sql/ts_series_null_policy.test (ts_adf_by macro path section)"
        status: pass
    human_judgment: false

duration: ~41min
completed: 2026-10-09
status: complete
---

# Phase 12 Plan 02: Single-Series NULL Policy — Diagnostics, Periods, Seasonality Summary

**One Rust FFI gate (`anofox_ts_series_gate`) now owns interior-NULL rejection, edge trimming, and all-NULL-to-NULL for every diagnostic, period estimator and seasonality function; every scalar parameter of those functions reads per row instead of row 0.**

## Performance

- **Duration:** ~41 min
- **Tasks:** 3 (all completed)
- **Files modified/created:** 15 (across 3 task commits + this metadata commit)

## Accomplishments

- Added `anofox_ts_series_gate` to the FFI boundary: a thin, `catch_unwind`-wrapped
  call into `anofox_fcst_core::trim_edges_reject_interior` (built in plan 12-01) —
  trims leading/trailing NULL/NaN, errors on any remaining interior missing value
  naming its 1-based position in the ORIGINAL list, and reports empty (not an error)
  for an all-missing input.
- Added `ApplySingleSeriesPolicy`, `ListDoubleReader::ReadSingleSeries` and
  `ScalarArgReader<T>` to the shared `ts_list_marshal.hpp` header — the second is a
  reusable per-row, `ToUnifiedFormat`-based scalar-parameter reader that fixes a
  constant-vector correctness bug (T-12-07) present in every one of these files.
- Ported all 7 diagnostic functions (`diagnostics.cpp`), all 13 series call sites
  across 12 registered period-estimator SQL names (`ts_periods.cpp`), and all 4
  seasonality function bodies (`ts_seasonality.cpp`) onto the shared reader; deleted
  3 separate file-local, buggy `ExtractListAsDouble`/`IsValueNull` helper copies.
- Converted every optional/threshold scalar parameter in the touched files
  (`max_lags`, `lags`, `alpha`, `max_period`, `min_confidence`, `tolerance`, method
  strings, `max_lag`, `min_strength`, `acf_threshold`, `min/max_period`,
  `n_frequencies`/`n_candidates`, `window_size`/`n_components`,
  `subsequence_length`/`n_best`, `zero_pad_factor`, `period`, `strength_threshold`,
  `timing_threshold`) from row-0-only `FlatVector::GetData(...)[0]` reads to
  per-row `ScalarArgReader<T>` reads.
- Proved the whole chain natively in SQL: edge-trim parity for 12 functions, an
  "interior NULL at position 6" error for 5 representative functions, all-NULL ->
  `NULL`, constant-list and constant-scalar-parameter correctness across rows, and
  `ts_adf_by` macro parity on a leading-NULL group — 29 assertions, no `require json`
  gate.
- Updated `docs/api/10-diagnostics.md` and `docs/api/05-period-detection.md`
  (replacing the "fix planned for v0.10.0, Phase 12" splice-NULLs note with the new
  rule, each with a runnable example verified against the built extension), the
  detection skill's gotchas, and the CHANGELOG (Breaking changes + Fixed).

## Task Commits

1. **Task 1: Tracer — anofox_ts_series_gate + ApplySingleSeriesPolicy/ReadSingleSeries/ScalarArgReader, wired through every diagnostic** - `cf4c324` (feat)
2. **Task 2: Period estimators and seasonality functions through the gate; list + scalar params read per row** - `b875c3a` (feat)
3. **Task 3: Build once, SQL proof, example section, diagnostics/period docs + detection skill + CHANGELOG** - `804687c` (docs)

**Plan metadata:** (this commit, below)

## Files Created/Modified

- `crates/anofox-fcst-ffi/src/lib.rs` - `anofox_ts_series_gate` FFI function
- `crates/anofox-fcst-ffi/tests/null_policy_ffi.rs` - 5 new gate ABI tests (15 total in file)
- `src/include/anofox_fcst_ffi.h` - regenerated header (cbindgen)
- `src/include/ts_list_marshal.hpp` - `ApplySingleSeriesPolicy`, `ReadSingleSeries`, `ScalarArgReader<T>`
- `src/scalar_functions/diagnostics.cpp` - all 7 diagnostics ported, helper deleted
- `src/table_functions/ts_periods.cpp` - all 13 series call sites + every scalar param ported, 2 helpers deleted
- `src/table_functions/ts_seasonality.cpp` - all 4 function bodies ported, helper deleted
- `test/sql/ts_series_null_policy.test` - 29 non-json-gated value assertions
- `examples/data_preparation/null_handling_examples.sql` - SECTION 2 appended
- `docs/api/10-diagnostics.md`, `docs/api/05-period-detection.md` - NULL-handling rule + runnable examples
- `plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md` - gotcha bullet
- `CHANGELOG.md` - Breaking changes + Fixed entries
- `.planning/phases/12-.../snippets/expected_errors.txt`, `pass-list-12-02.tsv` - snippet harness bookkeeping

## Decisions Made

- `ReadSingleSeries` combines the plan's two explicit checks (`IsNull` then
  `ApplySingleSeriesPolicy`) into one method call at every site — equivalent
  behavior, less duplicated boilerplate across ~20 call sites.
- `TsDetectPeriodsFunction`'s method-string argument now reads via
  `ScalarArgReader<string_t>::Get(row).GetString()` into an owned `std::string`
  rather than treating `string_t::GetData()` as a NUL-terminated C pointer — the
  interfaces block's explicit guidance; also fixes a latent correctness risk in
  the original code (Rule 1).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `TsDetectPeriodsFunction`'s method string read as a raw, possibly non-NUL-terminated pointer**
- **Found during:** Task 2 (ts_periods.cpp)
- **Issue:** Original code passed `string_t::GetData()`'s raw pointer directly to the
  FFI as a C string; DuckDB's `string_t` does not guarantee the underlying buffer is
  NUL-terminated for strings stored out-of-line.
- **Fix:** Read via `ScalarArgReader<string_t>::Get(row).GetString()` into an owned
  `std::string`, passing `.c_str()` to the FFI — guaranteed NUL-terminated.
- **Files modified:** `src/table_functions/ts_periods.cpp`
- **Verification:** `_ts_detect_periods` compiled and exercised via `make test_release`.
- **Committed in:** `b875c3a` (Task 2 commit)

**2. [Plan-verification mismatch, not a code bug] null_policy_ffi.rs test-count acceptance criterion**
- **Found during:** Task 1 verify gate
- **Issue:** The plan's acceptance criterion expected "at least 19 tests pass (12-01's
  14 + 5 gate tests)"; 12-01's actual SUMMARY recorded 10 pre-existing tests (not 14),
  confirmed by running the file before any edit. Adding the 5 required gate tests
  brings the file to 15, not 19.
- **Resolution:** Verified all 5 new gate-behavior tests pass (15/15 total) and that
  every behavior in the plan's `<behavior>` block for Task 1 is covered by a test.
  The `19` figure was an incorrect count carried over from the plan's own estimate of
  12-01's test count, not a functional gap in this plan's work — documented here
  rather than inflating the test file with redundant tests to hit an arbitrary number.
- **Files modified:** none beyond the 5 planned gate tests already added.
- **Committed in:** `cf4c324` (Task 1 commit)

---

**Total deviations:** 2 (1 auto-fixed bug, 1 documented plan-acceptance-criterion mismatch)
**Impact on plan:** No scope creep. Both are either a direct correctness improvement
following the plan's own explicit guidance, or a pre-existing miscount in the plan
text that does not reflect a gap in the delivered behavior.

## Issues Encountered

- Another executor (Phase 11, concurrent) committed twice (`ee7b457`, `877d64b`)
  directly between this plan's Task 2 and Task 3 commits in the shared working tree.
  Every commit in this plan used `git commit --only <exact paths>` (never `git add .`
  / `-A`), so no Phase 11 files were ever staged or touched by this plan's commits.
  The plan-commit ledger's measured count (`git rev-list --count
  8fbfd1263cb8ef7b7fa80669498cb67dc58afcb4..HEAD` = 5) includes those 2 concurrent
  commits; this plan's own commits are exactly the 3 listed above (`cf4c324`,
  `b875c3a`, `804687c`). Recorded as `commits: 5` per the ledger-measurement
  contract — the discrepancy is attributable to the documented concurrent-executor
  setup, not an uncommitted-change gap (working tree was clean of this plan's tracked
  changes at every commit boundary).
- `json` DuckDB extension autoloads successfully in this environment (network
  available), so `ts_periods.test` / `ts_seasonality.test` (both `require json`)
  could in principle run — but running them showed they are still skipped by the
  `unittest` binary specifically (no autoload inside that harness), consistent with
  the Phase 12 research's finding. No stale NULL-related expectations were found in
  any `require json`-gated file that exercises these functions with an interior-NULL
  list (checked via grep); every existing NULL-related assertion in those files tests
  a whole-list-NULL case, which is unaffected by this plan.

## Next Phase Readiness

- `anofox_ts_series_gate`, `ApplySingleSeriesPolicy`, `ReadSingleSeries` and
  `ScalarArgReader<T>` are ready for direct reuse by plan 12-03 (changepoints, peaks,
  decomposition, detrend, features — per the Phase 12 research, all still read their
  single series through their own private, buggy copies).
- Plan 12-03's aggregates/table functions that build their own `vector<double>`
  (rather than reading a `LIST(DOUBLE)` argument via `ListDoubleReader`) should call
  `ApplySingleSeriesPolicy` directly, per its doc comment.
- No blockers for plan 12-03.

---
*Phase: 12-statistics-marshalling-moves-to-rust*
*Completed: 2026-10-09*

## Self-Check: PASSED

All 15 created/modified files confirmed on disk. All three task commits confirmed
in git history (`cf4c324`, `b875c3a`, `804687c`).
