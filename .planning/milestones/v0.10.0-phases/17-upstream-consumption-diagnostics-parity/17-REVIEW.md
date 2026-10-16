---
phase: 17-upstream-consumption-diagnostics-parity
reviewed: 2026-10-15T17:04:32Z
depth: standard
files_reviewed: 8
files_reviewed_list:
  - crates/anofox-fcst-core/src/forecast.rs
  - crates/anofox-fcst-core/src/lib.rs
  - crates/anofox-fcst-core/src/validation.rs
  - crates/anofox-fcst-ffi/src/lib.rs
  - crates/anofox-fcst-ffi/src/types.rs
  - src/include/anofox_fcst_ffi.h
  - src/macros/ts_macros.cpp
  - src/scalar_functions/diagnostics.cpp
findings:
  critical: 0
  warning: 0
  info: 2
  total: 2
status: issues_found
---

# Phase 17: Code Review Report

**Reviewed:** 2026-10-15T17:04:32Z
**Depth:** standard
**Files Reviewed:** 8
**Status:** issues_found

## Summary

This is a second-pass adversarial review of Phase 17 (`anofox-forecast` 0.16.1 bump; ADF/KPSS
`regression := 'c'|'ct'` incl. a new in-core trend KPSS; the D-04 four-way stationarity verdict;
Ljung-Box `fitted_params`/exact chi-square p-values; the `'Holt' → ETS(A,A,N)` remap), done against
HEAD (`6ceea19`), i.e. **after** the one open finding from the prior review round (`fee17a2`,
17-REVIEW.md as of 16:57Z) was fixed and committed (`6ceea19`, "WR-01 add SPECIAL_HANDLING to
ts_ljung_box/ts_residual_diagnostics optional args").

Independent verification performed this round, all against the rebuilt extension
(`build/release/duckdb -unsigned`, `LOAD '.../anofox_forecast.duckdb_extension'`):

- **WR-01 (prior review) — confirmed FIXED.** `ts_ljung_box(series, NULL, 2)` now computes with the
  documented default lags instead of short-circuiting to a NULL struct; verified for the 2-arg,
  3-arg, and `anofox_fcst_ts_ljung_box` alias overloads, for `ts_residual_diagnostics(series, NULL)`,
  and through the `ts_ljung_box_by(..., fitted_params := NULL)` / `ts_adf_by(..., regression := NULL)`
  macro paths (literal-NULL named args now reach the function body rather than being bind-time
  folded to a constant NULL). A non-foldable NULL (e.g. `MAX(nullable_col)`) still reached the
  function body correctly both before and after the fix — the regression was specific to literal/
  foldable `NULL` arguments.
- **`classify_stationarity`'s D-04 truth table** (the 4-way ADF+KPSS mapping) was re-derived from
  first principles against the standard combination convention (ADF rejects+KPSS doesn't→stationary;
  neither rejects→trend_stationary; both reject→difference_stationary; ADF doesn't+KPSS
  rejects→non_stationary) and matches the new code exactly; the CHANGELOG's claim that 3 of 4 old
  labels were swapped is correct and the new mapping is the one a reviewer would independently derive.
- **`'Holt'` remap** is applied at both production dispatch sites (`forecast()`'s
  `ModelType::Holt => forecast_holt_lib` and `build_forecaster()`'s `ModelType::Holt =>
  ETSModel::new(ETSSpec::aan(), 1)`, used by direct/CV and ensemble-member construction
  respectively) — no stale third call site found (`HoltLinearTrend` no longer appears anywhere in
  non-test production code). `model_name` stays `"Holt"` end-to-end, confirmed via
  `ts_forecast_by(..., 'Holt', ...)` against the built extension.
- **Trend KPSS (`kpss_trend`)** structurally mirrors the crate's own level-KPSS internals
  (`anofox-forecast` 0.16.1 `validation::stationarity::kpss_test`) line-for-line for the bandwidth
  formula, Bartlett LRV kernel, and cumulative-sum numerator — only the residual source differs
  (OLS-detrended vs. demeaned). Degenerate cases (n<4, exactly-linear series under `'ct'`, huge/
  negative `lags` override) all return `NaN` without panicking, confirmed against the built
  extension.
- **FFI edge cases exercised directly against the built extension**, none crashed or misbehaved:
  negative `fitted_params`, `fitted_params == lags`, `lags` of `±2×10⁹`, unknown `regression`
  strings, constant series, series with only interior `NULL`s (hard error naming the 1-based
  position, as documented), series collapsing to length 0/1 after edge-trim.

No new Critical or Warning findings. The two Info-level findings from the prior review round remain
open (not addressed by the `6ceea19` fix, which touched only `diagnostics.cpp` registration and the
corresponding test file) and are carried forward below with independent re-verification against the
current `HEAD`.

## Info

### IN-01: `anofox_ts_ljung_box`'s negative-`fitted_params` validation is skipped on the `length == 0` fast path

**File:** `crates/anofox-fcst-ffi/src/lib.rs:7928-7935`

**Issue:** `anofox_ts_ljung_box` returns `true` (success, default/NaN result) immediately when
`length == 0`, before the `fitted_params < 0` check runs:

```rust
*out_result = AnofoxLjungBoxResult::default();
if length == 0 {
    return true;
}
if fitted_params < 0 {
    set_error(out_error, ErrorCode::InvalidInput,
               &format!("fitted_params must be >= 0, got {fitted_params}"));
    return false;
}
```

A direct FFI caller passing `length == 0` together with a negative `fitted_params` silently
succeeds instead of getting the `"fitted_params must be >= 0"` error it would get for any
non-empty input. Re-verified this round: not reachable through the `ts_ljung_box` SQL surface today
(`ApplySingleSeriesPolicy` in `ts_list_marshal.hpp` already short-circuits an empty/all-NULL list to
SQL NULL in the C++ layer before `anofox_ts_ljung_box` is ever called), so this is a library/FFI
API-contract inconsistency rather than a SQL-reachable bug — low severity, but worth the one-line
fix since `fitted_params` validation is otherwise unconditional and a future direct-FFI consumer
(or a future C++ call site that doesn't pre-filter empty series) would hit it.

**Fix:** Move the `fitted_params < 0` check above the `length == 0` early return (the `out_error`/
`out_result` initialization already happens first either way).

### IN-02: `kpss_trend`'s `lags.unwrap_or(0)` on the `n < 4` degenerate path doesn't reflect the resolved bandwidth

**File:** `crates/anofox-fcst-core/src/validation.rs:243-255`

**Issue:** When `n < 4`, `kpss_trend` returns `lags: lags.unwrap_or(0)` — the caller's raw override
if given, or a hardcoded `0` if not — rather than resolving through the same `.min(n / 2).max(1)`
clamp the non-degenerate path below it uses:

```rust
fn kpss_trend(series: &[f64], lags: Option<usize>) -> StationarityOut {
    let n = series.len();
    if n < 4 {
        return StationarityOut {
            statistic: f64::NAN,
            p_value: f64::NAN,
            lags: lags.unwrap_or(0),   // <- not .min(n/2).max(1)-clamped
            is_stationary: false,
            ...
        };
    }
    let l = lags.unwrap_or_else(|| ...).min(n / 2).max(1);   // the real path does clamp
    ...
```

Since the statistic is `NaN` either way (and no test currently asserts `lags` for this branch),
this is cosmetic, but it means `(ts_kpss(short_series, NULL, 'ct')).lags` reports `0` rather than
the `1` the resolved-bandwidth path would always produce for `n >= 1`. Re-verified this round:
still present, unchanged from the prior review, and not touched by the `6ceea19` fix.

**Fix:** `lags: lags.unwrap_or(0).min(n.max(1) / 2).max(1)` (or pin a test documenting that `0` is
intentional, if it is meant to signal "no bandwidth was even attempted" distinctly from a resolved
`1`).

---

_Reviewed: 2026-10-15T17:04:32Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
