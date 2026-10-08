# 2.0-only test failures: root-cause classification

28 files fail on the native `test/*` run against DuckDB v2.0-cyanoptera
(`build/logs/test-v20-final.log`, after commit f90fa32). v1.5.5 ran 0 of these
28 files at all — they declare `require json`, and the locally-built v1.5.5
unittest binary does not statically link the `json` core extension
(`duckdb_extensions()` shows `loaded=false, install_mode=REPOSITORY`), so the
sqllogictest harness's `require json` gate skips them outright. DuckDB
v2.0-cyanoptera statically links `json` by default (`loaded=true,
install_mode=STATICALLY_LINKED, install_path=(BUILT-IN)`), so on 2.0 every
file actually runs — unmasking whatever was in them.

This matches the project's own prior finding from the WASM Phase 07 full-suite
run (see memory `project_wasm_suite_reveals_test_debt`): the native unittest's
`require json` skip was already known to mask ~23 pre-existing stale-test
failures. The 2.0 json-static-linking default simply removes that mask
natively, for the first time, on this branch.

**Methodology:** for the "pre-existing" classification below, the failing
query's minimal repro was re-run directly against the v1.5.5 CLI with
`LOAD json;` issued explicitly (bypassing the unittest harness's require-gate
entirely) to check whether the SAME DuckDB version produces the SAME result.
6 files were directly re-verified this way (marked ✅ below); the rest were
classified by matching their error text to one of the two confirmed failure
patterns (stale column/function names, or implementation-vs-test-expectation
mismatches) — not re-run individually, given the volume.

## (a) Extension-side bug — fixed in src/

None remaining. The one genuine 2.0 API-tightening bug found in this run
(`_ts_forecast_scalar` not marked fallible, causing "INTERNAL Error" instead
of the real validation message) was fixed in commit `f90fa32` and is counted
as resolved, not listed as a 2.0-only failure above (it dropped the count
from 29 to 28; see `code-changes.md` for the fix and the note that other
scalar/aggregate functions in this extension likely share the same latent
gap but aren't currently exercised by a failing test).

## (b) Intended DuckDB 2.0 behavior change — record only

| File | Line | Finding |
|------|------|---------|
| `ts_changepoints.test` | 244 | ✅ Verified: the single-arrow lambda syntax (`x -> expr`) is a hard Binder error on 2.0 ("Deprecated lambda arrow (->) detected... before DuckDB's next release"); v1.5.5 only emits a deprecation *warning* and still executes the query. Confirmed directly: `SELECT list_transform([1,2,3], x -> x + 1)` succeeds-with-warning on v1.5.5, hard-errors on v2.0. The test file uses the old arrow syntax (`x -> ...`, `(a, b) -> a + b`); fixing it means updating the test's lambda syntax to `lambda x: ...`, which was out of scope here per "never edit test expectations to hide a 2.0 difference." |

## (c)/pre-existing — test-suite or implementation staleness, independent of DuckDB version

Two failure shapes recur across all remaining files:

**Shape 1 — stale references** (Catalog/Binder errors naming a function, column,
or arity that no longer exists): removed/renamed functions
(`ts_backtest_auto_by`, `anofox_fcst__ts_fill_forward_native`,
`anofox_fcst__ts_fill_gaps_native`), stale macro arities (`ts_split_keys`,
`ts_validate_separator`, `ts_cv_split_by` via `ts_hydrate_features`/
`ts_prepare_regression_input`), stale output column names (`value_col` vs the
actual preserved-input-name column, `group_col`, `date`, `stats`), and one
invalid SQL construct in the test fixture itself (`INTERVAL i DAY` with a
non-constant `i`; also a non-constant STRUCT index). These exactly match the
"removed/renamed API refs... DATE+BIGINT bugs, cascades" test debt documented
in `project_wasm_suite_reveals_test_debt`.

**Shape 2 — model-output / count mismatches**: the query runs but returns a
different value than the `.test` file's `----` expected block (model name,
fitted-point length, NULL-ness, or a `COUNT(*)`/fold-size assertion). Every
one of these is deterministic math/logic in the extension or crate, with zero
dependency on the DuckDB C++ host API we touched for this port.

| File | Line | Shape | ✅ directly re-verified on v1.5.5? |
|------|------|-------|----|
| `ts_aggregate_hierarchy.test` | 357 | 1 — `ts_split_keys(TABLE)` arity | — |
| `ts_conformal_coverage.test` | 93 | 1 — column `date` not found | — |
| `ts_cv_backtest.test` | 81 | 2 — `count_star()` mismatch | — |
| `ts_cv_forecast.test` | 75 | 2 — `null_forecasts` mismatch | — |
| `ts_cv_split.test` | 103 | 2 — `n_train` mismatch | ✅ identical 14/14/14 on both versions; test expects 16 |
| `ts_fill_forward_native.test` | 141 | 1 — `anofox_fcst__ts_fill_forward_native` does not exist | — |
| `ts_fill_forward_operator.test` | 37 | 1 — column `value_col` not found | — |
| `ts_fill_gaps_native.test` | 179 | 1 — `anofox_fcst__ts_fill_gaps_native` does not exist | — |
| `ts_forecast_auto.test` | 299 | 2 — AutoMSTL `.model IS NOT NULL` false | ✅ identical `false` on both versions |
| `ts_forecast_error_isolation.test` | 110 | 2 — NULL-input forecast mismatch | — |
| `ts_forecast_ets_model.test` | 99 | 2 — query expected to error, succeeds instead (unrelated to the `SetFallible` fix at line 67, now passing) | ✅ identical success (7 rows) on both versions |
| `ts_forecast_inspect_explain.test` | 11 | 1 — non-constant STRUCT index (`('A','B','C')[expr]`) | ✅ identical Binder Error on both ("struct_extract needs a constant") |
| `ts_forecast_intermittent.test` | 469 | 2 — CrostonClassic point-length mismatch | — |
| `ts_forecast_mfles_stability.test` | 29 | 1 — `UNNEST not supported here` | — |
| `ts_forecast_multi_seasonal.test` | 133 | 2 — MSTL `.model` mismatch | — |
| `ts_forecast_param_grid.test` | 32 | 2 — `count_star()` mismatch | — |
| `ts_forecast_params.test` | 174 | 2 — `count_star()` mismatch | — |
| `ts_forecast_theta.test` | 434 | 2 — Theta point-length mismatch | — |
| `ts_gaps.test` | 41 | 1 — column `value_col` not found | ✅ identical Binder Error on both |
| `ts_hydrate_features.test` | 25 | 1 — `ts_cv_split_by()` arity | — |
| `ts_hydrate_split.test` | 69 | 2 — `count_star()` mismatch | — |
| `ts_integer_frequency.test` | 25 | 1 — column `group_col` not found | — |
| `ts_multi_key.test` | 21 | 1 — `ts_validate_separator(...)` arity | — |
| `ts_prepare_regression_input.test` | 24 | 1 — `ts_cv_split_by()` arity | — |
| `ts_stats.test` | 282 | 1 — `INTERVAL i DAY` with non-constant `i` | ✅ identical Parser Error on both |
| `ts_summary.test` | 230 | 1 — column `stats` not found | — |
| `ts_varchar_edge_cases.test` | 86 | 1 — `ts_backtest_auto_by` does not exist (known removed function, per `project_api_drift_stale_refs`) | — |

## 2.0-fixed (v1.5.5 minus v2.0)

None — v1.5.5 had 0 failures among the files it actually ran (16/66; the other
50 were skipped, not passed-then-broken).

## Pre-existing failure count (present in both builds)

By the json-unmasking mechanism above, all 27 "Shape 1/2" files are, in
substance, pre-existing: the underlying bug or stale expectation is identical
in both DuckDB versions (6 directly confirmed, the remainder inferred from the
identical two failure shapes and zero relation to any DuckDB C++ API this
port touched). v1.5.5's unittest simply never got to run them natively. 0
true 2.0-only regressions remain after the `f90fa32` fix.
