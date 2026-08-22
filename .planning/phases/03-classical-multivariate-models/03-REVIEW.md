---
phase: 03-classical-multivariate-models
reviewed: 2026-08-22T00:00:00Z
depth: standard
files_reviewed: 24
files_reviewed_list:
  - crates/anofox-fcst-ffi/src/lib.rs
  - crates/anofox-fcst-ffi/src/types.rs
  - crates/anofox-fcst-core/src/forecast.rs
  - src/include/anofox_fcst_ffi.h
  - src/include/ts_forecast_var_native.hpp
  - src/table_functions/ts_forecast_native.cpp
  - src/table_functions/ts_forecast_var_native.cpp
  - src/scalar_functions/ts_forecast_scalar.cpp
  - src/macros/ts_macros.cpp
  - src/anofox_forecast_extension.cpp
  - examples/forecasting/classical_forecasting_examples.sql
  - benchmark/m4/garch_benchmark/run.py
  - benchmark/m4/kalman_benchmark/run.py
  - benchmark/m4/var_benchmark/run.py
  - benchmark/configs/garch.py
  - benchmark/configs/kalman.py
  - benchmark/configs/var.py
  - benchmark/pyproject.toml
  - docs/api/07-forecasting.md
  - docs/reference/models/classical/garch.md
  - docs/reference/models/state-space/kalman.md
  - docs/reference/models/multivariate/var.md
  - CMakeLists.txt
  - src/table_functions/ts_fill_gaps_native.cpp
findings:
  critical: 2
  warning: 3
  info: 2
  total: 7
status: issues_found
---

# Phase 3: Code Review Report

**Reviewed:** 2026-08-22
**Depth:** standard
**Files Reviewed:** 24
**Status:** issues_found

## Summary

Phase 3 adds GARCH and Kalman as univariate model variants on the existing `ts_forecast_by` path,
and a new multivariate `ts_forecast_var_by` function backed by a new FFI entry point
`anofox_ts_forecast_var`. The FFI memory safety story is substantially sound: `checked_mul` is
used on both buffer-dimension products in `anofox_ts_forecast_var`, error propagation is correct
(no `unwrap_or(0)`), `catch_unwind` is present, pointer null-checks are performed, and the free
function matches the allocation path. The ForecastOptions ABI extension is additive and the
cbindgen-generated C header matches the Rust struct field-for-field. New params are correctly
initialized (via `memset(&opts, 0, sizeof(opts))`) and populated in both
`ts_forecast_native.cpp` and `ts_forecast_scalar.cpp`. The GARCH core uses `forecast_variance()+sqrt`
as required. Calendar-aware date arithmetic is correct: `ParseFrequencyWithType` stores the
integer count (1 for "1mo") in `frequency_seconds`, and the monthly path multiplies by that count,
not by seconds.

Two BLOCKERs were found. One is a correctness bug that causes GARCH and Kalman to silently emit
spurious symmetric confidence intervals that the documentation explicitly says are not provided
in v1. The other is a regression where the WASM allocator free stub uses a hardcoded layout
regardless of allocation size, which is undefined behavior for all heap-allocated FFI buffers
including the new `VARForecastResult` on WASM targets.

Three WARNINGs cover: `list_models()` not including GARCH/Kalman after Phase 3 (stale consumer-
visible list); `calculate_fitted_values` applying a SES fallback for GARCH/Kalman when
`include_fitted=true` (misleading residuals); and the under-determination guard comparing
pre-imputation NaN counts against a threshold that Rust's internal imputation will shift.

---

## Critical Issues

### CR-01: GARCH and Kalman silently emit spurious confidence intervals

**File:** `crates/anofox-fcst-core/src/forecast.rs:739-774`
**Issue:** `forecast_garch` and `forecast_kalman` (via `extract_forecast`) both return
`lower: vec![]` / `upper: vec![]` as documented — no prediction intervals in v1. However,
`forecast()` (the dispatch function) always calls `calculate_confidence_intervals` at line 739
*after* the model dispatch and stores the result in the local `lower`/`upper` bindings that feed
the final `ForecastOutput`. The per-model `result.lower`/`result.upper` are never consulted after
line 736; they are discarded. As a result, every GARCH and Kalman call emits non-empty symmetric
bounds derived from the historical variance of the input series applied to the model's point
forecasts. For GARCH this is doubly wrong: the point forecasts are *volatilities* (σ), and the
synthetic intervals wrap those volatilities with ±z×σ_historical, producing interval bounds that
are neither correct GARCH quantiles nor interpretable as prediction intervals.

The docs (`docs/reference/models/classical/garch.md` line 75, `docs/reference/models/state-space/kalman.md`
line 78) explicitly state "yhat_lower and yhat_upper are not available for GARCH/Kalman in v1."

**Fix:** Gate the `calculate_confidence_intervals` call on the model type. For GARCH and Kalman
(and any other model where the result already carries its own intervals or explicitly opts out),
pass the result's lower/upper through unchanged. The minimal correct fix:

```rust
// Replace lines 738-740 in forecast.rs with:
let (lower, upper) = if result.lower.is_empty() && result.upper.is_empty() {
    // Models that do not produce intervals (GARCH, Kalman) — emit empty vecs,
    // do NOT synthesize spurious bounds from historical variance.
    (vec![], vec![])
} else {
    // Models that return empty intervals from extract_forecast (via predict())
    // fall back to the generic CI only when the model itself provides none.
    // Check the model type explicitly for models that have a documented v1 gap:
    match options.model {
        ModelType::GARCH | ModelType::Kalman => (vec![], vec![]),
        _ => calculate_confidence_intervals(&result.point, &clean_values, options.confidence_level),
    }
};
```

A cleaner approach is to add a `provides_intervals: bool` field to `ForecastOutput` and gate the
calculation on it.

---

### CR-02: WASM free stub uses hardcoded layout — undefined behavior for all FFI allocations

**File:** `crates/anofox-fcst-ffi/src/lib.rs:44-58` (WASM target only)
**Issue:** The WASM `free()` stub — which is called by `anofox_free_double_array` and therefore
by `anofox_free_var_forecast_result` on WASM targets — calls `dealloc` with a hardcoded
`Layout::from_size_align(8, 8)` regardless of the actual size of the allocation being freed. This
is undefined behavior under Rust's allocator model: `dealloc` requires the same `Layout` that was
used for `alloc`. The `alloc_double_array` function allocates with
`Layout::array::<f64>(len).unwrap()`, which has size `len * 8` and alignment 8. Passing
`Layout { size: 8, align: 8 }` to `dealloc` for a `len * 8` byte allocation is UB and may
silently corrupt the allocator's bookkeeping, causing future allocations to return garbage or
triggering downstream panics.

This is a pre-existing issue (acknowledged in a TODO comment), but Phase 3 creates new exposure:
`anofox_ts_forecast_var` allocates a `k_vars * n_horizon * 8` byte buffer on WASM which is freed
via the same stub. For a 3-variable × 12-horizon forecast, the allocated size is 288 bytes but
`free()` presents an 8-byte layout to the allocator.

**Fix:** The stub must receive the allocation size from the caller or use the same layout
calculation as `alloc_double_array`. Since the C ABI does not carry a length through the free
function pointer, the cleanest fix is to store `len` adjacent to the data pointer (e.g., a
length-prefixed allocation) or to switch to a global allocator that does not require layout on
free (such as `wee_alloc`). As a minimum viable fix for v1 WASM:

```rust
// In the WASM free() stub, thread the length through anofox_free_double_array
// by adding a len parameter:
#[no_mangle]
pub unsafe extern "C" fn anofox_free_double_array(ptr: *mut f64, len: usize) {
    if ptr.is_null() || len == 0 { return; }
    let layout = Layout::array::<f64>(len).expect("layout");
    dealloc(ptr as *mut u8, layout);
}
```

This requires updating the C header declaration and all callsites (C++ layer passes `len` from
`VARForecastResult.n_horizon * k_vars`, etc.). Alternatively, use `std::alloc::System` directly
on WASM if it supports size-agnostic free in the target environment, but this requires audit.

---

## Warnings

### WR-01: `list_models()` does not include GARCH or Kalman after Phase 3 addition

**File:** `crates/anofox-fcst-core/src/forecast.rs:2762-2811`
**Issue:** `list_models()` is the authoritative list of model names returned by the extension's
`ts_list_models()` SQL function (exported via `crates/anofox-fcst-core/src/lib.rs:71`). It was
not updated in Phase 3 and still ends at 32 models. Users who query `ts_list_models()` to
discover available models will not see "GARCH" or "Kalman", even though both are fully functional.
The doc comment at line 2761 says "32 models matching C++ extension" — both the count and list
are now stale.

**Fix:** Add "GARCH" and "Kalman" to the `list_models` return value and update the doc comment:

```rust
// After line 2806 ("Laplace"), add:
// Classical Models (Phase 3)
"GARCH",
"Kalman",
```

Update the doc comment count from 32 to 34 (or whatever the accurate post-Phase-3 total is).

---

### WR-02: `calculate_fitted_values` applies SES approximation for GARCH and Kalman

**File:** `crates/anofox-fcst-core/src/forecast.rs:2745-2758`
**Issue:** When `include_fitted=true` is requested for GARCH or Kalman, the `_ =>` catch-all
arm in `calculate_fitted_values` computes an α=0.3 SES-smoothed sequence and uses it as "fitted
values" for those models. The residuals derived from these SES-fitted values (`clean_values - SES_fit`)
are then emitted as `residuals` for GARCH and Kalman — a number that has no relationship to the
actual GARCH conditional variance residuals or Kalman filter innovations. Users calling
`ts_forecast_by('table', ..., 'GARCH', ..., params := MAP{'include_fitted':'1'})` will receive
silently wrong residuals.

Note: `forecast_garch` and `forecast_kalman` both return `fitted: None` / `residuals: None`
from the dispatch, which means the true model residuals are not surfaced even if they were
available from the underlying Rust crate objects.

**Fix:** Add explicit arms in `calculate_fitted_values` that return `None` (or empty) for GARCH
and Kalman, matching what the model itself returns:

```rust
// In calculate_fitted_values, before the _ => arm:
ModelType::GARCH | ModelType::Kalman => {
    // True fitted values are not surfaced for these models in v1.
    // Return empty to avoid emitting misleading SES-approximated residuals.
    vec![]
}
```

Then in the caller at line 743-756, handle the empty-fitted case (skip residual calculation if
fitted is empty) so users get `NULL` for fitted/residuals rather than wrong values.

---

### WR-03: VAR under-determination guard counts pre-imputation NaN rows — may diverge from Rust FFI

**File:** `src/table_functions/ts_forecast_var_native.cpp:443-476`
**Issue:** The C++ under-determination guard (lines 466-476) computes `n_eff` as the count of
non-NaN values per column in the raw sorted data *before* the Rust FFI call. Rust's
`anofox_ts_forecast_var` internally calls `fill_nulls_interpolate` before fitting, which can
fill leading/trailing NaN values through linear interpolation. If the input has scattered NaN
values that interpolation fills successfully, `n_eff` in C++ may be smaller than the effective
observation count Rust actually uses, potentially rejecting valid inputs. Conversely, if a column
starts with NaN values that cannot be interpolated (leading nulls), Rust truncates the series and
the effective `n_eff` Rust uses will be *smaller* than what C++ measured — meaning the C++ guard
passes but the Rust FFI still receives an underdetermined system, producing a computation error
from within Rust rather than the friendly C++ guard message.

This is a defence-in-depth gap: the guard is advisory but can be off in either direction relative
to Rust's actual post-imputation length.

**Fix:** Document the known divergence in a comment. For stronger correctness, compute `n_eff`
after applying the same imputation logic C++ uses, or remove the C++ guard entirely and rely on
the Rust-side `InsufficientData` error (already handled at line 507-510) which will fire on the
actual post-imputation n. The current behavior is not a crash or data corruption risk — it
produces wrong error messages in edge cases — so this is a warning-tier issue.

---

## Info

### IN-01: VAR doc "Typical Workflow" uses `<date_col>` as a literal SQL column name placeholder

**File:** `docs/reference/models/multivariate/var.md:131-143`
**Issue:** The "Typical Workflow" section at line 131-143 contains `<date_col>` as a placeholder
in the SQL snippet but does not format it as a placeholder:

```sql
<date_col> AS forecast_date,
```

This literal angle-bracket expression would fail if copy-pasted into DuckDB. The companion
examples earlier in the file correctly use the concrete column name `ds`.

**Fix:** Replace with either a concrete example column name or mark it unambiguously as a
placeholder in prose:

```sql
-- Replace <date_col> with your actual date column name, e.g.:
ds AS forecast_date,
```

---

### IN-02: Kalman `model_name` output is always "Kalman" regardless of spec

**File:** `crates/anofox-fcst-core/src/forecast.rs:2439`
**Issue:** `forecast_kalman` calls `extract_forecast(&model, horizon, "Kalman")` which hardcodes
the model name as `"Kalman"` regardless of whether `local_level` or `local_linear_trend` was
selected. Users cannot distinguish the two specs in the `model_name` output column without
re-reading the input parameters. This is inconsistent with `GARCH` which includes p and q
(e.g., `"GARCH(1,1)"`).

**Fix:**

```rust
fn forecast_kalman(values: &[f64], horizon: usize, spec: Option<&str>) -> Result<ForecastOutput> {
    let spec_str = spec.unwrap_or("local_level");
    let ts = make_timeseries(values)?;
    let mut model = match spec_str {
        "local_linear_trend" => KalmanForecaster::local_linear_trend(),
        _ => KalmanForecaster::local_level(),
    };
    model.fit(&ts)...;
    let mut out = extract_forecast(&model, horizon, "Kalman")?;
    out.model_name = format!("Kalman({})", spec_str);
    Ok(out)
}
```

---

_Reviewed: 2026-08-22_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
