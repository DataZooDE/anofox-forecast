---
phase: 12-statistics-marshalling-moves-to-rust
reviewed: 2026-10-10T00:00:00Z
depth: standard
files_reviewed: 57
files_reviewed_list:
  - CMakeLists.txt
  - crates/anofox-fcst-core/src/calendar.rs
  - crates/anofox-fcst-core/src/conformal.rs
  - crates/anofox-fcst-core/src/filter.rs
  - crates/anofox-fcst-core/src/gaps.rs
  - crates/anofox-fcst-core/src/imputation.rs
  - crates/anofox-fcst-core/src/lib.rs
  - crates/anofox-fcst-core/src/missing.rs
  - crates/anofox-fcst-ffi/src/lib.rs
  - crates/anofox-fcst-ffi/tests/calendar_ffi.rs
  - crates/anofox-fcst-ffi/tests/null_policy_ffi.rs
  - examples/data_preparation/null_handling_examples.sql
  - src/aggregate_functions/ts_features_agg.cpp
  - src/aggregate_functions/ts_forecast_agg.cpp
  - src/anofox_forecast_extension.cpp
  - src/include/anofox_fcst_ffi.h
  - src/include/anofox_forecast_extension.hpp
  - src/include/ts_backtest_native.hpp
  - src/include/ts_future_dates.hpp
  - src/include/ts_list_marshal.hpp
  - src/macros/ts_macros.cpp
  - src/scalar_functions/bootstrap.cpp
  - src/scalar_functions/conformal.cpp
  - src/scalar_functions/diagnostics.cpp
  - src/scalar_functions/metrics.cpp
  - src/scalar_functions/series_transforms.cpp
  - src/scalar_functions/ts_forecast_scalar.cpp
  - src/table_functions/ts_aggregate_hierarchy.cpp
  - src/table_functions/ts_backtest_native.cpp
  - src/table_functions/ts_changepoints.cpp
  - src/table_functions/ts_data_quality.cpp
  - src/table_functions/ts_decomposition.cpp
  - src/table_functions/ts_detrend.cpp
  - src/table_functions/ts_features.cpp
  - src/table_functions/ts_features_native.cpp
  - src/table_functions/ts_filter.cpp
  - src/table_functions/ts_forecast_ensemble_native.cpp
  - src/table_functions/ts_forecast_native.cpp
  - src/table_functions/ts_forecast_panel_native.cpp
  - src/table_functions/ts_forecast_var_native.cpp
  - src/table_functions/ts_imputation.cpp
  - src/table_functions/ts_metrics_native.cpp
  - src/table_functions/ts_peaks.cpp
  - src/table_functions/ts_periods.cpp
  - src/table_functions/ts_seasonality.cpp
  - test/sql/backtest_memory_investigation.sql
  - test/sql/ts_aggregate_hierarchy.test
  - test/sql/ts_conformal_null_policy.test
  - test/sql/ts_detection_null_policy.test
  - test/sql/ts_diff.test
  - test/sql/ts_filter.test
  - test/sql/ts_forecast_calendar.test
  - test/sql/ts_forecast_calendar_surfaces.test
  - test/sql/ts_list_null_policy.test
  - test/sql/ts_series_null_policy.test
  - test/sql/ts_series_transforms_rust.test
  - test/sql/ts_varchar_edge_cases.test
findings:
  critical: 1
  warning: 3
  info: 1
  total: 5
status: issues_found
---

# Phase 12: Code Review Report

**Reviewed:** 2026-10-10
**Depth:** standard
**Files Reviewed:** 57 (hunks since `6b9ae65d2302503ba8fb134ecf8e6340e71ad7a5^`)
**Status:** issues_found

## Summary

This phase moves missing-data policy, differencing, calendar stepping, hierarchy
SUM semantics, and metric/conformal/bootstrap marshalling out of C++ and into
Rust, behind a new shared list-reader (`ts_list_marshal.hpp`) and a family of
validity-bitmask FFI signatures. The bulk of the work is solid: `missing.rs`'s
trim/reject/complete-cases primitives are correctly implemented and well
tested; the D-12 calendar rule (`calendar.rs`) correctly handles month-end
detection, day-of-month clamping, and quarter/year stepping, with real-value
tests; `detect_frequency`'s tie-break is genuinely deterministic (`BTreeMap`
+ `(count, Reverse(diff))`); `diff_aligned`'s row-alignment and D-05/D-06
semantics are correct and exercised with real numbers; the hierarchy SUM
NULL-vs-partial semantics (D-08) is a clean, correct fix; and the dead
`ts_backtest_native`/`ts_filter.cpp`/`ts_imputation.cpp` removal is complete
and leaves no dangling registrations.

However, one newly-added FFI path has a genuine, untested, reachable
out-of-bounds heap read (see CR-01), and there are a few lower-severity
robustness/determinism gaps introduced by the refactor.

## Critical Issues

### CR-01: Heap buffer over-read in `ts_conformal_predict_per_step` when `point_forecasts` is longer than `horizon`

**File:** `crates/anofox-fcst-core/src/conformal.rs:2060-2075` (also
`crates/anofox-fcst-ffi/src/lib.rs:6096-6098` and
`src/scalar_functions/conformal.cpp:820-823`)

**Issue:**
`conformal_predict_per_step_validated` computes `lower`/`upper` with length
`n = point_filled.len().min(half_widths.len())` (i.e. `min(n_point_forecasts,
horizon)`), but clones the *untruncated* `point_filled` into `point`
(length `n_point_forecasts`):

```rust
let point_filled = missing::fill_missing(point_forecasts, 0.0);
let n = point_filled.len().min(half_widths.len());
let mut lower: Vec<f64> = (0..n).map(|i| point_filled[i] - half_widths[i]).collect();
let mut upper: Vec<f64> = (0..n).map(|i| point_filled[i] + half_widths[i]).collect();
let mut point = point_filled.clone();   // full length, NOT truncated to n
```

When `n_point_forecasts > horizon` (`n == horizon < point.len()`), the
returned `point`, `lower`, and `upper` vectors have *different* lengths
(`point.len() == n_point_forecasts`, `lower.len() == upper.len() == horizon`).

The FFI wrapper then uses `point.len()` as the single `n_forecasts` for all
three arrays:

```rust
// anofox_ts_conformal_predict_per_step, lib.rs:6096
(*out_result).n_forecasts = ps.point.len();   // = n_point_forecasts
...
(*out_result).lower = alloc_or_error(&ps.lower, ...)?;   // allocated with only `horizon` elements
(*out_result).upper = alloc_or_error(&ps.upper, ...)?;   // same
```

The C++ caller writes out all three result lists using that single
`n_forecasts`:

```cpp
// src/scalar_functions/conformal.cpp:820-822
WriteDoubleListNanAsNull(point_out, row_idx, ps_result.point, ps_result.n_forecasts);
WriteDoubleListNanAsNull(lower_out, row_idx, ps_result.lower, ps_result.n_forecasts);  // OOB read
WriteDoubleListNanAsNull(upper_out, row_idx, ps_result.upper, ps_result.n_forecasts);  // OOB read
```

`WriteDoubleListNanAsNull` reads `data[i]` for `i` in `[0, n_forecasts)`. For
`lower`/`upper`, that reads `n_point_forecasts - horizon` doubles past the end
of a heap allocation sized for only `horizon` elements — a reachable
out-of-bounds heap read triggerable directly from SQL, e.g.:

```sql
-- horizon=3 but point_forecasts has 5 elements
SELECT ts_conformal_predict_per_step(
    [100,105,110,100,105,110]::DOUBLE[],  -- 2 folds * 3
    [101,108,109,101,108,109]::DOUBLE[],
    [100.0,101.0,102.0,103.0,104.0]::DOUBLE[],  -- 5 point forecasts, horizon=3
    0.1, 3
);
```

No existing test exercises `len(point_forecasts) != horizon`; every test in
`test/sql/ts_conformal_null_policy.test` and the Rust unit tests in
`conformal.rs` keeps them equal, which is why this was not caught.

**Fix:** Make `point`/`lower`/`upper` share one length. Easiest: validate
`point_forecasts.len() == horizon` up front and error otherwise (matches the
D-10 "error naming both counts" philosophy already used for the
fold-length/horizon check), or truncate/pad `point`, `lower`, and `upper` all
to the same `n`:

```rust
if point_forecasts.len() != horizon {
    return Err(ForecastError::InvalidInput(format!(
        "per-step conformal: point_forecasts has {} elements but horizon is {}",
        point_forecasts.len(), horizon
    )));
}
```

## Warnings

### WR-01: `anofox_ts_is_constant` rejects the zero-length input its own C++ caller produces

**File:** `crates/anofox-fcst-ffi/src/lib.rs:769-805` (also
`src/scalar_functions/series_transforms.cpp:155-157`)

**Issue:** The doc comment for `anofox_ts_is_constant` says a group with
"fewer than two non-missing values (including an all-missing group)
classifies as constant," but the null-pointer guard runs unconditionally:

```rust
if values.is_null() || out_result.is_null() {
    (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
    return false;
}
```

`_ts_is_constant`'s only caller passes `nullptr` precisely when the list is
empty:

```cpp
bool success = anofox_ts_is_constant(n == 0 ? nullptr : values.data(), ...);
```

So `_ts_is_constant([])` (an empty-but-non-NULL list) always throws
`"_ts_is_constant: Null pointer argument"` instead of returning `true`, in
contradiction to the function's own documented contract. Compare with the
sibling `anofox_ts_series_gate`, which deliberately special-cases
`length == 0` *before* checking `values` for null, precisely to allow this
pattern. Not reachable via the shipped `ts_drop_constant_by` macro (every
`GROUP BY` partition has at least one row, so `n` is never 0 there), but it
is reachable by calling `_ts_is_constant([]::DOUBLE[])` directly, and the
inconsistency with `anofox_ts_series_gate`'s ordering is a latent defect.

**Fix:** Check `length == 0` before the null-pointer check (mirroring
`anofox_ts_series_gate`), and short-circuit to `*out_result = true` in that
case:

```rust
if length == 0 {
    *out_result = true;
    return true;
}
if values.is_null() || out_result.is_null() { ... }
```

### WR-02: Non-deterministic row ordering for duplicate timestamps in `ts_forecast_agg` / `ts_features_agg`

**File:** `src/aggregate_functions/ts_forecast_agg.cpp:311-330`,
`src/aggregate_functions/ts_features_agg.cpp:286-300`

**Issue:** Prior to this phase, `Finalize` sorted `vector<pair<int64_t,
double>>` by the full pair (timestamp, then value), which is a deterministic
total order even when timestamps repeat. The D-11 refactor needed to keep a
third `valid` column aligned with `(timestamp, value)`, and did so by sorting
only by timestamp:

```cpp
// ts_forecast_agg.cpp
std::sort(order.begin(), order.end(),
          [&data](size_t a, size_t b) { return data.timestamps[a] < data.timestamps[b]; });
```

```cpp
// ts_features_agg.cpp
std::sort(sorted_triples.begin(), sorted_triples.end(),
          [](const auto &a, const auto &b) { return std::get<0>(a) < std::get<0>(b); });
```

`std::sort` (not `std::stable_sort`) gives no guarantee about the relative
order of elements that compare equal. For a group containing two or more rows
with the same timestamp, the relative order of those rows' `(value, valid)`
pairs after sorting is now unspecified, and in practice can depend on the
order partial aggregate states were concatenated in `Combine()`, which itself
depends on DuckDB's thread scheduling. This can make `ts_forecast_agg` /
`ts_features_agg` results non-reproducible across runs for any group with a
duplicate-date row (a real occurrence in product/location panels with
same-day re-ingestion).

**Fix:** Use `std::stable_sort` (preserves original insertion order for
ties), or add value/valid as a secondary sort key so ties remain
deterministic regardless of thread interleaving.

### WR-03: Dead, duplicated statistics logic left behind by the refactor

**File:** `crates/anofox-fcst-core/src/metrics.rs:312-325` (`mqloss`),
`crates/anofox-fcst-core/src/conformal.rs:1894-1938`
(`conformal_predict_per_step`)

**Issue:** `anofox_ts_mqloss` (FFI) was rewritten to loop over quantile
levels and call `quantile_loss` per level with per-level `complete_cases`
filtering (D-02), replacing its previous call into
`anofox_fcst_core::metrics::mqloss`. Likewise,
`anofox_ts_conformal_predict_per_step` now calls
`conformal_predict_per_step_validated` instead of `conformal_predict_per_step`.
Both superseded functions remain `pub` in the crate but are no longer called
from the FFI boundary — only from their own unit tests (and, for
`conformal_predict_per_step`, from a test that cross-checks the two
implementations agree on complete data). This is dead production code that
duplicates logic now living in the FFI layer / the `_validated` variant; it
will not be exercised by any `ts_*` SQL surface and can silently drift out of
sync with the real path (as CR-01 shows for the `_validated` sibling).

**Fix:** Either delete `mqloss` and `conformal_predict_per_step` (moving their
useful unit tests onto the surviving call paths), or explicitly mark them
`#[cfg(test)]`/internal-only if they're kept purely as a cross-check oracle.

## Info

### IN-01: Stale doc comment referencing the deleted `ExtractListAsDouble` helper

**File:** `src/scalar_functions/diagnostics.cpp:7`

**Issue:** The file header says: "It follows the STRUCT-return pattern
established in bootstrap.cpp and the ExtractListAsDouble helper pattern from
the same file." `ExtractListAsDouble` was deleted from `bootstrap.cpp` (and
every other call site) in this phase's D-01 consolidation onto
`ts_list_marshal.hpp`'s `ListDoubleReader`; the comment was not updated and
now points at removed code.

**Fix:** Update the comment to reference `ts_list_marshal.hpp`'s
`ListDoubleReader`/`ScalarArgReader` instead of the deleted helper.

---

_Reviewed: 2026-10-10_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
