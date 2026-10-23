---
phase: 20-detection-metric-bug-fixes
plan: 02
subsystem: evaluation/metrics
status: complete
tags: [bug-fix, metrics, mase, union-all, pipelines, in-out-function, "#285", BUG-03]
requires:
  - 20-01 (build line, CHANGELOG [Unreleased] structure)
provides:
  - TsCollectFinalizeBarrier (src/include/ts_collect_finalize.hpp), a last-finalizer claim with no spin and a late-row guard, shared by collect-then-finalize natives
  - _ts_passthrough(TABLE), an internal streaming identity in-out function that exposes a macro's TABLE argument as a relation
  - all 11 metric macros read their source as (SELECT * FROM _ts_passthrough(source) ORDER BY ALL)
  - R MASE fixture cases issue285_panel_A/B/C
  - "#285 pins in test/sql/correctness/metrics.test"
  - 20-perf.sql workload + baseline binary for 20-05
affects:
  - ts_mae_by, ts_mse_by, ts_rmse_by, ts_mape_by, ts_smape_by, ts_r2_by, ts_bias_by, ts_mase_by, ts_rmae_by, ts_coverage_by, ts_quantile_loss_by (values on multi-pipeline sources only; schemas unchanged)
  - 20-03, 20-04, 20-05, 20-06 (their MATERIALIZED-CTE recipe must change; see deferred-items.md)
tech-stack:
  added: []
  patterns:
    - "Collect-then-finalize in-out natives: count init_local registrations, and the final arrival that completes the count processes (no spin-wait, MaxThreads unchanged)"
    - "Single-pipeline input for such natives via (SELECT * FROM _ts_passthrough(source) ORDER BY ALL) in the macro body, not a MATERIALIZED CTE"
key-files:
  created:
    - src/include/ts_collect_finalize.hpp
    - src/table_functions/ts_passthrough.cpp
    - .planning/phases/20-detection-metric-bug-fixes/20-perf.sql
    - .planning/phases/20-detection-metric-bug-fixes/deferred-items.md
  modified:
    - src/table_functions/ts_metrics_native.cpp
    - src/macros/ts_macros.cpp
    - src/anofox_forecast_extension.cpp
    - src/include/anofox_forecast_extension.hpp
    - CMakeLists.txt
    - validation/reference/r/gen_mase_reference.R
    - validation/reference/fixtures/r_mase/accuracy_mase.json
    - test/sql/correctness/metrics.test
    - CHANGELOG.md
    - docs/api/09-evaluation-metrics.md
    - plugins/anofox-forecast/skills/anofox-forecast-backtest/SKILL.md
decisions:
  - "The single-pipeline breaker in the metric macros is (SELECT * FROM _ts_passthrough(source) ORDER BY ALL), not WITH _ts_src AS MATERIALIZED. On DuckDB v2.0.0 the CTE intermittently raised an INTERNAL broadcast-exchange batch-index error, and it cost ~9x more"
  - "Collect-then-finalize natives use TsCollectFinalizeBarrier. init_local registers (DuckDB creates every operator local state before any fetch), and the final arrival that completes the count processes. No spin-wait, and MaxThreads is not reduced"
  - "Late-row guard: InvalidInputException if rows reach a native after it finalized. This is defence in depth that the macros never reach, so it is documented, not pinned"
  - "R fixture: one case per #285 series (issue285_panel_A/B/C), because metrics_reference.rs iterates cases as single series"
metrics:
  duration: "56 min (12:33 to 13:29 UTC, 2026-10-23)"
  completed: 2026-10-23
  tasks: 3
  files: 15
actuals:
  tokens: 19650
  tasks: 3
  commits: 3
plan_head_before: c5a2aad617dbe5f9084a793e13016052c62c74a3
---

# Phase 20 Plan 02: union-safe metric macros (#285) Summary

The `ts_mase_by` failure in #285 was not a row-order bug. When the source is a UNION ALL, the collect-then-finalize native gets several input pipelines that share one global state, and the first pipeline to finish computed and emitted the results. All 11 metric macros now feed their native through a single-pipeline `ORDER BY ALL` breaker over a new streaming `_ts_passthrough`. The five natives use a no-spin last-finalizer barrier that keeps parallel intake. MASE on the #285 panel is A 0.4764952155298452, B 0.26551450050730596, C 0.4759518449064799 for every shape, which equals R `forecast::accuracy`.

## Commits

| Task | Commit | Message |
|------|--------|---------|
| 1 (tracer) | bf7048c | fix(20-02): ts_mase_by gives one MASE for every row order and UNION ALL shape (#285) |
| 2 (tdd) | 2118175 | fix(20-02): all 11 metric macros order- and UNION-ALL-safe; pin #285 + R fixture |
| 3 | 73f01b4 | docs(20-02): metric macros are row-order and UNION-ALL independent (#285) |

`git rev-list --count c5a2aad..HEAD` = 3 before the metadata commit.

## Root cause

Probes on the pre-fix build (the 20-01 build at c5a2aad, DuckDB v2.0.0, 20 cores), #285 setup tables `s`/`f`:

| Probe | Observed |
|-------|----------|
| literal cut-off `ds < TIMESTAMP '2024-03-11' UNION ALL f`, default threads | A 0.4764952155298452, B 0.26551450050730596, C 0.4759518449064799 (correct on this run) |
| literal cut-off, `SET threads = 1` | A NULL, B NULL, C NULL |
| scalar-subquery cut-off `ds < (SELECT min(ds) FROM f) UNION ALL f` | A NULL, B NULL, C NULL |
| same subquery source wrapped `ORDER BY id, ds` | correct values |
| `ts_mae_by` y+1 branch (`ds < 2024-02-01`, 31 rows/id) UNION ALL y+3 branch (`ds >= (SELECT min(ds) FROM f)`, 14 rows/id) | A 1.0000000000000002, B 1.0, C 1.0000000000000002 (all-rows value is (31*1 + 14*3)/45 = 1.6222222222222222) |
| 1M-row panel, `ts_mae_by` on a UNION ALL of the same rows (baseline binary) | `5000,7463.999586777` (the correct sum is 7524.95) |

EXPLAIN excerpt of the subquery-cut-off call (pre-fix): the `_ts_mase_native` operator (`~55 rows`) has a UNION directly below it. One branch is `Seq Scan memory.main.f`. The other is `memory.main.s` joined `INNER` on `Conditions: ds < SUBQUERY` with an `Aggregates: min(#0)` over `Seq Scan memory.main.f`.

Mechanism, confirmed in the duckdb/ submodule (v2.0.0):
- `Pipeline::ResetSinkAndOperators` (src/parallel/pipeline.cpp:389-398) creates an operator's `op_state` only if it is null. Union pipelines that share the operator therefore share ONE global state.
- `PipelineExecutor::TryFlushCachingOperators` (src/parallel/pipeline_executor.cpp:160-200) calls `FinalExecute` (our `in_out_function_final`) at the end of EACH pipeline's executors.
- The old barrier (`finalize_claimed` CAS, then a spin until `threads_done_collecting == threads_collecting`) only counted threads already registered in InOut. The first finishing pipeline claimed the finalize and emitted results computed on its own branch: a NULL MASE when the forecast branch was missing (no forecast rows → t0 at end → NULL), or MAE 1.0 from the y+1 branch alone. The other branch's rows arrived afterwards and were dropped.
- The per-group date sort already in `_ts_mase_native` (ts_metrics_native.cpp, `std::sort(indices ...)`) cannot help. It sorts whatever rows the finalizing thread has, and the missing rows were never there. Which branch finishes first depends on the plan, the thread count and the version, which is why the literal shape passed at default threads but failed at `threads = 1`.
- Within one pipeline, `PipelineExecutor`'s constructor (pipeline_executor.cpp:32-75) creates every operator local state (`GetOperatorState` → our `init_local`) before it fetches any source data. Counting `init_local` and letting the arrival that completes the count process is therefore complete within one pipeline. Under a CTE exchange, `CTELocalState` builds its direct consumer executors in the producer's local sink state constructor, which also runs before any fetch.

## What changed

- `src/include/ts_collect_finalize.hpp`: `TsCollectFinalizeBarrier` provides `RegisterLocal()` (init_local), `ArriveAtFinal()` (true exactly once, for the arrival that makes arrived == registered), `Claimed()` and `AssertCollecting(fn)` (late-row guard → `InvalidInputException`). There is no spin-wait and MaxThreads is untouched (it stays 999999 on all five natives).
- `src/table_functions/ts_passthrough.cpp`: `_ts_passthrough(TABLE)`. Bind copies the input names and types, InOut references every input column, and there is no `in_out_function_final`.
- `ts_metrics_native.cpp`: all five natives (`_ts_metrics_native`, `_ts_mase_native`, `_ts_rmae_native`, `_ts_coverage_native`, `_ts_quantile_loss_native`) use the barrier, and the old atomics and `std::this_thread::yield` spin are removed.
- `ts_macros.cpp`: the 11 macro bodies are `SELECT * FROM _ts_<x>_native((SELECT * FROM _ts_passthrough(source) ORDER BY ALL), <same args>)`. Parameters, defaults, descriptions and examples are unchanged.

## API compatibility (Phase 20.1 additive-only rule)

`DESCRIBE` of all 11 macros, plus `duckdb_functions()` `parameters` and `parameter_types`, was captured on the baseline binary before any change and on the final build. `diff` of the two 33-line outputs: **identical**. Each macro returns the source's non-date/actual/forecast columns plus its metric column (`mae`, `mse`, `rmse`, `mape`, `smape`, `r2`, `bias`, `mase`, `rmae`, `coverage`, `quantile_loss`, all DOUBLE). The parameter lists are unchanged (`ts_mase_by`: source, date_col, actual_col, forecast_col, seasonal_period; `ts_rmae_by`: ..., pred1_col, pred2_col; `ts_coverage_by`: ..., lower_col, upper_col; `ts_quantile_loss_by`: ..., forecast_col, quantile). The only new function is internal and `_`-prefixed: `_ts_passthrough`.

## Verification (observed, final build at 2118175 code, DuckDB v2.0.0)

- Tracer verify (plan command): `A,1,0.4764952155,0;B,1,0.2655145005,0;C,1,0.4759518449,0;` (OFFICIAL_VERIFY_PASS). 40/40 repeat runs identical, and 10/10 with `SET threads = 1`.
- 1.4.4 syntax probe prints `2`. A second 1.4.4 probe of the shipped form, a macro with `(SELECT * FROM query_table(src::VARCHAR) ORDER BY ALL)`, also prints `2`.
- `metrics.test`: All tests passed (143 assertions), and 10/10 repeat runs pass. RED before Task 2's change: the two-branch MAE pin failed (`metrics.test:445`). On the same build, union vs date-ordered sources diverged for every other native (mae 1.484136 vs 1.424555, rmae 0.771572 vs 0.745491, coverage 0.685714 vs 0.714286, quantile_loss 0.953467 vs 0.908216).
- `ts_metrics.test` 124 and `ts_metrics_reference.test` 25 assertions pass.
- `cargo test -p anofox-fcst-core --test metrics_reference`: `ok. 2 passed; 0 failed`. The fixture's 8 existing cases are byte-identical after regeneration, and the new cases have mase_accuracy == mase_manual (difference 0.0).
- Full native suite: All tests passed (5794 assertions in 94 test cases).
- `make check-docs`: 345 PASS, 0 FAIL, 8 EXPECTED-ERROR, 154 SYNOPSIS, 4 SKIP-NETWORK, 0 STALE.
- `make audit` exit 0, `cargo fmt --all -- --check` exit 0, `cargo clippy --workspace --all-targets -- -D warnings` exit 0.

### Parallel-intake stress (1M rows = 5000 series x 200 days, default threads = 20)

Plan loop (`big` from 20-perf.sql, `f = y + 1`):
```
for k in $(seq 20); do { cat big.sql; echo "SELECT count(*), round(sum(mae), 9) FROM ts_mae_by((SELECT id, ds, y, f FROM big), 'ds', 'y', 'f');"; } | env -u AI_AGENT ./build/release/duckdb -unsigned -no-agent -csv -noheader; done | sort | uniq -c
     20 5000,5000.0
```
Every per-series MAE is exactly 1 when f = y + 1, so that loop cannot detect lost rows. A stronger loop uses a row-varying error `g = y + (day % 13) * 0.25 + (series % 7) * 0.01`, with the pure-SQL reference `SELECT count(*), round(sum(m), 9) FROM (SELECT id, avg(abs(y - g)) m FROM big2 GROUP BY id)` = `5000,7524.95`:
```
     20 5000,7524.95          (ts_mae_by, plain source, 20 runs)
     10 5000,7524.95          (ts_mae_by, UNION ALL source, 10 runs)
     5000,7463.999586777      (baseline binary, same UNION ALL query: silently wrong)
```

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] MATERIALIZED CTE recipe replaced by an ORDER BY ALL breaker**
- **Found during:** Task 1 verify.
- **Issue:** With the plan's `WITH _ts_src AS MATERIALIZED (SELECT * FROM _ts_passthrough(source)) SELECT * FROM _ts_mase_native((SELECT * FROM _ts_src), ...)`, the tracer verify query failed 12/30 runs with `INTERNAL Error: Pipeline broadcast exchange received invalid producer batch index 40000000000000`. A single plain-table `ts_mase_by` under `SELECT id, count(DISTINCT mase), min(mase) ... GROUP BY id` failed 4/30, while the baseline passed 30/30. DuckDB v2.0.0 runs every non-recursive materialized CTE through the new pipeline broadcast exchange (plan_cte.cpp, `use_exchange`). With insertion order preserved and more than one thread it uses BATCH_INDEX mode, and a final-emitting in-out operator fed by it trips the batch-index check in `PipelineBroadcastExchangeLocalState::GetSourcePartitionData` (pipeline_broadcast_exchange.cpp:1002). There were 0/30 failures with `SET preserve_insertion_order = false` and 0/30 with `SET threads = 1`, and pure SQL never failed. The baseline binary fails 30/30 on a hand-written CTE + native query, so the defect predates this plan; the CTE recipe would have exposed it in ordinary queries.
- **Fix:** The macros use `(SELECT * FROM _ts_passthrough(source) ORDER BY ALL)`. An ORDER BY is a sink whose sorted output is scanned by one parallel pipeline, so it is the same single-pipeline guarantee without the exchange. Results: 40/40 verify runs, 40/40 on the previously failing aggregate shape, and 30/30 on the hand-expanded triggering query. `ORDER BY ALL` sorts MAP, LIST, STRUCT, UNION, JSON, BLOB, INTERVAL, BIT, UUID, ARRAY and VARIANT columns.
- **Exploratory cost** (1M rows, `_ts_metrics_native` mae, 5 alternating runs, medians, no other build running): raw native 0.403 s, `ORDER BY 1` 0.440 s, `ORDER BY ALL` 0.437 s, MATERIALIZED CTE 0.765 s. 20-05 does the formal comparison against the baseline binary.
- **Acceptance impact:** the plan's artifact `contains: "AS MATERIALIZED"` for ts_macros.cpp no longer holds by design. Every other grep holds: `_ts_passthrough(source)` = 11, `in_out_function_final` in ts_passthrough.cpp = 0, `ArriveAtFinal` in ts_metrics_native.cpp = 5, `threads_done_collecting` = 0.
- **Files:** src/macros/ts_macros.cpp, src/include/ts_collect_finalize.hpp (comments), src/table_functions/ts_passthrough.cpp (comments).
- **Commits:** bf7048c, 2118175.

**2. [Rule 2 - Correctness] Stronger stress loop and non-NULL checks**
- The plan's stress loop uses f = y + 1, which gives MAE = 1 on any subset of rows, so it cannot detect a partial finalize. I added the row-varying-error loop with a pure-SQL reference (above). In metrics.test, each EXCEPT ALL comparison is preceded by a `count(*), count(metric)` = `3 3` check, so an all-NULL result cannot pass. The helper forecast has a row-varying error for the same reason.

**3. [Rule 3 - Blocking] R fixture shape**
- metrics_reference.rs treats every case as one series, so `issue285_panel` is split into `issue285_panel_A/B/C` (the grep `issue285_panel` matches 3).

**4. [Rule 1 - Bug] Stale docs workaround**
- docs/api/09-evaluation-metrics.md said the `_by` macros "have threading issues requiring `SET threads=1` for correct results". That is wrong now, and at threads = 1 the bug reproduced anyway. It is replaced by the order-independence statement (commit 73f01b4).

## Notes for later plans

- **20-03, 20-04, 20-05 and 20-06 prescribe `WITH _ts_src AS MATERIALIZED`.** On DuckDB v2.0.0 that recipe hits the internal error above whenever the native sits in the CTE's consumer. Use `ORDER BY ALL` on the native's input subquery instead, e.g. `(SELECT group_col, date_col, value_col FROM query_table(source::VARCHAR) ORDER BY ALL)` for the query_table-style macros. Recorded in deferred-items.md.
- `_ts_rmae_native`, `_ts_coverage_native` and `_ts_quantile_loss_native` accumulate in arrival order, without a per-group date sort. The values are order-independent mathematically, but the last float bit may move, so the #285 pins compare `round(x, 10)`. Adding a sort would add `std::sort` in src/ (a `make audit` concern), so it was not done.
- The late-row guard turns any remaining multi-pipeline input into an error instead of a silent partial result. The raw natives fed a UNION ALL directly now error 30/30 (`_ts_mase_native: input rows arrived after the function had finalized...`). The previous behaviour was a silent NULL or partial value; only internal `_`-prefixed functions are affected.

## Requirement status

BUG-03 stays **Pending** in REQUIREMENTS.md, because 20-03, 20-04 and 20-05 also carry it (the UNION ALL scope expansion in decisions_addendum_2). The tooling marked it complete and I reverted that. ROADMAP marks 20-02 done (2/7).

## Deferred / out of scope

See .planning/phases/20-detection-metric-bug-fixes/deferred-items.md: the CTE recipe in later plans, the stale `SET threads=1` code comment above the metric macros in ts_macros.cpp, and an upstream DuckDB report for the broadcast-exchange defect (not yet filed).

## Self-Check: PASSED

- FOUND: src/include/ts_collect_finalize.hpp, src/table_functions/ts_passthrough.cpp, .planning/phases/20-detection-metric-bug-fixes/20-perf.sql, /home/simonm/projects/duckdb/scratch-p20/duckdb-baseline (+ .sha = c5a2aad617dbe5f9084a793e13016052c62c74a3)
- FOUND: commits bf7048c, 2118175, 73f01b4 (git log)
