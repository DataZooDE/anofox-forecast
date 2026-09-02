---
phase: 05-explicit-member-ensemble
reviewed: 2026-08-31T09:57:36Z
depth: standard
files_reviewed: 9
files_reviewed_list:
  - crates/anofox-fcst-core/src/forecast.rs
  - crates/anofox-fcst-core/src/lib.rs
  - crates/anofox-fcst-ffi/src/lib.rs
  - src/include/anofox_fcst_ffi.h
  - src/table_functions/ts_forecast_ensemble_native.cpp
  - src/include/ts_forecast_ensemble_native.hpp
  - src/anofox_forecast_extension.cpp
  - src/macros/ts_macros.cpp
  - CMakeLists.txt
findings:
  critical: 0
  warning: 3
  info: 3
  total: 6
status: issues_found
---

# Phase 05: Code Review Report

**Reviewed:** 2026-08-31T09:57:36Z
**Depth:** standard
**Files Reviewed:** 9
**Status:** issues_found

## Summary

Phase 5 adds `ts_forecast_ensemble_by` — an explicit-member ensemble scalar function backed by a new `anofox_ts_forecast_ensemble` FFI export, the `build_forecaster` 36-variant factory, and `forecast_explicit_ensemble` in the Rust core.

The FFI boundary design is sound: the null-delimited `members_buf` is correctly framed by the explicit `members_buf_len` (no scan-past-end), the `members_count` cross-check detects C++/Rust marshalling bugs, UTF-8 parsing uses `from_utf8_lossy` (no panic), the null pointer guards cover `values` and `members_buf`, and `catch_unwind` wraps the entire Rust closure.

The `build_forecaster` match is compiler-exhaustively over all 36 `ModelType` variants with no wildcard arm, the 10 blocked variants return `Err(InvalidParameter)` (not panics), and seasonal models consistently apply `period > 1` gating before constructing with the supplied period.

Three warnings were found — all on the Rust core side — and three info items. No criticals.

## Warnings

### WR-01: `forecast_explicit_ensemble` uses `filter_map` to drop NULLs, diverging from every other forecast path which interpolates

**File:** `crates/anofox-fcst-core/src/forecast.rs:2824-2828`

**Issue:** The doc comment says "mirrors the main `forecast()` path" for NULL handling, but the implementation does the opposite: it silently discards NULL observations via `filter_map`, shortening the series. Every other public entry point (`forecast`, `forecast_with_exog`, all internal model helpers) calls `fill_nulls_interpolate`, which preserves series length and fills gaps by linear interpolation.

The divergence has two concrete consequences:

1. **Timestamp alignment is broken for sparse series.** The C++ layer sorts and passes `sorted_values` with a matching `validity_bits` bitmask to the FFI. The Rust side then calls `fill_nulls_interpolate` on the entire thing when using the single-model path, keeping the series at length `n`. For the ensemble path `filter_map` produces a shorter series; this is then passed to `make_timeseries` which generates an integer `[0..k]` axis — misaligned with the longer date axis the C++ computed. For series with few NULLs on a smooth series this matters little, but for series with many NULLs (e.g. 30% missing) the reduced length may also violate the minimum observations assumed by seasonal models (see WR-02).

2. **Inconsistent user experience.** A user switching from `ts_forecast_by(method := 'AutoARIMA')` to `ts_forecast_ensemble_by(members := ['AutoARIMA', ...])` gets different behaviour on the same NULL-bearing series. The single-model path interpolates; the ensemble path drops.

**Fix:** Replace the `filter_map` with the same `fill_nulls_interpolate` call used in `forecast()`, and keep the `build_series` result from the FFI rather than re-filtering. At minimum, document that this is an intentional design choice (not an oversight):

```rust
// Replace lines 2824-2828:
let clean_values: Vec<f64> = fill_nulls_interpolate(values);
if clean_values.is_empty() {
    return Err(ForecastError::InsufficientData { needed: 1, got: 0 });
}
```

---

### WR-02: `forecast_explicit_ensemble` has no minimum-length guard (`< 3`), unlike `forecast()`

**File:** `crates/anofox-fcst-core/src/forecast.rs:2829-2835`

**Issue:** `forecast()` rejects series shorter than 3 observations with an `InsufficientData` error before attempting to fit any model. `forecast_explicit_ensemble` has only an `is_empty()` check (length 0). A 1-observation or 2-observation series passes through, reaches `Ensemble::fit`, and each member model's `fit` call will either panic (caught by `catch_unwind` and surfaced as `PanicCaught` → silent NULL row) or return its own error.

For the ensemble path this lands as a silent NULL output row rather than an informative `InsufficientData` error, making the failure invisible to users. This is also triggered more easily than it appears because WR-01's `filter_map` can reduce an `n`-length series with `n-2` NULLs to only 2 observations, which then hits this path silently.

**Fix:**

```rust
if clean_values.len() < 3 {
    return Err(ForecastError::InsufficientData {
        needed: 3,
        got: clean_values.len(),
    });
}
```

Add this immediately after the `is_empty()` guard. `InsufficientData` (code 6) currently maps to a silent NULL row in the C++ layer (same as ComputationError) — that's appropriate behaviour for ensemble "skip short series silently" semantics — but the guard prevents a confusing panic-caught-as-NULL path.

---

### WR-03: `SMA` in `build_forecaster` hardcodes `window = 5`, inconsistent with all other SMA call sites

**File:** `crates/anofox-fcst-core/src/forecast.rs:2665`

**Issue:** Every other SMA invocation in the codebase computes the window as `period.max(3)` — adapting to the detected or user-supplied seasonal period. `build_forecaster` unconditionally passes `5` to `WindowAverage::new(5)` regardless of the supplied `period`. For series where `period > 5` (e.g. weekly data with `period = 7` or monthly with `period = 12`), the ensemble SMA member averages fewer observations than the period-aware single-model path would, producing a systematically different (shorter-memory) forecast.

A user calling `ts_forecast_by('SMA', seasonal_period := 12)` gets window = 12, but `ts_forecast_ensemble_by(['SMA', 'AutoARIMA'], seasonal_period := 12)` gets window = 5. This is a silent inconsistency.

```rust
// Current (line 2665):
ModelType::SMA => Ok(Box::new(WindowAverage::new(5))),

// Fix — use period.max(3) to match all other SMA call sites:
ModelType::SMA => {
    let window = if p > 1 { p.max(3) } else { 3 };
    Ok(Box::new(WindowAverage::new(window)))
}
```

---

## Info

### IN-01: No unit tests for `forecast_explicit_ensemble` or `build_forecaster` in the Rust test suite

**File:** `crates/anofox-fcst-core/src/forecast.rs` (test module, offset ~3800)

**Issue:** The Phase 5 functions `forecast_explicit_ensemble` and `build_forecaster` have no `#[test]` coverage in the inline test module. Every prior phase (3: Kalman/GARCH, 4: AutoEnsemble, `parse_combination_method`) added Rust unit tests. The SQL tracer (`ensemble_explicit_tracer.sql`) covers the happy path well but cannot reach Rust-level edge cases: all-NULL series after `filter_map`, the 1-observation series path, duplicate member names, or the `build_forecaster` blocked-model error arms.

**Suggested tests:**
- `forecast_explicit_ensemble` with all-NULL input → `InvalidParameter` error
- `forecast_explicit_ensemble` with 1 obs → produces NULL (or `InsufficientData` after WR-02 fix)
- `forecast_explicit_ensemble` with duplicate members `['AutoARIMA', 'AutoARIMA']` → succeeds
- `build_forecaster(ModelType::GARCH, None)` → `Err(InvalidParameter)`
- `build_forecaster(ModelType::AutoEnsemble, None)` → `Err(InvalidParameter)`
- `build_forecaster(ModelType::SeasonalNaive, Some(7))` → `Ok` and period is 7

---

### IN-02: `SeasonalWindowAverage` in `build_forecaster` hardcodes `n_seasons = 2` without documentation

**File:** `crates/anofox-fcst-core/src/forecast.rs:2690`

**Issue:** `SeasonalWindowAverage::new(sp, 2)` always uses `n_seasons = 2`, meaning the average is taken over the last 2 full seasons only, regardless of how much history is available. This is inconsistent with the `forecast_seasonal_window_average` helper (lines 1373-1387) which computes `n_seasons = (values.len() / p).max(1)` — adapting to series length. A 60-observation series with `period = 12` would average 5 seasons via the single-model path but only 2 seasons via the ensemble path.

This is less severe than WR-03 since `SeasonalWindowAverage` is rarely used as a primary model, but the code comment ("use 2 seasons as default") does not explain why 2 is chosen over the adaptive computation.

**Fix:** Either use the adaptive `n_seasons` calculation, or add a `// TODO ENS-03: derive n_seasons from series length at fit time` comment so the intent is explicit.

---

### IN-03: Non-UTF-8 bytes in member names silently produce a confusing error message

**File:** `crates/anofox-fcst-ffi/src/lib.rs:8001`

**Issue:** The member name buffer is decoded with `String::from_utf8_lossy`, which replaces invalid UTF-8 byte sequences with the Unicode replacement character `\u{FFFD}`. The resulting string (e.g. `"Auto\u{FFFD}RIMA"`) is then passed to `ModelType::from_str`, which returns `InvalidModel` → C++ throws `InvalidInputException` with a message like:

```
Invalid parameter 'members' = 'Auto\u{FFFD}RIMA': unknown model name 'Auto\u{FFFD}RIMA'
```

While this is safe (no panic), the error message is confusing because the user likely passed a well-formed string that got corrupted somewhere in the marshalling path. In practice DuckDB VARCHAR strings never contain embedded non-UTF-8 bytes, so this path is purely theoretical for production use. The risk is a misleading error during debugging of marshalling issues.

**Suggested improvement:** Add a defensive `from_utf8` check before `from_utf8_lossy` and emit an explicit error mentioning non-UTF-8 encoding if it fails, so the root cause is immediately visible rather than buried in a mangled model name.

---

_Reviewed: 2026-08-31T09:57:36Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
