---
phase: 15-detection-decomposition-on-fdars-0-47
reviewed: 2026-10-14T08:52:10Z
depth: standard
files_reviewed: 13
files_reviewed_list:
  - src/table_functions/ts_decomposition.cpp
  - src/table_functions/ts_periods.cpp
  - src/table_functions/ts_seasonality.cpp
  - src/table_functions/ts_detrend.cpp
  - src/table_functions/ts_peaks.cpp
  - src/table_functions/ts_mstl_decomposition_native.cpp
  - crates/anofox-fcst-core/src/periods.rs
  - crates/anofox-fcst-core/src/detrending.rs
  - crates/anofox-fcst-core/src/seasonality.rs
  - crates/anofox-fcst-core/src/peaks.rs
  - crates/anofox-fcst-core/src/changepoint.rs
  - crates/anofox-fcst-ffi/src/lib.rs
  - test/sql/ts_detection_reference.test
findings:
  critical: 2
  warning: 1
  info: 1
  total: 4
status: issues_found
---

# Phase 15: Code Review Report (iteration 2 — re-review of --auto fix loop)

**Reviewed:** 2026-10-14T08:52:10Z
**Depth:** standard
**Files Reviewed:** 13
**Status:** issues_found

## Summary

Re-reviewed iteration 1's fix commits (`d381dfb`, `80ea2cf`, `eba3904`, `8393cbb`, `5d300fd`) against the built extension (`build/release/duckdb -unsigned`, loading `build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension`, which was confirmed rebuilt at the HEAD commit `5d300fd`). All five previously-reported findings' literal reproduction steps were re-run live:

- **CR-01** (uninitialized memory in `_ts_mstl_decomposition` under `insufficient_data='none'`): genuinely fixed for the scalar function itself — `_ts_mstl_decomposition(..., 'none', [1000])` on a 20-row series now returns a non-NULL struct with empty `trend`/`remainder`/`periods` lists, not stale buffer memory. However, chasing the review brief's explicit "is this consistent with the native sibling?" question surfaced a **new, more severe sibling inconsistency** — see CR-04 below.
- **CR-02** (unbounded/negative `n_frequencies` on `ts_lomb_scargle`): genuinely fixed. Verified live at every requested boundary (`-1`, `0`, `1`, `2`, `1_000_000`, `1_000_001`, `2_000_000_000`): negative is rejected before the cast with a clear error, `0` behaves as the pre-existing "use default" sentinel, `1` is rejected (lower bound), `2`..`1_000_000` succeed, `1_000_001` and `2_000_000_000` are rejected (upper bound). No process abort, no silent wraparound.
- **CR-03** (`period <= 0.0` never rejects `NaN`/`±Infinity` across ~9 functions): the Rust-core fix itself is correct and complete — all 9 functions (confirmed via `cargo test`, including the 5 reordered to avoid the `period as usize` overflow on `+Infinity`) now reject non-finite periods with `InvalidParameter`. But re-testing **every one of the 9 functions' SQL surface**, not just the 3 the fixer added regression tests for, surfaced that **3 of the 9 SQL wrappers silently discard this now-correct error and return NULL instead of raising it** — see CR-05 below. This means the fix is incomplete at the SQL boundary for exactly the "NULL vs error consistency" axis the original CR-03 finding was about.
- **WR-01** (`period_min` validation in `estimate_period_regression_ts`): genuinely fixed, confirmed via `cargo test`; still correctly unreachable from SQL (no FFI export), as claimed.
- **WR-02** (PELT pruning worst case): doc-only change as planned, confirmed via diff; no behavior change, matches the original fix guidance ("no action required").

Two new Critical findings and one new Info finding surfaced during this deeper re-verification; one new Warning (stale duplicate validation at the FFI boundary) rounds out the report. IN-01 from iteration 1 remains unchanged (context note, no action needed).

## Critical Issues

### CR-04 (new): `ts_mstl_decomposition_by` silently drops groups with insufficient data — including under the *default* `insufficient_data='fail'` mode

**File:** `src/table_functions/ts_mstl_decomposition_native.cpp:326-358` (the `else { ... continue; }` branch), reached via `src/macros/ts_macros.cpp`'s `ts_mstl_decomposition_by` macro
**Issue:** CR-01's fix made the *scalar* `_ts_mstl_decomposition` correctly return a non-NULL struct with empty lists when decomposition isn't applied. The review brief asked whether this is "consistent with the native sibling" (`_ts_mstl_decomposition_native`, the table-in-out function backing `ts_mstl_decomposition_by`). It is not — it's worse than an inconsistency in NULL-vs-empty-list representation: **the native sibling drops the group's row entirely**, for both `insufficient_data='none'` *and* the **default** `insufficient_data='fail'`:

```cpp
if (success && mstl_result.decomposition_applied) {
    // ... populate row ...
} else if (bind_data.insufficient_data_mode == 1) {
    // skip mode - output empty arrays (row already has empty vectors)
} else {
    // fail mode or pad mode with failure - skip this group
    continue;   // <-- row for this group is never pushed to gstate.results
}
```

Mode `1` ("trend") correctly keeps a row. Modes `0` ("fail", the default) and `2` ("none") both fall into the `else` branch and `continue`, which means the group is **silently absent from the result set** — not even a NULL row, just a vanished group, with no error and no diagnostic.

Reproduced live against the built extension (`ts_mstl_decomposition_by`'s own documented default, no explicit `insufficient_data` key):

```sql
CREATE TABLE sales AS
  SELECT 1 AS product_id, DATE '2020-01-01' + INTERVAL (x) DAY AS date, x::DOUBLE AS qty FROM range(20) t(x)
  UNION ALL
  SELECT 2 AS product_id, DATE '2020-01-01' + INTERVAL (x) DAY AS date, x::DOUBLE AS qty FROM range(3000) t(x);

SELECT product_id, len(trend), len(periods)
FROM ts_mstl_decomposition_by('sales', product_id, date, qty, MAP{'seasonal_periods': '[1000]'});
-- Result: only product_id=2 appears. product_id=1 (20 rows, period 1000 unusable) has vanished.
```

This directly contradicts the macro's own documented contract ("`insufficient_data` is 'fail' (default), 'trend', or 'none'" — nothing in the docstring says groups are dropped) and the general expectation that a per-group decomposition over a `GROUP BY`-shaped source preserves one output row per input group. It is not a regression introduced by this phase's fix commits (`ts_mstl_decomposition_native.cpp` was not touched by any of the 5 commits), but it is squarely the defect class — and exactly the file/function pair — the re-review was asked to cross-check, and it is a worse failure mode (silent row loss from the default code path) than the bug CR-01 fixed.

**Fix:** Mirror the scalar function's CR-01 fix in the native finalize loop: push a row with empty `trend`/`seasonal`/`remainder`/`periods` for modes `0` and `2` when `decomposition_applied` is false, instead of `continue`-ing past the group. If silently dropping rows is ever intentional, it must be documented in the macro's description and probably gated behind an explicit opt-in, not the default.

### CR-05 (new): 3 of the 9 period-validating SQL functions discard the CR-03 `InvalidParameter` error and return NULL instead

**File:** `src/table_functions/ts_seasonality.cpp:562-565` (`TsDetectSeasonalityChangesFunction`), `:823-826` (`TsDetectAmplitudeModulationFunction`); `src/table_functions/ts_peaks.cpp:228-234` (`TsAnalyzePeakTimingFunction`)
**Issue:** CR-03's Rust-core fix is correct (verified via `cargo test` and via the 6 SQL wrappers that do propagate it — `ts_seasonal_strength`, `ts_seasonal_strength_windowed`, `ts_decompose_seasonal` [additive and multiplicative], `ts_classify_seasonality`). Those 6 wrappers all contain:

```cpp
if (!success) {
    if (error.code == INVALID_INPUT) {
        throw InvalidInputException(...);
    }
    FlatVector::SetNull(result, row_idx, true);
    continue;
}
```

But `ts_detect_seasonality_changes`, `ts_detect_amplitude_modulation`, and `ts_analyze_peak_timing` are missing the `error.code == INVALID_INPUT` branch entirely:

```cpp
if (!success) {
    FlatVector::SetNull(result, row_idx, true);
    continue;
}
```

This unconditionally converts *every* failure — including the now-correct `InvalidParameter` for a NaN/Infinity period — into a silent NULL, for these three functions only. Reproduced live against the built extension:

```sql
SELECT ts_detect_seasonality_changes(LIST(value), 'nan'::DOUBLE)  FROM t;  -- NULL, no error
SELECT ts_detect_amplitude_modulation(LIST(value), 'nan'::DOUBLE) FROM t;  -- NULL, no error
SELECT ts_analyze_peak_timing(LIST(value), 'nan'::DOUBLE)         FROM t;  -- NULL, no error
```
and the same three return NULL (not an error) for an ordinary negative or zero period too (`-5.0`, `0.0`), confirming this isn't specific to non-finite values — the gap is structural, not a residual finiteness edge case. All 6 of the other wrappers correctly throw `Invalid Input Error: ... Period must be a positive finite number` for the identical NaN input.

This is not a regression from the phase's fix commits (none of the three files/functions involved were touched by `eba3904` beyond the unrelated `ts_classify_seasonality` pre-check), but it directly undermines CR-03's stated goal — "a `NaN` period argument silently produce[s] plausible-but-wrong results instead of the documented `InvalidParameter` error" — for 3 of the 9 functions the original finding named, and it is exactly the "NULL vs error consistency" check the re-review was asked to verify. The iteration-1 fix report's own SQL regression tests only cover `ts_seasonal_strength`, `ts_decompose_seasonal`, and `ts_classify_seasonality` — the three that already worked — leaving this gap undetected.

**Fix:** Add the same `if (error.code == INVALID_INPUT) throw InvalidInputException(...);` branch to all three wrappers, mirroring the other 6:
```cpp
if (!success) {
    if (error.code == INVALID_INPUT) {
        throw InvalidInputException("ts_detect_seasonality_changes: %s",
            error.message ? error.message : "invalid input");
    }
    FlatVector::SetNull(result, row_idx, true);
    continue;
}
```
(and the equivalent for `ts_detect_amplitude_modulation` / `ts_analyze_peak_timing`). Add SQL regression assertions for all three using `'nan'::DOUBLE`, matching the three that were already covered.

## Warnings

### WR-03 (new): Stale duplicate `period <= 0.0` pre-checks at the FFI boundary were not updated alongside CR-03, producing an inconsistent error message for `-Infinity`

**File:** `crates/anofox-fcst-ffi/src/lib.rs:2315,2490,2574,2641,2715,2813,2980` (`anofox_ts_analyze_peak_timing`, `anofox_ts_decompose`, `anofox_ts_seasonal_strength`, `anofox_ts_seasonal_strength_windowed`, `anofox_ts_classify_seasonality`, `anofox_ts_detect_seasonality_changes`, `anofox_ts_detect_amplitude_modulation`)
**Issue:** All 7 of these FFI entry points have their own `if period <= 0.0 { set_error(InvalidInput, "Period must be positive"); return false; }` pre-check, duplicating the validation that CR-03 fixed one layer down in the corresponding `anofox-fcst-core` function. CR-03's commit did not touch this file. Correctness is not affected for `NaN` or `+Infinity` (both fail this stale check's `<= 0.0` test, same IEEE-754 reason CR-03 fixed, and fall through to the now-correct core-level check), but `-Infinity` *is* caught by this stale pre-check first (since `-inf <= 0.0` is true), so it is rejected with the **old** message "Period must be positive" instead of the fixed message "Period must be a positive finite number" that every other invalid-period path now produces. Confirmed live: `ts_seasonal_strength(values, '-inf'::DOUBLE)` → `Invalid Input Error: ts_seasonal_strength: Period must be positive` (old wording), vs. `ts_seasonal_strength(values, 'nan'::DOUBLE)` → `...Period must be a positive finite number` (new wording). No incorrect SQL-visible behavior results today, but this is dead/duplicate validation logic that a future refactor of the core-level checks could silently desync from (e.g. if the core bound is ever loosened, this FFI-level gate would still reject values the core now accepts, or vice versa for edge cases this review didn't exercise).
**Fix:** Either delete these 7 FFI-level pre-checks (the core-level check now does strictly more, correctly) or update them to `if !period.is_finite() || period <= 0.0` with the matching message, so there's exactly one source of truth for "period must be a positive finite number," consistent with the rest of this phase's fix direction.

## Info

### IN-02 (new): `ts_mstl_decomposition_by`'s doc comment doesn't mention group-dropping behavior

**File:** `src/macros/ts_macros.cpp:495` (the `ts_mstl_decomposition_by` `FunctionDescription`)
**Issue:** Context for CR-04 — the macro's description string ("`insufficient_data` is 'fail' (default), 'trend', or 'none'") gives no indication that 'fail' and 'none' silently remove groups from the output rather than erroring or returning a NULL/empty row. Once CR-04 is fixed this note is moot; listed separately in case CR-04 is deliberately *not* fixed (e.g. row-dropping is kept as intentional behavior for 'fail'/'none') — in that case the docstring needs to say so explicitly.
**Fix:** None required if CR-04 is fixed as described; otherwise, update the docstring to state the row-dropping behavior explicitly.

### IN-01 (carried over, unchanged): `estimate_period_regression_ts`/`regression_period_rss` remain dead code from an SQL/FFI perspective

**File:** `crates/anofox-fcst-core/src/periods.rs:342,581`
No change from iteration 1 — still `pub`, still no FFI export, still just context for WR-01 (now fixed). No action required.

---

_Reviewed: 2026-10-14T08:52:10Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
_Iteration: 2 (re-review of --auto fix loop)_
