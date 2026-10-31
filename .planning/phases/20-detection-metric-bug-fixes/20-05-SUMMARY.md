---
phase: 20-detection-metric-bug-fixes
plan: 05
subsystem: hierarchy/forecasting
status: complete
tags: [bug-fix, union-all, pipelines, in-out-function, hierarchy, "#285", BUG-03]
requires:
  - 20-02 (TsCollectFinalizeBarrier, _ts_passthrough + ORDER BY ALL breaker recipe)
  - 20-03 (union_source.test fixtures s2/f2/uv/ut/rv/sv)
  - 20-04 (BUG-03 audit table naming the four remaining functions)
provides:
  - union-safe ts_aggregate_hierarchy, ts_split_keys, ts_validate_separator (now table macros over _ts_*_native)
  - union-safe internal _ts_forecast_native (table macro over _ts_forecast_native_impl)
  - ts_forecast_by pinned on UNION ALL / reversed / shuffled sources (body untouched)
  - no native in src/ uses the old spin barrier any more
affects:
  - 20-06, 20-07 (append-only to union_source.test / CHANGELOG #285 entry)
tech-stack:
  added: []
  patterns:
    - "Collect-then-finalize native without a macro front: rename to _ts_*_native, add a ts_table_macros[] entry with the public name and signature, body SELECT * FROM _ts_*_native((SELECT * FROM _ts_passthrough(source) ORDER BY ALL), ...)"
    - "Internal (underscore-prefixed) table macros get no anofox_fcst_ alias"
key-files:
  created:
    - .planning/phases/20-detection-metric-bug-fixes/20-05-SUMMARY.md
  modified:
    - src/table_functions/ts_forecast_native.cpp
    - src/table_functions/ts_aggregate_hierarchy.cpp
    - src/table_functions/ts_split_keys.cpp
    - src/table_functions/ts_validate_separator.cpp
    - src/macros/ts_macros.cpp
    - test/sql/correctness/union_source.test
    - test/sql/correctness/data_prep.test
    - CHANGELOG.md
    - docs/api/02-hierarchical.md
    - plugins/anofox-forecast/skills/anofox-forecast-data-prep/SKILL.md
decisions:
  - "The four non-_by collect-then-finalize functions become table macros with unchanged public names/signatures over renamed natives (_ts_aggregate_hierarchy_native, _ts_split_keys_native, _ts_validate_separator_native, _ts_forecast_native_impl), fed through (SELECT * FROM _ts_passthrough(source) ORDER BY ALL)"
  - "RegisterTsTableMacros skips the anofox_fcst_ alias for macro names starting with '_' (internal, not public API)"
  - "ts_split_keys / ts_validate_separator skip NULL named parameters unconditionally (macros always forward separator/columns), not only when kNamedParamsHaveNullDefault"
metrics:
  duration: "about 75 min of execution (commits 21:38 to 22:36 +0200, 2026-10-23), plus about 25 min waiting for foreign builds before the idle-gated perf run"
  completed: 2026-10-23
  tasks: 3
  files: 10
actuals:
  tasks: 3
  commits: 3
plan_head_before: 757f6ac4d877621819dffd766c711a316b95f06e
---

# Phase 20 Plan 05: union-safe hierarchy functions and `_ts_forecast_native` Summary

The last four collect-then-finalize table functions now read their whole input on a UNION ALL source: `ts_aggregate_hierarchy`, `ts_split_keys`, `ts_validate_separator` and the internal `_ts_forecast_native`. They had no macro in front of them, so each native was renamed and its public name became a table macro with the same signature and output columns. The macro feeds the native through the 20-02 `_ts_passthrough(source) ORDER BY ALL` single-pipeline breaker, and the native uses `TsCollectFinalizeBarrier`. `ts_forecast_by` was already correct (`GROUP BY` + `_ts_forecast_scalar`, it never called `_ts_forecast_native`); its body is byte-identical to the phase base and it is now pinned. After this plan, `grep -rln threads_done_collecting src` returns nothing.

## Commits

| Task | Commit | Message |
|------|--------|---------|
| 1 (tracer) | fbe0945 | fix(20-05): _ts_forecast_native union-safe; ts_forecast_by pinned on UNION ALL (#285) |
| 2 (tdd) | 6a97efb | fix(20-05): ts_aggregate_hierarchy, ts_split_keys, ts_validate_separator union-safe (#285) |
| 3 | d35f1d4 | docs(20-05): changelog + hierarchy docs for union-safe hierarchy/forecast natives (#285) |

`git rev-list --count 757f6ac..d35f1d4` = 3. All three carry the `Co-Authored-By: Claude Opus 5.5 (1M context)` trailer.

## What changed

- **Natives** (4 files): the `finalize_claimed` / `threads_collecting` / `threads_done_collecting` spin barrier was replaced by `TsCollectFinalizeBarrier`, as in 20-03/20-04: `RegisterLocal()` in init_local, `AssertCollecting("<native>")` at InOut entry, `ArriveAtFinal()` once per local state (`arrived_at_final` flag). Registration names: `_ts_forecast_native_impl`, `_ts_aggregate_hierarchy_native`, `_ts_split_keys_native`, `_ts_validate_separator_native`. The `FunctionDescription` blocks moved from the three hierarchy natives onto their macro entries; the now-unused `create_table_function_info.hpp`, `<atomic>` and `<thread>` includes were dropped. MaxThreads, bind logic and error messages (which name the public function) are unchanged.
- **Macros** (`ts_macros.cpp`):
  - `_ts_forecast_native(source, horizon, frequency, method, params)` → `_ts_forecast_native_impl((SELECT * FROM _ts_passthrough(source) ORDER BY ALL), ...)`. Kept only because three test files call it directly and positionally.
  - `ts_aggregate_hierarchy(source, params)`, `ts_split_keys(source, separator := '|', columns := NULL)`, `ts_validate_separator(source, separator := '|')`, each forwarding to its native through the same breaker.
  - `RegisterTsTableMacros` skips the `anofox_fcst_` alias for names starting with `_`. The three hierarchy macros do get `anofox_fcst_ts_*` aliases (additive; the natives had none).
  - `ts_forecast_by`: untouched (`cmp` of its entry against 757f6ac: byte-identical).
- **Named-parameter NULL skip**: `ts_split_keys` / `ts_validate_separator` binds skip a NULL value unconditionally, since the macro always forwards `columns := columns` (NULL when omitted) on every DuckDB line.

## Performance (Task 3, idle-gated)

1M rows / 5000 series (`20-perf.sql`), 5 rounds alternating the pre-phase baseline binary (`scratch-p20/duckdb-baseline`) and the final build; first binary alternated per round; every run started only after `pgrep -x ninja|make|cc1plus|cargo|rustc|ld` was empty (foreign builds in anofox-i224/i226/i231/i235 delayed the start by about 25 min; none were touched). Medians of real seconds; logs in `/home/simonm/projects/duckdb/scratch-p20/perf/round<N>-<binary>.log`.

| Query | Baseline | 20-05 | Δ |
|---|---:|---:|---:|
| ts_forecast_by Theta | 0.111 | 0.112 | +0.001 |
| ts_forecast_by Theta, UNION ALL source | 0.101 | 0.098 | -0.003 |
| ts_forecast_by Naive | 0.069 | 0.069 | +0.000 |
| _ts_forecast_native Naive | 0.401 | 0.412 | +0.011 |
| _ts_forecast_native Theta | 0.794 | 0.804 | +0.010 |
| ts_mae_by | 0.242 | 0.261 | +0.019 |
| ts_mase_by | 0.257 | 0.255 | -0.002 |
| ts_stats_by | 2.886 | 2.886 | +0.000 |
| ts_fill_gaps_by | 0.132 | 0.148 | +0.016 |
| ts_forecast_by Theta, threads = 1 | 0.844 | 0.845 | +0.001 |

The breaker costs at most about 0.02 s per 1M-row call; `ts_forecast_by` is unchanged (within noise). Figures are in the CHANGELOG #285 entry.

## API compatibility (Phase 20.1 rule)

`duckdb_functions()` on the baseline vs the final build: same parameter names and order for all four public names (`col0`-style names on the natives became `source`/`params`/`separator`/`columns`; positional and named call forms unchanged). `function_type` changes from `table` to `table_macro`. New names only: `anofox_fcst_ts_aggregate_hierarchy`, `anofox_fcst_ts_split_keys`, `anofox_fcst_ts_validate_separator` (additive), plus the internal `_ts_*_native` / `_ts_forecast_native_impl`. `DESCRIBE` of one call of each (including `columns := [...]` and `separator := '-'`) is **identical** between the two binaries (24 lines). Output columns unchanged; nothing removed.

## Verification (observed)

- **Tracer** (Task 1 verify, CLI on the plan's seasonal fixture): `_ts_forecast_native((SELECT id, ds, y FROM uv), 3, '1d', 'Naive', MAP{})` → `9,548.917583` (pre-fix planner fact: 9 rows, 484.871756, first branch only). Tracer gate: no `gate` attribute, autonomous plan; verify re-run passed, continued.
- Task 1 acceptance: `"_ts_forecast_native_impl"` registered (grep 2), `threads_done_collecting` 0, `anofox_fcst__ts_forecast_native` absent (count 0), `ts_forecast_by` entry byte-identical. ts_forecast_param_grid 40, ts_native_model_names 63, ts_native_param_validation 49, union_source 292 assertions, all pass.
- Task 2 acceptance: all three natives registered under the new names (grep 2 each), `threads_done_collecting` 0 in all three, `duckdb_functions()` → `ts_aggregate_hierarchy:table_macro, ts_split_keys:table_macro, ts_validate_separator:table_macro`. ts_multi_key 50, ts_aggregate_hierarchy 77, data_prep 55, union_source 322 assertions, all pass with every pre-existing assertion unchanged.
- LTS syntax probe (1.4.4 CLI, named macro default forwarded as a named table-function argument): prints `2`.
- **Full native suite**: All tests passed (6140 assertions in 95 test cases); the only `*.test` not in it is `test/wasm/post_upload_smoke.test`.
- `make check-docs`: 345 PASS, 0 FAIL, 8 EXPECTED-ERROR, 154 SYNOPSIS, 4 SKIP-NETWORK, 0 STALE (unchanged; covers the `examples/multi_key_hierarchy` call forms).
- `make audit` exit 0 (after Task 1 and Task 2); `cargo fmt --all -- --check` exit 0.

## CI

Pushed `757f6ac..d35f1d4` to `milestone/v0.11.0-issue-sweep`. Static audits run **40020582745** (head d35f1d4): completed, success. MainDistributionPipeline (wasm-runtime-test, v1.4.5/v1.5.6/v2.0.0 builds) not run for this plan: it does not trigger on a branch push and is deferred to the orchestrator's end-of-Phase-20 dispatch, as for 20-03/20-04.

## Deviations from Plan

**1. [Fixture] Separate seasonal fixture for the exact `_ts_forecast_native` pin.** union_source.test's existing `s2` is linear (no sine term), while the plan's 548.917583 value comes from the seasonal planner fixture. Added `fs2/ff2/fuv/fut/frv/fsv` (the planner formula) for the exact-value and shape pins; the `ts_forecast_by` SeasonalNaive/Theta pins reuse the existing `uv/ut/rv/sv`.

**2. [Fixture] Own hierarchy fixture.** `hsales` (3 leaves: EU|S1|A, EU|S1|B, US|S2|C x 20 days) with a two-branch UNION ALL view (days 0..9 and 10..19, 30 rows each) plus table/reversed/shuffled shapes. Each EXCEPT ALL block is preceded by a value check (160 rows / 8 unique_ids; 60 rows / 2 distinct id_part_1; is_valid true / n_conflicts 0), so an empty result cannot pass. `ts_split_keys` and `ts_validate_separator` read projections of the UNION ALL view.

**3. [Cleanup] Unused includes dropped** in the three hierarchy natives once their `CreateTableFunctionInfo` blocks moved to the macros. No behaviour change.

**4. [Process] Task 2 RED not captured as a separate run.** The native edits for Task 2 were made while the Task 1 build was running, and the first Task 2 build already contained the macros, so no RED run of the new union_source pins exists. The pre-fix defect for this class is established by 20-02/20-04 and by the Task 1 tracer value (484.871756).

### Out-of-scope discoveries

None new.

## Known Stubs

None.

## Requirement status

With 20-05, every collect-then-finalize table function in src/ is union-safe (BUG-03 audit table in 20-04-SUMMARY.md, last four rows). BUG-03 can be marked complete once phase verification confirms it.

## Self-Check: PASSED

- FOUND: commits fbe0945, 6a97efb, d35f1d4 (git log), pushed to origin/milestone/v0.11.0-issue-sweep
- FOUND: `grep -rln threads_done_collecting src` → empty; union_source.test hierarchy + forecast sections; data_prep.test UNION ALL pin; CHANGELOG #285 entry with perf figures
