---
phase: 20-detection-metric-bug-fixes
plan: 04
subsystem: feature-extraction/cross-validation/forecasting
status: complete
tags: [bug-fix, union-all, pipelines, in-out-function, cross-validation, panel, var, "#285", BUG-03]
requires:
  - 20-02 (TsCollectFinalizeBarrier, ORDER BY ALL breaker recipe)
  - 20-03 (union_source.test fixtures s2/f2/uv/ut/rv/sv)
provides:
  - union-safe ts_features_by, ts_cv_folds_by, ts_cv_split_by, ts_cv_hydrate_by, ts_cv_forecast_by, ts_forecast_panel_by, ts_forecast_var_by
  - ts_forecast_panel_by fits the series in group-key order (row-order and run independent)
  - complete BUG-03 in-out / native-calling macro audit table (below)
  - union_source.test fixtures cvt/cvu/cvr/cvs, sxt/sxu/sxr/sxs, wt/wu/wr/ws, pbig
affects:
  - 20-05 (the four remaining collect-then-finalize functions; same recipe; append pins to union_source.test)
tech-stack:
  added: []
  patterns:
    - "query_table-style _by macros: native input (... FROM query_table(x::VARCHAR) ORDER BY ALL); CTE-fed macro (ts_cv_hydrate_by): (SELECT * FROM joined ORDER BY ALL)"
    - "Cross-series fits iterate the groups std::map (key order), never arrival order"
key-files:
  created: []
  modified:
    - src/table_functions/ts_features_native.cpp
    - src/table_functions/ts_cv_folds_native.cpp
    - src/table_functions/ts_cv_split_native.cpp
    - src/table_functions/ts_cv_hydrate_native.cpp
    - src/table_functions/ts_cv_forecast_native.cpp
    - src/table_functions/ts_forecast_panel_native.cpp
    - src/table_functions/ts_forecast_var_native.cpp
    - src/macros/ts_macros.cpp
    - test/sql/correctness/union_source.test
    - CHANGELOG.md
decisions:
  - "ORDER BY ALL breaker, not AS MATERIALIZED (plan amendment; 20-02 broadcast-exchange internal error). _ts_passthrough is not used: all seven macros take table names via query_table, or (hydrate) a CTE"
  - "ts_cv_hydrate_by: the breaker sits on the native's only input, (SELECT * FROM joined ORDER BY ALL); the joined CTE itself is unchanged"
  - "ts_forecast_panel_by series order = group key order (Rule 1): the shared GlobalETS fit used arrival order, so results moved with intake interleaving even on plain tables"
metrics:
  duration: "about 50 min (14:13 to about 15:03 UTC, 2026-10-23)"
  completed: 2026-10-23
  tasks: 3
  files: 10
actuals:
  tokens: 12400
  tasks: 3
  commits: 3
plan_head_before: d890b8166fa29c1bd35f89ca9d7e7cb8ee30cee9
---

# Phase 20 Plan 04: union-safe feature, CV, panel and VAR `_by` macros Summary

Seven more macro-fronted collect-then-finalize natives now read their whole input on a UNION ALL source: `ts_features_by`, `ts_cv_folds_by`, `ts_cv_split_by`, `ts_cv_hydrate_by`, `ts_cv_forecast_by`, `ts_forecast_panel_by` and `ts_forecast_var_by`. Each uses an `ORDER BY ALL` single-pipeline breaker on the native's input plus `TsCollectFinalizeBarrier`. Before this plan they silently dropped a branch: for example, `ts_cv_forecast_by` returned 0 of 30 forecasts on a UNION ALL fold table. A row-order dependence in the GlobalETS panel fit was also fixed. After this plan, no macro-fronted native uses the old spin barrier.

## Commits

| Task | Commit | Message |
|------|--------|---------|
| 1 (tracer) | 2d0dc79 | fix(20-04): ts_features_by, ts_cv_folds_by and ts_cv_split_by read every row of a UNION ALL source (#285) |
| 2 (tdd) | 8ca56be | fix(20-04): ts_cv_hydrate_by, ts_cv_forecast_by and ts_forecast_panel_by union-safe (#285) |
| 3 | 8ceaafa | fix(20-04): ts_forecast_var_by union-safe; ts_forecast_panel_by fits series in key order (#285) |

`git rev-list --count d890b81..HEAD` = 3 before the metadata commit. All three commits carry the `Co-Authored-By: Claude Opus 5.5 (1M context)` trailer (3 of 3 by `grep -c`).

## BUG-03 audit table (CONTEXT #285 decision 2)

Source: `grep -rln in_out_function src/table_functions` (19 files, 23 in-out names), plus every `_ts_*(` call in a `src/macros/ts_macros.cpp` macro body (script over the macro entries). "CTF" means collect-then-finalize, i.e. it has an `in_out_function_final`.

### In-out table functions

| In-out function | Public front | Class | Disposition |
|---|---|---|---|
| `_ts_passthrough` | used inside the 11 metric macros | streaming (no final) | unaffected (20-02 helper) |
| `ts_combine_keys` | public, no macro | streaming (no final) | unaffected |
| `_ts_metrics_native` | ts_mae_by, ts_mse_by, ts_rmse_by, ts_mape_by, ts_smape_by, ts_r2_by, ts_bias_by | CTF with macro | fixed in 20-02 |
| `_ts_mase_native` | ts_mase_by | CTF with macro | fixed in 20-02 |
| `_ts_rmae_native` | ts_rmae_by | CTF with macro | fixed in 20-02 |
| `_ts_coverage_native` | ts_coverage_by | CTF with macro | fixed in 20-02 |
| `_ts_quantile_loss_native` | ts_quantile_loss_by | CTF with macro | fixed in 20-02 |
| `_ts_detect_changepoints_by_native` | ts_detect_changepoints_by | CTF with macro | fixed in 20-03 |
| `_ts_stats_by_native` | ts_stats_by | CTF with macro | fixed in 20-03 |
| `_ts_fill_gaps_native` | ts_fill_gaps_by | CTF with macro | fixed in 20-03 |
| `_ts_fill_forward_native` | ts_fill_forward_by (+ ts_fill_forward_operator over it) | CTF with macro | fixed in 20-03 |
| `_ts_mstl_decomposition_native` | ts_mstl_decomposition_by | CTF with macro | fixed in 20-03 |
| `_ts_features_native` | ts_features_by | CTF with macro | **fixed in 20-04** (Task 1) |
| `_ts_cv_folds_native` | ts_cv_folds_by | CTF with macro | **fixed in 20-04** (Task 1) |
| `_ts_cv_split_native` | ts_cv_split_by | CTF with macro | **fixed in 20-04** (Task 1) |
| `_ts_cv_hydrate_native` | ts_cv_hydrate_by | CTF with macro | **fixed in 20-04** (Task 2) |
| `_ts_cv_forecast_native` | ts_cv_forecast_by | CTF with macro | **fixed in 20-04** (Task 2) |
| `_ts_forecast_panel_native` | ts_forecast_panel_by | CTF with macro | **fixed in 20-04** (Task 2, plus the series-order fix in Task 3) |
| `_ts_forecast_var_native` | ts_forecast_var_by | CTF with macro | **fixed in 20-04** (Task 3) |
| `ts_aggregate_hierarchy` | public, no `_by` macro | CTF without macro | fixed in 20-05 (decisions_addendum_2; in scope, not deferred) |
| `ts_split_keys` | public, no `_by` macro | CTF without macro | fixed in 20-05 (decisions_addendum_2) |
| `ts_validate_separator` | public, no `_by` macro | CTF without macro | fixed in 20-05 (decisions_addendum_2) |
| `_ts_forecast_native` | internal; called directly only by 3 test files | CTF without macro | fixed in 20-05 (decisions_addendum_2) |

`grep -rln threads_done_collecting src` now lists exactly the four 20-05 files: ts_aggregate_hierarchy.cpp, ts_split_keys.cpp, ts_validate_separator.cpp and ts_forecast_native.cpp.

### Native-calling macros whose internal function is NOT an in-out function

These call scalar functions under SELECT/GROUP BY, or aggregates. DuckDB combines those across pipelines itself. The class is unaffected and was not changed.

| Macro(s) | Internal function | Kind |
|---|---|---|
| ts_forecast_by | `_ts_forecast_scalar` | scalar under GROUP BY (parallel hot path) |
| ts_forecast_ensemble_by | `_ts_forecast_ensemble_native` | scalar |
| ts_ensemble_inspect_by, ts_auto_ensemble_inspect_by | `_ts_ensemble_inspect_native`, `_ts_auto_ensemble_inspect_native` | scalar |
| ts_forecast_inspect_by, ts_forecast_explain_by | `_ts_forecast_inspect_scalar`, `_ts_forecast_explain_scalar` | scalar |
| ts_forecast | `_ts_forecast` | scalar |
| ts_forecast_exog, ts_forecast_exog_by | `_ts_forecast_exog`, `_ts_future_dates` | scalar |
| ts_conformal_by, ts_conformal_calibrate | `_ts_conformal_from_pairs`, `_ts_conformal_calibrate` | scalar |
| ts_stats | `_ts_stats_with_dates` | scalar |
| ts_data_quality, ts_data_quality_summary, ts_data_quality_by | `_ts_data_quality` | scalar |
| ts_drop_constant_by, ts_fill_nulls_mean_by, ts_diff_by | `_ts_is_constant`, `_ts_fill_nulls_mean`, `_ts_diff` | scalar |
| ts_detect_changepoints | `_ts_detect_changepoints_bocpd` | scalar |
| ts_detect_periods, ts_detect_periods_by | `_ts_detect_periods` | scalar |
| ts_classify_seasonality_by and other `*_agg` users | src/aggregate_functions (7 aggregates, which sort by timestamp in finalize) | aggregate |

Spot check on the final build (union_source fixture, `uv` vs `ut`, EXCEPT ALL both ways summed): ts_forecast_by 0, ts_data_quality_by 0, ts_detect_periods_by 0, in 5 of 5 runs. ts_forecast_by returned 9 rows (3 ids x 3).

## What changed

- **Natives** (7 files): the `finalize_claimed` / `threads_collecting` / `threads_done_collecting` spin barrier was replaced by `TsCollectFinalizeBarrier`, applied mechanically exactly as in 20-03. Each native calls `RegisterLocal()` in init_local, `AssertCollecting("<native>")` at InOut entry and `ArriveAtFinal()` once per local state, and the unused InOut `lstate` was removed. MaxThreads is unchanged: features `max_threads`, cv_folds 1, cv_split its existing value, cv_hydrate 1, cv_forecast its existing value, panel and var 999999. `grep -c threads_done_collecting` returns 0 for all seven files.
- **Macros** (`ts_macros.cpp`, 7 bodies): `ORDER BY ALL` was appended inside the native's input subquery. ts_features_by, ts_cv_split_by and ts_forecast_panel_by use `(SELECT g, d, v FROM query_table(source::VARCHAR) ORDER BY ALL)`. ts_cv_folds_by and ts_cv_forecast_by keep their aliased column lists and add `ORDER BY ALL`. ts_forecast_var_by uses `(SELECT * FROM query_table(source::VARCHAR) ORDER BY ALL)`. ts_cv_hydrate_by uses `(SELECT * FROM joined ORDER BY ALL)`; joined is the native's only input, and the cv/src CTEs are untouched. Every other argument is unchanged, and `AS MATERIALIZED` appears nowhere.
- **Panel series order** (`ts_forecast_panel_native.cpp`): at the start of finalize, `group_order` is rebuilt from the `groups` std::map (key order). It no longer uses the arrival order. No `std::sort` was added, and `make audit` stays clean.

## API compatibility (Phase 20.1 rule)

I captured `duckdb_functions()` `parameters` and `parameter_types` for the seven macros, plus `DESCRIBE` of one call of each, on the 20-03 build (binary saved as /home/simonm/projects/duckdb/scratch-p20/duckdb-20-03). I captured them again after Task 1, after Task 2 and on the final build. `diff` returned **identical** output all three times (159 lines). Parameters:
- ts_features_by: source, group_col, date_col, value_col
- ts_cv_folds_by: source, group_col, date_col, target_col, n_folds, horizon, params
- ts_cv_split_by: ..., training_end_times, horizon, params
- ts_cv_hydrate_by: cv_folds, source, group_col, date_col, unknown_features, params
- ts_cv_forecast_by: ml_folds, group_col, date_col, target_col, method, params
- ts_forecast_panel_by: ..., method, horizon, frequency, params
- ts_forecast_var_by: source, date_col, value_cols, horizon, frequency, p, params

The output columns are unchanged, and no function was added.

## Verification (observed)

- **RED, 20-03 build** (CLI probe, a wobbled copy of the s2 fixture, 3 of 3 runs): ts_features_by gave 3 of 3 rows differing on `uv` vs `ut`. ts_cv_folds_by returned 171 rows (`ut` 255), ts_cv_split_by 162 (`ut` 177), and ts_forecast_panel_by differed on 9 of 9 rows. On the test fixture, `ts_features_by('uv')` gave length 31 and slope 0.3 (correct: 45 and 0.6345059289). Panel A ranged 29.282066..29.82939 (correct: 45.321346..46.135993).
- **RED, Task 1 build** (Task 2 TDD): union_source.test failed at :547 (panel, 9 ≠ 0) in 4 of 5 runs and at :482 in 1 of 5. CLI: `ts_cv_forecast_by('cvu')` returned **0** rows in 10 of 10 runs (`cvt` returned 30). ts_cv_hydrate_by returned 69 of 255 rows in 1 of 10 runs.
- **RED, Task 2 build** (VAR): union_source.test failed at :619 with 6 ≠ 0. On `wu`, y1 was 14.442128..14.449617 (the first branch alone); on `wt` it was 19.071735..19.168724.
- **Tracer gate** (interactive, `human_verify_mode=end-of-phase`, automated verify, so a re-run): the plan's verify printed `All tests passed (170 assertions` → TRACER_VERIFY_PASS. The CLI probe passed 10 of 10 at default threads and 10 of 10 at `threads = 1`.
- **GREEN**: union_source.test passes with 269 assertions in 12 of 12 repeat runs on the final build. Earlier counts: 170 after Task 1 (12 of 12), 239 after Task 2 (12 of 12), 257 after Task 3 (15 of 15).
- All test files that mention the seven macros or their natives pass: 16 for Task 1 and 15 for Task 2, including ts_cv_folds 625, ts_cv_split 102, ts_cv_forecast 85, ts_features_reference 111 and ts_native_model_names 63 assertions.
- **Large-panel stress** (200k rows = 1000 series x 200 days; UNION ALL, reversed and shuffled views; fold, source and wide views; 3000-row VAR): EXCEPT ALL against the table was 0 for all seven macros in 10 of 10 runs. A 400k-row run of features, folds and panel gave equal union and table values in 10 of 10 runs, and that run exposed the panel order issue (Deviation 1).
- Late-row guard: `_ts_features_native` fed the UNION ALL directly now raises `input rows arrived after the function had finalized` in 9 of 10 runs; the 10th returned 3 rows. Only the internal native is affected, the same as in 20-03.
- Full native suite: All tests passed (6069 assertions in 95 test cases), both before and after the panel fix.
- `make check-docs`: 345 PASS, 0 FAIL, 8 EXPECTED-ERROR, 154 SYNOPSIS, 4 SKIP-NETWORK, 0 STALE.
- `make audit` exit 0, `cargo fmt --all -- --check` exit 0, `cargo clippy --workspace --all-targets -- -D warnings` exit 0.
- `scripts/check_correctness_inventory.py`: all seven new CHANGELOG names are `True` (union_source.test). It still exits 1, only on the pre-existing `MISSING ts_features_config_template` / `ts_features_list` (logged in deferred-items.md by 20-03).
- Plan Task 3 grep: `! grep -n threads_done_collecting <13 macro-fronted natives>` → NO_SPIN_BARRIER_LEFT. The CHANGELOG `[Unreleased]` grep for `` `ts_cv_forecast_by` `` = 2.

## CI

Pushed `d890b81..8ceaafa` to `milestone/v0.11.0-issue-sweep`. The "Static audits" run is **39974255810**; its result is recorded below. As established in 20-03, a branch push does not trigger MainDistributionPipeline, and per the orchestrator instruction I did not dispatch it (the orchestrator runs it once at the end of Phase 20). For this plan, the wasm-runtime-test and the v1.4.5/v1.5.6/v2.0.0 build jobs are therefore covered by that end-of-phase run and not here.

- Static audits run 39974255810 (head 8ceaafa): **completed, success**. Job "Audit no-stats-in-C++ + length-only sweep": success.
- MainDistributionPipeline (wasm-runtime-test, v1.4.5 / v1.5.6 / v2.0.0 jobs): **not run for this plan**. It is deferred to the orchestrator's single end-of-Phase-20 dispatch.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] ts_forecast_panel_by fitted the series in arrival order**
- **Found during:** Task 3 stress verification.
- **Issue:** `_ts_forecast_panel_native` builds the GlobalETS panel matrix in `group_order`, which is the order in which each group's first row arrived. The shared-parameter fit depends on that order. On a 400k-row panel (2000 series), `sum(yhat)` on the plain table alternated between 127800.5516 and 127800.5522 at default threads on the post-barrier build. The 20-03 binary gave 127800.5522 at default threads but 127800.5519 at `threads = 1`, so the order dependence predates this plan. This is the row-order dependence that CONTEXT #285 decision 2 says to fix, and it broke the "same result on reversed/shuffled sources" truth on large panels.
- **Fix:** At the claim, `group_order` is rebuilt from the key-ordered `groups` std::map, without `std::sort`. Afterwards, default threads and `threads = 1` both give 127800.5516 (6 of 6 runs), and the 200k-row stress is 0 in 10 of 10. A new union_source.test pin compares the default-threads result with the `threads = 1` table and with a shuffled copy, both ways (6000 rows, 2000 ids, sum 127800.5516).
- **Effect:** On plain tables, panel forecasts can change at about the 1e-7 level per forecast versus the previous run-dependent value. Every existing panel test passes unchanged, and the CHANGELOG states the change.
- **Files:** src/table_functions/ts_forecast_panel_native.cpp, test/sql/correctness/union_source.test, CHANGELOG.md. **Commit:** 8ceaafa.

**2. [Amendment] ORDER BY ALL breaker instead of a MATERIALIZED CTE**
- This plan's body and its key_link pattern `AS MATERIALIZED` are superseded by the top amendment (20-02 broadcast-exchange internal error). `_ts_passthrough(` does not apply here: these macros take table names through `query_table`, and hydrate is CTE-fed, so the form is the query_table `ORDER BY ALL` one from 20-03. For hydrate, the plan's "mark the joined CTE AS MATERIALIZED" became `(SELECT * FROM joined ORDER BY ALL)`.

**3. [Rule 2 - Correctness] Non-vacuous pins**
- Each EXCEPT ALL block has an observed count/value check before it, so an empty or all-NULL result cannot pass: features length/slope, folds and split per fold/split counts, cv_forecast 15 non-NULL yhat per fold, hydrate per fold/split counts with a non-NULL temperature, panel per-id yhat range, VAR per-variable range. The check matters most for cv_forecast: on the Task 1 build it returned 0 rows on `cvu`, so only the table-minus-view direction caught it.
- The hydrate pin makes BOTH inputs multi-pipeline (`cvu` and `sxu`), because the JOIN's build side would absorb a UNION ALL in only one of them.

**4. [Plan fixture] CV and VAR fixtures**
- The 45-row-per-id `uv` view supports 2 folds x horizon 5, so the CV pins use it directly through a materialized fold table `cvt` from `ut` and its UNION ALL, reversed and shuffled views. VAR uses its own contiguous 84-day wide fixture, split at 2024-02-15 with both branches non-empty, as the plan allowed.

### Out-of-scope discoveries

None new. The pre-existing correctness-inventory exit 1 is already in deferred-items.md.

## Known Stubs

None.

## Requirement status

BUG-03 stays **Pending**, because 20-05 also carries it (the four non-`_by` collect-then-finalize functions).

## Self-Check: PASSED

- FOUND: test/sql/correctness/union_source.test (grep -c on the final file: 8 ts_forecast_var_by, 31 cv_hydrate/cv_forecast/panel, 25 features/folds/split matching lines; `AS MATERIALIZED` in ts_macros.cpp: 0), CHANGELOG.md entry, and this SUMMARY
- FOUND: commits 2d0dc79, 8ca56be and 8ceaafa (git log); pushed to origin/milestone/v0.11.0-issue-sweep (`d890b81..8ceaafa`)
