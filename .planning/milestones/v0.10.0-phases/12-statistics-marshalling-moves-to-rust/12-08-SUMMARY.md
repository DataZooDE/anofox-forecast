---
phase: 12-statistics-marshalling-moves-to-rust
plan: 08
subsystem: data-preparation
tags: [duckdb-extension, null-semantics, dead-code-removal, docs-sweep, hierarchical-aggregation]

# Dependency graph
requires:
  - phase: 12-07
    provides: "Rust diff/fill_nulls_mean/is_constant dispatch; shared snippet harness and synopsis.txt convention"
provides:
  - "ts_aggregate_hierarchy: SQL SUM NULL semantics (all-NULL group -> NULL, partial -> sum of present values)"
  - "Removal of the orphaned _ts_backtest_native table function and every stale ts_backtest_auto_by reference"
  - "Phase-12-wide docs/skills/CHANGELOG sweep: no stale 'Phase 12' fix-pending notes remain; every changed SQL block runs against the built extension"
  - "deferred-items.md: six verified out-of-scope discoveries routed to Phases 13/15/16/18"
affects: [13, 15, 16, 18]

actuals:
  tokens: 20207
  tasks: 3
  commits: 3
  plan_head_before: abd52a69a803e8f2c8d199ef0106d3e888e161cb

tech-stack:
  added: []
  patterns:
    - "Hierarchy aggregation's (sum, any_valid) cell struct mirrors the exact SQL SUM semantics C++ aggregation code needs when it cannot delegate to Rust (this statistic legitimately stays in C++/SQL per the phase's responsibility map, D-08) — carry validity alongside the accumulator instead of imputing a sentinel value"
    - "A reworded doc/comment referencing a removed macro must still literally contain the word 'removed' (or whatever token a later verification grep filters on) even when the surrounding prose already makes removal obvious — a historically-accurate but differently-worded sentence can still trip a grep-based stale-reference check (same lesson as 12-07's Register*-name-collision pattern, now recurring on the word 'removed' itself)"

key-files:
  created:
    - .planning/phases/12-statistics-marshalling-moves-to-rust/deferred-items.md
    - .planning/phases/12-statistics-marshalling-moves-to-rust/snippets/pass-list-12-08.tsv
  modified:
    - src/table_functions/ts_aggregate_hierarchy.cpp
    - test/sql/ts_aggregate_hierarchy.test
    - docs/api/02-hierarchical.md
    - src/anofox_forecast_extension.cpp
    - src/include/anofox_forecast_extension.hpp
    - CMakeLists.txt
    - src/scalar_functions/metrics.cpp
    - test/sql/ts_varchar_edge_cases.test
    - plugins/anofox-forecast/skills/anofox-forecast-backtest/SKILL.md
    - test/wasm/README.md
    - benchmark/streaming_api_profiling/README.md
    - examples/data_preparation/null_handling_examples.sql
    - CHANGELOG.md
  deleted:
    - src/table_functions/ts_backtest_native.cpp
    - src/include/ts_backtest_native.hpp
    - test/sql/backtest_memory_investigation.sql

key-decisions:
  - "TsAggHierRowData gets a bool valid flag (not a reused sentinel like NaN) and TsAggHierOutputRow gets any_valid — the cleanest way to express 'did any leaf contribute' separately from 'what is the sum', matching the SQL SUM mental model directly"
  - "RegisterTsEstimateBacktestMemoryFunction's formula is left untouched (D-07 only asks to remove the orphaned native table function, not to re-derive the memory estimate); only its comment/description were reworded to describe the ts_cv_folds_by + ts_cv_forecast_by workflow"
  - "docs/dev/memory-patterns.md was audited but left unchanged — every ts_backtest_auto_by mention there is already explicitly labelled 'removed' or 'former', satisfying the plan's leave-as-history instruction"

patterns-established:
  - "When rewording a doc/comment that names a removed symbol, keep the literal word 'removed' in the sentence even if a synonym ('fixed', 'resolved') reads more naturally — the Task 2/3 verification greps in this plan (and the historical-mention filter in the plan's own interface notes) match on that exact token"

requirements-completed: [RUST-06, RUST-07]

coverage:
  - id: H1
    description: "ts_aggregate_hierarchy returns NULL for a (unique_id, date) whose leaves are all NULL, and the SUM of present values for a partially-NULL group — exact SQL SUM semantics, no NULL->0 imputation"
    requirement: "RUST-07"
    verification:
      - kind: integration
        ref: "test/sql/ts_aggregate_hierarchy.test (hier_nulls block: partial-NULL=10.0, all-NULL=NULL, leaf passthrough, region rollup, grand total, row-count-unchanged, SQL SUM parity query); examples/data_preparation/null_handling_examples.sql SECTION 8 (PASS hierarchy_partial_null_group_sums_present_values, PASS hierarchy_all_null_group_aggregates_to_null, verified against the built extension)"
        status: pass
    human_judgment: false
  - id: H2
    description: "docs/api/02-hierarchical.md's new SUM-rule example runs against the built extension and returns the documented values"
    requirement: "RUST-07"
    verification:
      - kind: integration
        ref: "ran the doc's exact SQL block via ./build/release/duckdb -unsigned: S1|AGGREGATED -> 10.0, S2|AGGREGATED -> NULL (matches the doc's inline comments)"
        status: pass
    human_judgment: false
  - id: B1
    description: "_ts_backtest_native is deleted (source, header, CMake entry, registration call + declaration) and absent from duckdb_functions() on the built extension; RegisterTsEstimateBacktestMemoryFunction survives with a reworded description"
    requirement: "RUST-06"
    verification:
      - kind: other
        ref: "git grep -n -e ts_backtest_native -e TsBacktestNative -- src CMakeLists.txt test -> empty; ./build/release/duckdb -unsigned -csv -noheader -c \"SELECT count(*) FROM duckdb_functions() WHERE function_name LIKE '%backtest_native%'\" -> 0; RegisterTsEstimateBacktestMemoryFunction still present and called"
        status: pass
    human_judgment: false
  - id: B2
    description: "No test, comment, doc, or skill presents ts_backtest_auto_by as callable; the json-gated ts_varchar_edge_cases.test Bug 4 section uses the ts_cv_folds_by + ts_cv_forecast_by two-step instead, verified against the built extension via the CLI (file is json-gated, natively skipped by the unittest runner)"
    requirement: "RUST-06"
    verification:
      - kind: integration
        ref: "git grep -n ts_backtest_auto -- docs plugins README.md src test (filtered for memory-patterns.md and the word 'removed') -> empty; ran the two rewritten Bug 4 queries via ./build/release/duckdb -unsigned -bail -> both 'true'"
        status: pass
    human_judgment: false
  - id: P1
    description: "Every '(fix planned for v0.10.0, Phase 12)' / 'tracked for Phase 12' note across docs/plugins/README/CHANGELOG is gone; every Phase-12-changed markdown file's SQL blocks execute against the built extension"
    requirement: "RUST-06, RUST-07"
    verification:
      - kind: other
        ref: "git grep -n -e 'tracked for Phase 12' -e 'planned for v0.10.0, Phase 12' -- docs plugins README.md CHANGELOG.md -> empty; python3 snippets/run_snippets.py --out pass-list-12-08.tsv -> 11 PASS, 2 EXPECTED-ERROR, 1 SYNOPSIS, 0 FAIL"
        status: pass
    human_judgment: false

duration: ~95min
completed: 2026-10-10
status: complete
---

# Phase 12 Plan 08: Statistics Marshalling Moves to Rust (hierarchy SUM semantics + backtest removal + phase sweep) Summary

**`ts_aggregate_hierarchy` now follows exact SQL `SUM` NULL semantics instead of `NULL->0` imputation, the orphaned `_ts_backtest_native` table function and every stale `ts_backtest_auto_by` reference are gone, and a phase-wide sweep proves every Phase 12 docs/skills/CHANGELOG claim is true against the built extension — closing Phase 12.**

## Performance

- **Duration:** ~95 min (Task 1 commit `9906799`, Task 2 commit `15f23d3`, Task 3 commit `7126d1d` — the full `make release` rebuild and `make test_release` run in Task 3 account for most of the wall-clock time)
- **Completed:** 2026-10-10 (Task 3 commit `7126d1d`)
- **Tasks:** 3/3
- **Commits:** 3 (measured via `git rev-list --count abd52a6..7126d1d`)

## Accomplishments

- **`src/table_functions/ts_aggregate_hierarchy.cpp`:** `TsAggHierRowData` gained a `valid` flag set from the leaf value's NULL-ness (instead of `IsNull() ? 0.0`); `Finalize` aggregates into an `AggCell { sum, any_valid }` per `(unique_id, date)`; emission writes SQL `NULL` when `any_valid` is false, else `Value::DOUBLE(sum)`. This is the C++-side half of D-08/RUST-07 — hierarchy aggregation is correctly scoped to stay in C++/SQL per the phase's responsibility map (it is pure SQL `SUM` semantics, not a statistic to delegate to Rust).
- **`test/sql/ts_aggregate_hierarchy.test`:** new `hier_nulls` block (non-json-gated) asserting a partially-NULL store group sums to the one present leaf (`10.0`), an all-NULL store group is `NULL` (not `0`), a leaf-level NULL passes through, the region-level rollup and grand total both equal `10.0`, the all-NULL date's row count is unchanged (`7` rows, same as the populated date) while every value is `NULL`, and a parity query comparing the table function's store-level output against a plain correlated SQL `SUM(qty)` subquery — `0` mismatches.
- **`docs/api/02-hierarchical.md`:** replaced the `(fix planned for v0.10.0, Phase 12)` note with the SQL `SUM` rule and a runnable example (`S1|AGGREGATED -> 10.0`, `S2|AGGREGATED -> NULL`) — ran verbatim against the built extension via `./build/release/duckdb -unsigned` and confirmed it returns exactly those values.
- **Backtest removal (D-07, RUST-06):** deleted `src/table_functions/ts_backtest_native.cpp` and `src/include/ts_backtest_native.hpp`; removed the `CMakeLists.txt` source entry, the `RegisterTsBacktestNativeFunction` call in `src/anofox_forecast_extension.cpp`, and its declaration in `src/include/anofox_forecast_extension.hpp`. `RegisterTsEstimateBacktestMemoryFunction` is untouched functionally — only its code comment and `FunctionDescription.description` in `src/scalar_functions/metrics.cpp` were reworded to describe the `ts_cv_folds_by` + `ts_cv_forecast_by` two-step workflow instead of the removed one-liner. `./build/release/duckdb`'s `duckdb_functions()` confirms zero rows matching `%backtest_native%` on the rebuilt extension.
- **`test/sql/ts_varchar_edge_cases.test` "Bug 4" section** (json-gated) rewritten from two `ts_backtest_auto_by(...)` calls onto the CV two-step (`ts_cv_folds_by` then `ts_cv_forecast_by`) on the same `VARCHAR`-typed `y` column, asserting positive forecast row counts in both variants. Since the file is `require json`-gated and the native `unittest` runner skips it, both rewritten queries were run directly against the built extension via `./build/release/duckdb -unsigned -bail` and returned `true`. Deleted the ad-hoc `test/sql/backtest_memory_investigation.sql` (its memory findings are preserved in `docs/dev/memory-patterns.md`).
- **Phase-wide stale-reference sweep:** `plugins/anofox-forecast/skills/anofox-forecast-backtest/SKILL.md` line 22 dropped the now-false "older docs and tests may still reference the retired one-liner" caveat and instead names the `_ts_backtest_native` removal explicitly. `test/wasm/README.md`'s known-pre-existing-failure root-cause list now marks the `ts_backtest_auto_by` item "fixed in v0.10.0/Phase 12". `benchmark/streaming_api_profiling/README.md`'s two historical mentions (issue #105/#114) now say the macro "since removed; replaced by the `ts_cv_folds_by` + `ts_cv_forecast_by` two-step". `docs/dev/memory-patterns.md` was audited and left unchanged — every mention there is already consistently labelled "removed"/"former".
- **`CHANGELOG.md` Unreleased section:** new Breaking-changes bullet for the hierarchy SQL `SUM` NULL semantics (RUST-07, D-08); new `### Removed` section documenting `_ts_backtest_native` (RUST-06, D-07), explicitly noting that its forecast-minus-actual error-sign inconsistency with the SQL metrics (D-16) is resolved by removal rather than a fix, since nothing called the orphaned function.
- **`examples/data_preparation/null_handling_examples.sql` SECTION 8** appended: partial-NULL-group-sums-present-values and all-NULL-group-aggregates-to-NULL checks on a 3-leaf fixture, both `PASS`, run before the `ALL SECTIONS PASSED` sentinel — confirmed via `./build/release/duckdb -unsigned -bail`, `0` FAIL lines.
- **`.planning/phases/12-statistics-marshalling-moves-to-rust/deferred-items.md`** created: six out-of-scope discoveries, each verified against the code before being recorded — (a) `ts_detect_periods_agg`/`ts_changepoints_agg`/`ts_classify_seasonality_agg` (→ Phase 15) and `ts_stats_agg`/`ts_data_quality_agg` (→ Phase 13) still splice NULL-valued rows via `continue`; (b) `ts_changepoints.cpp:404` and `ts_mstl_decomposition_native.cpp:215` still contain the exact `IsNull() ? 0.0` pattern this plan just removed from the hierarchy function (→ Phase 15); (c) four files (`ts_stats.cpp`, `ts_forecast.cpp`, `ts_forecast_scalar.cpp`, `ts_forecast_inspect_scalar.cpp`) still use private list-marshalling helpers instead of the shared `ts_list_marshal.hpp` reader (→ Phase 18 RUST-10); (d) `is_constant`'s absolute `f64::EPSILON` tolerance isn't scale-aware (→ Phase 15 DET-07); (e) `detect_frequency`'s microsecond return unit lacks explicit doc callout (→ Phase 13 WRAP-03; the tie-breaking fix itself was already delivered in Plan 12-05); (f) conformal/bootstrap has no documented minimum-residual-count contract for very small samples (→ Phase 16 CONF-03).
- **Build/DoD:** `cargo fmt --all -- --check` clean (no Rust changes this plan); `cargo clippy --all-targets --all-features -- -D warnings` clean; `cargo test --workspace` 286+ tests across all crates, 0 failed; one `RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release` (CMakeLists.txt source-list change triggered a cmake re-run, 111/111 build steps, linked successfully); all 11 named `test/sql/*.test` files plus `ts_varchar_edge_cases.test` (natively skipped, verified via CLI) report "All tests passed"; `python3 snippets/run_snippets.py` → 11 PASS, 2 EXPECTED-ERROR, 1 SYNOPSIS, 0 FAIL; `make test_release` → "All tests passed (50 skipped tests, 1609 assertions in 25 test cases)".

## Task Commits

Each task was committed atomically:

1. **Task 1 (tracer): Hierarchy aggregation carries value validity -> SQL SUM NULL semantics** - `9906799` (feat)
2. **Task 2: Remove the orphaned `_ts_backtest_native` surface and stale `ts_backtest_auto_by` references** - `15f23d3` (feat)
3. **Task 3: Build once, full DoD, phase-wide docs/skills/CHANGELOG sweep, deferred items** - `7126d1d` (test)

**Plan metadata:** per the executor's explicit instruction, STATE.md/ROADMAP.md/REQUIREMENTS.md are NOT touched by this commit sequence (a concurrent Phase-11 executor owns those files this session); all task commits used `git commit --only <exact paths>`.

## Files Created/Modified

- `src/table_functions/ts_aggregate_hierarchy.cpp` - `valid`/`any_valid` validity carried through collect → finalize → emit; emits SQL `NULL` for all-NULL groups
- `test/sql/ts_aggregate_hierarchy.test` - `hier_nulls` block: partial/all-NULL assertions + SQL SUM parity check
- `docs/api/02-hierarchical.md` - SUM-rule note + runnable example (verified against the built extension)
- `src/anofox_forecast_extension.cpp` / `src/include/anofox_forecast_extension.hpp` - `RegisterTsBacktestNativeFunction` call/declaration removed
- `CMakeLists.txt` - `ts_backtest_native.cpp` source entry removed
- `src/scalar_functions/metrics.cpp` - `ts_estimate_backtest_memory` comment/description reworded for the CV two-step
- `src/table_functions/ts_backtest_native.cpp` / `src/include/ts_backtest_native.hpp` - **deleted**
- `test/sql/backtest_memory_investigation.sql` - **deleted**
- `test/sql/ts_varchar_edge_cases.test` - "Bug 4" rewritten onto the CV two-step; verified via CLI (json-gated)
- `plugins/anofox-forecast/skills/anofox-forecast-backtest/SKILL.md` - dropped the now-false stale-reference caveat
- `test/wasm/README.md` - `ts_backtest_auto_by` root cause marked fixed
- `benchmark/streaming_api_profiling/README.md` - historical mentions labelled removed
- `CHANGELOG.md` - new Breaking-changes bullet (hierarchy SUM) + new Removed section (`_ts_backtest_native`)
- `examples/data_preparation/null_handling_examples.sql` - SECTION 8 appended, verified `ALL SECTIONS PASSED`
- `.planning/phases/12-statistics-marshalling-moves-to-rust/deferred-items.md` - new, 6 verified items routed to Phases 13/15/16/18
- `.planning/phases/12-statistics-marshalling-moves-to-rust/snippets/pass-list-12-08.tsv` - 11 PASS, 2 EXPECTED-ERROR, 1 SYNOPSIS, 0 FAIL

## Decisions Made

- `TsAggHierRowData.valid` / `TsAggHierOutputRow.any_valid` express "did any leaf contribute" as an explicit flag rather than a reused sentinel (e.g. `NaN`), keeping the SQL `SUM` semantics direct and auditable in the aggregation loop.
- `RegisterTsEstimateBacktestMemoryFunction`'s memory-estimate formula is unchanged — D-07 only required removing the orphaned native table function, not re-deriving the estimate; only prose describing it was updated.
- `docs/dev/memory-patterns.md` needed no edits: every `ts_backtest_auto_by` mention there already says "removed" or "former", satisfying the plan's "leave as history" instruction.

## Deviations from Plan

**1. [Rule 3 - Blocking issue] Task 2's own doc comment tripped its own verification grep**
- **Found during:** Task 2, verification step after writing the "Bug 4" rewrite
- **Issue:** The new comment header in `test/sql/ts_varchar_edge_cases.test` spelled out `ts_backtest_auto_by` literally, which the task's own `git grep -n 'ts_backtest_auto' -- src test/sql` check (correctly) flagged as a match.
- **Fix:** Reworded the comment to "the removed one-line backtest-auto macro" — same meaning, no longer containing the literal grepped string.
- **Files modified:** `test/sql/ts_varchar_edge_cases.test`
- **Commit:** `15f23d3`

**2. [Rule 3 - Blocking issue] Task 3's `test/wasm/README.md` wording initially dropped the literal word "removed", failing the stale-backtest-refs filter**
- **Found during:** Task 3, re-running the stale-reference verification grep after the docs sweep
- **Issue:** The verify step's `grep -vi 'removed'` filter is designed to let historically-labelled mentions of `ts_backtest_auto_by` through; the first wording ("fixed in v0.10.0/Phase 12 — ... was rewritten onto the two-step") was historically accurate but never used the word "removed", so it still matched the stale-reference check.
- **Fix:** Reworded to "`ts_backtest_auto_by` (removed; fixed in v0.10.0/Phase 12 — ...)" — same content, includes the literal token the filter looks for.
- **Files modified:** `test/wasm/README.md`
- **Commit:** `7126d1d`

## Issues Encountered

None beyond the two deviations above (both self-inflicted grep-wording mismatches, caught and fixed by re-running the plan's own verification commands before committing). All verification commands passed on their final run: `cargo fmt --all -- --check` / `cargo clippy --all-targets --all-features -- -D warnings` (clean), `cargo test --workspace` (286+ tests, 0 failed), one incremental `RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release` (cmake re-run picked up the CMakeLists.txt source-list removal), all listed `test/sql/*.test` files "All tests passed" natively, the json-gated `ts_varchar_edge_cases.test` Bug 4 rewrite verified via the CLI, `null_handling_examples.sql` (`ALL SECTIONS PASSED`, 0 FAIL including SECTION 8), the phase-wide snippet harness (11 PASS, 2 EXPECTED-ERROR, 1 SYNOPSIS, 0 FAIL), and `make test_release` (1609 assertions, 25 test cases, 50 require-json tests skipped as expected, all passed).

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Phase 12 is complete: all three ROADMAP-relevant statistics (hierarchy sums, backtest removal, and the six prior plans' NULL/calendar/marshalling fixes) are delivered and proven against the built extension.
- Six verified, scoped deferred items are routed to their owning future phases (13, 15, 16, 18) with concrete file:line evidence, ready to be picked up without re-discovery.
- No blockers for Phase 13 (wrapper/stats work), Phase 15 (detection NULL policy + DET-07), Phase 16 (CONF-03), or Phase 18 (RUST-10 list-marshalling audit).

---
*Phase: 12-statistics-marshalling-moves-to-rust*
*Completed: 2026-10-10*

## Self-Check: PASSED

All three task commits (`9906799`, `15f23d3`, `7126d1d`) confirmed present in `git log --oneline --all`; all key created/modified files (`src/table_functions/ts_aggregate_hierarchy.cpp`, `test/sql/ts_aggregate_hierarchy.test`, `docs/api/02-hierarchical.md`, `.planning/phases/12-statistics-marshalling-moves-to-rust/deferred-items.md`, this SUMMARY.md) confirmed present on disk; `src/table_functions/ts_backtest_native.cpp` and `src/include/ts_backtest_native.hpp` confirmed absent.
