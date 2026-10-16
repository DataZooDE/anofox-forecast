---
phase: 10-forecast-wrapper-delegates-to-crate-models
plan: 05
subsystem: testing
tags: [duckdb, sqllogictest, anofox-forecast, forecasting, arima, ets, seasonal, examples]

requires:
  - phase: 10-01
    provides: forecast()/forecast_with_exog() crate-passthrough foundation (D-01/D-02/D-03/D-11/D-12)
  - phase: 10-02
    provides: real crate ARIMA/SARIMA fit, strict FFI option-string parsing pattern
  - phase: 10-03
    provides: resolve_period/requires_seasonal_period (D-07 — the root cause re-blessed here)
  - phase: 10-04
    provides: exog surface routing through the crate's own exog support (D-10); deferred-items.md item 1
provides:
  - "examples/forecasting/crate_delegation_examples.sql: a self-checking, PASS/error()-gated runnable example proving every Phase 10 behaviour (crate-native intervals, real ARIMA via arima_order/seasonal_order, 'ETS'=AutoETS standalone==ensemble-member, AutoARIMA-with-regressors, D-11/D-12 trailing-NULL realignment) against the built extension under duckdb -unsigned -bail"
  - "test/sql/ts_native_model_names.test, test/sql/ts_backtest_equivalence.test, test/sql/ts_model_distinctness.test re-blessed to the D-05/D-06/D-07/D-09 behaviour the corrected extension actually produces — make test_release is green"
  - "Enumerated, categorized list of json-gated (require json, skipped by the native runner) test files whose assertions reference behaviour Phase 10 changed, handed to Phase 18 (TEST-03) by file:line"
affects: ["18"]

actuals:
  tokens: 6874
  tasks: 3
  commits: 3
  plan_head_before: ba0c9ac5a4fc7913a4e6436ebf563425e306ed44

tech-stack:
  added: []
  patterns:
    - "Self-checking SQL examples: every section ends with SELECT CASE WHEN <condition> THEN 'PASS <label>' ELSE error('FAIL ...') END AS result; — the AS result alias is required, otherwise the duckdb CLI echoes the full CASE expression (which contains the literal string 'FAIL ...' inside the error() call) as the column header, poisoning any downstream grep -c 'FAIL' check even when every assertion passes."
    - "Non-seasonal AutoARIMA/ARIMA model_name assertions on multi-group fixtures must tolerate per-group drift divergence (' with drift' suffix) — use a regex-match COUNT(*) WHERE NOT regexp_matches(...) = 0 pattern instead of SELECT DISTINCT ... ---- <single value>, since two groups fit at the same order can select drift differently."

key-files:
  created:
    - examples/forecasting/crate_delegation_examples.sql
  modified:
    - examples/forecasting/README.md
    - test/sql/ts_native_model_names.test
    - test/sql/ts_backtest_equivalence.test
    - test/sql/ts_model_distinctness.test

key-decisions:
  - "Naive interval tolerances in the new example are 0.05 absolute (half-width) and 1e-3 relative (80/95 ratio) rather than the plan's aspirational 1e-6/1e-9 — the crate computes z from a private Abramowitz & Stegun 26.2.23 approximation (not an exact Φ⁻¹ table lookup, per 10-01), so exact-ratio precision is not achievable; this matches the precedent already documented in test/sql/ts_forecast_crate_delegation.test's own 0.1-absolute tolerance comment."
  - "Chose seasonal_period=14 (not 7) for test/sql/ts_native_model_names.test's model_name_data fixture — the fixture's sin(i*pi/7)/cos(i*pi/7) terms have a full-cycle period of 14 (argument increases by 2*pi every 14 steps, not 7), verified against the built extension before editing."
  - "ts_model_distinctness.test section 4 (HoltWinters/MSTL/TBATS via the 3-arg _ts_forecast path) needed the same D-07 re-bless as section 3 (SeasonalES/SeasonalESOptimized, the deferred item from 10-04) — this was NOT in the original failure list because the sqllogictest harness aborts the rest of a .test FILE on the first unexpected failure, masking section 4's staleness until section 3 was fixed and the file re-run surfaced it. Same root cause (10-03's resolve_period change, commit e01e87b), same file, same justification — fixed together rather than deferred again."

patterns-established:
  - "The duckdb native sqllogictest runner aborts the REST of a .test file after the first unexpected statement/query failure (not just that assertion) — fixing an early failure in a file can surface previously-masked failures later in the SAME file; iterate re-run-fix-re-run per file until a full pass, don't assume one fix per file is sufficient."

requirements-completed: [FCST-01, FCST-04, FCST-05, FCST-06, FCST-07, FCST-08]

coverage:
  - id: D1
    description: "A single runnable, self-checking example (examples/forecasting/crate_delegation_examples.sql) proves every Phase 10 behaviour through the public SQL surface: crate-native Naive intervals, real ARIMA via arima_order/seasonal_order within 1% of R, 'ETS'=AutoETS with standalone==ensemble-member equality and non-NULL ensemble bounds, AutoARIMA running its own order search with regressors, and RandomWalkDrift's trailing-NULL realignment"
    requirement: FCST-01
    verification:
      - kind: e2e
        ref: "./build/release/duckdb -unsigned -bail < examples/forecasting/crate_delegation_examples.sql (10/10 PASS, 0 FAIL, exit 0)"
        status: pass
    human_judgment: false
  - id: D2
    description: "test/sql/ts_native_model_names.test, test/sql/ts_backtest_equivalence.test, test/sql/ts_model_distinctness.test re-blessed to the corrected D-05/D-06/D-07/D-09 behaviour; test/sql/ts_native_param_validation.test and test/sql/ts_cv_backtest.test confirmed already passing and left untouched; no assertion deleted without a replacement asserting the new behaviour"
    requirement: FCST-04
    verification:
      - kind: e2e
        ref: "./build/release/test/unittest \"test/sql/ts_native_model_names.test\" (63/63), \"test/sql/ts_backtest_equivalence.test\" (44/44), \"test/sql/ts_model_distinctness.test\" (34/34)"
        status: pass
    human_judgment: false
  - id: D3
    description: "The full natively-run test/sql/*.test suite passes under make test_release (50 require-json files still skipped by the native runner, unchanged from Phase 9's baseline); json-gated files referencing Phase-10-changed behaviour are enumerated by file:line for Phase 18 (TEST-03), not edited here"
    requirement: FCST-06
    verification:
      - kind: e2e
        ref: "make test_release -> \"All tests passed (50 skipped tests, 1395 assertions in 18 test cases)\""
        status: pass
    human_judgment: false

duration: ~2h (single session)
completed: 2026-10-09
status: complete
---

# Phase 10 Plan 05: Prove Phase 10 End-to-End and Close the Native Test Suite Summary

**A new self-checking runnable example (`crate_delegation_examples.sql`) proves every Phase 10 behaviour against the built extension, and three natively-run `test/sql/*.test` files are re-blessed to the crate's real behaviour (D-05/D-06/D-07/D-09), bringing `make test_release` back to "All tests passed" with the 50 pre-existing `require json` skips unchanged.**

## Performance

- **Duration:** ~2h (single session; 2 code-change task commits + this SUMMARY commit)
- **Tasks:** 3 (all completed; Task 3 required no file edits — its four candidate files already passed)
- **Files modified:** 5 (1 created, 4 modified)

## Accomplishments

- `examples/forecasting/crate_delegation_examples.sql` (new): five sections, each ending in a `SELECT CASE WHEN <condition> THEN 'PASS <label>' ELSE error('FAIL ...') END AS result;` assertion that aborts the whole script under `-bail` on a wrong value — proving (1) crate-native Naive 95%/80% interval half-widths (not the old ~234 fabrication), (2) real ARIMA via `arima_order`/`seasonal_order` matching R's airline model within 1%, (3) `'ETS'` = AutoETS selection with standalone == `['ETS','ETS']` ensemble member (max diff < 1e-9) and non-NULL ensemble bounds, (4) AutoARIMA running its own order search WITH regressors including a holiday flag never observed as 1 during fitting, and (5) `RandomWalkDrift`'s D-11/D-12 trailing-NULL realignment (`forecast_step=1` lands on the day after the ORIGINAL last row, not the last valid one). `examples/forecasting/README.md` gained a table row and Quick Start line for the new file.
- `test/sql/ts_native_model_names.test`: re-blessed every `_ts_forecast_native`/`_ts_cv_forecast_native` smoke-test call for `SeasonalNaive`/`HoltWinters`/`SeasonalES`/`SeasonalESOptimized`/`SeasonalWindowAverage`/`MSTL`/`TBATS`/`AutoMSTL`/`AutoTBATS` to pass an explicit `seasonal_period` (D-07 — the fixture's own period is 14, not 7, since `sin(i*pi/7)` completes a full cycle every 14 steps), `AutoMFLES`/`AutoMSTL` to expect the reported family name `MFLES`/`MSTL` rather than the Auto-prefixed alias (D-06), `'ETS'` without `ets_model` to assert the AutoETS spec pattern rather than the literal string `'ETS'` (D-05), and `'ARIMA'` to require an explicit `arima_order` with a regex-tolerant drift-suffix assertion (D-09).
- `test/sql/ts_backtest_equivalence.test`: `SeasonalNaive` CV-equivalence calls now pass `seasonal_period := 7` (the fixture's own `(i % 7) * 3.0` weekly term) instead of relying on the removed silent default (D-07).
- `test/sql/ts_model_distinctness.test`: closed the deferred item from Plan 10-04's `deferred-items.md` — re-blessed `SeasonalES`/`SeasonalESOptimized` section-3 values from `14.451866`/`18.803254` to `12.304672`/`16.998999`, AND discovered (by re-running the file after the section-3 fix) that section 4 (`HoltWinters`/`MSTL`/`TBATS`) had the identical, previously-masked staleness from the same Plan 10-03 `resolve_period` change — re-blessed those too (`19.953912`/`17.364756`/`17.46623` -> `18.00003`/`18.18416`/`17.832329`). Both sections' `all_distinct=1` regression-guard invariant still holds.
- Confirmed `test/sql/ts_native_param_validation.test` and `test/sql/ts_cv_backtest.test` already pass against the corrected extension — left untouched, per the plan's "do not edit files whose tests pass" instruction.
- `test/sql/ts_forecast_param_grid.test`, `test/sql/ts_cv_forecast.test`, `test/sql/ts_parallel_correctness.test`, `test/sql/ts_table_macro_aliases.test` (Task 3's candidate files) all already pass — no edits needed.
- `make test_release` -> `All tests passed (50 skipped tests, 1395 assertions in 18 test cases)`.

## Task Commits

Each task was committed atomically:

1. **Task 1: Tracer — self-checking runnable example of every Phase 10 behaviour** — `f654197` (feat)
2. **Task 2: Migrate natively-run tests whose expectations encoded removed fallbacks or old names** — `75b6d49` (fix)
3. **Task 3: Remaining natively-run forecast tests and a green full make test_release** — no code changes (all four candidate files already passed); findings below, captured in this SUMMARY commit.

**Plan metadata:** (this commit, immediately following)

## Files Created/Modified

- `examples/forecasting/crate_delegation_examples.sql` (new) — self-checking Phase 10 proof example.
- `examples/forecasting/README.md` — Example Files table row + Quick Start run line.
- `test/sql/ts_native_model_names.test` — D-05/D-06/D-07/D-09 re-bless (see justification table in Task 2's commit message).
- `test/sql/ts_backtest_equivalence.test` — D-07 re-bless (`SeasonalNaive` CV equivalence).
- `test/sql/ts_model_distinctness.test` — D-07 re-bless (sections 3 and 4, both the same Plan 10-03 root cause).

## Decisions Made

See frontmatter `key-decisions`. Most significant: discovering and fixing `ts_model_distinctness.test` section 4's identical-root-cause staleness in the same pass as section 3's already-deferred item, rather than deferring it again — the sqllogictest harness's abort-on-first-failure behavior had hidden it until section 3 was fixed and the file re-run.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Test-design] `crate_delegation_examples.sql`'s CASE-expression column headers leaked the literal string "FAIL" into every PASS row's printed output**
- **Found during:** Task 1, first run of the example under `-bail`
- **Issue:** `SELECT CASE WHEN ... THEN 'PASS ...' ELSE error('FAIL ...') END;` with no alias causes the duckdb CLI to print the full CASE expression text (which contains the literal substring `'FAIL ...'` inside the unexecuted `error()` branch) as the column header — so `grep -c 'FAIL'` over the output counted false positives even when every check passed (and the plan's own `<verify>` block uses exactly this grep).
- **Fix:** Added `AS result` to every check `SELECT`, suppressing the full-expression header.
- **Files modified:** `examples/forecasting/crate_delegation_examples.sql`
- **Verification:** Re-ran under `-bail`; `grep -c 'FAIL'` now reports 0 on a clean pass.
- **Committed in:** `f654197` (Task 1 commit)

**2. [Rule 1 - Test-design] Exog model_name regex in the new example didn't match the seasonal AutoARIMA bracket form**
- **Found during:** Task 1, same run
- **Issue:** `^AutoARIMA\(.*\) with regressors$` requires the string to end in `)` immediately before `" with regressors"`, but the actual seasonal format ends in `[15] with regressors"` (bracket, not paren) — the identical issue already documented and fixed in 10-04's `ts_forecast_exog.test` deviation note.
- **Fix:** Used the same triplet-based pattern already proven in 10-04: `^AutoARIMA\(\d+,\d+,\d+\)(\(\d+,\d+,\d+\)\[\d+\])? with regressors$`.
- **Files modified:** `examples/forecasting/crate_delegation_examples.sql`
- **Committed in:** `f654197` (Task 1 commit)

**3. [Rule 1 - Test-design] Naive interval tolerance in the plan's `must_haves.truths` (1e-6/1e-9) does not match the crate's actual precision**
- **Found during:** Task 1, verifying the half-width/ratio assertions against the built extension
- **Issue:** The crate's own z comes from a private Abramowitz & Stegun 26.2.23 approximation, not an exact Φ⁻¹ lookup (documented in 10-01). Measured half-width is `66.0857125158351` (vs R's `66.07118560930098`, diff ~0.0145) and the measured 80/95 ratio is `0.6538115077919895` (vs the exact-Φ⁻¹ ratio `0.6538648544837132`, diff ~5.3e-5) — neither within the plan's stated 1e-6/1e-9.
- **Fix:** Used 0.05 absolute and 1e-3 relative tolerances instead, matching the precedent already set (and explained) in `test/sql/ts_forecast_crate_delegation.test`'s own 0.1-absolute tolerance comment — still two to three orders of magnitude tighter than the ~168 gap the old fabrication would show.
- **Files modified:** `examples/forecasting/crate_delegation_examples.sql`
- **Committed in:** `f654197` (Task 1 commit)

**4. [Rule 1 - Test-design] `ts_native_model_names.test` section 4's HoltWinters/MSTL/TBATS values (not in the original failure list) were ALSO stale from the same root cause as the deferred `ts_model_distinctness.test` item**
- **Found during:** Task 2, re-running `ts_model_distinctness.test` after fixing its section-3 deferred item
- **Issue:** The native sqllogictest harness aborts the rest of a `.test` FILE after the first unexpected failure — so `ts_model_distinctness.test`'s section 4 (`HoltWinters`/`MSTL`/`TBATS` via the 3-arg `_ts_forecast` path, same `requires_seasonal_period`/`resolve_period` dependency as section 3) never ran in the full-suite report and its staleness was invisible until section 3 was fixed and the file re-run standalone.
- **Fix:** Re-blessed section 4's values the same way as section 3, with the same D-07/Plan-10-03 justification, rather than logging a second deferred item for an already-identified root cause in the same file this plan is already touching.
- **Files modified:** `test/sql/ts_model_distinctness.test`
- **Verification:** `./build/release/test/unittest "test/sql/ts_model_distinctness.test"` — 34/34 assertions pass.
- **Committed in:** `75b6d49` (Task 2 commit)

**5. [Rule 1 - Test-design] `ts_native_model_names.test` had five additional stale assertions beyond the one originally reported (`SeasonalWindowAverage`, `'ETS'` 3-arg, `'ARIMA'` 3-arg) surfaced the same way as #4 — iterative re-run-fix-re-run within the file**
- **Found during:** Task 2, iterating `./build/release/test/unittest "test/sql/ts_native_model_names.test"` after each fix
- **Issue:** Same harness-abort masking as #4, within the SAME file: fixing the reported `SeasonalNaive` failure (line 80) surfaced `SeasonalWindowAverage` (line 140, D-07), then `'ETS'` 3-arg (line 179, D-05 — two groups select different AutoETS specs, so `DISTINCT` returned 2 rows against an expected single value), then `'ARIMA'` 3-arg (line 192, D-09 — plus a drift-suffix divergence across the two groups at the same order).
- **Fix:** Added `seasonal_period := 14` for `SeasonalWindowAverage`; converted the `'ETS'`/`'ARIMA'` assertions from `SELECT DISTINCT ... ---- <value>` to `SELECT count(*) ... WHERE NOT regexp_matches(...) ---- 0`, tolerant of per-group spec/drift divergence.
- **Files modified:** `test/sql/ts_native_model_names.test`
- **Verification:** `./build/release/test/unittest "test/sql/ts_native_model_names.test"` — 63/63 assertions pass (final clean run).
- **Committed in:** `75b6d49` (Task 2 commit)

---

**Total deviations:** 5 auto-fixed (all Rule 1 — test-design issues directly caused by this plan's own new example and the behavior changes it is proving/re-blessing; none architectural, none requiring a checkpoint)
**Impact on plan:** All five fixes are in-scope (either the new example's own self-check mechanics, or additional instances of the exact same D-05/D-06/D-07/D-09/Plan-10-03 root causes the plan already targets, surfaced only because the native sqllogictest harness hides later-in-file failures until earlier ones are fixed). No scope creep.

## Issues Encountered

- **DoD scoping note:** this plan's full diff (`git diff ba0c9ac..HEAD --name-only`) touches zero `.rs`/`.cpp`/`.h` files — only `test/sql/*.test`, one new `examples/forecasting/*.sql`, and `README.md`. `cargo clean` had just been run before this plan started (per the orchestrator's note), so `cargo fmt --check`/`cargo clippy -D warnings`/`cargo test --workspace` would each force a full from-scratch Rust workspace rebuild (disk was at 91-92%, 41G free, with an explicit instruction to avoid large extra build trees and avoid stalls). Since there is no Rust/C++ diff for those checks to validate, they were intentionally skipped; `make test_release` (the DoD check that actually exercises this plan's changes) was run twice and is green both times (see Accomplishments). The existing `build/release/duckdb`/`build/release/test/unittest` binaries (built after Plan 10-04's `0b60784`, prior to the `cargo clean`) were used unmodified throughout.
- The native sqllogictest harness (DuckDB's `test_sqllogictest.cpp`) aborts the REST of a `.test` FILE on the first unexpected statement/query failure rather than continuing to collect all failures in that file. This meant the initial `make test_release` run under-reported the true number of stale assertions — each of the three touched files required 2-4 rounds of "fix the reported line, re-run the single file, find the next masked failure" before reaching a clean pass. Documented as a reusable pattern (`patterns-established`) for future re-bless work in this repo.

## Known Stubs

None — the new example wires real data through the real built extension with no mock/placeholder paths; all re-blessed test values were measured against the built extension, not invented.

## Threat Flags

None beyond what the plan's own `<threat_model>` already covers (T-10-16, T-10-17) — every changed expectation above is listed with old/new value and its D-xx justification (also recorded in Task 2's commit message), and the new example's `error()`-gated assertions make a wrong value abort the run under `-bail`, exactly as the threat register specifies.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- Phase 10's remaining plans (10-06..10-09) can rely on: a proven, runnable, self-checking demonstration of every FCST-01/04/05/06/07/08 behaviour exists at `examples/forecasting/crate_delegation_examples.sql`; the native `test/sql/*.test` suite is fully green (`make test_release` -> "All tests passed", 50 `require json` skips unchanged from Phase 9's baseline); the `ts_model_distinctness.test` deferred item from 10-04's `deferred-items.md` is fully closed (both sections re-blessed, not just the originally-reported one).
- `./build/release/duckdb -unsigned -bail < examples/forecasting/crate_delegation_examples.sql` exits 0 with 10/10 PASS, 0 FAIL.
- `make test_release` → `All tests passed (50 skipped tests, 1395 assertions in 18 test cases)`.

### Json-gated tests for TEST-03 (Phase 18)

The native unittest binary has no JSON extension built in (Phase 9: "50 skipped tests [require json]"), so none of the 50 `require json` files below were executed natively this plan — grepped only, per Task 3's explicit "do not edit them here" instruction. Categorized by confidence:

**CONFIRMED BROKEN (D-09 — `'ARIMA'` with no `arima_order` anywhere in the file; unconditional, no data-dependence):**
- `test/sql/ts_forecast_agg.test:267` — `(ts_forecast_agg(ts, value, 'ARIMA', 3, MAP([], []))).model_name`
- `test/sql/ts_forecast_by.test:290,295` — `ts_forecast_by('test_groups', ..., 'ARIMA', 3, '1d', MAP([], []))`
- `test/sql/ts_forecast_statespace.test:161-429` — an entire ARIMA-dedicated section (~25 assertion sites: `.model`, `.point`, `.lower`/`.upper`, `.fitted`/`.residuals`, `.aic`/`.bic`/`.mse`, `ts_forecast_agg` variants), every call uses bare `'ARIMA'` with no `arima_order`.
  (`test/sql/ts_forecast_exog.test`'s two `'ARIMA'` hits at lines 54/104 are prose comments describing the OLD behavior, already migrated in Plan 10-04 — not live assertions.)

**AT RISK (D-07 — seasonal model called via `MAP{}`/`MAP([], [])`/no-MAP 3-arg path with no explicit `seasonal_period`; whether it now errors is data-dependent, as this plan's own Task 2 work repeatedly demonstrated — some fixtures auto-detect fine, others don't):**
- `test/sql/ts_forecast_agg.test:231,439` — `HoltWinters`/`MSTL` via `ts_forecast_agg(..., MAP([], []))`
- `test/sql/ts_forecast_basic_models.test:114-162,506` — `SeasonalNaive` via `_ts_forecast`/`ts_forecast_agg` with no period
- `test/sql/ts_forecast_by.test:124-221,456-472` — `SeasonalNaive`/`HoltWinters`/`SeasonalES`/`SeasonalESOptimized`/`SeasonalWindowAverage`/`MSTL`/`TBATS` via `MAP([], [])` (lines 488/494 in the same file already pass `seasonal_period` explicitly and are NOT at risk)
- `test/sql/ts_forecast_error_isolation.test:911,922` — `HoltWinters`/`MSTL` via bare 3-arg `_ts_forecast`
- `test/sql/ts_forecast_exp_smoothing.test:147-554` — `HoltWinters`/`SeasonalES`/`SeasonalESOptimized`/`SeasonalWindowAverage` throughout, no period anywhere in the file
- `test/sql/ts_forecast_multi_seasonal.test:134-472` — `MSTL`/`TBATS` throughout, no period anywhere in the file
- `test/sql/ts_forecast_params.test:98-326` — `SeasonalNaive`/`HoltWinters`/`MSTL`/`TBATS` via bare `_ts_forecast` (lines 169/488-494 in the same `ts_forecast_by.test`-style calls already pass `seasonal_period` and are NOT at risk; `ts_forecast_inspect_explain.test:98,137` also already pass `seasonal_period`/`seasonal_periods` explicitly and are NOT at risk).

**LOW RISK (D-06 — `AutoMFLES`/`AutoMSTL` model_name rename; checked and found NOT actually broken):**
- `test/sql/ts_forecast_agg.test`, `ts_forecast_auto.test`, `ts_forecast_by.test`, `ts_forecast_mfles_stability.test` all pass `'AutoMFLES'`/`'AutoMSTL'` as the method argument and assert only `model_name IS NOT NULL` / `model IS NOT NULL` — never a literal `'AutoMFLES'`/`'AutoMSTL'` expected value. Grepped every `----` expected-result line across all 50 files for a literal `AutoMSTL`/`AutoMFLES` match: zero hits. Phase 18 can likely skip these without re-verification, but they are listed here per the plan's instruction to enumerate every hit.

**NOT FOUND (checked, no hits):** literal `'ARIMAX'`/`'ThetaX'`/`'MFLESX'` stand-in-name assertions (the two `ts_forecast_exog.test` hits are prose comments, already migrated in 10-04); literal old fabricated-interval (`234.`) or fabricated-MSE (`2090.28`) value assertions anywhere in the 50 json-gated files.

---
*Phase: 10-forecast-wrapper-delegates-to-crate-models*
*Completed: 2026-10-09*

## Self-Check: PASSED

All claimed files found on disk (`crate_delegation_examples.sql`, `README.md`, `ts_native_model_names.test`, `ts_backtest_equivalence.test`, `ts_model_distinctness.test`, this SUMMARY) and all claimed commits (`f654197`, `75b6d49`) found in `git log --oneline --all`.
