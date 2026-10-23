# Phase 20 deferred items

## From 20-02 (2026-10-23)

- **Plans 20-03, 20-04, 20-05, 20-06 prescribe `WITH _ts_src AS MATERIALIZED (...)` as the single-pipeline recipe.** On DuckDB v2.0.0 that CTE runs through the pipeline broadcast exchange. A final-emitting in-out native fed by it intermittently raises `INTERNAL Error: Pipeline broadcast exchange received invalid producer batch index` (observed in 20-02: 4/30 runs of a single `ts_mase_by` under `count(DISTINCT mase), min(mase) GROUP BY id`; 9/30 to 19/30 on the 4-shape verify query; 0/30 with `preserve_insertion_order = false` or `threads = 1`). 20-02 used `(SELECT * FROM _ts_passthrough(source) ORDER BY ALL)` instead (0/40 failures, about 0.04 s per 1M rows versus about 0.36 s for the CTE). Those plans should use the ORDER BY ALL breaker. For query_table-style macros this is `(SELECT group_col, date_col, value_col FROM query_table(source::VARCHAR) ORDER BY ALL)`. Their `AS MATERIALIZED` acceptance greps also need updating.
- `src/macros/ts_macros.cpp` (comment above the metric macros, ~line 2420) still says the table macros "have threading issues requiring SET threads=1 for correct results". That is stale after #285. It was not changed in 20-02 because the comment is outside Task 3's file list.
- Upstream: the broadcast-exchange internal error above is a DuckDB v2.0.0 defect (it also hits the pre-20-02 baseline binary when a user wraps a native in a MATERIALIZED CTE: 30/30). It has not been reported yet.

## From 20-07 (2026-10-23)

- **`scripts/check_correctness_inventory.py` exits 1 with THREE missing names, not the
  two 20-03/20-04 already logged below.** `MISSING ts_features_config_template` and
  `MISSING ts_features_list` are the pre-existing Phase 19 BUG-02 entries (unchanged).
  `MISSING _ts_forecast_scalar` is new to this report but NOT caused by this plan: the
  backtick mention the script keys off (`` `_ts_forecast_scalar` ``) was added to
  CHANGELOG `## [Unreleased]` by Phase 20 plan 20-02 (confirmed present, unchanged, in
  CHANGELOG.md at the pre-20-07 commit `4a9694d`) and only ever appears inside a
  `union_source.test` comment (`# ts_forecast_by: already union-safe (GROUP BY +
  _ts_forecast_scalar); pinned ...`), never as an actual correctness-test call the
  inventory script's coverage check recognises. This plan touched neither
  `_ts_forecast_scalar` nor `union_source.test`. Every function this plan added to the
  CHANGELOG (`ts_detect_changepoints_by`/`ts_detect_changepoints_agg`) IS covered. This
  is a manual release-DoD gate (per 20-03's note below), so all three must be fixed
  before the next tag.

## From 20-03 (2026-10-23)

- **docs/api/04-data-preparation.md "Do not chain table functions in CTEs" warning may be stale.** Its own "broken" example (`WITH gaps_filled AS (ts_fill_gaps_by(...)), nulls_filled AS (ts_fill_nulls_const_by('gaps_filled', ...)) SELECT count(*) FROM ts_drop_short_by('nulls_filled', ...)`) on a 1M-row panel (5000 series x 200 days, every 11th row dropped) returned 999090 rows in 3/3 runs on BOTH the pre-20-03 binary (21dfe7e) and the 20-03 build, so the documented 0-row failure did not reproduce on DuckDB v2.0.0 at default threads. Not changed in 20-03: removing a workaround needs a pin across the v1.4.5/v1.5.6/v2.0.0 lines (memory: docs workarounds = open bugs).
- **`scripts/check_correctness_inventory.py` exits 1 (pre-existing, not from 20-03):** `MISSING ts_features_config_template` and `MISSING ts_features_list`. Both names come from the Phase 19 BUG-02 entry already in CHANGELOG `[Unreleased]` (HEAD before 20-03), and no `test/sql/correctness/*.test` exercises them. Every function this plan added to the CHANGELOG is covered (union_source.test). This is a manual release-DoD gate, so it must be fixed before the next tag.
