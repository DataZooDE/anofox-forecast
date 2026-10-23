---
phase: 20-detection-metric-bug-fixes
plan: 03
subsystem: detection/statistics/data-preparation
status: complete
tags: [bug-fix, union-all, pipelines, in-out-function, changepoints, fill-gaps, "#285", "#287", BUG-03]
requires:
  - 20-02 (TsCollectFinalizeBarrier, ORDER BY ALL breaker recipe)
provides:
  - "#287 default-output md5 byte-identity pins (detection_decomposition.test), captured before any Phase 20 changepoint change; guard 20-06/20-07"
  - test/sql/correctness/union_source.test (UNION ALL / reversed / shuffled equivalence pins, extended by 20-04/20-05)
  - union-safe ts_detect_changepoints_by, ts_stats_by, ts_fill_gaps_by, ts_fill_forward_by (+ ts_fill_forward_operator), ts_mstl_decomposition_by
affects:
  - 20-04, 20-05 (append their pins to union_source.test; same recipe)
  - 20-06, 20-07 (must keep the #287 md5 pins green)
tech-stack:
  added: []
  patterns:
    - "query_table-style _by macros: the native's input is (SELECT group_col, date_col, value_col FROM query_table(source::VARCHAR) ORDER BY ALL)"
key-files:
  created:
    - test/sql/correctness/union_source.test
  modified:
    - test/sql/correctness/detection_decomposition.test
    - src/table_functions/ts_changepoints.cpp
    - src/table_functions/ts_stats.cpp
    - src/table_functions/ts_fill_gaps_native.cpp
    - src/table_functions/ts_fill_forward_native.cpp
    - src/table_functions/ts_mstl_decomposition_native.cpp
    - src/macros/ts_macros.cpp
    - CHANGELOG.md
    - docs/api/04-data-preparation.md
decisions:
  - "query_table-style macros use (... FROM query_table(source::VARCHAR) ORDER BY ALL), not _ts_passthrough(source): these macros take a table NAME, so _ts_passthrough(source) cannot bind; the ORDER BY ALL breaker is the load-bearing part (deferred-items.md from 20-02 prescribes exactly this form)"
  - "Branch MainDistributionPipeline not dispatched: it does not run on branch push, and a workflow_dispatch runs extension-upload.sh against the bucket; left to the user / PR run"
metrics:
  duration: "43 min (13:32 to 14:15 UTC, 2026-10-23)"
  completed: 2026-10-23
  tasks: 3
  files: 10
actuals:
  tokens: 11300
  tasks: 3
  commits: 4
plan_head_before: 21dfe7ede4f894e8cb2e3b9ec8580caa3f577497
---

# Phase 20 Plan 03: union-safe detection, stats and data-prep `_by` macros Summary

`ts_detect_changepoints_by`, `ts_stats_by`, `ts_fill_gaps_by`, `ts_fill_forward_by` (and `ts_fill_forward_operator` over it) and `ts_mstl_decomposition_by` now read their whole source on a UNION ALL view. They use the 20-02 recipe: an `ORDER BY ALL` single-pipeline breaker on the native's query_table input, plus `TsCollectFinalizeBarrier` in all five natives. Before any source edit, the #287 default changepoint outputs were frozen as md5 pins, and those pins still pass.

## Commits

| Task | Commit | Message |
|------|--------|---------|
| 1a (pins first) | d345344 | test(20-03): freeze #287 default changepoint outputs as md5 byte-identity pins |
| 1 (tracer) | 7c9dd46 | fix(20-03): ts_detect_changepoints_by and ts_stats_by read every row of a UNION ALL source (#285) |
| 2 (tdd) | 90c77e8 | fix(20-03): ts_fill_gaps_by, ts_fill_forward_by and ts_mstl_decomposition_by union-safe (#285) |
| 3 | 4e7800f | docs(20-03): detection, stats and data-prep _by macros read their whole source (#285) |

`git rev-list --count 21dfe7e..HEAD` = 4 before the metadata commit. Every commit carries the `Co-Authored-By: Claude Opus 5.5 (1M context)` trailer (checked with `git log --format=%B | grep -c`).

## #287 golden pins (captured on the 21dfe7e build, DuckDB v2.0.0, before any edit)

| Query on the #287 panel | md5 (5/5 runs identical) |
|---|---|
| `ts_detect_changepoints_by(..., MAP{})` | 40b71f3f330b63b30f5ca2f490bba6bf |
| `ts_detect_changepoints_by(..., MAP{'hazard_lambda': '50'})` | 30e6fc7372714db376be95f4d66d0bc5 |
| `ts_detect_changepoints_agg(ds, y, MAP{})` (unnested) | 40b71f3f330b63b30f5ca2f490bba6bf |

The flagged dates match the planner facts. A and C: 2024-01-22, 01-29, 02-05, 02-12, 02-19, 02-26. B: 2024-01-29, 2024-02-20. Max probability is 0.996. The table function and the aggregate give the same md5 with defaults. The pins were committed alone (d345344) before the source change (7c9dd46).

## What changed

- **Natives** (`ts_changepoints.cpp`, `ts_stats.cpp`, `ts_fill_gaps_native.cpp`, `ts_fill_forward_native.cpp`, `ts_mstl_decomposition_native.cpp`): the old `finalize_claimed` / `threads_collecting` / `threads_done_collecting` spin barrier is replaced by `TsCollectFinalizeBarrier`, exactly as in 20-02. Each native calls `RegisterLocal()` in init_local, `AssertCollecting("<native>")` at InOut entry, and `ArriveAtFinal()` once per local state in finalize. MaxThreads is unchanged: 999999 for changepoints, stats and mstl, and `num_slots` for fill_gaps and fill_forward. The fill natives still have a single output owner (the PhysicalBatchInsert constraint), and `current_slot` is reset at the claim. `grep -c threads_done_collecting` returns 0 for all five files.
- **Macros** (`ts_macros.cpp`): the five bodies changed from `(SELECT group_col, date_col, value_col FROM query_table(source::VARCHAR))` to `(... FROM query_table(source::VARCHAR) ORDER BY ALL)`. Every other argument is unchanged. `ts_fill_forward_operator`'s own body is untouched; it inherits the fix through `ts_fill_forward_by`. EXPLAIN shows `INOUT_FUNCTION _ts_stats_by_native` → `ORDER_BY id ASC, ds ASC, y ASC` → `UNION`.

## API compatibility (Phase 20.1 rule)

For all six macros (the five plus `ts_fill_forward_operator`), I captured `duckdb_functions()` `parameters` and `parameter_types`, plus `DESCRIBE` of each call, on the baseline build (21dfe7e). I captured them again after Task 1 and after Task 2. `diff` showed **identical** output both times (62 lines). Parameters:
- `ts_detect_changepoints_by`: source, group_col, date_col, value_col, params
- `ts_stats_by`: ..., frequency, seasonal_period
- `ts_fill_gaps_by`: ..., frequency
- `ts_fill_forward_by` and `ts_fill_forward_operator`: ..., target_date, frequency
- `ts_mstl_decomposition_by`: ..., params

The output columns are unchanged. No new function was added; `_ts_passthrough` from 20-02 is not used here.

## Verification (observed)

- Tracer verify (plan command) printed `TRACER_VERIFY_PASS` (`A,45;B,45;C,45;`). The tracer gate was interactive with `human_verify_mode=end-of-phase` and an automated verify, so it was a re-run: 20/20 CLI runs and 10/10 with `SET threads = 1` gave `A,45;B,45;C,45;` for changepoints and the same counts for stats length.
- RED on the baseline: union_source.test failed at :65, where changepoints on the UNION ALL view gave `A 31 31 / B 31 31 / C 31 31` instead of 45. Task 2 RED was on the Task 1 build: :194, where fill_gaps on the view gave `C 31 31` instead of `84 45`. On that build the view also gave fill_gaps `93,93` rows/values (the table gave `252,135`) and fill_forward `273,93` (the table gave `156,135`).
- GREEN: union_source.test passed with 108 assertions, 20/20 repeat runs identical. detection_decomposition.test passed with 88 assertions (md5 pins included), 3/3 runs.
- CTAS over the UNION ALL view (fill_gaps, fill_forward, mstl, stats, changepoints): `252,135,156,135,135,135,135`, 10/10 runs at default threads and 10/10 at `threads = 1`.
- Late-row guard: the raw `_ts_detect_changepoints_by_native` fed the UNION ALL directly now errors 9/10 (`input rows arrived after the function had finalized`). The 10th run returned all 135 rows. Only the internal, `_`-prefixed native is affected.
- All 21 existing test files that mention the six macros pass.
- Full native suite: All tests passed (5920 assertions in 95 test cases).
- `make check-docs`: 345 PASS, 0 FAIL, 8 EXPECTED-ERROR, 154 SYNOPSIS, 4 SKIP-NETWORK, 0 STALE.
- `make audit` exit 0, `cargo fmt --all -- --check` exit 0, `cargo clippy --workspace --all-targets -- -D warnings` exit 0.
- Older engine: on the v1.5.6 CLI, a pure-SQL table macro shaped like the new bodies (`(SELECT g, d, v FROM query_table(source::VARCHAR) ORDER BY ALL)`, plus a COLUMNS([param]) wrapper like ts_fill_forward_operator) returned the expected rows over a UNION ALL view. No v1.4.x CLI was available locally; 20-02 probed the same form on 1.4.4 and it printed `2`.

## CI

Pushed `21dfe7e..4e7800f` to `milestone/v0.11.0-issue-sweep`. The "Static audits" run **39967388086** passed. **MainDistributionPipeline did not run**: its triggers are `push` to main or tags, `pull_request` and `workflow_dispatch`, so a branch push does not start it. The plan assumed it would. I did not dispatch it, because on a branch the deploy jobs still run `extension-upload.sh` against the bucket (with latest/versioned false) and the run takes about 4 h. That is a side effect beyond a push, so it needs the user's go-ahead. Because of this, the wasm-runtime-test (v1.5.6 curated subset, including the fill-forward pins) and the v1.4.5/v1.5.6/v2.0.0 build jobs are **not verified** for this plan. The gap is recorded in .planning/WINDOWS.md (#5, unrun-verify). To close it, the user can dispatch the pipeline on the branch or rely on the PR run.

## Deviations from Plan

### Auto-fixed Issues

**1. [Amendment, Rule 1] ORDER BY ALL breaker instead of MATERIALIZED CTE / `_ts_passthrough(source)`**
- **Found during:** Task 1 (the plan amendment and 20-02's deferred-items).
- **Issue:** The plan body prescribes `WITH _ts_src AS MATERIALIZED`. That form hits the DuckDB v2.0.0 broadcast-exchange internal error. The amendment's literal form `_ts_passthrough(source)` cannot bind for these macros, because `source` is a table name read via `query_table(source::VARCHAR)` and not a TABLE argument.
- **Fix:** `(SELECT group_col, date_col, value_col FROM query_table(source::VARCHAR) ORDER BY ALL)`, which is the form 20-02's deferred-items.md prescribes for query_table-style macros. `AS MATERIALIZED` is not used anywhere. Acceptance impact: `grep -c "query_table(source::VARCHAR) ORDER BY ALL)" src/macros/ts_macros.cpp` = 5, but the amendment's `_ts_passthrough(` grep does not apply to these five bodies.
- **Commits:** 7c9dd46, 90c77e8.

**2. [Rule 2 - Correctness] Non-NULL / count checks next to every EXCEPT ALL**
- In union_source.test, each macro's EXCEPT ALL block is preceded by per-id count checks, so an all-NULL or empty result cannot pass:
  - changepoints: `count(*)`, `count(changepoint_probability)` = 45/45
  - stats: `length` 45
  - fill_gaps: 84/45
  - fill_forward and the operator: 52/45 and max ds
  - mstl: len(trend)/len(remainder)/list_count(trend) = 45 and periods [7]
- The test table was named `cp287` (not `s`) in detection_decomposition.test to avoid clashing with other tables. The md5 does not include the table name.

**3. [Rule 3] CI watch not possible as planned:** see CI above.

### Out-of-scope discoveries (logged to deferred-items.md, not fixed)

- The docs "Do not chain table functions in CTEs" warning did not reproduce: its own example returned 999090 rows 3/3 on both the baseline and the new build (1M-row panel).
- `scripts/check_correctness_inventory.py` exits 1 with `MISSING ts_features_config_template`, `MISSING ts_features_list`. Both come from the pre-existing Phase 19 BUG-02 CHANGELOG entry. Every function this plan added to the CHANGELOG is covered.

## Requirement status

BUG-03 stays **Pending**, because 20-04 and 20-05 also carry it.

## Self-Check: PASSED

- FOUND: test/sql/correctness/union_source.test, test/sql/correctness/detection_decomposition.test, this SUMMARY
- FOUND: commits d345344, 7c9dd46, 90c77e8, 4e7800f (git log); origin/milestone/v0.11.0-issue-sweep at 4e7800f (git ls-remote)
