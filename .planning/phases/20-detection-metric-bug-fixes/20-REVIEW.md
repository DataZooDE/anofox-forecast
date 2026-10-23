---
phase: 20-detection-metric-bug-fixes
reviewed: 2026-10-23T22:11:04Z
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
  critical: 2
  warning: 5
  info: 1
  total: 8
status: issues_found
---

## User Decisions (2026-10-24, binding for the fix pass)

- **CR-01: WON'T FIX.** `ts_detect_amplitude_modulation` keeps returning NaN for a degenerate `seasonal_strength`; this is the deliberate 20-01 decision ("amplitude-modulation keeps NaN"). Do not change it.
- **CR-02: FIX, option "default threshold".** When `deseasonalize = true` and `threshold` is unset, the effective threshold is 0.5. Decide this in Rust (`ChangepointOptions` / `detect_changepoints_bocpd_with_options`), not in C++, so both SQL surfaces (`ts_detect_changepoints_by` and `ts_detect_changepoints_agg`) inherit it. An explicit `threshold` always wins. With `deseasonalize` false/unset, results stay byte-identical (the md5 pins in detection_decomposition.test must stay green). This option is unreleased (CHANGELOG `[Unreleased]` only), so no compatibility concern. Add a Rust unit test and a SQL pin: on a noisy weekly sine (amplitude 5) + level shift (+8 at i=90, n=180), `deseasonalize` alone flags only the true shift, the same as `deseasonalize` + `threshold` 0.5. Update `docs/api/06-changepoint-detection.md`, the detection SKILL.md, the CHANGELOG entry and the example to state the 0.5 default, and remove the "can still flag weekly-spaced points" caveat.
- **WR-03:** no code change expected. Record it in deferred-items.md: cross-line verification of TsCollectFinalizeBarrier on the v1.4.5/v1.5.6 lines happens through the PR CI run.

# Phase 20: Code Review Report

**Reviewed:** 2026-10-23T22:11:04Z
**Depth:** standard
**Files Reviewed:** 34
**Status:** issues_found

## Summary

Phase 20 does two things: (1) adds a shared `TsCollectFinalizeBarrier` (`ts_collect_finalize.hpp`) that replaces a hand-rolled atomic spin-wait barrier in 18 collect-then-finalize native table functions, paired with a new `_ts_passthrough` identity in-out function and `(SELECT * FROM _ts_passthrough(source) ORDER BY ALL)` / `(... FROM query_table(source) ORDER BY ALL)` single-pipeline-breaker wrapping in the public macros, to fix #285 (UNION ALL sources silently truncated); and (2) extends BOCPD changepoint detection with a shared `ChangepointOptions`/`ChangepointOptionsFFI` struct carrying `hazard_lambda`/`threshold`/`deseasonalize`/`seasonal_period`, unifying `ts_detect_changepoints_by` and `ts_detect_changepoints_agg` on one parser (`ts_changepoints_params.hpp`) and one FFI entry point (`anofox_ts_detect_changepoints_bocpd_opts`), plus a `#286`/BUG-04 fix that makes `classify_seasonality`/`detect_amplitude_modulation` source `seasonal_strength` from the same STL-based `stl_strengths` value `ts_stats_by` uses instead of fdars's own raw-series strength.

The barrier refactor is mechanically consistent across all 18 call sites (verified via `RegisterLocal`/`AssertCollecting`/`ArriveAtFinal` grep), and the changepoint params unification is a genuine improvement (previously `ts_detect_changepoints_agg`'s params parsing was a documented no-op). However, this review found one correctness regression that crosses a file outside the explicit diff (`ts_detrend.cpp`'s `ts_detect_amplitude_modulation` was not updated to match a NaN→NULL contract this same phase introduced for its two sibling call sites), and reproduced — with a standalone Rust test run against the actual crate code — the exact "deseasonalize floods the series with spurious changepoints" behavior the orchestrator observed on the built extension, tracing it to a ~70x collapse in the robust noise scale once the seasonal component is removed. Several smaller robustness/consistency gaps round out the findings below.

## Critical Issues

### CR-01: `ts_detect_amplitude_modulation` can leak raw NaN into a DOUBLE column instead of SQL NULL

**File:** `src/table_functions/ts_detrend.cpp:839` (not in the diff, but regressed by it; see `crates/anofox-fcst-core/src/seasonality.rs:889-911`)
**Issue:** This phase changes `detect_amplitude_modulation` (seasonality.rs) to source `seasonal_strength` from `stl_strengths(values, &[period.round()]).seasonality_strength.unwrap_or(f64::NAN)` instead of fdars's own raw-series strength (#286, BUG-04). The Rust doc comment for this function explicitly states: *"`seasonal_strength` is `NaN` when `stl_strengths` has no usable period... the FFI/C++ layer maps `NaN` to SQL `NULL`"*. That mapping was in fact added to the diff — but only at two of this function's three call sites:
- `src/table_functions/ts_seasonality.cpp:503` (`TsClassifySeasonalityFunction`) — guarded with `std::isnan(...) → FlatVector::SetNull`.
- `src/aggregate_functions/ts_classify_seasonality_agg.cpp:224` (`TsClassifySeasonalityAggFinalize`) — same guard, same diff.
- `src/table_functions/ts_detrend.cpp:839` (`TsDetectAmplitudeModulationFunction`, the public `ts_detect_amplitude_modulation` scalar function) — **unguarded**: `FlatDataMut<double>(StructEntry(children[1]))[row_idx] = am_result.seasonal_strength;` writes the raw FFI double straight through.

Both of the two "fixed" call sites only reuse `class_result.seasonal_strength` (from `classify_seasonality`) for their own `seasonal_strength` output field, so the dangerous value never reaches a result row there even without the explicit guard — the guard added in those two files is in fact for the SAME `classify_seasonality` call, not two independent fixes for `detect_amplitude_modulation`. `ts_detect_amplitude_modulation` is the *only* place that puts `detect_amplitude_modulation`'s own `seasonal_strength` field directly into a user-visible column, and it is exactly the one place this phase did not touch. A user running `ts_detect_amplitude_modulation(values, period)` on a series where STL finds no usable period (or a degenerate decomposition) will now get a literal `nan` double in `seasonal_strength` instead of `NULL` — breaking `IS NULL` checks, `COALESCE`, and any downstream arithmetic, and contradicting the function's own doc comment.
**Fix:**
```cpp
// src/table_functions/ts_detrend.cpp, TsDetectAmplitudeModulationFunction
if (std::isnan(am_result.seasonal_strength)) {
    FlatVector::SetNull(StructEntry(children[1]), row_idx, true);
} else {
    FlatDataMut<double>(StructEntry(children[1]))[row_idx] = am_result.seasonal_strength;
}
```
(`<cmath>` is already included in this file.)

### CR-02: `deseasonalize: true` without `threshold` makes changepoint detection materially *worse*, and the docs understate how much

**File:** `crates/anofox-fcst-core/src/changepoint.rs:626-662` (`deseasonalize_for_changepoints`), consumed via `src/include/ts_changepoints_params.hpp` / `src/table_functions/ts_changepoints.cpp` / `src/aggregate_functions/ts_changepoints_agg.cpp`; documented at `docs/api/06-changepoint-detection.md:303-307`
**Issue:** Reproduced directly against the crate (temporary unit test, reverted after use) with a 180-point series matching the scenario reported against the built extension (weekly sine, amplitude 5, one level shift of +8 at `i=90`, light noise):

| Options | `changepoints` |
|---|---|
| default (`deseasonalize: false`, `threshold: None`) | `[91]` |
| `deseasonalize: true, seasonal_period: Some(7)`, `threshold: None` | `[15, 17, 31, 37, 47, 52, 54, 90, 111, 118, 134, 142, 150, 153, 158, 160]` |
| `deseasonalize: true, seasonal_period: Some(7)`, `threshold: Some(0.5)` | `[90]` |

`robust_noise_scale` on the raw series is `3.34`; on the deseasonalized residual it is `0.29` (and drops further, to `0.045`, as input noise is reduced) — a **>10x, up to ~70x**, collapse. BOCPD's Student-t predictive standardizes every point by this scale, but the hazard prior (`h = 1/hazard_lambda`) is a fixed constant independent of it. Once the dominant seasonal variance is subtracted out, the residual's own small, structurally-imperfect leftovers (STL/MSTL's loess-based seasonal fit does not exactly reconstruct a pure sinusoid, especially with `n/period ≈ 25` cycles) become large *relative to* the now-tiny noise floor, and the MAP run-length-reset heuristic (used whenever `threshold` is `None`) treats each of them as a new run — flagging changepoints scattered across the *entire* series, not just near the real shift.

The shipped documentation acknowledges a *version* of this ("`deseasonalize` alone (no `threshold`) can still flag weekly-spaced points around a real shift... extra flags at roughly one-period intervals") but materially understates its scope and severity: the actual behavior is not "extra flags near the real shift" but a near-total breakdown of the heuristic across the whole series (16-21 flagged points out of 180, versus 1 for the non-deseasonalized default). A user who tries `MAP{'deseasonalize': 'true'}` alone — a reasonable first attempt, since every other key in this params MAP has a usable standalone default — gets a result that is *worse* than not using the feature at all, with no error, warning, or validation anywhere in the SQL/FFI/Rust stack steering them toward also setting `threshold`.
**Fix:** Either (a) tighten the documentation to state plainly that `deseasonalize: true` is only usable together with `threshold` and is expected to be *worse than the default* otherwise (not just "can still flag extra points"), or (b) enforce the pairing at the parameter-validation layer — e.g. have `ParseChangepointParamSet` / `detect_changepoints_bocpd_with_options` raise `InvalidParameter` when `deseasonalize == true && threshold.is_none()`, forcing callers to make an explicit choice (a `threshold` of e.g. `0.0`/`1.0` can still opt back into "flag everything"/"flag nothing" if that's genuinely wanted). Given the project's "Permanent SQL API backward compat" rule, (a) is the safe near-term fix; (b) would need a deprecation-window discussion since it changes already-shipped behavior for `deseasonalize: true` + unset `threshold`.

## Warnings

### WR-01: New FFI export doesn't check `malloc()` for null before writing through it

**File:** `crates/anofox-fcst-ffi/src/lib.rs:3498-3508` (`anofox_ts_detect_changepoints_bocpd_opts`)
**Issue:** `let is_cp_ptr = malloc(n * std::mem::size_of::<bool>()) as *mut bool;` is immediately dereferenced in a loop (`*is_cp_ptr.add(i) = cp;`) with no null check; same pattern for `prob_ptr` and `idx_ptr`. On allocation failure this is a null-pointer write (crash) instead of the `ErrorCode::AllocationError` path the error enum already defines. The two pre-existing sibling functions (`anofox_ts_detect_changepoints`, `anofox_ts_detect_changepoints_bocpd`) have the same gap, but this phase added a third copy of it rather than fixing it once.
**Fix:**
```rust
let is_cp_ptr = malloc(n * std::mem::size_of::<bool>()) as *mut bool;
if is_cp_ptr.is_null() {
    if !out_error.is_null() {
        (*out_error).set_error(ErrorCode::AllocationError, "malloc failed for is_changepoint");
    }
    return false;
}
```
(repeat for `prob_ptr`, `idx_ptr`), or factor a small `checked_malloc` helper shared by all three functions.

### WR-02: Asymmetric null-guard on sibling fields from the same `BocpdResult`

**File:** `src/aggregate_functions/ts_changepoints_agg.cpp:213-215`
**Issue:**
```cpp
cp_child_data[current_size + j] = cp_result.is_changepoint[j];              // unguarded
prob_child_data[current_size + j] = cp_result.changepoint_probability ?    // guarded
    cp_result.changepoint_probability[j] : 0.0;
```
Both arrays are populated by the exact same `anofox_ts_detect_changepoints_bocpd_opts` call and are null under the exact same failure mode (WR-01, or any other path that leaves `is_changepoint` null on success). The inconsistent guard means a future `is_changepoint == nullptr` state (e.g. once WR-01 is fixed to allow this path to legitimately report partial failure) crashes here first.
**Fix:**
```cpp
cp_child_data[current_size + j] = cp_result.is_changepoint ? cp_result.is_changepoint[j] : false;
```

### WR-03: `TsCollectFinalizeBarrier` correctness depends on an internal DuckDB scheduling invariant verified only against v2.0.0, but is now load-bearing for 18 native functions across 3 shipped DuckDB lines

**File:** `src/include/ts_collect_finalize.hpp`
**Issue:** The header's own comment states the design was "checked against duckdb v2.0.0" for two specific internal facts (local-state creation ordering in `PipelineExecutor`'s constructor; one `FinalExecute` call per executor once input is exhausted). This project builds and ships the *same* C++ source against v2.0.0, v1.5.6, and v1.4.5 LTS (per `CLAUDE.md`'s Constraints section). Nothing in the reviewed files demonstrates the same two facts hold on the 1.x lines. The failure mode if they don't is not a crash but a **silent empty result**: if any registered local state (counted via `RegisterLocal()`) never reaches `in_out_function_final()` — e.g. it's cancelled by the scheduler, or an error on a sibling operator unwinds that one task without ever calling finalize on a successfully-collected one — `arrived` never reaches `registered`, no local state is ever the "winner", and the query returns zero rows with no error. This pattern is now used by 18 of the extension's native table functions (`grep -rl TsCollectFinalizeBarrier src/`), so a scheduling difference on an LTS line would be a wide-blast-radius, hard-to-detect defect (looks like "no changepoints found" / "no rows", not a crash or exception).
**Fix:** Add (or confirm existing, if elsewhere) CI coverage that runs at least one collect-then-finalize native under a forced multi-threaded, multi-morsel workload (not just correctness tests) on all three DuckDB lines, not just the default dev line, and consider making `AssertCollecting`'s defence-in-depth model symmetric: if a query-level timeout/row-count sanity check is feasible, flag "zero rows AND no error" as a distinguishable condition during testing specifically for these natives.

### WR-04: Key-name typos silently defeat the "typo'd value must not silently disable the feature" protection

**File:** `src/include/ts_changepoints_params.hpp:56-152`
**Issue:** The header deliberately makes `threshold`/`deseasonalize`/`seasonal_period` throw on an *unparsable value* for a correctly-named key ("a typo'd threshold silently disabling the feature would be a worse silent-drift bug than erroring"). But `read_key` does an exact string match and returns `Value()` (null) for any key it doesn't recognize — so `MAP{'Threshold': '0.5'}`, `MAP{'treshold': '0.5'}`, or `MAP{'seasonalPeriod': '7'}` are all silently treated as "key absent, use default" with no error, which is the exact "silent-drift" failure mode the value-level strictness was written to prevent, just one layer up (key name instead of key value). This is consistent with the header's documented "unknown keys are ignored" policy, but that policy is in tension with the stated rationale for the value-level strictness right next to it.
**Fix:** Either document this tension explicitly (unknown-key leniency is a deliberate, separate tradeoff from value-level strictness and typo'd keys are the caller's responsibility), or, if key-typo protection is desired, validate that every key in the input MAP/STRUCT is one of the four recognized names and throw naming the unrecognized key.

### WR-05: `ParseChangepointParamSet`'s STRUCT branch appears unreachable from either declared call site

**File:** `src/include/ts_changepoints_params.hpp:72-80`
**Issue:** `read_key` branches on `params_value.type().id() == LogicalTypeId::STRUCT` to support a STRUCT-typed params argument, per the struct's own doc comment ("Reads ... from a MAP(VARCHAR, VARCHAR) or STRUCT Value"). But both of this parser's call sites declare the parameter's logical type as `MAP(LogicalTypeId::VARCHAR, LogicalTypeId::VARCHAR)` (`_ts_detect_changepoints_by_native`'s table function signature in `ts_changepoints.cpp`, and `ts_detect_changepoints_agg`'s `AggregateFunction` signature in `ts_changepoints_agg.cpp`), and the public `ts_detect_changepoints_by` macro additionally forces `params::MAP(VARCHAR, VARCHAR)` in its SQL text before the native ever sees it (`ts_macros.cpp`). No `test/sql` case was found passing a bare STRUCT literal (e.g. `{'hazard_lambda': 100}`) through either consumer — every test uses `MAP{...}` literal syntax. If the STRUCT branch is in fact unreachable, it's untested dead code; if it is reachable in some DuckDB-cast-coercion path not covered by the files reviewed, that path has no test coverage either.
**Fix:** Add a `test/sql` case passing a bare STRUCT literal through `ts_detect_changepoints_by`/`ts_detect_changepoints_agg` to confirm the branch is reachable and correct, or remove it and update the doc comment if it is not.

## Info

### IN-01: Comment calls an underscore-prefixed internal name "public, user-facing"

**File:** `src/table_functions/ts_forecast_native.cpp:849-851`
**Issue:** `RegisterTsForecastNativeFunction`'s comment reads "The public, user-facing name `_ts_forecast_native` is now a ts_table_macros[] entry" — but `ts_macros.cpp`'s own comment on that same macro entry states it is "INTERNAL ONLY" and exists only because three test files call it directly, and `RegisterTsTableMacros` explicitly skips the `anofox_fcst_` alias for any name starting with `_`. The two comments contradict each other on whether this name is public.
**Fix:** Reword to "internal-only name" for consistency with `ts_macros.cpp`'s own description of the same macro.

---

_Reviewed: 2026-10-23T22:11:04Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
