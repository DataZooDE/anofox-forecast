---
phase: 12-statistics-marshalling-moves-to-rust
plan: 01
subsystem: api
tags: [rust, ffi, duckdb, metrics, null-handling, cpp]

requires:
  - phase: 10-forecast-wrapper-delegates-to-crate-models
    provides: edge-NULL trim conventions (prepare_series/finalize_output) and the
      established Rust-FFI-C++-SQL delivery pattern this plan extends to metrics

provides:
  - "src/include/ts_list_marshal.hpp: shared ListDoubleReader/NestedListDoubleReader
    (ToUnifiedFormat-based, constant/dictionary-safe) + WriteDoubleList/
    WriteDoubleListNanAsNull — the one C++ list-marshalling helper (D-01), reusable by
    every later Phase 12 plan (single-series gate, conformal/bootstrap, aggregates)"
  - "crates/anofox-fcst-core/src/missing.rs: Rust-owned missing-data policy
    (complete_cases D-02, trim_edges_reject_interior D-03/D-04, drop_missing)"
  - "All 12 metric FFIs (anofox_ts_mae/mse/rmse/mape/smape/r2/bias/rmae/mase/
    quantile_loss/mqloss/coverage) accept validity bitmasks and apply complete_cases
    before computing"
  - "test/sql/ts_list_null_policy.test (31 non-json-gated assertions) +
    examples/data_preparation/null_handling_examples.sql (self-checking phase example)"
  - "Phase-local snippet harness at .planning/phases/12-.../snippets/ for later plans"
affects: [12-02, 12-03, 12-04, 12-05, 12-06, 12-07]

actuals:
  tokens: 36300
  tasks: 3
  commits: 5

tech-stack:
  added: []
  patterns:
    - "ListDoubleReader/NestedListDoubleReader: construct once per chunk per list
      argument (ToUnifiedFormat in the constructor), call IsNull/Read per row —
      replaces every ad hoc ExtractListAsDouble copy"
    - "Rust missing-data policy centralization: C++ only marshals values+validity;
      complete_cases/trim_edges_reject_interior own every missing-data decision"
    - "FFI validity-pointer convention: const uint64_t *foo_validity immediately
      after each values pointer, nullable (null = all valid), mirrors anofox_ts_adf"

key-files:
  created:
    - crates/anofox-fcst-core/src/missing.rs
    - src/include/ts_list_marshal.hpp
    - crates/anofox-fcst-ffi/tests/null_policy_ffi.rs
    - test/sql/ts_list_null_policy.test
    - examples/data_preparation/null_handling_examples.sql
  modified:
    - crates/anofox-fcst-ffi/src/lib.rs
    - src/include/anofox_fcst_ffi.h
    - src/scalar_functions/metrics.cpp
    - src/table_functions/ts_metrics_native.cpp
    - src/table_functions/ts_data_quality.cpp
    - docs/api/09-evaluation-metrics.md
    - plugins/anofox-forecast/skills/anofox-forecast-backtest/SKILL.md
    - CHANGELOG.md

key-decisions:
  - "ts_metrics_native.cpp's pre-existing manual isnan-based joint row filtering
    (mase/rmae/coverage/quantile_loss call sites) was left in place rather than
    removed — it is functionally redundant with (but harmless alongside)
    complete_cases, and the plan's action items only required updating the FFI
    call signatures, not removing the C++-side filter"
  - "NestedListDoubleReader's NULL-inner-list case (a whole quantile level missing)
    is padded in TsMqlossFunction to actual.size() all-invalid rather than passed as
    a 0-length array, so the FFI always sees length-matched validity per level"

requirements-completed: [RUST-01, RUST-02]

coverage:
  - id: D1
    description: "ts_mae([1,NULL,3,4],[1,2,NULL,4]) returns 0.0 (pairwise-complete in
      Rust, not spliced in C++)"
    requirement: RUST-02
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-ffi/tests/null_policy_ffi.rs#mae_drops_incomplete_pairs_via_validity_bitmask"
        status: pass
      - kind: integration
        ref: "test/sql/ts_list_null_policy.test (ts_mae pairwise-complete assertion)"
        status: pass
    human_judgment: false
  - id: D2
    description: "ts_mae(a, [0,0,0]) is correct on every row of a 5-row table
      (constant/dictionary list arguments read via ToUnifiedFormat)"
    requirement: RUST-01
    verification:
      - kind: integration
        ref: "test/sql/ts_list_null_policy.test (five table, constant list section)"
        status: pass
      - kind: e2e
        ref: "examples/data_preparation/null_handling_examples.sql (SECTION 2, PASS constant_list_correct_every_row)"
        status: pass
    human_judgment: false
  - id: D3
    description: "Every paired metric (mae, mse, rmse, mape, smape, r2, bias, mase,
      rmae, quantile_loss, mqloss, coverage) drops incomplete pairs/rows in Rust; no
      complete pair/row returns NULL; the ts_*_by table surface matches the scalar"
    requirement: RUST-02
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-ffi/tests/null_policy_ffi.rs (10 tests covering all 12 metrics)"
        status: pass
      - kind: integration
        ref: "test/sql/ts_list_null_policy.test (all 12 metrics + ts_mae_by table-surface parity)"
        status: pass
    human_judgment: false
  - id: D4
    description: "ts_list_marshal.hpp is the only list-extraction code in metrics.cpp
      and ts_data_quality.cpp (private ExtractListAsDouble/ExtractNestedListAsDouble
      copies deleted)"
    verification:
      - kind: unit
        ref: "grep -En 'static void Extract(Nested)?ListAsDouble' src/scalar_functions/metrics.cpp (0 matches)"
        status: pass
    human_judgment: false
  - id: D5
    description: "missing.rs owns all three phase policies (complete_cases,
      trim_edges_reject_interior, drop_missing), unit-tested, NaN treated as missing"
    requirement: RUST-02
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/missing.rs (10 inline tests)"
        status: pass
    human_judgment: false

duration: ~50min
completed: 2026-10-09
status: complete
---

# Phase 12 Plan 01: Shared List Reader + Rust Missing-Data Policy Summary

**All 12 paired metric functions now compute pairwise-complete observations in Rust from a validity bitmask, through one shared ToUnifiedFormat-based C++ list reader that replaced ten ad hoc copies.**

## Performance

- **Duration:** ~50 min
- **Tasks:** 3 (all completed)
- **Files modified/created:** 20 (across 3 task commits + 1 metadata commit)

## Accomplishments

- Built `crates/anofox-fcst-core/src/missing.rs` — the single Rust-owned missing-data
  policy module (`complete_cases`, `trim_edges_reject_interior`, `drop_missing`),
  10 unit tests, NaN treated identically to NULL.
- Built `src/include/ts_list_marshal.hpp` — `ListDoubleReader` / `NestedListDoubleReader`
  (full `ToUnifiedFormat` on both list and child vectors; constant/dictionary-safe) plus
  `WriteDoubleList`/`WriteDoubleListNanAsNull` output helpers.
- Extended all 12 metric FFIs (`anofox_ts_mae/mse/rmse/mape/smape/r2/bias/rmae/mase/
  quantile_loss/mqloss/coverage`) to accept validity bitmasks and run `complete_cases`
  before computing; `InsufficientData` after filtering maps to `ErrorCode::InsufficientData`
  (SQL `NULL`), not an error.
- Ported every list-reading function in `metrics.cpp` and `ts_data_quality.cpp` to the
  shared reader; deleted the private `ExtractListAsDouble`/`ExtractNestedListAsDouble`
  copies.
- Proved the whole chain natively: `ts_mae([1,NULL,3,4],[1,2,NULL,4])` = `0.0` (was
  `0.333` before this plan), and `ts_mae(a,[0,0,0])` is correct on all 5 rows of a
  constant-second-argument query (was correct on row 0 only).
- Added the phase example, a non-json-gated SQL test file (31 assertions), and the
  phase-local snippet harness that later Phase 12 plans will reuse.

## Task Commits

1. **Task 1: Tracer — missing.rs + validity-aware anofox_ts_mae + shared reader** - `d0ff31a` (feat)
2. **Task 2: Every paired metric through complete_cases; shared reader everywhere** - `0bef31b` (feat)
3. **Task 3: Build once, SQL proof, phase example + snippet harness + docs** - `89ede28` (docs)

**Plan metadata:** (this commit, below)

## Files Created/Modified

- `crates/anofox-fcst-core/src/missing.rs` - Rust missing-data policy module
- `src/include/ts_list_marshal.hpp` - shared C++ list reader/writer
- `crates/anofox-fcst-ffi/src/lib.rs` - 12 metric FFIs extended with validity params
- `src/include/anofox_fcst_ffi.h` - regenerated header (cbindgen)
- `crates/anofox-fcst-ffi/tests/null_policy_ffi.rs` - 10 FFI-ABI tests
- `src/scalar_functions/metrics.cpp` - all 12 metric scalar functions ported
- `src/table_functions/ts_metrics_native.cpp` - all metric FFI call sites updated
- `src/table_functions/ts_data_quality.cpp` - ported to `ListDoubleReader`
- `test/sql/ts_list_null_policy.test` - 31 value assertions
- `examples/data_preparation/null_handling_examples.sql` - self-checking phase example
- `docs/api/09-evaluation-metrics.md` - NULL handling paragraph
- `plugins/anofox-forecast/skills/anofox-forecast-backtest/SKILL.md` - gotcha bullet
- `CHANGELOG.md` - Breaking changes + Fixed entries
- `.planning/phases/12-statistics-marshalling-moves-to-rust/snippets/*` - phase-local docs harness (copied from Phase 10)

## Decisions Made

- `ts_metrics_native.cpp`'s pre-existing manual `isnan`-based row filtering (mase/rmae/
  coverage/quantile_loss) was left in place — redundant with but harmless alongside
  `complete_cases`; only the FFI call signatures needed updating per the plan.
- `NestedListDoubleReader`'s NULL-inner-list case is padded to `actual.size()`
  all-invalid in `TsMqlossFunction` rather than left as a 0-length array, so every
  quantile level the FFI sees is length-matched.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Clippy `needless_range_loop` false positive in the new mqloss loop**
- **Found during:** Task 3 (verify gate: `cargo clippy --all-targets --all-features -- -D warnings`)
- **Issue:** The `for i in 0..n_levels` loop in `anofox_ts_mqloss` indexes both
  `levels_vec[i]` and raw pointers (`quantiles.add(i)`, `quantiles_validity.add(i)`),
  which clippy's `needless_range_loop` lint can't see past.
- **Fix:** Added a scoped `#[allow(clippy::needless_range_loop)]` with a comment
  explaining why an iterator can't express the pointer-offset access.
- **Files modified:** `crates/anofox-fcst-ffi/src/lib.rs`
- **Verification:** `cargo clippy --all-targets --all-features -- -D warnings` clean.
- **Committed in:** `89ede28` (Task 3 commit)

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** No scope creep — a lint fix required to pass the plan's own verify gate.

## Issues Encountered

- Another executor (Phase 11, upstream crate work) runs concurrently in this same
  working tree and briefly staged its own untracked files (`11-baseline-lint.patch`,
  `11-upstream-base.env`) into the shared index between my `git add` and `git commit`
  calls for Task 1. Caught before committing (via `git status --short` diffing against
  the session-start snapshot) and unstaged with `git restore --staged` before
  proceeding. Switched to `git commit --only <exact paths>` for Tasks 2 and 3 per the
  orchestrator's mid-plan note, avoiding the shared index entirely for the rest of the
  plan.
- The plan-commit ledger's measured count (`git rev-list --count
  d9a99390d5320d7bfb88e5c4949e729ba7cae609..HEAD` = 5) includes 2 commits
  (`13231c1`, `b681c88`) made by the concurrent Phase 11 executor, not this plan — this
  plan's own commits are exactly the 3 listed above (`d0ff31a`, `0bef31b`, `89ede28`).
  Recorded as `commits: 5` per the ledger-measurement contract; the discrepancy is
  attributable to the documented concurrent-executor setup, not an uncommitted-change
  gap (working tree was clean of tracked changes at every commit boundary).

## Next Phase Readiness

- `ts_list_marshal.hpp` and `missing.rs` are ready for reuse by plan 12-02 (single-series
  gate: ADF/KPSS/Ljung-Box/DW/JB, periods, seasonality, changepoints, peaks, decomposition,
  detrend, features — all still read lists via their own private, buggy copies per the
  Phase 12 research).
- The phase-local snippet harness (`.planning/phases/12-.../snippets/`) and the phase
  example `examples/data_preparation/null_handling_examples.sql` are set up for every
  later Phase 12 plan to extend with its own section/`--out` pass-list.
- No blockers for plan 12-02.

---
*Phase: 12-statistics-marshalling-moves-to-rust*
*Completed: 2026-10-09*

## Self-Check: PASSED

All created files confirmed on disk (`crates/anofox-fcst-core/src/missing.rs`,
`src/include/ts_list_marshal.hpp`, `crates/anofox-fcst-ffi/tests/null_policy_ffi.rs`,
`test/sql/ts_list_null_policy.test`, `examples/data_preparation/null_handling_examples.sql`).
All three task commits confirmed in git history (`d0ff31a`, `0bef31b`, `89ede28`).
