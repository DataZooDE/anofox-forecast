---
status: resolved
trigger: "ts_stats_by results depend on input row order (found during DuckDB 2.0 parity check, quick task 261006-ujg)"
created: 2026-10-08
updated: 2026-10-08T22:30
---

# Debug: ts_stats_by results depend on input row order

## Work location (IMPORTANT)

- Fix target is `main`, NOT the DuckDB 2.0 branch.
- Worktree: `/home/simonm/projects/duckdb/anofox-forecast-fix-stats`, branch `fix/ts-stats-order`,
  forked from `origin/main` @ 9d01d9c (DuckDB submodule v1.5.6). Submodules initialized; NOT built yet.
- Do all code edits, builds and commits in that worktree. Do not touch the main tree
  (`/home/simonm/projects/duckdb/anofox-forecast`, branch `test/duckdb-2.0`) except this debug file.
- Build there: `GEN=ninja make release` (cold build ~10-15 min; run in background with a log ending
  in `EXIT=<code>`). Before the build: `rustup target list --installed` includes
  `wasm32-unknown-emscripten` on this machine, and main's CMakeLists.txt:71 still has the bug that
  forces the Rust target to wasm whenever it is installed -> native link fails with
  `libanofox_fcst_ffi.a: file format not recognized`. Locally (UNCOMMITTED, do not include in the
  fix commits) change that line to `if (EMSCRIPTEN AND NOT WASM_TARGET_FOUND EQUAL -1)`.
  That fix ships in a separate PR.
- Telemetry: export `DATAZOO_DISABLE_TELEMETRY=1` for every duckdb/unittest run.
- Another session periodically runs heavy builds in `/home/simonm/projects/duckdb/anofox-integrate`;
  it is unrelated — do not touch it.

## Symptoms

- **Expected:** `ts_stats_by(table, group_col, date_col, value_col, freq)` returns the same
  per-series statistics regardless of the physical row order of the input table (the date column
  defines order).
- **Actual:** order-dependent statistics change when the same rows arrive in a different order.
  M4 Daily series `D1278` (`benchmark/data/m4_daily_train_long.parquet`, from the main tree):
  - full table in file order: `trend_strength = 0.145061`
  - same rows after `CREATE TABLE m4shuf AS SELECT * FROM m4 ORDER BY random()`:
    `trend_strength = 0.002384` (v1.5.5 build) / `0.021227` (another shuffle, 2.0 build)
  - small table with only 5 series: `trend_strength = 0.932593`, `stability = 2.076544`
    (differs again from the full-table value)
  - Columns that differed between two builds fed the same data in different scan order (5 of
    4227 series): `autocorr_lag1`, `trend_strength`, `seasonality_strength`, `stability` (large
    diffs, e.g. trend_strength 0.93 vs 0.145), plus `mean/sum/variance/std_dev/skewness/kurtosis`
    differing only in the last float bits (summation order).
  - Data checks: D1278 has no duplicate dates; rows are in ds order in the parquet file.
- **Errors:** none — silently wrong values.
- **Timeline:** present on v1.5.5 (@7e23980) and DuckDB 2.0 builds alike; not a DuckDB-version
  regression. Likely since ts_stats_by became the streaming native implementation
  (`_ts_stats_by_native`, see `src/macros/ts_macros.cpp` ~line 1775).
- **Reproduction:**
  ```sql
  LOAD '<worktree>/build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';
  CREATE TABLE m4 AS SELECT * FROM read_parquet('/home/simonm/projects/duckdb/anofox-forecast/benchmark/data/m4_daily_train_long.parquet');
  SELECT round(trend_strength,6) FROM ts_stats_by('m4', unique_id, ds, y, '1d') WHERE unique_id='D1278';
  CREATE TABLE m4shuf AS SELECT * FROM m4 ORDER BY random();
  SELECT round(trend_strength,6) FROM ts_stats_by('m4shuf', unique_id, ds, y, '1d') WHERE unique_id='D1278';
  -- expected equal; actual differ
  ```
  Run with `<worktree>/build/release/duckdb -unsigned`.

## Current Focus

status_note: RESOLVED — user confirmed. Fix 2f221eb pushed as PR #271 (main); cherry-picked to test/duckdb-2.0 as 86a97db
next_action_now: human confirms (optionally re-run scratchpad/repro.sql against the worktree build); then archive session to resolved/ and write knowledge-base entry. Not pushed / no PR (separate decision).
reasoning_checkpoint:
  hypothesis: "TsStatsByFinalize hands grp.values/timestamps/validity to anofox_ts_stats_with_dates_and_type in arrival (scan/thread-interleave) order; the Rust kernel computes all order-dependent stats on that order and only sorts a copy of the dates for gap metrics -> results depend on physical row order."
  confirming_evidence:
    - "Code: no sort in TsStatsByFinalize; compute_ts_stats_with_dates_and_type calls compute_ts_stats(series) unsorted and sorts only dates.to_vec()."
    - "Repro: D1278 trend_strength 0.145061 (file order) / 0.02017 (random) / 0.001817 (hash order) vs 0.932593 from ts_stats (LIST ORDER BY ds) on identical rows; 4227/4227 series differ on shuffled input."
    - "New regression test RED on unfixed build: 64/64 series differ (test/sql/ts_stats_by_order_independence.test:52)."
  falsification_test: "If sorting by date in finalize does NOT make shuffled == ordered == ts_stats oracle (bit-identical EXCEPT = 0 rows) for M4 and the regression test, the hypothesis is wrong/incomplete."
  fix_rationale: "Sorting indices by (date, valid-first, value with NaN last) in finalize and permuting values+validity+timestamps together gives the kernel the date-ordered series the stats are defined on — the same pattern every other *_native finalizer and every aggregate already uses. It removes the order dependence at its source rather than masking a symptom."
  blind_spots: "Duplicate timestamps have no 'correct' order; the tie-break just makes it deterministic. Rust kernel contract (compute_ts_stats_with_dates*) still trusts caller order — the scalar _ts_stats_with_dates is only reachable via the ts_stats macro which uses LIST(... ORDER BY)."
  candidate_causes:
    - "code: missing sort in C++ finalize (CONFIRMED)"
    - "code (Rust core): kernel sorts dates only for gap metrics, not values (contributing contract gap, not required to fix)"
    - "environment: DuckDB 1.5 vs 2.0 scan order / thread count (ELIMINATED as cause; it only changes which wrong order arrives)"
    - "data: duplicate dates / unsorted parquet (ELIMINATED: D1278 has no dup dates, file is ds-ordered)"
  and_gate: "no — the missing sort alone is sufficient; varying arrival order (threads, CTAS order) is merely the trigger that exposes it."

bug_class: Bohrbug (deterministic given a fixed physical row order; varies only because arrival order varies)
hypothesis: TsStatsByFinalize (src/table_functions/ts_stats.cpp) passes grp.values in arrival order to anofox_ts_stats_with_dates_and_type; the Rust core compute_ts_stats_with_dates_and_type sorts only a COPY of the dates (for gap counting) and computes all order-dependent stats (autocorr_lag1, trend_strength, seasonality_strength, stability, ...) on the unsorted series.
test: build worktree, run repro (ordered vs shuffled D1278); then audit sibling *_native/aggregate paths for the same pattern
expecting: ordered != shuffled before fix; equal after sorting by date in finalize
next_action: (done) see status_note / next_action_now above
regression_test: test/sql/ts_stats_by_order_independence.test (no `require json`; RED pre-fix, GREEN post-fix)
known_pattern_candidate: none (no .planning/debug/knowledge-base.md exists yet)
build_log: /tmp/claude-1000/-home-simonm-projects-duckdb-anofox-forecast/c83cfa07-b4cb-4b46-bfdb-1456af899b96/scratchpad/build1.log (local CMakeLists.txt:71 tweak applied, uncommitted)

## Fix requirements

- Sort each group's values by date before computing statistics (match how other *_native
  functions do it, e.g. `_ts_forecast_native` sorts indices by date in its finalize).
- Check sibling native/aggregate paths for the same defect (e.g. ts_features_by, ts_data_quality_by,
  ts_stats_agg, ts_classify_seasonality, ts_detect_periods) and fix or list them.
- Add a regression sqllogictest (shuffled vs ordered input give identical stats) under test/sql/
  that does NOT `require json` (so it actually runs natively).
- Run `cargo fmt --check` if any Rust changes; run the full `test/*` suite before/after and report
  any change in pass/fail.
- Commit on `fix/ts-stats-order` (do not push). Commit message trailer:
  `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`

## Evidence

- timestamp: 2026-10-08
  checked: src/table_functions/ts_stats.cpp TsStatsByInOut / TsStatsByFinalize (worktree @9d01d9c)
  found: InOut appends (timestamp, value, validity) per group in arrival order under a mutex; Finalize
    builds the validity bitmask and calls anofox_ts_stats_with_dates_and_type(grp.values, ..., grp.timestamps)
    with NO sort by timestamp. Groups are also iterated in first-arrival order (output row order only).
  implication: the series handed to Rust is in physical scan order (and interleaved across threads).

- timestamp: 2026-10-08
  checked: crates/anofox-fcst-core/src/stats.rs compute_ts_stats_with_dates_and_type
  found: calls compute_ts_stats(series) on the series AS GIVEN, then sorts a copy of `dates` only for
    expected_length / n_gaps. The values are never reordered.
  implication: expected_length/n_gaps are order-independent, but every order-dependent statistic
    (autocorr_lag1, trend_strength, seasonality_strength, stability, n_zeros_start/end, plateau_size...)
    is computed on scan order; order-independent ones (mean/sum/variance...) differ only by FP summation order.
    Matches the symptom pattern exactly.

- timestamp: 2026-10-08
  checked: sibling audit — every in_out_function_final table function and every aggregate in the worktree
  found: sort-by-date before the FFI call is present in: _ts_forecast_native, _ts_features_native,
    _ts_mstl_decomposition_native, _ts_forecast_panel_native, _ts_forecast_var_native, _ts_changepoints
    (native), _ts_backtest_native, _ts_cv_forecast_native, _ts_cv_folds/_ts_cv_split (index sort),
    _ts_metrics_native (all 5 finalizers: metrics/mase/rmae/coverage/quantile_loss), and all 7 aggregates
    (ts_stats_agg, ts_features_agg, ts_data_quality_agg, ts_classify_seasonality_agg, ts_detect_periods_agg,
    ts_changepoints_agg, ts_forecast_agg) which sort (timestamp,value) pairs. No sort in:
    _ts_stats_by_native (THE BUG), _ts_fill_gaps_native / _ts_fill_forward_native (Rust core fill_gaps sorts
    pairs itself; fill_forward only uses max(date) and appends rows -> output is a set, order-independent),
    ts_split_keys / ts_validate_separator (no time semantics). Macros: the only LIST() without ORDER BY are
    LIST(residual) for ts_conformal_quantile (order-independent) and LIST(_grp) of invalid group names.
    The non-_by `ts_stats` macro uses LIST(value ORDER BY date) -> correct, usable as a derived oracle.
  implication: _ts_stats_by_native is the ONLY affected path; fix is local to TsStatsByFinalize.
    Side note: aggregates/_ts_forecast_native sort by date only (or (date,value)), so exact-duplicate
    timestamps are tie-broken by std::sort (unstable) — not in scope; ts_stats_by fix uses a full tie-break.

- timestamp: 2026-10-08
  checked: repro on UNFIXED worktree build (9d01d9c + local CMake tweak), script scratchpad/repro.sql;
    output saved to scratchpad/repro_before.txt
  found: D1278 (round 6):
      input            trend     acf1       seas      stab       ent       mean
      ordered (file)   0.145061  0.998085   0.996006  2.331672   2.160825  6864.043969713373
      random()         0.02017   -0.000257  0.030115  42.607488  2.160825  6864.0439697133725
      hash()           0.001817  -0.029549  0.034212  58.04885   2.160825  6864.043969713364
      oracle ts_stats  0.932593  0.998689   0.997254  2.076544   2.160825  6864.043969713359
    Full-table EXCEPT vs oracle (ts_stats = LIST(... ORDER BY ds)): ordered 85/4227 rows differ,
    random() 4227/4227, hash() 4227/4227.
  implication: reproduced deterministically. Even the parquet file-order input is wrong for 85 series
    (parallel parquet scan splits a series across threads -> interleaved arrival). The correct value is
    the oracle's (0.932593 == the "5-series small table" value in Symptoms, where the scan was single-threaded
    and in order). entropy is order-independent in this kernel; mean differs only in last bits.

- timestamp: 2026-10-08
  checked: full `test/*` sqllogictest suite on UNFIXED worktree build (incl. new regression test);
    log scratchpad/tests_before.log
  found: test cases 67 | 66 passed | 1 failed | 50 skipped (require json: 50); assertions 1334 | 1333 passed | 1 failed.
    Only failure: test/sql/ts_stats_by_order_independence.test:52 (actual 64, expected 0) -> RED as intended.
  implication: baseline for pre-existing tests = all pass (16 executed + 50 json-skipped). Because 50 files are
    json-gated (incl. test/sql/ts_stats.test, which covers ts_stats_by), also running json-stripped copies of those
    50 files (scratchpad/nojson, --test-dir) before/after as an extra diff signal.

- timestamp: 2026-10-08
  checked: FIXED build — repro (scratchpad/repro_after.txt), full suite (tests_after.log), json-stripped set (nojson_after.log)
  found: D1278 ordered / random() / hash() / oracle all = trend 0.932593, acf1 0.998689, seas 0.997254,
    stab 2.076544, ent 2.160825, mean 6864.043969713359 (bit-identical). Full-table EXCEPT vs oracle:
    0 / 0 / 0 of 4227 rows differ. Full suite: "All tests passed (50 skipped tests, 1357 assertions in 17 test
    cases)", EXIT=0. json-stripped 50 files: 22 passed / 28 failed both before and after, identical failing
    file:line set (pre-existing test debt, e.g. ts_stats.test:281 `INTERVAL i DAY` parser error,
    anofox_fcst__ts_fill_forward_native missing). With ts_stats.test additionally patched to `INTERVAL (i) DAY`
    (scratchpad/nojson2): all 96 assertions pass on the fixed build.
  implication: fix verified on the original repro, the derived oracle, and no regressions in any runnable test.

- timestamp: 2026-10-08
  checked: fix-acceptance guardrail (manual mutants, revert-and-reconfirm, final state)
  found:
    - M1 (validity bitmask not permuted: grp.validity[i] instead of [src]) -> regression test FAILS at :52 (killed)
    - M2 (comparator = timestamp only, no tie-break) -> passes :52..:150, FAILS at :165 duplicate-timestamp EXCEPT (killed)
    - Revert (git stash of ts_stats.cpp, clean recompile): regression test FAILS at :52; repro D1278 back to
      0.145061 / 0.01643 / 0.002557 vs oracle 0.932593; 85 / 4227 / 4227 rows differ; patched ts_stats.test 96/96 pass.
    - Reapply (stash pop, cmp == fixed copy, clean recompile): full suite all pass (1357 assertions / 17 test cases,
      50 json-skipped); regression test 5/5 runs pass (29 assertions); patched ts_stats.test 96/96; repro all equal
      to oracle, 0/0/0 rows differ.
    - Note: one throwaway M2 attempt was invalidated (zsh `cp -i` alias hung, its trailing build ran concurrently);
      result discarded, M2 redone cleanly with `command cp -f`.
  implication: the sort (and the validity permutation, and the tie-break) are each load-bearing and each is pinned
    by a distinct assertion; the change alone fixes the bug.

- timestamp: 2026-10-08
  checked: commit on fix/ts-stats-order
  found: 2f221eb "fix(ts_stats_by): sort each series by date before computing stats" — exactly 2 files
    (src/table_functions/ts_stats.cpp +46/-7, test/sql/ts_stats_by_order_independence.test new, 178 lines).
    Left uncommitted on purpose: CMakeLists.txt:71 local wasm tweak, src/include/anofox_fcst_ffi.h (cbindgen
    regenerated a pre-existing doc-comment drift on anofox_ts_forecast_ensemble during the build), .cache/.
    No Rust changed -> cargo fmt --check not applicable.
  implication: commit is clean and minimal.

## Eliminated

- hypothesis: DuckDB-version regression (2.0 vs 1.5.x scan behaviour)
  evidence: reproduced on main @9d01d9c with DuckDB v1.5.6; the defect is in extension code (no sort), DuckDB only
    changes the arrival order that exposes it.
  timestamp: 2026-10-08

- hypothesis: input data problem (duplicate dates or unordered parquet)
  evidence: Symptoms data checks (no dup dates, file is ds-ordered); ts_stats (LIST ORDER BY) on the same rows
    returns a stable, plausible value; shuffling identical rows changes ts_stats_by output.
  timestamp: 2026-10-08

## Resolution

root_cause: "_ts_stats_by_native (src/table_functions/ts_stats.cpp, TsStatsByFinalize) passed each group's values/validity/timestamps to anofox_ts_stats_with_dates_and_type in arrival order (physical scan order, interleaved across threads) without sorting by date. The Rust kernel computes all order-dependent statistics on the series as given (it only sorts a copy of the dates for expected_length/n_gaps), so autocorr_lag1, trend_strength, seasonality_strength, stability, n_zeros_start/end, plateau sizes etc. depended on row order; order-independent stats differed only by FP summation order. It is the only native/aggregate stats path lacking the sort every sibling has."
fix: "TsStatsByFinalize now builds an index permutation sorted by (timestamp, valid-before-NULL, value with NaN last via a NaN-safe strict-weak-order helper StatsValueLess) and permutes values, validity bitmask and timestamps together before the FFI call. No Rust change."
oracle_type: derived (ts_stats macro = LIST(value ORDER BY date) through the same Rust kernel) + metamorphic
  (permutation of input rows must not change output); boundary neighbours: NULL in series, leading/trailing
  zeros, duplicate timestamps incl. NULL + NaN, DATE vs TIMESTAMP, >1 row group / 4 threads.
verification:
  target_test:        { result: pass, test: test/sql/ts_stats_by_order_independence.test, red_before_fix: ":52 actual 64 expected 0" }
  mutation_check:     { result: pass, tool: "manual (no Stryker for C++)", mutants: ["M1 validity not permuted -> killed at :52", "M2 no tie-break -> killed at :165"] }
  no_op_deletion:     { result: pass, note: "additive: index sort + permutation; no branch/assertion removed" }
  adjacent_tests:     { result: pass, suites_run: ["test/* full suite: before 66/67 pass (only new test red), after 17 run + 50 json-skipped all pass, 1357 assertions", "50 json-gated files with require json stripped: 22 pass / 28 fail before AND after, identical failing set (pre-existing debt)", "ts_stats.test json-stripped + INTERVAL syntax patched: 96/96 before (reverted) and after"] }
  revert_and_reconfirm: { result: pass, bug_returned_on_revert: true, fixed_on_reapply: true }
  stability:          "regression test 5/5 consecutive passes; M4 repro with random() shuffle (fresh each run) equal to oracle"
  guardrail_verdict:  accepted
commit: 2f221eb (branch fix/ts-stats-order, not pushed)
files_changed:
  - src/table_functions/ts_stats.cpp
  - test/sql/ts_stats_by_order_independence.test (new)
why_not_caught: "ts_stats_by tests (test/sql/ts_stats.test) are json-gated so never run natively, and all use tiny tables built in date order (single row group, single-thread in-order scan), so arrival order == date order; no metamorphic/permutation test existed for any *_by native function."
sibling_audit: "Only _ts_stats_by_native affected. Already sorted: _ts_forecast_native, _ts_features_native, _ts_mstl_decomposition_native, _ts_forecast_panel_native, _ts_forecast_var_native, _ts_changepoints, _ts_backtest_native, _ts_cv_forecast_native, _ts_cv_folds/_ts_cv_split, _ts_metrics_native (5 finalizers), and all 7 ts_*_agg aggregates (ts_stats_agg, ts_features_agg, ts_data_quality_agg, ts_classify_seasonality_agg, ts_detect_periods_agg, ts_changepoints_agg, ts_forecast_agg). fill_gaps sorts in Rust; fill_forward is order-independent; split_keys/validate_separator have no time semantics. Follow-up (not fixed, low severity): the other sorters use date-only std::sort (or (date,value) pairs dropping validity), so exact-duplicate timestamps can still be tie-broken by arrival order there."

### Human verification (2026-10-08)

- User confirmed resolved.
- Orchestrator re-verified independently: worktree build (main @ 9d01d9c + fix), freshly shuffled full M4
  Daily vs file order -> 0/4227 series differ; D1278 trend_strength 0.932593, stability 2.076544
  (= ts_stats reference); `ts_stats_by_order_independence.test` passes (29 assertions).
- Shipped: PR #271 (https://github.com/DataZooDE/anofox-forecast/pull/271).
- Cherry-picked to `test/duckdb-2.0` (86a97db): clean apply, 2.0 build EXIT=0, new test passes, suite
  43/67 pass with the identical 24 pre-existing failures; ts_stats_by output now bit-identical between
  the fixed v1.5.6 and 2.0 builds (4227/4227 series).
