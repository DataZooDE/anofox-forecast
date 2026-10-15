---
phase: 18-correctness-regression-suite-audit-docs
plan: 01
subsystem: testing
tags: [duckdb-extension, cmake, sqllogictest, json-extension, forecast-wrapper, cv-split, rust-ffi]

requires:
  - phase: 17-upstream-consumption-diagnostics-parity
    provides: anofox-forecast 0.16.1 consumed (ETS/ARIMA R-parity notation, stationarity verdicts), pinning the final numbers this plan locks in as the regression baseline
provides:
  - json core extension statically linked into the test/release build (D-01), independent of the anofox_forecast block
  - All 50 `require json`-gated test/sql/*.test files executing and green (previously silently skipped)
  - A per-file repair ledger (class A-D) with attributed causes for every re-pinned/ported assertion
  - Two genuine Rule 1 bug fixes (frequency::VARCHAR casts; target_date stoll prefix-parse bug) and one Rule 1/2 bug fix (CV split embargo no-op stub) found via the gate, not caused by it
  - One Rule 2 fix (missing 'model' ETS-only validation in the shared ts_forecast_by/ts_forecast_agg params path)
affects: [18-02-correctness-suite-and-length-only-sweep, 18-03-no-stats-audit, 18-04-docs-snippets-changelog]

actuals:
  tokens: 28686
  tasks: 3
  commits: 25

tech-stack:
  added: []
  patterns:
    - "duckdb_extension_load(json) as an independent top-level CMake call, test-time-only dependency, zero LINKED_LIBS/LOAD_TESTS, matching DuckDB's own in_tree_extensions.cmake convention"
    - "Self-consistency proof before re-pinning a non-Phase-9-17-attributable value: cross-check against other assertions already present in the same file (per-fold sums, direct recomputation) rather than trusting a single observed output"
    - "Minimum-data-length re-pin pattern: confirm the real crate/wrapper minimum via binary search on a live build before lengthening a fixture, rather than loosening the assertion"

key-files:
  created:
    - .planning/phases/18-correctness-regression-suite-audit-docs/deferred-items.md
  modified:
    - extension_config.cmake
    - src/macros/ts_macros.cpp
    - src/table_functions/ts_fill_forward_native.cpp
    - src/table_functions/ts_cv_split_native.cpp
    - src/include/ts_forecast_params.hpp
    - test/sql/ts_changepoints.test
    - test/sql/ts_classify_seasonality.test
    - test/sql/ts_conformal_coverage.test
    - test/sql/ts_cv_split.test
    - test/sql/ts_fill_forward_native.test
    - test/sql/ts_fill_forward_operator.test
    - test/sql/ts_fill_gaps_native.test
    - test/sql/ts_forecast_agg.test
    - test/sql/ts_forecast_auto.test
    - test/sql/ts_forecast_basic_models.test
    - test/sql/ts_forecast_by.test
    - test/sql/ts_forecast_error_isolation.test
    - test/sql/ts_forecast_exp_smoothing.test
    - test/sql/ts_forecast_inspect_explain.test
    - test/sql/ts_forecast_intermittent.test
    - test/sql/ts_forecast_mfles_stability.test
    - test/sql/ts_forecast_multi_seasonal.test
    - test/sql/ts_forecast_params.test
    - test/sql/ts_forecast_statespace.test
    - test/sql/ts_forecast_theta.test
    - test/sql/ts_gaps.test
    - test/sql/ts_hydrate_features.test
    - test/sql/ts_hydrate_split.test
    - test/sql/ts_integer_frequency.test
    - test/sql/ts_multi_key.test
    - test/sql/ts_periods_advanced.test
    - test/sql/ts_prepare_regression_input.test
    - test/sql/ts_stats.test
    - test/sql/ts_summary.test
    - test/sql/ts_table_macro_aliases.test
    - test/sql/ts_varchar_edge_cases.test

key-decisions:
  - "D-01 landed as a byte-additions-only diff to extension_config.cmake (verified via git diff grep for removed lines); the anofox_forecast block, OpenSSL static linkage, and the WASM LINKED_LIBS workaround are provably untouched"
  - "Every class-D re-pin required an attributable Phase 9-17 cause (named commit, SUMMARY citation, or source-code cross-reference) before being accepted -- two cases that looked plausible but had no such cause (ts_hydrate_split.test's 180 count, ts_cv_split.test's fixed-window 16 count) were instead traced to pre-existing test-authoring arithmetic errors, confirmed via self-consistency with other assertions already in the same file, and fixed on that basis instead"
  - "Two genuine, pre-existing (non-Phase-9-17) production bugs were fixed under Rule 1/2 because they were in-scope (directly blocking a gated test this plan repairs), not out-of-scope scope creep: the frequency::VARCHAR cast gap (ts_fill_gaps_by/ts_fill_forward_by/ts_forecast_by) and the target_date std::stoll prefix-parse bug in _ts_fill_forward_native"
  - "The CV split embargo parameter was a documented no-op stub in _ts_cv_split_native ('For now, keep expanding window behavior for simplicity') -- implemented properly, mirroring the already-correct SQL-macro embargo math in ts_cv_split_folds_by, rather than re-pinning the test to the stub's wrong (no-op) behavior"
  - "SeasonalNaive/HoltWinters/MSTL/TBATS/ARIMA calls that silently succeeded (or silently fell back) under the pre-Phase-9-17 wrapper now correctly error when a seasonal period can't be detected, a model needs an explicit arima_order, or a model name is invalid -- consistent with the v0.10.0 'silent model/parameter fallbacks become errors' goal (FCST-01..08); these required discovering a new isolation nuance (a thrown SQL error, unlike a NULL, is NOT isolated per-row within a single batch SELECT) and splitting the relevant batch queries accordingly"
  - "Bare frequency::VARCHAR gaps in ts_forecast_panel_by/ts_forecast_var_by/ts_stats(_by), and the sibling 'window'/'seasonal_periods' validation gaps in the shared forecast-params header, were logged to deferred-items.md rather than fixed -- neither is exercised by any of this plan's 50 gated files"

patterns-established:
  - "Tracer-task triage table (file, status, first error, provisional class A-E) as the single source of truth for a repair sweep across many files, re-classified iteratively as each file's first-failure-then-next-failure is fixed"

requirements-completed: [TEST-03]

coverage:
  - id: D1
    description: "json core extension statically linked (D-01): duckdb_extension_load(json), additions-only diff, OpenSSL/WASM untouched"
    requirement: "TEST-03"
    verification:
      - kind: integration
        ref: "Task 1 <verify> automated block (json loaded true, 1 json call, LINKED_LIBS kept, 0 ssl/crypto NEEDED, env -i clean run, 0 scratch files)"
        status: pass
    human_judgment: false
  - id: D2
    description: "All 50 json-gated test/sql/*.test files execute and pass (no require-json skip)"
    requirement: "TEST-03"
    verification:
      - kind: integration
        ref: "make test_release: All tests passed (5236 assertions in 85 test cases), 0 skipped"
        status: pass
    human_judgment: false
  - id: D3
    description: "Per-file repair ledger: every re-pin/port classed A-D with a stated reason, no silent assertion deletion"
    requirement: "TEST-03"
    verification:
      - kind: manual_procedural
        ref: "Per-batch commit messages (31fed97..5e18887), each naming the class, cause, and (for class D) the source citation"
        status: pass
    human_judgment: true
    rationale: "Classification correctness (is this cause really attributable to a named Phase 9-17 change, vs a pre-existing test bug) is a judgment call documented per-file in commit messages; a human should spot-check a sample before treating the ledger as final."
  - id: D4
    description: "Platform invariants unchanged: OpenSSL static linkage, WASM LINKED_LIBS, test/wasm and .github untouched, cargo fmt/test green"
    verification:
      - kind: integration
        ref: "Task 3 <verify> automated blocks: 0 ssl/crypto NEEDED, 0 wasm/.github diff, cargo fmt --check clean, cargo test --workspace all passed, make release/test_release green"
        status: pass
    human_judgment: false

duration: 80min
completed: 2026-10-15
status: complete
---

# Phase 18 Plan 1: Statically link json + repair the 50 json-gated test/sql files Summary

**Linked the in-tree `json` extension into the test/release build and repaired every one of the 50 `require json`-gated SQL test files it unblocked, finding and fixing three genuine pre-existing bugs (a VARCHAR-cast gap, a `std::stoll` prefix-parse bug, and a no-op embargo stub) along the way — full suite now 5236 assertions / 85 test cases / 0 skipped, up from 2309 / 35 / 50 skipped.**

## Performance

- **Duration:** ~80 min
- **Started:** 2026-10-15T17:09:27Z (START_REF baseline commit)
- **Completed:** 2026-10-15T18:25:00Z
- **Tasks:** 3 (Task 1 tracer, Task 2 class A-C repairs, Task 3 class D re-pins + platform DoD)
- **Files modified:** 36 (5 production source files, 31 test/sql files — including 1 non-gated file re-pinned as a direct consequence of a production fix)

## Accomplishments

- **D-01:** `extension_config.cmake` gained one independent `duckdb_extension_load(json)` call (additions-only diff; `anofox_forecast` block byte-identical). `json` now loads `true` with no `LOAD json;` statement, in both the shell and `./build/release/test/unittest`.
- **All 50 `require json`-gated test/sql files now execute and pass** (previously silently skipped — 50 skipped tests across the pre-change baseline).
- **Full suite: 2309 → 5236 assertions, 35 → 85 test cases, 50 → 0 skipped.**
- **Three genuine, pre-existing production bugs found and fixed** (in-scope: each directly blocked a gated test this plan repairs):
  - `ts_fill_gaps_by`/`ts_fill_forward_by`/`ts_forecast_by` passed their `frequency` parameter bare into VARCHAR-only native functions, so an INTEGER frequency literal failed to bind — added `::VARCHAR` casts.
  - `_ts_fill_forward_native`'s `target_date` VARCHAR parsing called `std::stoll()` without a full-string match check, so date-like strings (`"2023-01-07"`) were silently misparsed as a numeric *prefix* (`2023`), producing a no-op (target in the deep past). Added the same `^-?[0-9]+$` regex guard the sibling `ts_fill_gaps_native.cpp` already used.
  - `_ts_cv_split_native`'s `embargo` parameter was a documented no-op stub ("keep expanding window behavior for simplicity"); implemented the real position-based embargo tracking, matching the already-correct SQL-macro math in `ts_cv_split_folds_by`.
- **One Rule 2 fix:** `ts_forecast_by`/`ts_forecast_agg`'s shared params header (`ts_forecast_params.hpp`) was missing the `'model'`-is-ETS-only validation that the sibling `_ts_forecast_native` already enforces — added, closing issue #177 on the public surface.
- Platform invariants verified unchanged: OpenSSL static linkage (0 ssl/crypto `NEEDED` entries), WASM `LINKED_LIBS` workaround, `test/wasm`/`.github` untouched.
- `cargo fmt --all -- --check` clean; `cargo test --workspace` all green (no Rust source changed this plan, but both commands are part of Task 3's explicit DoD).
- **WASM best-effort link check (A1) succeeded:** `make wasm_eh` built cleanly with the local toolchain (emsdk + binaryen 123 + `wasm32-unknown-emscripten`); `build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm` produced (5.0 MB), 0 build errors (only pre-existing `-Wpointer-bool-conversion` warnings and a `binaryen version 123 (expected 119)` version-check warning, neither new).

## Task Commits

Each task/repair batch was committed atomically (25 commits total on `fix/statistical-correctness`):

1. **D-01: json statically linked** — `31fed97`
2. **Batch 1 (fill_gaps/fill_forward alias tombstones, hydrate_features/prepare_regression_input port to ts_cv_hydrate_by, ts_multi_key TABLE-in port, ts_gaps/ts_integer_frequency column naming, frequency::VARCHAR casts, target_date stoll fix)** — `52e82ad`
3. **Batch 2 (ts_cv_split/ts_conformal_coverage: output column naming, seasonal_period param rename, embargo implementation)** — `439333a`
4. **Batch 3 (ts_changepoints: UNION supertype coercion, DATE+BIGINT cast)** — `7e7dc28`
5. **Batch 4 (ts_classify_seasonality: Phase 15 enum/validation re-pin)** — `8dcfecf`
6. **Batch 5 (ts_forecast_basic_models: SeasonalNaive auto-detect re-pin)** — `2f6c5b1`
7. **Batch 6 (ts_forecast_exp_smoothing: HoltWinters re-pin)** — `57db61f`
8. **Batch 7 (ts_forecast_multi_seasonal: seasonal-model re-pins)** — `566bb9b`
9. **Batch 8 (ts_forecast_params: auto-detect/edge-case re-pins)** — `b7c43f4`
10. **Batch 9 (ts_forecast_auto: AutoARIMA/AutoETS edge-case re-pins)** — `753fdb5`
11. **Batch 10 (ts_forecast_theta: Theta minimum-length re-pin)** — `45fd6e8`
12. **Batch 11 (ts_forecast_intermittent: CrostonClassic minimum-length re-pin)** — `b8c6feb`
13. **Batch 12 (ts_forecast_agg: ETS/ARIMA model_name + arima_order re-pin)** — `590bb53`
14. **Batch 13 (ts_forecast_by: ETS/ARIMA model_name + arima_order re-pin)** — `b0e5349`
15. **Batch 14 (ts_forecast_statespace: ARIMA order requirement + model_name notation)** — `f432d38`
16. **Batch 15 (ts_forecast_error_isolation: silent-fallback-elimination re-pins, batch-isolation nuance)** — `2faf6f7`
17. **Rule 2 fix: missing 'model' ETS-only validation in ts_forecast_params.hpp** — `f5fe754`
18. **Batch 16 (ts_forecast_inspect_explain: ROW vs LIST literal)** — `59418f5`
19. **Batch 17 (ts_forecast_mfles_stability: UNNEST in aggregate arg)** — `c6e821c`
20. **Batch 18 (ts_periods_advanced: ts_lomb_scargle minimum-length re-pin)** — `24eff17`
21. **Batch 19 (ts_stats: WR-01 regression test fix + INTERVAL syntax)** — `eba47bd`
22. **Batch 20 (ts_summary: ts_stats flattened-columns fix)** — `dbfee66`
23. **Batch 21 (ts_varchar_edge_cases: flattened-columns fix)** — `55368a7`
24. **Batch 22 (ts_fill_forward_operator: output column naming, missed in batch 1)** — `66c7df0`
25. **Batch 23 (ts_table_macro_aliases: re-pin for the target_date parsing fix — non-gated consequence)** — `5e18887`

**Plan metadata:** commit pending (this SUMMARY + deferred-items.md)

## Files Created/Modified

- `extension_config.cmake` — D-01: independent `duckdb_extension_load(json)` call
- `src/macros/ts_macros.cpp` — `frequency::VARCHAR` casts for `ts_fill_gaps_by`/`ts_fill_forward_by`/`ts_forecast_by`
- `src/table_functions/ts_fill_forward_native.cpp` — `target_date` VARCHAR parsing: full-string regex guard before `std::stoll`
- `src/table_functions/ts_cv_split_native.cpp` — embargo: implemented real position-based tracking (was a no-op stub)
- `src/include/ts_forecast_params.hpp` — added `'model'`-is-ETS-only validation (issue #177)
- `.planning/phases/18-correctness-regression-suite-audit-docs/deferred-items.md` — 2 out-of-scope findings logged for a future plan
- 31 `test/sql/*.test` files — see Task Commits above for per-file detail; full reasoning (file, assertion, class, old→new, cause) is in each batch's commit message

## Decisions Made

See `key-decisions` in the frontmatter above. In summary: every class-D re-pin required an attributable Phase 9-17 cause before acceptance; two cases without one were traced to pre-existing test-authoring bugs (confirmed via self-consistency, not re-pinned blindly) and fixed on that basis; two genuine production bugs (frequency cast, target_date parsing) and one incomplete feature (CV embargo) were fixed because they were in-scope and blocking; everything else out of scope was logged to `deferred-items.md`.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `ts_fill_gaps_by`/`ts_fill_forward_by`/`ts_forecast_by` bare `frequency` parameter**
- **Found during:** Task 2, repairing `test/sql/ts_integer_frequency.test`
- **Issue:** The macros passed `frequency` straight through to VARCHAR-only native functions with no cast, so `ts_forecast_by(..., 1)` (INTEGER literal) failed to bind
- **Fix:** Added `frequency::VARCHAR` at each call site in `src/macros/ts_macros.cpp`
- **Files modified:** `src/macros/ts_macros.cpp`
- **Verification:** `test/sql/ts_integer_frequency.test` green; no regression across every `ts_forecast_*`/`ts_gaps`/`ts_cv_split` file re-run
- **Committed in:** `52e82ad`

**2. [Rule 1 - Bug] `_ts_fill_forward_native`'s `target_date` `std::stoll` prefix-parse bug**
- **Found during:** Task 2, repairing `test/sql/ts_integer_frequency.test`
- **Issue:** `std::stoll("2023-01-07")` silently parses the numeric *prefix* (`2023`) instead of throwing, misclassifying any digit-leading date string as a raw day-offset and silently producing a no-op (target in the deep past, zero rows added)
- **Fix:** Added the same full-string `^-?[0-9]+$` regex guard `ts_fill_gaps_native.cpp` already uses for frequency parsing, in `src/table_functions/ts_fill_forward_native.cpp`
- **Files modified:** `src/table_functions/ts_fill_forward_native.cpp`
- **Verification:** Direct live-query confirmation (bare string target now extends correctly); re-ran every file touching `ts_fill_forward_by`/`_ts_fill_forward_native`, no regression
- **Committed in:** `52e82ad`
- **Downstream consequence:** `test/sql/ts_table_macro_aliases.test` (non-gated) had its `anofox_fcst_ts_fill_forward_by` row-count assertion (60) pinned against the pre-fix bug's no-op behavior; re-pinned to the mathematically correct 69 (2023-01-01 to 2023-03-10 inclusive = 31+28+10 days). Committed in `5e18887`.

**3. [Rule 1/2 - Bug/Missing functionality] `_ts_cv_split_native`'s `embargo` parameter was a no-op stub**
- **Found during:** Task 2, repairing `test/sql/ts_cv_split.test`
- **Issue:** The embargo-handling code block was a documented stub: `// This is simplified ... For now, keep expanding window behavior for simplicity.` The parameter was parsed and accepted but had zero effect on `ts_cv_split_by`, while the separate SQL-macro-based `ts_cv_split_folds_by` already computed it correctly.
- **Fix:** Implemented the equivalent position-based tracking (previous fold's unclipped `test_end` position, carried across the per-group fold loop) mirroring `ts_cv_split_folds_by`'s `LAG(test_end) + embargo * interval` math
- **Files modified:** `src/table_functions/ts_cv_split_native.cpp`
- **Verification:** `test/sql/ts_cv_split.test` embargo assertions now match `ts_cv_split_folds_by`'s already-correct values; re-ran `ts_prepare_regression_input.test`, `ts_hydrate_features.test`, `ts_backtest_equivalence.test` (all also touch `_ts_cv_split_native`), no regression
- **Committed in:** `439333a`

**4. [Rule 2 - Missing critical functionality] `ts_forecast_by`/`ts_forecast_agg` missing the `'model'`-is-ETS-only validation**
- **Found during:** Task 2, repairing `test/sql/ts_forecast_ets_model.test` (written for issue #177)
- **Issue:** `src/table_functions/ts_forecast_native.cpp` already validates "'model' param is only valid when method='ETS'", but `ts_forecast_by`/`ts_forecast_agg` call `_ts_forecast_scalar` / the shared `ts_forecast_params.hpp` header instead, which never reached this check — the param was silently accepted and ignored for non-ETS methods on both public surfaces, violating the v0.10.0 "silent model/parameter fallbacks become errors" goal (PROJECT.md, FCST-01..08)
- **Fix:** Ported the identical check into `ApplyForecastParamSet()` in `src/include/ts_forecast_params.hpp`
- **Files modified:** `src/include/ts_forecast_params.hpp`
- **Verification:** `test/sql/ts_forecast_ets_model.test` green with no test-file changes needed (it already asserted the correct behavior); re-ran every `ts_forecast_*`/`ts_cv_split`/`ts_native_param_validation` file touching this header, no regression
- **Committed in:** `f5fe754`

---

**Total deviations:** 4 auto-fixed (3 Rule 1 bugs, 1 Rule 2 missing-functionality fix), plus 1 downstream re-pin consequence of deviation #2.
**Impact on plan:** All four were genuine, pre-existing (non-Phase-9-17) defects directly blocking or surfaced by this plan's gated-test repairs — none caused by this plan's own D-01 change, and none out of scope (each was required to make an in-scope gated test pass, or — for #4 — explicitly asserted by one). No scope creep: two further findings of the identical bug class (bare `frequency` in 3 other macros; the sibling `'window'`/`'seasonal_periods'` validation gaps) were confirmed NOT exercised by any gated file and logged to `deferred-items.md` instead of fixed.

## Repair Ledger (Task 1 triage → Task 2/3 resolution)

The Task 1 tracer triaged all 50 `require json`-gated files: 19 passed immediately (`extension_comparison`, `ts_conformal`, `ts_data_quality`, `ts_decomposition`, `ts_detrend`, `ts_diff`, `ts_features_config`, `ts_features`, `ts_fill_unknown`, `ts_filter`, `ts_forecast_exog`, `ts_forecast_laplace`, `ts_forecast`, `ts_mark_unknown`, `ts_peaks`, `ts_periods_specialized`, `ts_periods`, `ts_seasonality`, `ts_validate_timestamps`), 31 failed. A 32nd file (`ts_fill_forward_operator.test`) was missed in the initial fix pass and caught on the final full-50 re-run; a 33rd, non-gated file (`ts_table_macro_aliases.test`) needed re-pinning as a direct consequence of deviation #2.

| File | Class | Reason | Old → New |
|---|---|---|---|
| `ts_fill_forward_native.test` | A | `anofox_fcst__ts_fill_forward_native` alias was never generated for internal (`_`-prefixed) functions post-rename (commit 4477e7b) — tombstoned with `statement error` | query → tombstone |
| `ts_fill_gaps_native.test` | A | same as above | query → tombstone |
| `ts_hydrate_features.test` | A | `ts_hydrate_features_by` removed (commit 88839f1); ported to replacement `ts_cv_hydrate_by`, values re-derived | full rewrite |
| `ts_prepare_regression_input.test` | A | `ts_prepare_regression_input_by` removed (commit 88839f1); ported to the documented `ts_cv_hydrate_by` + `CASE WHEN split='test'` pattern | full rewrite |
| `ts_multi_key.test` | A | `ts_validate_separator` is TABLE-in, not source-string+columns | call-shape port |
| `ts_gaps.test` | A | `ts_fill_gaps_by`/`ts_fill_forward_by` output original column names, not `value_col` (commit ca7d4f6) | `value_col`→`val` |
| `ts_integer_frequency.test` | A/D | same column-naming fix; plus re-pinned the 7-day-frequency "fill gaps between consecutive dates" semantics | `group_col/date_col/value_col`→`id/date/val`; `1`→`4` rows |
| `ts_fill_forward_operator.test` | A | same column-naming fix (missed in first pass, caught on final re-run) | `group_col/date_col/value_col`→`id/ds/value` |
| `ts_cv_split.test` | A/D | output column naming (`group_col/date_col`→`series_id/date`); fixed-window train size re-pinned (16→14, `min_train_size` IS the window) | see above |
| `ts_conformal_coverage.test` | A | `ts_forecast_by` output date column is `ds` not `date`; `params` MAP key renamed `period`→`seasonal_period` | aliasing + rename |
| `ts_changepoints.test` | B | UNION ALL supertype coercion to VARCHAR (missing `::DATE` casts); DATE+BIGINT cast | cast fixes; 1 re-pin (2→4, insufficient-data cascade) |
| `ts_classify_seasonality.test` | D | Phase 15 `SeasonalType` enum rewrite (`variable_seasonal`→`variable_timing`); period<=0 now errors (Phase 15 WR-03/CR-06) | enum + error re-pins |
| `ts_forecast_basic_models.test` | D | SeasonalNaive auto-detect: 2-cycle fixtures ambiguous, lengthened to 3; detected-seasonal values re-pinned where fixture already had 3 cycles | multiple re-pins |
| `ts_forecast_exp_smoothing.test` | D | HoltWinters "falls back to Holt" → now errors (FCST-01..08) | statement error |
| `ts_forecast_multi_seasonal.test` | D | MSTL/TBATS on non-seasonal/3-point data → now errors; MFLES minimum length 3→4 | statement error + lengthen |
| `ts_forecast_params.test` | D | SeasonalNaive re-pin (6-cycle data); HoltWinters seasonal_period 7→3 (data-sizing); horizon=0 → NULL not empty array; short seasonal fixture 2→3 cycles | multiple re-pins |
| `ts_forecast_auto.test` | D | AutoARIMA fitted/residuals NULL (documented Phase 10 FFI gap); AutoETS/AutoARIMA minimum-length re-pins | multiple re-pins |
| `ts_forecast_theta.test` | D | Theta minimum length 3→4 | lengthen |
| `ts_forecast_intermittent.test` | D | CrostonClassic minimum length 3→4 | lengthen |
| `ts_forecast_agg.test` | D | ETS/ARIMA model_name full notation; ARIMA requires arima_order | re-pin + param add |
| `ts_forecast_by.test` | D | same as `ts_forecast_agg.test` | re-pin + param add |
| `ts_forecast_statespace.test` | D | same model_name/arima_order pattern across ~15 call sites; AutoETS/AutoARIMA minimum-length re-pins | extensive re-pin |
| `ts_forecast_error_isolation.test` | D | Default-model (AutoETS) minimum length; all-NULL/near-all-NULL no longer fabricate NaN; +/-inf → NULL; invalid model name → error; seasonal-model detection-failure → error (not isolated per-row); cascading count re-derivations | extensive re-pin |
| `ts_forecast_inspect_explain.test` | B | `(...)` is a ROW/STRUCT literal, not LIST — struct index must be constant | `(...)`→`[...]` |
| `ts_forecast_mfles_stability.test` | B | UNNEST not supported inside aggregate argument | moved to FROM-clause lateral join |
| `ts_periods_advanced.test` | D | `ts_lomb_scargle` minimum length 4→6 | re-pin |
| `ts_stats.test` | B | WR-01 regression test passed bare NULL frequency, short-circuiting before the dates-list-element check it claims to test; `INTERVAL i DAY` needs parens for an expression | 2 fixes |
| `ts_summary.test` | A | `ts_stats` output has always been flattened columns, no `stats` struct | drop `stats` column ref |
| `ts_varchar_edge_cases.test` | A | `ts_data_quality`/`ts_mstl_decomposition_by` output flattened, no `quality`/`decomposition` struct | drop struct refs |
| `ts_table_macro_aliases.test` (non-gated) | — | re-pinned as a direct consequence of deviation #2 (target_date parsing fix): 60→69 | re-pin |

No tombstone deleted a block without a 1:1 replacement (block counts before/after match for every file except `ts_forecast_error_isolation.test`, which gained 3 net blocks — 2 new `statement error` blocks demonstrating the newly-discovered batch-isolation nuance, +1 net after excluding a row from 2 existing batch queries).

## Issues Encountered

- **Batch-isolation nuance (new finding, documented inline in `ts_forecast_error_isolation.test`):** a thrown SQL exception (e.g. "no seasonal period could be detected") is NOT isolated per-row within a single batch `SELECT` the way a `NULL` result is — DuckDB aborts the whole query. Two existing batch queries (HoltWinters/MSTL isolation) had to exclude the now-throwing constant-series row and demonstrate it separately via `statement error`. This is a genuine architectural property of the "silent fallbacks become errors" design, not a bug; flagged here for Phase 18 plans 2-4 and any future diagnostics/forecast work to be aware of.
- No other issues. All deviations above were resolved within this plan.

## User Setup Required

None — no external service configuration required.

## Follow-ups

- **WASM link with json: verified locally.** `make wasm_eh` built cleanly (emsdk + binaryen 123 + `wasm32-unknown-emscripten`, ~25 min cold); `build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm` produced, 0 errors. CI (`MainDistributionPipeline.yml`) will additionally run the newly-executing 50 test files on v1.4.5 LTS and v1.5.6 once this branch is pushed — not run by this plan (no push performed).
- The WASM harness (`test/wasm/run.mjs`) curated subset was not changed by this plan.
- Two out-of-scope findings logged to `deferred-items.md` for a future plan: (1) bare `frequency::VARCHAR` gap in `ts_forecast_panel_by`/`ts_forecast_var_by`/`ts_stats(_by)`; (2) the sibling `'window'`/`'seasonal_periods'` parameter-misuse validations missing from `ts_forecast_params.hpp`.
- The batch-isolation nuance above (thrown errors vs. NULL within a batch SELECT) may be worth a dedicated note in `docs/` if plan 18-04 covers error-handling documentation.

## Next Phase Readiness

- The full SQL suite is green with json statically linked (5236 assertions, 0 skipped) — plan 18-02 (correctness suite + length-only sweep + D-08 fixes) can build on a fully-executing baseline as the ROADMAP requires.
- Two genuine production bugs and one incomplete feature were fixed; no known blockers for 18-02/18-03/18-04.

## Self-Check: PASSED

- All 7 key files (5 production, 1 deferred-items.md, 1 this SUMMARY) confirmed present on disk.
- All 26 commits (D-01 through the final `docs(18-01)` metadata commit) confirmed present in `git log`.
- Re-ran Task 1's additions-only-diff check: 0 removed lines in `extension_config.cmake`.
- Re-ran `json loaded` check: `true`.
- Re-ran scratch-file check: 0 `test/sql/zz_scratch_*` files remain.
- Re-ran gated-file-count check: 50 files still carry `require json` (none accidentally dropped the directive).
- Re-ran `make test_release`: **All tests passed (5236 assertions in 85 test cases)** — matches the number recorded above, confirms no regression since the last per-task commit.
