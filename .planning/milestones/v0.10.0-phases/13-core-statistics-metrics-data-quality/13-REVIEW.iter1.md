---
phase: 13-core-statistics-metrics-data-quality
reviewed: 2026-10-10T22:24:51Z
depth: standard
files_reviewed: 18
files_reviewed_list:
  - crates/anofox-fcst-core/src/calendar.rs
  - crates/anofox-fcst-core/src/gaps.rs
  - crates/anofox-fcst-core/src/lib.rs
  - crates/anofox-fcst-core/src/metrics.rs
  - crates/anofox-fcst-core/src/quality.rs
  - crates/anofox-fcst-core/src/stats.rs
  - crates/anofox-fcst-core/src/stl.rs
  - crates/anofox-fcst-core/src/supsmu.rs
  - crates/anofox-fcst-ffi/src/lib.rs
  - crates/anofox-fcst-ffi/src/types.rs
  - src/include/anofox_fcst_ffi.h
  - src/aggregate_functions/ts_data_quality_agg.cpp
  - src/aggregate_functions/ts_stats_agg.cpp
  - src/macros/ts_macros.cpp
  - src/scalar_functions/metrics.cpp
  - src/table_functions/ts_data_quality.cpp
  - src/table_functions/ts_metrics_native.cpp
  - src/table_functions/ts_stats.cpp
findings:
  critical: 1
  warning: 4
  info: 1
  total: 6
status: issues_found
---

# Phase 13: Code Review Report

**Reviewed:** 2026-10-10T22:24:51Z
**Depth:** standard
**Files Reviewed:** 18
**Status:** issues_found

## Summary

Reviewed the diff (`c1121c5^..HEAD`) for the statistics/metrics/data-quality milestone: STL/supsmu ports, Pincus approximate entropy, scipy-exact skewness/kurtosis, the shared missing-period gap counter (`gap_counts`/`resolve_gap_step`), the data-quality `behavioral_score` redesign, Hyndman-Koehler MASE, and the FFI/C++ wiring for all of the above.

Overall the work is careful: NULL/validity propagation through the new FFI `Option<f64>`/`*out_is_null` patterns is consistently applied, bit-layout assumptions between the C++ `AppendWithValidity` helper and Rust's `build_series` match exactly, the `ts_stats_agg`/`ts_data_quality_agg` `IgnoreNull()` fix correctly threads NULL-valued (but non-NULL-timestamped) rows through sort/validity rebuilding, and numeric edge cases in the new moment/entropy/STL-strength code are guarded against division-by-zero and short-series panics (verified by tracing, not by running the Rust test suite). The STL/supsmu ports carry a dedicated R-reference-fixture test (`tests/stl_reference.rs`) rather than only the vacuous "components sum to original" smoke test, which is the right verification strategy for a 1e-8-tolerance port.

One correctness defect found is significant enough to block: the new shared calendar-gap-counting path (`resolve_gap_step` in `gaps.rs`) silently drops the user-declared multiplier for Monthly/Quarterly/Yearly frequency strings (e.g. `'2mo'`, `'3q'`, `'5y'` — all of which the extension's own frequency parser accepts), always counting gaps as if the multiplier were 1. This produces silently wrong `n_gaps`/`expected_length`/`temporal_score` with no error raised, for exactly the "calendar-aware gap detection" feature this phase (and `ts_stats_by`'s own changelog comment, "Bug 3: Correctly detects gaps for calendar frequencies") claims to deliver correctly.

Four further warnings and one info item cover a NULL-list-element-to-epoch-0 coercion newly reachable via `ts_data_quality`'s new dates parameter, duplicated frequency-parsing logic, an inconsistent NaN/0.0 "no value" sentinel between `entropy` and `trend_strength`/`seasonality_strength`, a fragile buffer-reuse pattern in the STL port, and one small piece of now-dead-but-reachable scalar surface.

## Critical Issues

### CR-01: Calendar frequency multiplier silently dropped in gap/temporal scoring

**File:** `crates/anofox-fcst-core/src/gaps.rs:432-449` (`resolve_gap_step`), consumed by `crates/anofox-fcst-core/src/stats.rs` (`compute_ts_stats_with_dates_and_type`) and `crates/anofox-fcst-core/src/quality.rs` (`compute_data_quality`), reached from `src/table_functions/ts_stats.cpp:21-45` (`ParseFrequencyForStats`) and `src/table_functions/ts_data_quality.cpp:14-36` (`ParseFrequencyForDataQuality`)

**Issue:** `resolve_gap_step` maps `FrequencyType::Monthly/Quarterly/Yearly` unconditionally to `CalendarStep::Months(1)`/`Months(3)`/`Months(12)`:

```rust
FrequencyType::Monthly => Ok(Some(CalendarStep::Months(1))),
FrequencyType::Quarterly => Ok(Some(CalendarStep::Months(3))),
FrequencyType::Yearly => Ok(Some(CalendarStep::Months(12))),
```

It never looks at the multiplier the user declared. The extension's own frequency grammar (`ParseFrequencyWithType` in `ts_fill_gaps_native.cpp`) explicitly accepts multi-unit calendar frequencies — `'2mo'`, `'3q'`, `'5y'`, `'6 months'`, etc. — returning `{count, false, FrequencyType::Monthly}` with `count=2` for `'2mo'`. The C++ wrapper (`ParseFrequencyForStats`/`ParseFrequencyForDataQuality`) even computes an approximate `frequency_micros` from that count (`count * 86400 * 30 * 1e6` for `'2mo'`), but that `frequency_micros` value is *never consulted* for calendar types on the Rust side — `resolve_gap_step`'s `Monthly`/`Quarterly`/`Yearly` arms ignore it entirely and hard-code the single-unit step.

Concretely: `SELECT * FROM ts_data_quality('sales', id, date, qty, 10, '2mo')` on a series that is correctly sampled every 2 months will have every consecutive pair misclassified as "1 missing month", inflating `n_gaps`/`n_gap_runs` and tanking `temporal_score`/`overall_score` — silently, with no error or warning. The same applies to `ts_stats`/`ts_stats_by` for any non-"1" multiplier Monthly/Quarterly/Yearly frequency string. `CalendarStep::Months(m)` (in `calendar.rs`) already supports an arbitrary multiplier `m` — the bug is that the multiplier computed by the C++ parser never survives the trip through the Rust `FrequencyType` enum, which has no multiplier field at all.

**Fix:** Thread the declared multiplier through to `resolve_gap_step`. Either (a) extend the FFI `FrequencyType`/`compute_data_quality`/`compute_ts_stats_with_dates_and_type` signatures with an explicit `period_count: usize` that `resolve_gap_step` folds into `CalendarStep::Months(period_count * base_months)`, or (b) have the C++ layer pass the parsed multiplier via the existing `frequency_micros` parameter for calendar types too (using a sentinel encoding) and decode it on the Rust side before building the `CalendarStep`. Add a regression test with a `'2mo'`/`'3q'` fixture asserting `n_gaps == 0` for a correctly-spaced bi-monthly/tri-quarterly series.

## Warnings

### WR-01: NULL date-list elements silently become epoch 0 in gap/quality scoring

**File:** `src/table_functions/ts_data_quality.cpp:63-95` (`ExtractListTimestamps`); same pre-existing pattern at `src/table_functions/ts_stats.cpp:145-172`

**Issue:** When extracting a per-group `LIST(TIMESTAMP)` into a flat `int64_t` array, a NULL element *inside* an otherwise non-NULL list is written as `0` (1970-01-01T00:00:00Z) rather than rejected or excluded:

```cpp
if (child_data.validity.RowIsValid(unified_child_idx)) {
    out_timestamps[i] = child_values[unified_child_idx].value;
} else {
    out_timestamps[i] = 0;
}
```

A single NULL `date_col` value for one row (e.g. a bad ingestion row) will silently inject an epoch-0 timestamp into the per-group date array, which — once sorted into the series — looks like a multi-decade gap to `gap_counts`/`resolve_gap_step`, corrupting `n_gaps`, `n_gap_runs`, `expected_length`, and `temporal_score` for the whole group without any error. This pattern already existed in `ts_stats.cpp` before this phase (so it is not a new defect there), but `ts_data_quality.cpp` is a **new** consumer of per-row dates introduced by this phase (D-05/WRAP-02) — this is the first time `ts_data_quality`'s gap/temporal scoring is exposed to this failure mode.

**Fix:** Either throw an `InvalidInputException` naming the offending group when a NULL element is found inside a non-NULL dates list (matching the existing "dates and values length mismatch" error style), or exclude that entire row's date from the gap computation (treat the whole row as if it were dropped from the list) instead of injecting a sentinel timestamp.

### WR-02: Duplicated frequency-parsing helper risks future drift

**File:** `src/table_functions/ts_data_quality.cpp:14-36` (`ParseFrequencyForDataQuality`) vs `src/table_functions/ts_stats.cpp:21-45` (`ParseFrequencyForStats`)

**Issue:** `ParseFrequencyForDataQuality` is a verbatim copy-paste of `ParseFrequencyForStats` (same comment literally says "mirrors ts_stats.cpp's ParseFrequencyForStats"). Any future fix to one (e.g. fixing CR-01 above) must be remembered and applied to the other, or the two functions silently diverge.

**Fix:** Move the shared logic into `ts_fill_gaps_native.hpp`/`.cpp` (which already owns `ParseFrequencyWithType`) as a single `ParseFrequencyToMicrosAndType()` helper, and have both `ts_stats.cpp` and `ts_data_quality.cpp` call it.

### WR-03: Inconsistent "no value" sentinel for `entropy` vs `trend_strength`/`seasonality_strength`

**File:** `crates/anofox-fcst-ffi/src/types.rs:327-341` (`impl From<anofox_fcst_core::TsStats> for TsStatsResult`)

**Issue:**

```rust
trend_strength: stats.trend_strength.unwrap_or(f64::NAN),
seasonality_strength: stats.seasonality_strength.unwrap_or(f64::NAN),
entropy: stats.entropy.unwrap_or(0.0),
```

`entropy`'s "no value" case (`None`) is encoded as `0.0`, while the two structurally identical neighboring fields use `f64::NAN`. `0.0` is indistinguishable from a legitimate "entropy is exactly zero" result (a genuinely constant series also reports `Some(0.0)` per the Rust doc comment), so any C++/consumer code path that forgets to check `has_entropy` before reading `entropy` will silently treat "no entropy computed" as "perfectly deterministic series" instead of getting an obviously-wrong `NaN` that would surface in testing. All current call sites (`ts_stats.cpp`, `ts_stats_agg.cpp`) do check `has_entropy` correctly today, so this is not an active bug, but it is an unnecessary landmine for the next caller.

**Fix:** Change to `entropy: stats.entropy.unwrap_or(f64::NAN)` for consistency with the other two Option-backed fields, and add a test asserting `has_entropy == false` implies the raw `entropy` field is NaN (defense-in-depth, matching the other two fields' tests).

### WR-04: STL port reuses the caller's output buffer as scratch before it is written

**File:** `crates/anofox-fcst-core/src/stl.rs:219-247` (`stlstp`), `crates/anofox-fcst-core/src/stl.rs:255-324` (`stlss`)

**Issue:** `stlstp` calls `stlss(..., buf1, buf2, buf3, buf4, season)` — i.e. the function's own `season: &mut [f64]` output parameter (length `n`, eventually overwritten at the very end of `stlstp` with `season[i] = buf1[np+i] - buf0[i]`) is passed into `stlss` as its `work4` scratch parameter, and is written-to-and-read-from repeatedly inside `stlss`'s per-cycle-subseries loop before `stlstp` ever assigns real values into it. This is correct today only because nothing in `stlstp` reads `season`'s contents between the `stlss` call and the final overwrite loop — a faithful (if fragile) replication of the original Fortran's work-array reuse. The aliasing invariant is documented only in a comment at the `stlss` call site, not enforced by the type system; a future refactor of `stlstp`'s inner-loop order (e.g. reading `season` for a convergence check before the final assignment) would silently consume loess scratch garbage instead of the intended seasonal estimate, with no compiler or test signal pointing at the cause.

**Fix:** At minimum, add a `debug_assert!`/comment-adjacent runtime check or rename the parameter at the `stlss` call site to make the aliasing undeniable (e.g. `let season_as_scratch = &mut *season;`), or — more robustly — give `stlss` its own fourth scratch buffer slice out of `work` (there is spare capacity in the `5 * n2p` allocation) instead of repurposing the final output array.

## Info

### IN-01: Dead/unreachable `_ts_stats` values-only scalar path

**File:** `src/table_functions/ts_stats.cpp:270-280` (`TsStatsFunction`)

**Issue:** The comment added in this phase states plainly: `// seasonal_period: auto-detect (D-02); this values-only scalar has no caller today (ts_stats/ts_stats_by use the dates-aware paths below)`. The function remains registered and reachable (it is marked `info.internal = true`, hiding it from `duckdb_functions()`/autocomplete, but it is still callable by name). Carrying confirmed-dead production surface forward without a removal plan adds maintenance burden.

**Fix:** No action required for this phase, but consider tracking removal of `_ts_stats`/`anofox_ts_stats` (the values-only FFI entry point) in a follow-up cleanup once confirmed unused by any external binding.

---

_Reviewed: 2026-10-10T22:24:51Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
