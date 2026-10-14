---
phase: 15-detection-decomposition-on-fdars-0-47
reviewed: 2026-10-14T08:25:37Z
depth: standard
files_reviewed: 17
files_reviewed_list:
  - crates/anofox-fcst-core/Cargo.toml
  - crates/anofox-fcst-core/src/changepoint.rs
  - crates/anofox-fcst-core/src/decomposition.rs
  - crates/anofox-fcst-core/src/detrending.rs
  - crates/anofox-fcst-core/src/filter.rs
  - crates/anofox-fcst-core/src/lib.rs
  - crates/anofox-fcst-core/src/peaks.rs
  - crates/anofox-fcst-core/src/periods.rs
  - crates/anofox-fcst-core/src/quality.rs
  - crates/anofox-fcst-core/src/seasonality.rs
  - crates/anofox-fcst-core/src/stats.rs
  - crates/anofox-fcst-ffi/src/lib.rs
  - src/macros/ts_macros.cpp
  - src/table_functions/ts_decomposition.cpp
  - src/table_functions/ts_detrend.cpp
  - src/table_functions/ts_mstl_decomposition_native.cpp
  - src/table_functions/ts_periods.cpp
findings:
  critical: 3
  warning: 2
  info: 1
  total: 6
status: issues_found
---

# Phase 15: Code Review Report

**Reviewed:** 2026-10-14T08:25:37Z
**Depth:** standard
**Files Reviewed:** 17
**Status:** issues_found

## Summary

Reviewed the fdars-core 0.47 bump and the new detrend/decompose/peaks/period-detection/changepoint surface (full read of the small/medium files; `periods.rs`, `crates/anofox-fcst-ffi/src/lib.rs`, `ts_macros.cpp`, and `ts_periods.cpp` via targeted diff per the review brief, with follow-up full reads where the diff implicated a specific function). The scipy `find_peaks` port, the BOCPD run-length cap, the `mstl_fit` refactor (verified bit-identical for the `k==1` single-period case by hand-tracing the two-pass loop), and the new `FromStr` strict-parsing error paths are all solid and well covered by reference-fixture tests.

Three BLOCKER-level issues surfaced, all SQL-reachable:

1. An uninitialized-memory read in the (internal but still directly callable) `_ts_mstl_decomposition` scalar function when `insufficient_data = 'none'` and no requested period is usable — a correctness/data-integrity bug, not merely a style issue.
2. An unbounded/negative `n_frequencies` argument to `ts_lomb_scargle` that can trigger an unrecoverable allocation abort (crashes the whole DuckDB process, not a catchable DuckDB exception) — the new validation added in this phase checks only the lower bound.
3. A systemic gap across ~9 functions (`period <= 0.0` is the only period-validity check, and it is always `false` for `NaN`) that lets a `NaN` period argument silently produce plausible-but-wrong results instead of the documented `InvalidParameter` error.

Two WARNINGs and one INFO item round out the review (dead-code validation gap in an unreached function, PELT's theoretical pruning worst-case, and a documentation nuance).

## Critical Issues

### CR-01: `_ts_mstl_decomposition` reads uninitialized memory when a requested period is unusable under `insufficient_data = 'none'`

**File:** `src/table_functions/ts_decomposition.cpp:138-156` (and the mirrored remainder block at `190-208`)
**Issue:**
`mstl_decompose` (`crates/anofox-fcst-core/src/decomposition.rs:216-233`) returns `trend: None, remainder: None` (with `decomposition_applied: false`) when periods were requested but none is usable and `insufficient_data_mode == InsufficientDataMode::None`. The FFI layer (`anofox_ts_mstl_decomposition`, `crates/anofox-fcst-ffi/src/lib.rs:3092-3102`) then sets `out_result.n_observations = length` (the ORIGINAL series length, not 0) while leaving `out_result.trend`/`out_result.remainder` as null pointers, and still returns `true` (success).

Back in `TsMstlDecompositionFunction` (`ts_decomposition.cpp`), `success` is `true` so the row is not set NULL. The trend/remainder list-setting code then does:
```cpp
list_data[row_idx].length = mstl_result.n_observations;   // full input length, e.g. 10
...
if (mstl_result.trend) {                                  // null -> skipped
    memcpy(child_data + current_size, mstl_result.trend, ...);
}
```
`ListVector::Reserve`/`SetListSize` only grow the buffer; they do not zero it. The result is a `trend`/`remainder` LIST column of length `n_observations` backed by whatever was previously in that buffer (uninitialized or stale heap memory) rather than an empty list or a NULL row. This is reachable today with e.g.:
```sql
SELECT _ts_mstl_decomposition(my_short_series, 'none', [1000]);
```
on any series shorter than `2*1000+1`. `_ts_mstl_decomposition` is marked `internal = true` (hidden from `duckdb_functions()`/autocomplete) but is still a normally-registered, callable SQL scalar function — `info.internal` does not block direct invocation, and the existing test suite (`test/sql/ts_decomposition.test`, `test/sql/ts_detection_reference.test`) has no coverage at all for `insufficient_data = 'none'`, so this path has never been exercised.

Contrast with the sibling table-in-out function `_ts_mstl_decomposition_native` (`src/table_functions/ts_mstl_decomposition_native.cpp:321-364`), which gets this right: it only `push_back`s into the output vector when the corresponding pointer is non-null, so the vector naturally stays empty-length when `trend`/`remainder` are null.

**Fix:** Mirror the native function's guard, or explicitly zero `n_observations` (and the seasonal list lengths) whenever `decomposition_applied` is `false`:
```cpp
// trend
size_t trend_len = mstl_result.trend ? mstl_result.n_observations : 0;
list_data[row_idx].length = trend_len;
ListVector::Reserve(trend_list, current_size + trend_len);
ListVector::SetListSize(trend_list, current_size + trend_len);
if (mstl_result.trend) {
    memcpy(child_data + current_size, mstl_result.trend, trend_len * sizeof(double));
}
```
Apply the same pattern to the remainder block.

### CR-02: Unbounded/negative `n_frequencies` on `ts_lomb_scargle` can abort the DuckDB process

**File:** `crates/anofox-fcst-core/src/periods.rs:842-907` (new validation), `crates/anofox-fcst-ffi/src/lib.rs:1802-1839`, `src/table_functions/ts_periods.cpp:1081-1095`
**Issue:**
This phase added new parameter validation to `lomb_scargle` (`periods.rs:498-507`):
```rust
if let Some(nf) = n_frequencies {
    if nf < 2 {
        return Err(ForecastError::InvalidParameter { ... });
    }
}
```
This only rejects the LOWER bound. There is no upper bound anywhere in the stack:
- C++ (`ts_periods.cpp:1081-1083`): `size_t n_frequencies = static_cast<size_t>(n_frequencies_reader->Get(row_idx));` where the reader is `int64_t`. A negative SQL argument (e.g. `-1`) wraps to `SIZE_MAX` under this cast.
- Rust FFI (`anofox_ts_lomb_scargle`, `lib.rs:1834-1838`): `n_freq = if n_frequencies > 0 { Some(n_frequencies) } else { None }` — a huge wrapped value is `> 0`, so it is passed straight through.
- `lomb_scargle` itself (`periods.rs:900-903`): `let freqs: Vec<f64> = (0..n_freq).map(...).collect();` with `n_freq` effectively uncapped.

A single SQL call such as:
```sql
SELECT ts_lomb_scargle(LIST(value ORDER BY date), NULL, NULL, -1) FROM t;
-- or simply a large positive literal, no cast trick needed:
SELECT ts_lomb_scargle(LIST(value ORDER BY date), NULL, NULL, 2000000000) FROM t;
```
attempts to allocate a `Vec<f64>` of ~1.8×10^19 (negative case) or ~16 GB (large-positive case) elements. Rust's default global allocator calls `handle_alloc_error` on allocation failure, which **aborts the process** — this is not a `catch_unwind`-catchable panic, so the existing panic-to-`AnofoxError` machinery in this same function does not protect against it. This crashes the whole DuckDB process (or OOM-kills it), not just the one query, from a single untrusted scalar-function argument.

**Fix:** Add an explicit, documented upper bound to the new validation block in `lomb_scargle` (e.g. `nf > 1_000_000`) alongside the existing `nf < 2` check, and fix the C++ cast to reject negative `int64_t` before the `static_cast<size_t>` (currently any negative value silently becomes a huge `size_t`):
```rust
if let Some(nf) = n_frequencies {
    if nf < 2 || nf > 1_000_000 {
        return Err(ForecastError::InvalidParameter {
            param: "n_frequencies".to_string(),
            value: nf.to_string(),
            reason: "must be between 2 and 1,000,000".to_string(),
        });
    }
}
```
```cpp
int64_t raw = n_frequencies_reader->Get(row_idx);
if (raw < 0) {
    throw InvalidInputException("ts_lomb_scargle: n_frequencies must be non-negative, got %lld", raw);
}
size_t n_frequencies = static_cast<size_t>(raw);
```

### CR-03: `period <= 0.0` validation never rejects `NaN`, letting a `NaN` period silently produce wrong (non-error, non-NULL) results across ~9 functions

**File:** `crates/anofox-fcst-core/src/detrending.rs:388-394,435-441` (`decompose_additive`/`decompose_multiplicative`); `crates/anofox-fcst-core/src/seasonality.rs:523-529,562-568,616-622,668-674,717-723,789-795` (`seasonal_strength_variance`, `seasonal_strength_spectral`, `seasonal_strength_windowed`, `classify_seasonality`, `detect_seasonality_changes`, `detect_amplitude_modulation`); `crates/anofox-fcst-core/src/peaks.rs:486-492` (`analyze_peak_timing`)
**Issue:**
Every one of these functions validates its `period: f64` argument the same way:
```rust
if period <= 0.0 {
    return Err(ForecastError::InvalidParameter { ... "Period must be positive" ... });
}
```
`NaN <= 0.0` is `false` in IEEE-754 (all ordered comparisons with `NaN` are `false`), so a `NaN` period sails through this guard. The companion insufficient-data guard used in several of the same functions, `if n < 2 * period as usize { ... }`, does not catch it either: `f64::NAN as usize` saturates to `0` in Rust, so `n < 0` is always `false` for a `usize` `n`.

Traced into the underlying `fdars-core` 0.47 implementations (e.g. `seasonal_strength_variance` in `fdars-core-0.47.0/src/seasonal/strength.rs:26-78`): their own early-return guard is also `period <= 0.0` (same `NaN` gap), and the subsequent Fourier-basis fit degrades silently — every `b_sum > 1e-15` check is `false` for a `NaN`-derived basis, so the seasonal component stays the zero vector and the function returns `0.0`, not `NaN` and not an error. `decompose_additive`/`decompose_multiplicative` similarly proceed into `fdars_decompose_additive` with `omega = 2*PI/NaN`, producing an all-`NaN` `trend`/`seasonal`/`remainder` result that is returned as success.

Net effect: `SELECT ts_seasonal_strength(LIST(value ORDER BY date), 'nan'::DOUBLE) FROM t` returns `0.0` ("not seasonal at all") instead of an `InvalidParameter` error; `ts_decompose_seasonal(values, 'nan'::DOUBLE)` returns a row of all-`NaN` trend/seasonal/remainder values instead of erroring. Both are silently wrong, user-visible, SQL-reachable results (`'nan'::DOUBLE`, `0.0/0.0`, or a `NULL`-coalesced computed period are all realistic ways to produce this at the SQL layer), not merely a style nit — this directly violates the documented contract ("Period must be positive") and the review brief's explicit "NULL vs error consistency" check.

**Fix:** Change every one of these guards (and the equivalent ones inside `fdars-core` if that crate is ever patched) to also reject non-finite values:
```rust
if !period.is_finite() || period <= 0.0 {
    return Err(ForecastError::InvalidParameter {
        param: "period".to_string(),
        value: period.to_string(),
        reason: "Period must be a positive finite number".to_string(),
    });
}
```
This single change at each of the ~9 call sites closes the gap without needing any `fdars-core` change (the guard runs before the series ever reaches `fdars-core`).

## Warnings

### WR-01: `estimate_period_regression_ts`/`regression_period_rss` have no `period_min > 0` validation (currently unreachable from SQL, but a latent correctness trap)

**File:** `crates/anofox-fcst-core/src/periods.rs:581-650`
**Issue:** `estimate_period_regression_ts` validates `period_min >= period_max` (new in this phase) but never validates that `period_min` itself is positive. With `period_min <= 0.0`, the default integer-grid path (`lo = p_min.ceil() as i64 .. hi = p_max.floor() as i64`) includes zero and negative candidate periods. `harmonic_design_matrix` degrades these to an intercept-only column (since `2*k > period` breaks the harmonic loop immediately for any `period <= 2.0`), so on a series with no real seasonality every candidate's RSS ties at ~`SST`, and the tie-break rule (`min_by period ascending`) can select the most negative candidate as `best_period` — i.e. the function can return a negative "detected period."

This function and `regression_period_rss` are currently `pub` in the crate and re-exported from `lib.rs`, but have **no** FFI export and no C++ caller (`grep` across `crates/anofox-fcst-ffi/src/lib.rs` and `src/` finds zero references), so this is not reachable from SQL today. Flagging because it is inconsistent with the sibling `lomb_scargle`, which validates both bounds, and because it is `pub` API that a future phase is likely to wire up to SQL without re-deriving this gap.

**Fix:** Mirror `lomb_scargle`'s validation:
```rust
if let Some(mn) = period_min {
    if !mn.is_finite() || mn <= 0.0 {
        return Err(ForecastError::InvalidParameter { param: "period_min".into(), value: mn.to_string(), reason: "must be a positive finite number".into() });
    }
}
```

### WR-02: PELT's exact-safe pruning has no bound on `live` set growth for adversarial inputs (O(n²) worst case)

**File:** `crates/anofox-fcst-core/src/changepoint.rs:246-286`
**Issue:** `detect_changepoints`'s `live` candidate set is pruned via `f[tau] + cost(tau, t) <= f[t]`, which is amortized-efficient on typical data (confirmed by `test_pelt_pruning_effective_and_exact_on_long_series`) but has no hard cap — an adversarially-constructed series could keep most candidates alive, degrading to the O(n²) unpruned-DP bound. The module's own doc comment states PELT currently has "no SQL caller... Rust/FFI-only," so this is not reachable from untrusted SQL input today, but flagging per the review brief's explicit ask to check PELT's pruning worst case, in case a future phase re-exposes it to SQL without re-auditing this.

**Fix:** No action required while PELT stays Rust/FFI-only; if/when a SQL surface is added, consider a documented cap on `live.len()` (merge/drop the lowest-priority candidate once exceeded) analogous to BOCPD's `MAX_RUN_LENGTH`.

## Info

### IN-01: `estimate_period_regression_ts`/`regression_period_rss` are currently dead code from an SQL/FFI perspective

**File:** `crates/anofox-fcst-core/src/periods.rs:342,581`
**Issue:** Both functions are `pub`, re-exported from `lib.rs`, and covered by reference-fixture tests (`tests/period_detection_reference.rs`), but have no FFI export (`anofox_ts_estimate_period_regression` does not exist in `crates/anofox-fcst-ffi/src/lib.rs`) and no C++/SQL caller. This is consistent with the Phase 15 plan's stated scope (`D-11` reworked the algorithm as a prerequisite, without necessarily wiring a new SQL surface), so likely intentional — noting only so the WR-01 validation gap above is understood as "latent," not "exploitable today."
**Fix:** None required; just context for WR-01.

---

_Reviewed: 2026-10-14T08:25:37Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
