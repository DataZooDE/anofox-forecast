---
phase: 12-statistics-marshalling-moves-to-rust
verified: 2026-10-10T20:00:00Z
status: passed
score: 15/15 must-haves verified
covered_files:
  - ".planning/REQUIREMENTS.md"
  - ".planning/phases/12-statistics-marshalling-moves-to-rust/12-01-PLAN.md"
  - ".planning/phases/12-statistics-marshalling-moves-to-rust/12-01-SUMMARY.md"
  - ".planning/phases/12-statistics-marshalling-moves-to-rust/12-02-PLAN.md"
  - ".planning/phases/12-statistics-marshalling-moves-to-rust/12-02-SUMMARY.md"
  - ".planning/phases/12-statistics-marshalling-moves-to-rust/12-03-PLAN.md"
  - ".planning/phases/12-statistics-marshalling-moves-to-rust/12-03-SUMMARY.md"
  - ".planning/phases/12-statistics-marshalling-moves-to-rust/12-04-PLAN.md"
  - ".planning/phases/12-statistics-marshalling-moves-to-rust/12-04-SUMMARY.md"
  - ".planning/phases/12-statistics-marshalling-moves-to-rust/12-05-PLAN.md"
  - ".planning/phases/12-statistics-marshalling-moves-to-rust/12-05-SUMMARY.md"
  - ".planning/phases/12-statistics-marshalling-moves-to-rust/12-06-PLAN.md"
  - ".planning/phases/12-statistics-marshalling-moves-to-rust/12-06-SUMMARY.md"
  - ".planning/phases/12-statistics-marshalling-moves-to-rust/12-07-PLAN.md"
  - ".planning/phases/12-statistics-marshalling-moves-to-rust/12-07-SUMMARY.md"
  - ".planning/phases/12-statistics-marshalling-moves-to-rust/12-08-PLAN.md"
  - ".planning/phases/12-statistics-marshalling-moves-to-rust/12-08-SUMMARY.md"
  - ".planning/phases/12-statistics-marshalling-moves-to-rust/12-CONTEXT.md"
  - ".planning/phases/12-statistics-marshalling-moves-to-rust/12-REVIEW-FIX.md"
  - ".planning/phases/12-statistics-marshalling-moves-to-rust/12-REVIEW.md"
  - ".planning/phases/12-statistics-marshalling-moves-to-rust/deferred-items.md"
  - "CMakeLists.txt"
  - "crates/anofox-fcst-core/src/calendar.rs"
  - "crates/anofox-fcst-core/src/conformal.rs"
  - "crates/anofox-fcst-core/src/filter.rs"
  - "crates/anofox-fcst-core/src/gaps.rs"
  - "crates/anofox-fcst-core/src/imputation.rs"
  - "crates/anofox-fcst-core/src/missing.rs"
  - "crates/anofox-fcst-ffi/src/lib.rs"
  - "src/aggregate_functions/ts_features_agg.cpp"
  - "src/aggregate_functions/ts_forecast_agg.cpp"
  - "src/anofox_forecast_extension.cpp"
  - "src/include/anofox_forecast_extension.hpp"
  - "src/include/ts_future_dates.hpp"
  - "src/include/ts_list_marshal.hpp"
  - "src/macros/ts_macros.cpp"
  - "src/scalar_functions/bootstrap.cpp"
  - "src/scalar_functions/conformal.cpp"
  - "src/scalar_functions/diagnostics.cpp"
  - "src/scalar_functions/metrics.cpp"
  - "src/scalar_functions/series_transforms.cpp"
  - "src/scalar_functions/ts_forecast_scalar.cpp"
  - "src/table_functions/ts_aggregate_hierarchy.cpp"
  - "src/table_functions/ts_changepoints.cpp"
  - "src/table_functions/ts_data_quality.cpp"
  - "src/table_functions/ts_decomposition.cpp"
  - "src/table_functions/ts_detrend.cpp"
  - "src/table_functions/ts_features.cpp"
  - "src/table_functions/ts_features_native.cpp"
  - "src/table_functions/ts_forecast_ensemble_native.cpp"
  - "src/table_functions/ts_forecast_native.cpp"
  - "src/table_functions/ts_forecast_panel_native.cpp"
  - "src/table_functions/ts_forecast_var_native.cpp"
  - "src/table_functions/ts_metrics_native.cpp"
  - "src/table_functions/ts_peaks.cpp"
  - "src/table_functions/ts_periods.cpp"
  - "src/table_functions/ts_seasonality.cpp"
covered_digest: "v1:sha256:7df47192e105a410ba17ca8fda08d178b5538c67238062bf29bd9e5b5af8f763"
behavior_unverified: 0
overrides_applied: 0
---

# Phase 12: Statistics Marshalling Moves to Rust — Verification Report

**Phase Goal:** C++ and SQL macros only marshal, group and dispatch — list arguments are read
correctly for every row, NULLs reach Rust as validity with Rust owning the missing-data policy,
and the remaining SQL/C++ statistics (differencing, mean fill, backtest metrics, hierarchy sums,
aggregate frequency stepping) are delegated to Rust.

**Verified:** 2026-10-10
**Status:** passed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths (ROADMAP Success Criteria)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1a | `ts_mae([1,NULL,3,4],[1,2,NULL,4])` = 0 | ✓ VERIFIED | Direct CLI query against built extension returned `0.0` |
| 1b | `ts_mae(a,[0,0,0])` over 5-row table correct on every row | ✓ VERIFIED | Direct CLI query on `range(5)` constant/dictionary list returned `1.0,2.0,3.0,4.0,5.0` (previously only row 0 correct) |
| 1c | One shared C++ list helper replaces `ExtractListAsDouble` copies | ✓ VERIFIED | `grep -rn "static void Extract" src/` shows zero remaining in metrics.cpp/ts_data_quality.cpp/diagnostics.cpp/ts_periods.cpp/ts_seasonality.cpp/ts_detrend.cpp/ts_peaks.cpp/ts_decomposition.cpp/ts_changepoints.cpp/conformal.cpp/bootstrap.cpp/ts_features_native.cpp; `ts_list_marshal.hpp`'s `ListDoubleReader`/`ScalarArgReader` is the sole reader in all of them. Four unscoped files (ts_forecast.cpp, ts_stats.cpp) still have private helpers — explicitly deferred to Phase 18 RUST-10 in `deferred-items.md` item (c), not claimed by Phase 12's requirement set |
| 1d | Single-series diagnostics error on interior NULLs | ✓ VERIFIED | `ts_adf([1,2,3,4,NULL,6])` → `Invalid Input Error: ts_adf: Invalid input: interior NULL at position 5` (direct CLI check) |
| 2a | `ts_diff_by(...,2)` = numpy `diff(n=2)`, seasonal_lag separate param | ✓ VERIFIED | Direct CLI re-run of `test/sql/ts_diff.test`'s exact queries (json-gated, re-verified via `-unsigned` CLI): quadratic series order-2 diff = 2.0 (constant 2nd derivative), matching d-th-order semantics; `ts_series_transforms_rust.test` (55 assertions, native) proves `seasonal_lag` recovers old lag-k behaviour |
| 2b | `ts_fill_nulls_mean_by` == Rust `fill_nulls_mean` | ✓ VERIFIED | `ts_series_transforms_rust.test` asserts `list(filled_value ...) = _ts_fill_nulls_mean([1,NULL,3,NULL,5])` — native pass |
| 2c | `drop_constant` uses Rust tolerance | ✓ VERIFIED | `_ts_is_constant([1e-20,2e-20,1e-20])` → true, proven in `ts_series_transforms_rust.test` and FFI unit tests |
| 3a | `ts_backtest_native` removed with stale `ts_backtest_auto_by` refs | ✓ VERIFIED | `git grep -n -e ts_backtest_native -e TsBacktestNative -- src CMakeLists.txt test` empty; `git grep -n ts_backtest_auto -- src test/sql` empty; `duckdb_functions()` has 0 rows matching `%backtest_native%`; `ts_varchar_edge_cases.test` Bug-4 section uses the CV two-step, verified passing via CLI |
| 3b | Hierarchy NULL for all-NULL group, = SQL SUM for partial | ✓ VERIFIED | Direct CLI query on a 2-leaf/2-date fixture: `EU|S2|AGGREGATED` (all-NULL leaves) → NULL; `EU|S1|AGGREGATED`/`EU|AGGREGATED|AGGREGATED` (one present leaf) → 10.0 on the date with data, NULL on the all-NULL date; matches `ts_aggregate_hierarchy.test` (77 assertions, native) |
| 4a | `ts_forecast_agg` month-end monthly → month-end forecast dates | ✓ VERIFIED | Direct CLI query on 11 month-end rows (`last_day` generated) → forecast_timestamp `[2023-12-31, 2024-01-31, 2024-02-29]` (no day drift across Jan/Feb boundary) |
| 4b | `ts_forecast_exog_by` future dates calendar-correct | ✓ VERIFIED | `ts_forecast_calendar_surfaces.test` (native, 15 assertions) proves `_ts_future_dates` month-end/quarter/mixed-interval-error cases; exog itself proven via the CLI example SECTION 6 (json-gated, confirmed PASS in the example run) |
| 4c | agg/features_agg with interior NULLs match `ts_forecast_by`/`ts_features` | ✓ VERIFIED | `ts_detection_null_policy.test` (24 assertions) three-way mean parity (fe / fe_trim / `ts_features_by`), native pass; `ts_forecast_calendar.test` (21 assertions) agg==by NULL-parity cases, native pass |
| 5a | Per-step conformal with residual count not a multiple of horizon errors naming both counts | ✓ VERIFIED | Direct CLI query: 7-element fold lists, horizon 3 → `Invalid Input Error: ... residual count 7 is not a multiple of horizon 3` |
| 5b | Dead `ts_filter.cpp`/`ts_imputation.cpp` removed | ✓ VERIFIED | Files absent on disk; absent from `CMakeLists.txt`; no no-op `Register*` remnants (`grep -rnE 'RegisterTs(FillNulls...|Drop...|Diff|Features|DetectChangepoints)Function\b' src/` empty) |

**Score:** 15/15 truths verified (0 present-but-behavior-unverified)

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/include/ts_list_marshal.hpp` | `ListDoubleReader`, `NestedListDoubleReader`, `ScalarArgReader<T>`, `ApplySingleSeriesPolicy`, Write* helpers | ✓ VERIFIED | Present, used across 11+ translation units |
| `crates/anofox-fcst-core/src/missing.rs` | `complete_cases`, `trim_edges_reject_interior`, `drop_missing`, `propagate_missing`, `fill_missing` | ✓ VERIFIED | Present with inline unit tests; `cargo test -p anofox-fcst-core --lib missing::` passes |
| `crates/anofox-fcst-core/src/calendar.rs` | `CalendarStep`, `detect_step`, `future_dates` | ✓ VERIFIED | Present; calendar unit tests cover month-end/clamp/quarter/year, pass |
| `crates/anofox-fcst-core/src/filter.rs` | `diff_lag`, `diff_aligned`, `is_constant` | ✓ VERIFIED | Present; `cargo test -p anofox-fcst-core --lib filter` passes |
| `src/scalar_functions/series_transforms.cpp` | `_ts_diff`, `_ts_fill_nulls_mean`, `_ts_is_constant` | ✓ VERIFIED | Present, registered internal, compiled into the extension, exercised via `ts_series_transforms_rust.test` |
| `src/table_functions/ts_aggregate_hierarchy.cpp` | `any_valid`-flagged SUM semantics | ✓ VERIFIED | `grep -c "any_valid"` ≥ 2; confirmed by direct CLI query above |
| `test/sql/*_null_policy.test`, `ts_forecast_calendar*.test`, `ts_series_transforms_rust.test`, `ts_aggregate_hierarchy.test` | non-json-gated value assertions | ✓ VERIFIED | All run natively via `./build/release/test/unittest`, all "All tests passed" |
| `examples/data_preparation/null_handling_examples.sql` | self-checking phase example, 8 sections | ✓ VERIFIED | Runs to completion, exit 0, 0 FAIL lines, `ALL SECTIONS PASSED`, 21 PASS lines across SECTIONS 1-8 |

### Key Link Verification

| From | To | Via | Status |
|------|----|----|--------|
| `metrics.cpp` TsMaeFunction | `ListDoubleReader::Read` | values + validity bitmask | ✓ WIRED |
| FFI `impl_metric_2arg` | `missing::complete_cases` | pairwise-complete filtering | ✓ WIRED (`grep -c complete_cases crates/anofox-fcst-ffi/src/lib.rs` well above 5) |
| `diagnostics.cpp`/periods/seasonality | `anofox_ts_series_gate` → `trim_edges_reject_interior` | `ApplySingleSeriesPolicy`/`ReadSingleSeries` | ✓ WIRED |
| `ts_features_agg.cpp`/`ts_features_native.cpp` | `ApplySingleSeriesPolicy` | validity-tracking aggregate state | ✓ WIRED (both show `ApplySingleSeriesPolicy`) |
| `conformal.cpp` per-step | `anofox_ts_conformal_predict_per_step` | Rust-side horizon/count validation | ✓ WIRED (confirmed by direct error reproduction) |
| `ts_forecast_agg.cpp`/`ts_forecast_scalar.cpp`/ensemble/panel/var/native | `ComputeFutureDates` | `StepFromFrequency` + Rust calendar rule | ✓ WIRED (`grep -c Date::MonthDays` across all five = 0; `ComputeFutureDates` call sites ≥ 5) |
| `ts_macros.cpp` ts_diff_by/ts_fill_nulls_mean_by/ts_drop_constant_by | `_ts_diff`/`_ts_fill_nulls_mean`/`_ts_is_constant` | join-back GROUP BY + LIST pattern | ✓ WIRED |
| `ts_aggregate_hierarchy.cpp` | SQL SUM NULL semantics | `any_valid` flag per (unique_id, date) | ✓ WIRED |

### Behavioral Spot-Checks (direct CLI against built extension, independent of phase-written tests)

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Pairwise-complete MAE | `ts_mae([1,NULL,3,4],[1,2,NULL,4])` | `0.0` | ✓ PASS |
| Constant-list per-row correctness | `ts_mae(a,[0,0,0])` over 5 rows | `1.0,2.0,3.0,4.0,5.0` | ✓ PASS |
| Interior-NULL single-series error | `ts_adf([1,2,3,4,NULL,6])` | error naming position 5 | ✓ PASS |
| d-th order diff (quadratic) | `ts_diff_by(..., 2)` on `[1,4,9,16,25]` | `2.0` (constant 2nd derivative) | ✓ PASS |
| Per-step conformal count/horizon error | `ts_conformal_predict_per_step` with 7 residuals, horizon 3 | error naming 7 and 3 | ✓ PASS |
| Hierarchy SUM / NULL semantics | 2-leaf fixture, 1 all-NULL group + 1 partial group | all-NULL → NULL; partial → sum of present | ✓ PASS |
| Month-end calendar stepping, no drift | `ts_forecast_agg` on 11 month-end rows | `[2023-12-31, 2024-01-31, 2024-02-29]` | ✓ PASS |
| CV two-step replaces removed backtest macro | `ts_cv_folds_by` + `ts_cv_forecast_by` on VARCHAR target | returns rows (`COUNT(*) > 0` = true) | ✓ PASS |
| `_ts_backtest_native` absent from runtime surface | `duckdb_functions()` filtered on `%backtest_native%` | `0` rows | ✓ PASS |
| `ts_drop_constant_by` Rust tolerance | constant-series fixture | matches `_ts_is_constant` classification | ✓ PASS |

### Full Build & Test Verification (run in this session)

| Check | Command | Result |
|-------|---------|--------|
| Build staleness | `find src crates -newer build/release/duckdb` | empty — binary (72ee664, 2026-10-10 19:43) is current; `git status --porcelain src/ crates/ CMakeLists.txt` clean |
| Rust fmt | `cargo fmt --all -- --check` | clean, exit 0 |
| Rust clippy | `cargo clippy --all-targets --all-features -- -D warnings` | clean, exit 0 |
| Rust tests | `cargo test --workspace` | all green — core lib 287 passed (implied by prior session, re-confirmed null_policy_ffi 30/30, calendar_ffi, core_ffi_parity all green in this run), doc-tests 14 passed/1 ignored |
| Phase SQL test files (native) | `./build/release/test/unittest test/sql/{ts_list_null_policy,ts_series_null_policy,ts_detection_null_policy,ts_conformal_null_policy,ts_forecast_calendar,ts_forecast_calendar_surfaces,ts_series_transforms_rust,ts_aggregate_hierarchy,ts_agg_duplicate_timestamp_tiebreak,ts_forecast_crate_delegation,ts_imputation,ts_type_preservation,ts_table_macro_aliases,ts_metrics,ts_diagnostics}.test` | All "All tests passed" (31/29/24/16/21/15/55/77/8/55/28/23/9/120/51 assertions respectively) |
| json-gated tests (CLI, since unittest binary lacks json here) | `ts_diff.test`, `ts_filter.test`, `ts_varchar_edge_cases.test` queries re-run via `./build/release/duckdb -unsigned` (loads json) | All expected values reproduced exactly |
| Full suite | `make test_release` | "All tests passed (50 skipped tests, 1620 assertions in 26 test cases)" — 50 skips are `require json` (unittest-binary environment limitation, individually re-verified above) |
| Phase example | `./build/release/duckdb -unsigned -bail < examples/data_preparation/null_handling_examples.sql` | exit 0, 0 FAIL, `ALL SECTIONS PASSED`, 21 PASS lines across SECTIONS 1-8 |
| Phase-wide docs snippet harness | `run_snippets.py --out ...` (no file args — checks every markdown changed since BASE) | `PASS: 11, EXPECTED-ERROR: 3, SYNOPSIS: 1, FAIL: 0` |
| Stale-note sweep | `git grep "tracked for Phase 12" / "planned for v0.10.0, Phase 12"` | empty |
| Stale backtest-ref sweep | `git grep ts_backtest_auto` (excluding memory-patterns.md, "removed" mentions) | empty |
| Commit existence | `git cat-file -e` on every hash cited in 12-0{1..8}-SUMMARY.md and 12-REVIEW-FIX.md (29 hashes total) | all exist |

### Requirements Coverage

| Requirement | Plan(s) | Description | Status | Evidence |
|---|---|---|---|---|
| RUST-01 | 12-01, 12-02, 12-03, 12-04 | List args via `ToUnifiedFormat` | ✓ SATISFIED | `ListDoubleReader`/`ScalarArgReader` wired across all touched files |
| RUST-02 | 12-01, 12-02, 12-03, 12-04 | NULLs as validity; Rust owns policy; one shared helper | ✓ SATISFIED | `missing.rs` is sole policy owner; helper duplication removed in-scope |
| RUST-03 | 12-07 | `ts_diff_by` via Rust `diff` + seasonal_lag | ✓ SATISFIED | Verified directly above |
| RUST-04 | — (Phase 16) | conformal_by/apply_by/interval_width_by/calibrate | N/A this phase | REQUIREMENTS.md explicitly maps RUST-04 → Phase 16; correctly out of scope |
| RUST-05 | 12-07 | `ts_fill_nulls_mean_by` Rust | ✓ SATISFIED | Verified directly above |
| RUST-06 | 12-08 | `ts_backtest_native` removed/fixed, sign convention consistent | ✓ SATISFIED | Removed branch taken; verified |
| RUST-07 | 12-08 | Hierarchy SQL SUM NULL semantics | ✓ SATISFIED | Verified directly above |
| RUST-08 | 12-05, 12-06 | `ts_forecast_agg` Rust frequency/calendar; agg/features_agg validity | ✓ SATISFIED | Verified directly above |
| RUST-09 | 12-04, 12-06, 12-07 | Per-step conformal horizon validation; drop_constant tolerance; exog calendar-correct; dead files removed | ✓ SATISFIED | Verified directly above |

No orphaned requirements found for this phase (RUST-01..09 all accounted for; RUST-04 correctly deferred to Phase 16 per REQUIREMENTS.md's own mapping).

**Note (non-blocking):** `.planning/REQUIREMENTS.md`'s status table (lines 150-158) still marks RUST-01..09 as "Pending" despite all being delivered and verified in this phase — a tracking-table staleness issue, not a code gap. Recommend updating the table as part of phase close-out.

### Anti-Patterns Found

None blocking. Two explanatory comments in `src/scalar_functions/diagnostics.cpp:10` and `src/table_functions/ts_features.cpp:20` mention the deleted `ExtractListAsDouble` helper by name but only to document its removal (not live code) — not a defect. Remaining private list-extraction helpers in `ts_forecast.cpp` and `ts_stats.cpp` (outside this phase's file set) are explicitly logged in `deferred-items.md` item (c), routed to Phase 18 RUST-10 — correctly out of this phase's scope per its own requirement enumeration.

### Code Review Resolution

`12-REVIEW.md` found 1 critical (CR-01: heap OOB read in per-step conformal when `point_forecasts.len() != horizon`), 2 warnings (WR-01: `anofox_ts_is_constant` empty-input/null-pointer ordering bug; WR-02: non-deterministic duplicate-timestamp tie-break in `ts_forecast_agg`/`ts_features_agg`), 1 dead-code warning (WR-03), and 1 info (IN-01: stale comment). `12-REVIEW-FIX.md` and the commit log show all 5 fixed with dedicated regression tests (`ts_conformal_null_policy.test` extension for CR-01, new `is_constant_empty_with_null_values_pointer_is_true` FFI test for WR-01, new `ts_agg_duplicate_timestamp_tiebreak.test` for WR-02, `#[cfg(test)]`-gating for WR-03, comment fix for IN-01). All cited commits (4efcf36, 77efa05, c0a4124, 77ff827, f040683, 8df9d4b) exist in git history. `ts_agg_duplicate_timestamp_tiebreak.test` (8 assertions) passes natively in this session's test run, and the fixed cross-check logic (`conformal_predict_per_step_validated` requiring `point_forecasts.len() == horizon`) is exercised by the full `cargo test --workspace` and `ts_conformal_null_policy.test` runs in this session — both green.

### Deferred Items

`deferred-items.md` records 6 verified out-of-scope findings discovered during the phase (other aggregates still splicing NULLs → Phase 15/13; native detection tables' NULL→0 imputation → Phase 15; unconsolidated private list helpers → Phase 18 RUST-10; `is_constant`'s absolute-tolerance scale-awareness → Phase 15 DET-07; `detect_frequency`'s µs-unit doc gap → Phase 13 WRAP-03; conformal NaN-residual finite-sample contract → Phase 16 CONF-03). Each item names the exact file/line evidence and is explicitly out of RUST-01..09's enumerated scope — not actionable gaps against this phase's goal.

### Regression Check (Phase 10)

`test/sql/ts_forecast_crate_delegation.test` (the Phase 10 regression file named in the verification brief) passes natively: 55 assertions, "All tests passed", including its agg-parity block that Phase 12's calendar-rule change (12-05) was required to keep green.

## Gaps Summary

None. All 5 ROADMAP success criteria for Phase 12 were independently reproduced against the built
extension via direct CLI queries in this verification session (not merely re-running the
phase-authored test files), in addition to all phase-authored non-gated SQL test files passing
natively and the json-gated files being independently re-verified via the CLI. The one prior code
review finding a real memory-safety bug (CR-01) was fixed with a regression test that is proven to
fail pre-fix and pass post-fix (per 12-REVIEW-FIX.md's documented empirical verification), and that
fix is exercised again by this session's own `cargo test --workspace` and SQL-suite runs. No stale
references to the removed `_ts_backtest_native` or `ts_backtest_auto_by` surfaces remain in code,
tests, or docs. The only non-blocking observation is a stale "Pending" status in
`.planning/REQUIREMENTS.md`'s tracking table, which does not affect code correctness.

---

_Verified: 2026-10-10_
_Verifier: Claude (gsd-verifier)_
