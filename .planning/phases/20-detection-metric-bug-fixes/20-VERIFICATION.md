---
phase: 20-detection-metric-bug-fixes
verified: 2026-10-24T01:20:00Z
status: passed
score: 5/5 must-haves verified
covered_files: [".planning/REQUIREMENTS.md", ".planning/phases/20-detection-metric-bug-fixes/20-01-PLAN.md", ".planning/phases/20-detection-metric-bug-fixes/20-01-SUMMARY.md", ".planning/phases/20-detection-metric-bug-fixes/20-02-PLAN.md", ".planning/phases/20-detection-metric-bug-fixes/20-02-SUMMARY.md", ".planning/phases/20-detection-metric-bug-fixes/20-03-PLAN.md", ".planning/phases/20-detection-metric-bug-fixes/20-03-SUMMARY.md", ".planning/phases/20-detection-metric-bug-fixes/20-04-PLAN.md", ".planning/phases/20-detection-metric-bug-fixes/20-04-SUMMARY.md", ".planning/phases/20-detection-metric-bug-fixes/20-05-PLAN.md", ".planning/phases/20-detection-metric-bug-fixes/20-05-SUMMARY.md", ".planning/phases/20-detection-metric-bug-fixes/20-06-PLAN.md", ".planning/phases/20-detection-metric-bug-fixes/20-06-SUMMARY.md", ".planning/phases/20-detection-metric-bug-fixes/20-07-PLAN.md", ".planning/phases/20-detection-metric-bug-fixes/20-07-SUMMARY.md", ".planning/phases/20-detection-metric-bug-fixes/20-REVIEW-FIX.md", ".planning/phases/20-detection-metric-bug-fixes/20-REVIEW.md", ".planning/phases/20-detection-metric-bug-fixes/COVERAGE.md", ".planning/phases/20-detection-metric-bug-fixes/deferred-items.md", "CHANGELOG.md", "CMakeLists.txt", "crates/anofox-fcst-core/src/changepoint.rs", "crates/anofox-fcst-core/src/lib.rs", "crates/anofox-fcst-core/src/seasonality.rs", "crates/anofox-fcst-core/tests/changepoint_reference.rs", "crates/anofox-fcst-core/tests/metrics_reference.rs", "crates/anofox-fcst-core/tests/stl_reference.rs", "crates/anofox-fcst-ffi/src/lib.rs", "crates/anofox-fcst-ffi/src/types.rs", "docs/api/02-hierarchical.md", "docs/api/04-data-preparation.md", "docs/api/05a-decomposition.md", "docs/api/06-changepoint-detection.md", "docs/api/09-evaluation-metrics.md", "examples/changepoint_detection/synthetic_changepoint_examples.sql", "plugins/anofox-forecast/skills/anofox-forecast-backtest/SKILL.md", "plugins/anofox-forecast/skills/anofox-forecast-data-prep/SKILL.md", "plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md", "src/aggregate_functions/ts_changepoints_agg.cpp", "src/aggregate_functions/ts_classify_seasonality_agg.cpp", "src/anofox_forecast_extension.cpp", "src/include/anofox_fcst_ffi.h", "src/include/anofox_forecast_extension.hpp", "src/include/ts_changepoints_params.hpp", "src/include/ts_collect_finalize.hpp", "src/macros/ts_macros.cpp", "src/table_functions/ts_aggregate_hierarchy.cpp", "src/table_functions/ts_changepoints.cpp", "src/table_functions/ts_cv_folds_native.cpp", "src/table_functions/ts_cv_forecast_native.cpp", "src/table_functions/ts_cv_hydrate_native.cpp", "src/table_functions/ts_cv_split_native.cpp", "src/table_functions/ts_features_native.cpp", "src/table_functions/ts_fill_forward_native.cpp", "src/table_functions/ts_fill_gaps_native.cpp", "src/table_functions/ts_forecast_native.cpp", "src/table_functions/ts_forecast_panel_native.cpp", "src/table_functions/ts_forecast_var_native.cpp", "src/table_functions/ts_metrics_native.cpp", "src/table_functions/ts_mstl_decomposition_native.cpp", "src/table_functions/ts_passthrough.cpp", "src/table_functions/ts_seasonality.cpp", "src/table_functions/ts_split_keys.cpp", "src/table_functions/ts_stats.cpp", "src/table_functions/ts_validate_separator.cpp", "test/sql/correctness/detection_decomposition.test", "test/sql/correctness/metrics.test", "test/sql/correctness/union_source.test", "test/sql/ts_classify_seasonality.test", "test/sql/ts_detection_null_policy.test", "test/sql/ts_detection_reference.test", "test/sql/ts_detrend.test", "test/sql/ts_periods_specialized.test", "test/sql/ts_seasonality.test", "validation/reference/fixtures/changepoints/bocpd_cases.json", "validation/reference/fixtures/r_decomposition/cases.json", "validation/reference/fixtures/r_mase/accuracy_mase.json", "validation/reference/fixtures/r_stl_strength/cases.json", "validation/reference/python/gen_changepoint_reference.py", "validation/reference/r/gen_decomposition_reference.R", "validation/reference/r/gen_mase_reference.R", "validation/reference/r/gen_stl_strength_reference.R"]
covered_digest: "v1:sha256:7ccb2e422c3ef6016a1cb8cacba9c46393d1ab29084efb0a7b3f2465dc237269"
behavior_unverified: 0
overrides_applied: 0
---

# Phase 20: Detection & Metric Bug Fixes Verification Report

**Phase Goal:** The metric and detection functions give correct, order-independent answers on the series and panels reported in #285, #286 and #287.
**Verified:** 2026-10-24T01:20:00Z
**Status:** passed
**Re-verification:** No — initial verification

## Note on REQUIREMENTS.md edit made during this verification

`20-02-SUMMARY.md`, `20-03-SUMMARY.md`, `20-04-SUMMARY.md` and `20-05-SUMMARY.md` all
deliberately left `BUG-03` as `[ ] Pending` in `.planning/REQUIREMENTS.md` because the
UNION-ALL/row-order fix spans five plans (20-02 through 20-05 plus the scope expansion
decided in `20-CONTEXT.md` addendum 2). `20-05-SUMMARY.md` states explicitly: "BUG-03 can
be marked complete once phase verification confirms it." `20-07-SUMMARY.md` then
(incorrectly) narrated that BUG-03 was "already closed ... per REQUIREMENTS.md" — it was
not; the checkbox and the status table both still read `Pending` as of the start of this
verification (confirmed via `git log -p -- .planning/REQUIREMENTS.md`, every revision from
phase start through HEAD `afa772f` shows BUG-03 Pending). Having independently verified
Success Criterion 1 below (live reproduction + full regression suite), this verification
flips `.planning/REQUIREMENTS.md` BUG-03 to `[x]` / `Complete`, per the explicit hand-off in
20-05-SUMMARY.md. This is the one gap found in this phase's bookkeeping; the underlying
code and tests are not at fault.

## Goal Achievement

### Observable Truths

| # | Truth (ROADMAP Success Criterion) | Status | Evidence |
|---|---|---|---|
| 1 | `ts_mase_by` gives the same value regardless of row order (sorted/reversed/UNION ALL literal-cutoff/UNION ALL scalar-subquery-cutoff/shuffled), equal to the date-sorted Hyndman-Koehler MASE | VERIFIED | Live reproducer (5 orderings of a 3-series/45-day panel through the built extension) returned identical `(A=NULL, B=0.4285714286, C=0.75)` across sorted, reversed, union-literal, union-subquery and hash-shuffled sources. `test/sql/correctness/metrics.test` (143 assertions) and `test/sql/correctness/union_source.test` (322 assertions) pass, including the exact `#285` repro shapes and an R `forecast::accuracy` MASE reference fixture (`validation/reference/fixtures/r_mase/accuracy_mase.json`, case `issue285_panel_*`). |
| 2 | `ts_classify_seasonality_by` classifies the #286 strong weekly series as seasonal (not `non_seasonal`, never NaN); `seasonal_strength` is consistent with `cycle_strengths` and with `ts_stats_by` | VERIFIED | Live query on an 84-day weekly-sine series: `is_seasonal=true`, `timing_classification='stable_seasonal'`, `seasonal_strength=0.9999314407916057`, `cycle_strengths=[1.0]*11` (all consistent, no contradiction); `ts_stats_by`'s `seasonality_strength` on the identical table returned the exact same value `0.9999314407916057` (same shared Rust `stl_strengths` call). `test/sql/correctness/detection_decomposition.test` (140 assertions), `test/sql/ts_classify_seasonality.test` (39), `test/sql/ts_detection_reference.test` (147, R `tsfeatures` reference) all pass. |
| 3 | On the #287 panel, opt-in `deseasonalize := true` makes `ts_detect_changepoints_by`/`_agg` flag none of the weekly troughs and still flag B's 2024-02-20 level shift; defaults unchanged without the option | VERIFIED | Live query on the exact #287 fixture (3 series x 84 days, B +15 from day 50) with `MAP{'deseasonalize':'true','threshold':'0.5'}`: only `B` flagged, exactly `2024-02-20`; `A`/`C` empty. Default (`MAP{}`) md5 byte-identity pins in `detection_decomposition.test` pass unchanged. |
| 4 | `ts_detect_changepoints_agg`: with `threshold` set, `is_changepoint = (changepoint_probability >= threshold)` for every row after the first of each series (first row never flagged); `ts_detect_periods_by` returns period 7 on the #287 panel | VERIFIED | Live queries: table/agg parity diff count = 0 with `MAP{'threshold':'0.5'}`; 0 rows violate the D2-ii rule after the first row of each series; 0 first-rows flagged; `ts_detect_periods_by` returns `primary_period=7.0` for A, B, C. |
| 5 | Each fix computed in Rust, pinned by value-asserting tests under `test/sql/correctness/`; `make audit` and `cargo fmt --check` stay green | VERIFIED | `make audit`: "34 hits, 34 allow-listed, 0 violations, 0 stale entries" (no statistics in C++/SQL). `cargo fmt --all -- --check`: exit 0, no diff. All five correctness-test files present and passing (see Behavioral Spot-Checks / Probe Execution below). |

**Score:** 5/5 truths verified (0 present, behavior-unverified)

### Required Artifacts

| Artifact | Expected | Status | Details |
|---|---|---|---|
| `test/sql/correctness/metrics.test` | #285 value pins (sorted/reversed/UNION ALL literal+subquery/shuffled) for all 11 metric macros | VERIFIED | 143 assertions pass; contains R-reference-backed MASE pins and EXCEPT-ALL equivalence pins for mae/mse/rmse/mape/smape/r2/bias/rmae |
| `test/sql/correctness/union_source.test` | UNION ALL/reversed/shuffled equivalence pins for every collect-then-finalize `_by` macro fixed in Phase 20 | VERIFIED | 322 assertions pass; covers metrics, changepoints, stats, fill-gaps/forward, MSTL, features, CV family, panel/VAR, forecast, hierarchy/split/validate-separator |
| `test/sql/correctness/detection_decomposition.test` | #286/#287 value + byte-identity pins | VERIFIED | 140 assertions pass; includes classify-seasonality strength parity, #287 md5 default-output pins, deseasonalize/threshold/period-7 pins |
| `src/include/ts_collect_finalize.hpp` | Shared last-finalizer barrier (`TsCollectFinalizeBarrier`) | VERIFIED | Present, used by 18 native functions per `20-04-SUMMARY.md` audit table; code-review (20-REVIEW.md) independently confirmed with no diff in the fix pass |
| `src/table_functions/ts_passthrough.cpp` | `_ts_passthrough(TABLE)` single-pipeline breaker | VERIFIED | Present; used by macros per `(SELECT * FROM _ts_passthrough(source) ORDER BY ALL)` pattern, confirmed in `ts_macros.cpp` |
| `src/include/ts_changepoints_params.hpp` | Shared MAP params parser for both `ts_detect_changepoints_by` and `_agg` | VERIFIED | Agg/table parity verified live (diff count 0) and in detection_decomposition.test |

### Key Link Verification

| From | To | Via | Status | Details |
|---|---|---|---|---|
| 11 metric `_by` macros | `_ts_metrics_native` / `TsCollectFinalizeBarrier` | `(SELECT * FROM _ts_passthrough(source) ORDER BY ALL)` | WIRED | Confirmed in `src/macros/ts_macros.cpp`; live UNION ALL/reversed/shuffled reproduction gives identical results |
| `ts_detect_changepoints_agg` | shared params parser | `ParseChangepointParamSet` | WIRED | Agg previously ignored `params`; now parses it — live parity check vs. table function returns 0 diffs |
| `ts_detect_changepoints_by`/`_agg` (`deseasonalize`) | `deseasonalize_for_changepoints` (Rust, `mstl_fit` + `detrend_linear`) | FFI `detect_changepoints_bocpd_with_options` | WIRED | Live `deseasonalize:=true` run flags only B's 2024-02-20 shift, matching the roadmap acceptance criterion |
| `ts_classify_seasonality_by` / `ts_stats_by` | shared `stl_strengths` (Rust) | direct FFI call | WIRED | Live side-by-side query on the same table returns bit-identical `seasonal_strength` / `seasonality_strength` |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|---|---|---|---|
| `ts_mase_by` order-independence (#285) | 5-shape live query against `build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension` | Identical per-id MASE across sorted/reversed/union(literal)/union(subquery)/shuffled | PASS |
| `ts_classify_seasonality_by` vs `ts_stats_by` strength parity (#286) | Live query, same table, two functions | `0.9999314407916057` both | PASS |
| `ts_detect_changepoints_by` deseasonalize (#287) | Live query, `MAP{'deseasonalize':'true','threshold':'0.5'}` | Only B flagged, exactly 2024-02-20 | PASS |
| `ts_detect_changepoints_agg` == `ts_detect_changepoints_by` with threshold (#287, D2-ii) | Live `EXCEPT`-based parity query + first-row/after-first rule checks | 0 diffs; 0 rule violations | PASS |
| `ts_detect_periods_by` period 7 (#287) | Live query | `primary_period=7.0` for A, B, C | PASS |
| Public SQL signatures unchanged | `duckdb_functions()` for `ts_mase_by`, `ts_classify_seasonality_by`, `ts_detect_changepoints_by`/`_agg`, `ts_detect_periods_by`, `ts_aggregate_hierarchy`, `ts_split_keys`, `ts_validate_separator` | All parameter names/counts match pre-phase signatures (additive `MAP` params only) | PASS |

### Probe Execution / Regression Gate

| Suite | Command | Result | Status |
|---|---|---|---|
| Full native sqllogictest (top-level `test/sql/*`) | `./build/release/test/unittest "test/sql/*"` | 95 test cases, 6192 assertions, all passed | PASS |
| Correctness suite | `./build/release/test/unittest "test/sql/correctness/*"` | 10 test cases, 844 assertions, all passed | PASS |
| `cargo test --workspace --release` | full workspace | 33 test binaries, 0 failed (incl. `changepoint_reference.rs`, `metrics_reference.rs`, `stl_reference.rs` value-assertion tests against R/numpy fixtures) | PASS |
| `cargo fmt --all -- --check` | | exit 0, no diff | PASS |
| `make audit` (no-statistics-in-C++/SQL + length-only sweep + WASM signature self-test) | | "34 hits, 34 allow-listed, 0 violations, 0 stale entries"; "0 un-allow-listed length-only tests"; WASM self-test 6/6 | PASS |
| `make check-docs` (`scripts/check_doc_snippets.py`) | | exit 0 | PASS |
| `python3 scripts/check_correctness_inventory.py` | | exit 1 — 3 pre-existing MISSING names (`_ts_forecast_scalar`, `ts_features_config_template`, `ts_features_list`), all documented in `deferred-items.md` as pre-existing/unrelated to this phase's diff, a manual release-DoD gate (not CI-wired) | KNOWN, DEFERRED (see below) |

Build used for all live/spot checks: `build/release/duckdb` + `anofox_forecast.duckdb_extension`, both timestamped after HEAD `afa772f` (matches current `git log -1`).

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|---|---|---|---|---|
| BUG-03 | 20-02, 20-03, 20-04, 20-05 | `ts_mase_by` (and the whole collect-then-finalize family) row-order/UNION-ALL independence | SATISFIED (REQUIREMENTS.md updated this verification, see note above) | Live reproduction + `metrics.test` + `union_source.test` |
| BUG-04 | 20-01 | `ts_classify_seasonality_by` seasonal strength shared with `ts_stats_by` | SATISFIED (already marked Complete) | Live parity + `detection_decomposition.test` + `ts_detection_reference.test` (R fixture) |
| BUG-05 | 20-07 | Opt-in `deseasonalize` for changepoints | SATISFIED (already marked Complete) | Live #287 reproduction + `detection_decomposition.test` |
| BUG-06 | 20-06 | `ts_detect_changepoints_agg` params parsing + threshold D2-ii rule | SATISFIED (already marked Complete) | Live agg/table parity + rule checks |
| BUG-07 | 20-06 | `ts_detect_periods_by` period 7 on #287 panel | SATISFIED (already marked Complete) | Live query + regression pin |

No orphaned requirements found: `REQUIREMENTS.md`'s Phase 20 row set (BUG-03..07) exactly matches the `requirements:` fields declared across all seven plans.

### Anti-Patterns Found

No `TBD`/`FIXME`/`XXX`/`TODO`/`HACK`/`PLACEHOLDER` markers found in the phase's `covered_files` list (grep clean). The one Info-tier finding from code review (`IN-01`, a stale "public, user-facing" comment on the internal `_ts_forecast_native` macro name in `src/table_functions/ts_forecast_native.cpp:849-851`) is cosmetic, explicitly out of the fix pass's `critical_warning` scope, and does not affect behavior or the public API surface.

### Human Verification Required

None. All five Success Criteria were confirmed by direct SQL execution against the built extension plus the full automated regression gate; no visual, real-time, or external-service behavior is in scope for this phase.

### Gaps Summary

One bookkeeping gap was found and closed during this verification: `.planning/REQUIREMENTS.md` still listed `BUG-03` as `[ ] Pending` (status table: `Pending`) at HEAD, even though the code/tests satisfying it were complete as of `20-05` and `20-05-SUMMARY.md` explicitly deferred the flip to "once phase verification confirms it." This verification independently reproduced the #285 fix live against the built extension across 5 row-order/shape permutations, confirmed the full `metrics.test`/`union_source.test` pins pass, and then updated `REQUIREMENTS.md` to `[x]`/`Complete`. No code or test gap was found — the underlying fix was already correct and complete; only the requirements ledger was stale.

---

_Verified: 2026-10-24T01:20:00Z_
_Verifier: Claude (gsd-verifier)_
