---
phase: 15-detection-decomposition-on-fdars-0-47
reviewed: 2026-10-14T12:00:00Z
depth: standard
files_reviewed: 12
files_reviewed_list:
  - src/table_functions/ts_mstl_decomposition_native.cpp
  - src/table_functions/ts_detrend.cpp
  - src/table_functions/ts_peaks.cpp
  - src/table_functions/ts_seasonality.cpp
  - src/table_functions/ts_decomposition.cpp
  - src/aggregate_functions/ts_classify_seasonality_agg.cpp
  - src/macros/ts_macros.cpp
  - crates/anofox-fcst-ffi/src/lib.rs
  - crates/anofox-fcst-core/src/seasonality.rs
  - crates/anofox-fcst-core/src/peaks.rs
  - crates/anofox-fcst-core/src/decomposition.rs
  - test/sql/ts_detection_reference.test
findings:
  critical: 2
  warning: 1
  info: 2
  total: 5
status: issues_found
---

# Phase 15: Code Review Report (iteration 3 — final re-review of the --auto fix loop)

**Reviewed:** 2026-10-14T12:00:00Z
**Depth:** standard
**Files Reviewed:** 12
**Status:** issues_found

## Summary

Reproduced every iteration-2 finding (CR-04, CR-05, WR-03) live against the rebuilt
extension (`build/release/duckdb -unsigned`, loading
`build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension`; binary
mtime confirmed newer than every touched source file, i.e. built at HEAD `282839f`).
All three are genuinely fixed, and none of the fixes introduced a regression:

- **CR-04**: `_ts_mstl_decomposition_native('fail', ...)` (the documented default,
  reached via both the internal function directly and the public
  `ts_mstl_decomposition_by` macro with `INSTALL/LOAD json`) now raises
  `Invalid Input Error: ts_mstl_decomposition_by: group '1': Insufficient data: need
  at least 2001 observations, got 20` instead of silently dropping the group.
  `'trend'` and `'none'` modes both preserve row count (2 groups in, 2 rows out),
  with the correct per-mode shape (trend-only vs. fully-empty) for the short group,
  confirmed live. The `ParseInsufficientDataMode` string-matching fix
  (`"trend"`→1, `"none"`→2) also verified correct — both modes now actually apply
  instead of silently behaving as `'fail'`.
- **CR-05**: `ts_detect_seasonality_changes`, `ts_detect_amplitude_modulation`, and
  `ts_analyze_peak_timing` now raise `Invalid Input Error: ...: Invalid parameter
  'period' = '...': Period must be a positive finite number` for `nan`, `inf`,
  `-inf`, `0.0`, and `-5.0`, confirmed live for every value. The root-cause FFI fix
  (switching from a hardcoded `ErrorCode::ComputationError` to `ffi_error_code(&e)`)
  does not misroute genuine computation failures: triggering an `InsufficientData`
  error (too-short series with a valid period) on all three functions still
  correctly returns `NULL`, not an exception — confirmed live. Valid inputs for all
  three still succeed, confirmed live.
- **WR-03**: `-Infinity` now produces the same unified "Period must be a positive
  finite number" message as every other invalid-period path (previously the stale
  "Period must be positive" wording), confirmed live on `ts_seasonal_strength`.
  Deleting the 7 stale FFI-level pre-checks did not change behavior for valid
  inputs across `ts_seasonal_strength`, `ts_seasonal_strength_windowed`,
  `ts_decompose_seasonal` (additive/multiplicative), and `ts_classify_seasonality` —
  all confirmed live with a valid period.

However, pulling on the "consistent messages across all 9 period-validating
functions" thread this task explicitly asked to verify surfaced that **one of the
9 functions named in the original CR-03 finding — `classify_seasonality` — was
never actually fixed for the ordinary (non-finite-unrelated) non-positive-period
case**, and that its **GROUP BY sibling has an entirely separate, untouched
implementation of the same defect class, including the identical undefined-behavior
cast CR-03 fixed elsewhere**. Both are pre-existing (not introduced by this
iteration's commits) but both are squarely inside the scope this review was asked
to re-verify, both are live-reproducible today, and both were missed by every
fix iteration's own regression tests (which only ever exercised `'nan'::DOUBLE`
for `ts_classify_seasonality`, never `0.0`/`-5.0`, and never touched
`ts_classify_seasonality_by`/`_agg` at all).

## Critical Issues

### CR-06 (new): `ts_classify_seasonality` silently returns `NULL` instead of raising for ordinary non-positive (but finite) periods — 0.0 and -5.0

**File:** `src/table_functions/ts_seasonality.cpp:422-429`
**Issue:** Iteration 1's CR-03 fix (`eba3904`) added a `std::isfinite(period)` pre-check
immediately before the pre-existing `period <= 0` guard, to stop a NaN/Infinity
period hitting a `static_cast<size_t>(2 * period)` UB cast. It did not touch the
pre-existing guard itself:
```cpp
if (!std::isfinite(period)) {
    throw InvalidInputException(
        "ts_classify_seasonality: Period must be a positive finite number, got %f", period);
}
if (period <= 0 || values.size() < static_cast<size_t>(2 * period)) {
    FlatVector::SetNull(result, row_idx, true);   // <-- period == 0.0 or -5.0 lands here
    continue;
}
```
So `ts_classify_seasonality` raises for NaN/±Infinity (the `isfinite` branch) but
silently returns `NULL` for an ordinary zero or negative period, unlike all 8 of its
siblings (`ts_seasonal_strength`, `ts_seasonal_strength_windowed`,
`ts_decompose_seasonal` ×2, `ts_detect_seasonality_changes`,
`ts_detect_amplitude_modulation`, `ts_analyze_peak_timing`, and the
`_ts_mstl_decomposition` family is out of scope here), every one of which was
verified live in this iteration to raise `Invalid Input Error: ... Period must be a
positive finite number` for `0.0` and `-5.0` identically. Iteration 2's review
incorrectly credited `ts_classify_seasonality` as one of "6 ... wrappers [that]
already throw `InvalidInputException` on `error.code == INVALID_INPUT`" — true for
NaN/Infinity, false for 0/negative, because this guard never reaches the FFI call
(and hence the `error.code` check) at all for those values. Reproduced live:
```sql
SELECT ts_classify_seasonality(LIST(value), 0.0)  FROM t;   -- NULL, no error
SELECT ts_classify_seasonality(LIST(value), -5.0) FROM t;   -- NULL, no error
SELECT ts_classify_seasonality(LIST(value), 'nan'::DOUBLE) FROM t;  -- correctly raises
```
This is the exact "NULL vs error consistency" defect class CR-03/CR-05 were meant
to close, in the one function the fix iterations' own regression tests never
exercised with `0.0`/`-5.0` (`test/sql/ts_detection_reference.test`'s new
`foreach period nan inf -inf -5.0 0.0` loop, added in `282839f`, covers only the
three CR-05 functions — not `ts_classify_seasonality`).
**Fix:** Change the guard to match the pattern used elsewhere in this phase's fixes:
```cpp
if (period <= 0) {
    throw InvalidInputException(
        "ts_classify_seasonality: Invalid parameter 'period' = '%f': Period must be a positive finite number", period);
}
if (values.size() < static_cast<size_t>(2 * period)) {
    FlatVector::SetNull(result, row_idx, true);
    continue;
}
```
(or simply delete this C++-level guard entirely and let the already-correct
core-level `anofox_ts_classify_seasonality` → `classify_seasonality` check in
`crates/anofox-fcst-core/src/seasonality.rs` do the validation, mirroring WR-03's
"one source of truth" approach — the FFI entry point's `ffi_error_code` mapping
already exists post-`86802cc`.)

### CR-07 (new): `ts_classify_seasonality_by` / `ts_classify_seasonality_agg` has a second, untouched copy of the same defect — NaN, 0.0, and -5.0 all silently produce a NULL row, and the NaN case re-introduces the exact UB cast CR-03 fixed in the scalar sibling

**File:** `src/aggregate_functions/ts_classify_seasonality_agg.cpp:142` (`TsClassifySeasonalityAggFinalize`), reached via the `ts_classify_seasonality_by` macro (`src/macros/ts_macros.cpp:532-555`)
**Issue:** The aggregate-function backend for the GROUP BY macro has its own,
independent period-validation code that is a near-exact copy of the scalar
function's pre-`eba3904` code — but **without even the `isfinite` guard** that
`eba3904` added to the scalar sibling:
```cpp
double period = data.period;

if (period <= 0 || data.values.size() < static_cast<size_t>(2 * period)) {
    FlatVector::SetNull(result, row, true);
    continue;
}
```
`period <= 0` is `false` for `NaN` (IEEE-754), so a `NaN` period reaches
`static_cast<size_t>(2 * period)` directly — the identical undefined-behavior cast
`eba3904`'s own commit message explicitly called out and fixed in
`ts_seasonality.cpp`'s `TsClassifySeasonalityFunction`, but never touched here.
Reproduced live (no crash observed in this release build, but the C++ standard
leaves the cast's result unspecified/UB regardless):
```sql
CREATE TABLE sales AS
  SELECT 1 AS product_id, DATE '2024-01-01' + INTERVAL (i) DAY AS ds, (10 + i % 12)::DOUBLE AS v
  FROM range(120) r(i);

SELECT * FROM ts_classify_seasonality_by('sales', product_id, ds, v, 'nan'::DOUBLE);
-- one row, product_id=1, every other column NULL -- no error
SELECT * FROM ts_classify_seasonality_by('sales', product_id, ds, v, 0.0);
-- same: NULL row, no error
SELECT * FROM ts_classify_seasonality_by('sales', product_id, ds, v, -5.0);
-- same: NULL row, no error
```
This file was not touched by any of iteration 1's or iteration 2's fix commits and
is not covered by any of the new regression assertions (which only exercise the
scalar `ts_classify_seasonality` and the three CR-05 scalar functions). It is
pre-existing (confirmed via `git log --follow`, present since `98cc976` /
Issue #74, predating Phase 15), but it is live-reproducible today and sits squarely
inside the "NULL vs error consistency across the period-validating SQL surface"
scope this re-review was asked to verify — the macro silently returns a row of all
`NULL`s for an invalid period instead of raising, for the one `_by` macro in this
family that has its own duplicated validation logic rather than delegating to the
already-fixed scalar function.
**Fix:** Mirror CR-06's fix (reject `!std::isfinite(period) || period <= 0` with an
`InvalidInputException` naming the group, before the `2 * period` cast), or better,
eliminate the duplication: have `ts_classify_seasonality_agg`'s finalize call into
the already-fixed FFI entry point's validation path rather than re-implementing the
`period <= 0` check inline a second time.

## Warnings

### WR-04 (new): `ts_classify_seasonality`'s own NaN/Infinity error message format diverges from the unified wording used by its 8 siblings

**File:** `src/table_functions/ts_seasonality.cpp:422-424`
**Issue:** For non-finite periods, `ts_classify_seasonality` raises its own
locally-formatted message:
```
Invalid Input Error: ts_classify_seasonality: Period must be a positive finite number, got nan
```
while every other period-validating function (via the FFI/core-level check) raises:
```
Invalid Input Error: ts_seasonal_strength: Invalid parameter 'period' = 'NaN': Period must be a positive finite number
```
Confirmed live for both wordings side by side. Behavior is correct in both cases
(both raise), so this is message-format drift, not a functional defect — but it is
exactly the "consistent messages" axis this re-review was asked to check, and if
CR-06 is fixed by deleting this C++-level pre-check entirely (as suggested above),
this finding resolves automatically as a side effect.
**Fix:** If CR-06 is fixed by deleting the local pre-check, no separate action
needed. If the local pre-check is kept, reword to match the unified format:
`"ts_classify_seasonality: Invalid parameter 'period' = '%s': Period must be a positive finite number"`.

## Info

### IN-01 (carried over, unchanged): `estimate_period_regression_ts`/`regression_period_rss` remain dead code from an SQL/FFI perspective

**File:** `crates/anofox-fcst-core/src/periods.rs:342,581`
No change from iteration 2 — still `pub`, still no FFI export. No action required.

### IN-02 (carried over, resolved by CR-04's fix): `ts_mstl_decomposition_by`'s doc comment

**File:** `src/macros/ts_macros.cpp:495`
CR-04 was fixed as "raise for 'fail', emit a row for 'trend'/'none'", which matches
the existing docstring's own description of the three modes — no documentation
change required, per this finding's own original guidance.

---

_Reviewed: 2026-10-14T12:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
_Iteration: 3 (final re-review of the --auto fix loop)_
