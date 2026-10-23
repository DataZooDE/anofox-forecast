---
phase: 20-detection-metric-bug-fixes
reviewed: 2026-10-24T00:00:00Z
depth: standard
files_reviewed: 34
files_reviewed_list:
  - crates/anofox-fcst-core/src/changepoint.rs
  - crates/anofox-fcst-core/src/lib.rs
  - crates/anofox-fcst-core/src/seasonality.rs
  - crates/anofox-fcst-core/tests/changepoint_reference.rs
  - crates/anofox-fcst-core/tests/stl_reference.rs
  - crates/anofox-fcst-ffi/src/lib.rs
  - crates/anofox-fcst-ffi/src/types.rs
  - src/aggregate_functions/ts_changepoints_agg.cpp
  - src/aggregate_functions/ts_classify_seasonality_agg.cpp
  - src/anofox_forecast_extension.cpp
  - src/include/anofox_fcst_ffi.h
  - src/include/anofox_forecast_extension.hpp
  - src/include/ts_changepoints_params.hpp
  - src/include/ts_collect_finalize.hpp
  - src/macros/ts_macros.cpp
  - src/table_functions/ts_aggregate_hierarchy.cpp
  - src/table_functions/ts_changepoints.cpp
  - src/table_functions/ts_cv_folds_native.cpp
  - src/table_functions/ts_cv_forecast_native.cpp
  - src/table_functions/ts_cv_hydrate_native.cpp
  - src/table_functions/ts_cv_split_native.cpp
  - src/table_functions/ts_features_native.cpp
  - src/table_functions/ts_fill_forward_native.cpp
  - src/table_functions/ts_fill_gaps_native.cpp
  - src/table_functions/ts_forecast_native.cpp
  - src/table_functions/ts_forecast_panel_native.cpp
  - src/table_functions/ts_forecast_var_native.cpp
  - src/table_functions/ts_metrics_native.cpp
  - src/table_functions/ts_mstl_decomposition_native.cpp
  - src/table_functions/ts_passthrough.cpp
  - src/table_functions/ts_seasonality.cpp
  - src/table_functions/ts_split_keys.cpp
  - src/table_functions/ts_stats.cpp
  - src/table_functions/ts_validate_separator.cpp
findings:
  critical: 0
  warning: 0
  info: 1
  total: 1
status: clean
---

## User Decisions (2026-10-24, binding for the fix pass)

- **CR-01: WON'T FIX.** `ts_detect_amplitude_modulation` keeps returning NaN for a degenerate `seasonal_strength`; this is the deliberate 20-01 decision ("amplitude-modulation keeps NaN"). Do not change it.
- **CR-02: FIX, option "default threshold".** When `deseasonalize = true` and `threshold` is unset, the effective threshold is 0.5. Decide this in Rust (`ChangepointOptions` / `detect_changepoints_bocpd_with_options`), not in C++, so both SQL surfaces (`ts_detect_changepoints_by` and `ts_detect_changepoints_agg`) inherit it. An explicit `threshold` always wins. With `deseasonalize` false/unset, results stay byte-identical (the md5 pins in detection_decomposition.test must stay green). This option is unreleased (CHANGELOG `[Unreleased]` only), so no compatibility concern. Add a Rust unit test and a SQL pin: on a noisy weekly sine (amplitude 5) + level shift (+8 at i=90, n=180), `deseasonalize` alone flags only the true shift, the same as `deseasonalize` + `threshold` 0.5. Update `docs/api/06-changepoint-detection.md`, the detection SKILL.md, the CHANGELOG entry and the example to state the 0.5 default, and remove the "can still flag weekly-spaced points" caveat.
- **WR-03:** no code change expected. Record it in deferred-items.md: cross-line verification of TsCollectFinalizeBarrier on the v1.4.5/v1.5.6 lines happens through the PR CI run.

# Phase 20: Code Review Report (Iteration 2 — post-fix re-review)

**Reviewed:** 2026-10-24T00:00:00Z
**Depth:** standard
**Files Reviewed:** 34
**Status:** clean

## Summary

This is the iteration-2 re-review after the fix pass recorded in `20-REVIEW-FIX.md`
(commits `59b2b86..a11325c`). Each of the 6 findings the fixer claims to have resolved
(CR-02, WR-01, WR-02, WR-03, WR-04, WR-05) was independently re-verified against the actual
diffs in this tree, not just taken on the fixer's word, plus a check for regressions in the
surrounding code:

- **CR-02** (`deseasonalize: true` + unset `threshold` flooding the series with spurious
  changepoints): verified in `crates/anofox-fcst-core/src/changepoint.rs`. The fix adds an
  `effective_threshold` computed as `(opts.deseasonalize, opts.threshold)` match — `(true,
  None) => Some(0.5)`, everything else passes `opts.threshold` through unchanged — placed
  *after* `bocpd_trace` returns `trace.prob`, so `is_changepoint` is recomputed from
  `effective_threshold` exactly like the existing `Some(threshold)` branch, and `changepoints`
  is still derived from the resulting `is_changepoint`. `opts.deseasonalize == false` takes the
  `(_, explicit)` arm with `explicit == opts.threshold`, which is byte-identical to the
  pre-fix code path — confirmed by the pinned `detection_decomposition.test` md5 blocks for
  non-deseasonalized defaults being untouched in the diff. Two new unit tests exercise both the
  default-to-0.5 behavior and that an explicit threshold overrides it. The SQL pin
  (`detection_decomposition.test`) was updated from asserting the old "B gets extra weekly
  flags" behavior to an `EXCEPT ALL`-based equivalence check between `deseasonalize alone` and
  `deseasonalize + threshold=0.5`, which is a stronger, self-verifying assertion than a fixed
  literal list. Documentation (`docs/api/06-changepoint-detection.md`, the detection
  `SKILL.md`, `CHANGELOG.md`, `examples/changepoint_detection/synthetic_changepoint_examples.sql`)
  was updated consistently at every site that previously stated the old "can still flag
  weekly-spaced points" caveat — a repo-wide grep for that phrase after the fix finds no
  remaining instances. `cargo fmt --all -- --check` reproduces clean on this tree. No
  regression found.
- **WR-01** (unchecked `malloc()` in `anofox_ts_detect_changepoints_bocpd_opts`): verified in
  `crates/anofox-fcst-ffi/src/lib.rs`. All three `malloc()` calls (`is_cp_ptr`, `prob_ptr`,
  `idx_ptr`) now null-check before the write loop, set `ErrorCode::AllocationError` via
  `out_error`, and — importantly — each later failure path frees and nulls out every buffer
  already allocated earlier in the same call via `free_ptr` (confirmed defined in
  `crates/anofox-fcst-ffi/src/allocation.rs:223`, itself null-safe), avoiding a leak on a
  second-or-third allocation failure. Both C++ call sites
  (`src/table_functions/ts_changepoints.cpp`, `src/aggregate_functions/ts_changepoints_agg.cpp`)
  already branch on `success == false` before reading any `BocpdResult`/`cp_result` field, so
  the new `return false` path is handled identically to the pre-existing `INVALID_INPUT` error
  path (default-filled rows / `SetNull`) — no crash, no dangling-pointer read on allocation
  failure. No regression found.
- **WR-02** (asymmetric null guard on `is_changepoint` vs. `changepoint_probability`):
  verified in `src/aggregate_functions/ts_changepoints_agg.cpp`. The unguarded
  `cp_result.is_changepoint[j]` is now `cp_result.is_changepoint ? cp_result.is_changepoint[j]
  : false`, matching the pre-existing `prob_child_data` guard exactly. No regression found.
- **WR-03** (cross-line scheduling-invariant risk in `TsCollectFinalizeBarrier`): per the
  binding user decision, no code change was expected. Confirmed `src/include/ts_collect_finalize.hpp`
  has zero diff in this fix pass, and `deferred-items.md` records the finding with the agreed
  deferral to the PR CI run across all three DuckDB lines. Treated as resolved for this review
  per the binding decision carried forward above — not re-raised.
- **WR-04** (key-name typos silently falling back to defaults): per the binding decision, this
  was a docs-only fix. Confirmed `src/include/ts_changepoints_params.hpp` gained only a header
  comment documenting the known, deliberate leniency-vs-strictness tradeoff; `read_key`'s
  exact-match-or-`Value()` behavior is unchanged. No regression found (no behavior change was
  expected).
- **WR-05** (apparently-unreachable STRUCT branch in `ParseChangepointParamSet`): verified both
  claimed facts directly rather than trusting the fix report — `_ts_detect_changepoints_by_native`'s
  `TableFunction` parameter list (`src/table_functions/ts_changepoints.cpp:593`) and
  `ts_detect_changepoints_agg`'s `AggregateFunction` signature
  (`src/aggregate_functions/ts_changepoints_agg.cpp:267,288,304`) both declare the params
  argument as exactly `LogicalType::MAP(VARCHAR, VARCHAR)`, never `LogicalType::ANY`, so
  DuckDB's binder cannot route a bare STRUCT literal to this parser. The STRUCT branch removal
  is therefore a true no-op (dead code deleted, no behavior change), and the doc comment
  explaining why plus what reviving it would require is accurate. No regression found.

**CR-01** remains open per the binding "won't fix" decision and is correctly untouched —
`git diff` confirms `src/table_functions/ts_detrend.cpp` has zero changes across the entire
fix-pass commit range, so the unguarded NaN write in `TsDetectAmplitudeModulationFunction`
documented in iteration 1 is unchanged, as agreed. This is not re-raised as a finding per the
instructions carried into this review.

**IN-01** (stale "public, user-facing" comment on `_ts_forecast_native`'s internal macro name
in `src/table_functions/ts_forecast_native.cpp:849-851`) was explicitly left unaddressed —
the fix pass's `fix_scope` was `critical_warning`, which excludes Info-tier findings by
design. Confirmed the contradictory comment is still present verbatim. Carried forward below
as the sole remaining finding; it does not block `status: clean` since it is Info-only.

No new Critical or Warning findings were identified during this independent re-verification of
the fix pass or the surrounding call sites.

## Info

### IN-01: Comment calls an underscore-prefixed internal name "public, user-facing"

**File:** `src/table_functions/ts_forecast_native.cpp:849-851`
**Issue:** `RegisterTsForecastNativeFunction`'s comment reads "The public, user-facing name
`_ts_forecast_native` is now a ts_table_macros[] entry" — but `ts_macros.cpp`'s own comment on
that same macro entry states it is "INTERNAL ONLY" and exists only because three test files
call it directly, and `RegisterTsTableMacros` explicitly skips the `anofox_fcst_` alias for any
name starting with `_`. The two comments contradict each other on whether this name is public.
Carried forward unchanged from iteration 1; not fixed in this pass (Info-tier, out of
`fix_scope`).
**Fix:** Reword to "internal-only name" for consistency with `ts_macros.cpp`'s own description
of the same macro.

---

_Reviewed: 2026-10-24T00:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
