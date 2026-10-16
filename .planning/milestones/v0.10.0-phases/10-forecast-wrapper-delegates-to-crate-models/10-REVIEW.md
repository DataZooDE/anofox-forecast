---
phase: 10-forecast-wrapper-delegates-to-crate-models
reviewed: 2026-10-09T17:18:08Z
depth: standard
files_reviewed: 29
files_reviewed_list:
  - crates/anofox-fcst-core/src/forecast.rs
  - crates/anofox-fcst-core/src/lib.rs
  - crates/anofox-fcst-core/tests/exog_ols_memory.rs
  - crates/anofox-fcst-core/tests/forecast_arima.rs
  - crates/anofox-fcst-core/tests/forecast_exog.rs
  - crates/anofox-fcst-core/tests/forecast_fallbacks.rs
  - crates/anofox-fcst-core/tests/forecast_intervals.rs
  - crates/anofox-fcst-ffi/src/lib.rs
  - crates/anofox-fcst-ffi/src/types.rs
  - crates/anofox-fcst-ffi/tests/core_ffi_parity.rs
  - examples/forecasting/autoensemble.sql
  - examples/forecasting/crate_delegation_examples.sql
  - examples/forecasting/ensemble_explicit.sql
  - examples/forecasting/ensemble_intervals.sql
  - src/aggregate_functions/ts_forecast_agg.cpp
  - src/include/anofox_fcst_ffi.h
  - src/macros/ts_macros.cpp
  - src/scalar_functions/ts_forecast_scalar.cpp
  - src/table_functions/ts_cv_forecast_native.cpp
  - src/table_functions/ts_forecast.cpp
  - src/table_functions/ts_forecast_ensemble_native.cpp
  - src/table_functions/ts_forecast_native.cpp
  - test/sql/ts_backtest_equivalence.test
  - test/sql/ts_forecast_crate_delegation.test
  - test/sql/ts_forecast_exog.test
  - test/sql/ts_model_distinctness.test
  - test/sql/ts_native_model_names.test
  - validation/reference/r/gen_forecast_reference.R
  - validation/reference/sql/check_dep01_exog.sql
findings:
  critical: 1
  warning: 3
  info: 0
  total: 4
status: issues_found
---

# Phase 10: Code Review Report

**Reviewed:** 2026-10-09T17:18:08Z
**Depth:** standard
**Files Reviewed:** 29
**Status:** issues_found

## Summary

This phase's core work — deleting `calculate_confidence_intervals`/`calculate_fitted_values`,
wiring every model through the crate's own `predict_with_intervals`/`fitted_values`/`residuals`,
adding `arima_order`/`seasonal_order`, the `n_residuals` length-field fix, the `PreparedSeries`/
`finalize_output`/`align_exog` trailing-NULL realignment machinery, and the exog regressor
validity-bitmask plumbing — is well executed and extensively value-tested against R fixtures
(`forecast_arima.rs`, `exog_ols_memory.rs`, `core_ffi_parity.rs`'s `arima_airline_matches_r_through_ffi`
and the two new validity-bit tests). I traced `prepare_series`/`finalize_output`/`align_exog`
by hand against the `crate_delegation_examples.sql` trailing-NULL worked example and the math
checks out. FFI string-field reads (`c_field_str`) are correctly bounded via
`CStr::from_bytes_until_nul` over the array's own bytes (never `CStr::from_ptr`), and the new
`ExogenousRegressor.validity`/`future_validity` bitmask reads are bounds-checked against a
caller-validated `n_values`/`n_future` before any bitmask slice is constructed.

However, one of this phase's central guarantees — "no remaining silent fallbacks, no default
seasonal period" (D-07) — is **not actually enforced on the exogenous-regressor path**:
`forecast_with_exog` never calls the `resolve_period` helper that the non-exog `forecast()`
path uses to turn "no period detected for a seasonal-required model" into a hard error. For
`MSTL`/`AutoMSTL` (the only `requires_seasonal_period` model that is also `exog_capable`),
a caller who omits `seasonal_period`/`seasonal_periods` and whose series has no detectable
seasonality gets a silent `period = 1` fallback instead of the documented error — exactly the
defect class D-07 was written to eliminate, just not closed off on this one path. This is
undocumented by the locked decisions (the only CONTEXT.md carve-out is calibration, not
detection scope) and is not covered by any test with real regressors + undetectable
seasonality + MSTL (the existing `ets_spec_with_regressors` test documents the gap in a code
comment but always passes an explicit `seasonal_period`, so it never exercises the failure
mode).

## Critical Issues

### CR-01: `forecast_with_exog` bypasses D-07's `resolve_period` — MSTL/AutoMSTL with regressors silently default to seasonal_period=1 instead of erroring

**File:** `crates/anofox-fcst-core/src/forecast.rs:973-983` (period resolution), consumed at `crates/anofox-fcst-core/src/forecast.rs:1433-1448` (`forecast_crate_exog`'s `MSTL | AutoMSTL` arm)

**Issue:** The non-exog `forecast()` entry point resolves the seasonal period via `resolve_period()`
(`forecast.rs:1819`), which hard-errors for any `requires_seasonal_period` model (includes
`MSTL`/`AutoMSTL`) when no period is explicitly set, no `seasonal_periods` list is given, and
`detect_seasonality` finds nothing — this is exactly D-07's contract ("if none is detected →
SQL error. No default 12 or 2.").

`forecast_with_exog` reimplements period resolution inline instead of calling `resolve_period`:

```rust
// Detect seasonality if needed
let period = if options.auto_detect_seasonality && options.seasonal_period == 0 {
    detect_seasonality(&prep.values, None)
        .ok()
        .and_then(|p| p.first().cloned())
        .unwrap_or(1) as usize
} else if options.seasonal_period > 0 {
    options.seasonal_period
} else {
    1
};
```

This `.unwrap_or(1)` / `else { 1 }` never errors — it always produces a usable (if wrong)
`period`. `MSTL`/`AutoMSTL` are in `exog_capable()` (forecast.rs:648-667) and, in
`forecast_crate_exog`, use this `period` directly when `options.seasonal_periods` is empty:

```rust
ModelType::MSTL | ModelType::AutoMSTL => {
    let periods = if !options.seasonal_periods.is_empty() {
        options.seasonal_periods.clone()
    } else {
        vec![period]
    };
    fit_predict_exog(MSTLForecaster::new(periods), ...)
}
```

So `ts_forecast_exog_by('src', ..., 'MSTL', 12, '1d', MAP{})` (or `_ts_forecast_exog` directly)
on a series whose seasonality the detector cannot find, with at least one non-constant
regressor attached, silently fits `MSTLForecaster::new(vec![1])` — a degenerate,
non-seasonal "seasonal" decomposition — and reports a normal success with `model_name =
"MSTL with regressors"`, instead of the hard, model-naming `InvalidInput` that the identical
non-exog call (`'MSTL'` with no regressors) would correctly raise via `resolve_period`.

This is confirmed by `crates/anofox-fcst-core/tests/forecast_exog.rs:329-335`, whose own
comment documents the gap as a deliberate scope choice ("forecast_with_exog's exog path does
not auto-detect for MSTL — D-07's auto-detection is forecast()'s non-exog scope") rather than
as an error path — but no test exercises the actual failure case (undetectable seasonality +
no explicit period + regressors attached), so the silent-fallback behavior itself is
unverified and, per the locked D-07 decision text (which draws no exog/non-exog distinction),
not authorized.

**Fix:** Route `forecast_with_exog`'s period resolution through the same `resolve_period()`
helper `forecast()` uses, passing `options.model` so `MSTL`/`AutoMSTL` get the same
auto-detect-or-error treatment:

```rust
let period = resolve_period(
    &prep.values,
    options.model,
    options.seasonal_period,
    options.auto_detect_seasonality,
    !options.seasonal_periods.is_empty(),
)?;
```

Then update `forecast_exog.rs`'s `ets_spec_with_regressors` test (or add a new test) to cover
the error path: MSTL + regressors + no seasonal_period + a non-seasonal series must return
`InvalidInput` naming `MSTL`, matching the non-exog behavior exercised by
`forecast_explain_mstl_without_period_errors`.

## Warnings

### WR-01: `forecast_crate_exog` never validates `arima_order`/`seasonal_order` against non-ARIMA models — silently ignored instead of erroring

**File:** `crates/anofox-fcst-core/src/forecast.rs:951-1010` (`forecast_with_exog`, regressors-present branch) vs. `forecast.rs:717-730` (`forecast()`'s equivalent guard)

**Issue:** `forecast()` explicitly rejects `arima_order`/`seasonal_order` set for any model other
than `ARIMA`:

```rust
if options.model != ModelType::ARIMA
    && (options.arima_order.is_some() || options.seasonal_order.is_some())
{
    ...
    return Err(ForecastError::InvalidParameter { param, ... });
}
```

`forecast_with_exog` has no equivalent check. When `prepared_exog` is `None` (no real
regressors), it delegates to `forecast()` via `opts_without_exog: ForecastOptions =
options.into()`, so the guard does run in that branch. But once at least one non-constant
regressor is present, execution goes through `forecast_crate_exog` directly, which never
inspects `options.arima_order`/`options.seasonal_order` for any model other than `ARIMA`
itself — e.g. `ts_forecast_exog_by(..., 'ETS', ..., MAP{'arima_order': '[1,1,1]', ...})` with a
real regressor silently fits `ETS` and ignores `arima_order` instead of raising the same
`InvalidParameter` the non-exog surface would. This is an inconsistency in parameter
validation across the two entry points that the review was specifically asked to check, and it
re-opens exactly the "silently ignored parameter" failure mode D-04 says every former silent
fallback should become an error for.

**Fix:** Add the same `model != ModelType::ARIMA && (arima_order.is_some() ||
seasonal_order.is_some())` guard near the top of `forecast_with_exog`, before the
`exog_capable`/`prepare_exog_regressors` branch, so behavior is independent of whether any
regressor happened to be non-constant.

### WR-02: `ts_forecast_agg` STRUCT return type omits `residuals`/`aic`/`bic`/`mse` despite the function description promising them

**File:** `src/aggregate_functions/ts_forecast_agg.cpp:74-88` (`GetForecastAggResultType`), `src/aggregate_functions/ts_forecast_agg.cpp:667`, `:682` (descriptions)

**Issue:** `GetForecastAggResultType()` builds a 9-field STRUCT: `forecast_step`,
`forecast_timestamp`, `point_forecast`, `lower_<suffix>`, `upper_<suffix>`, `model_name`,
`insample_fitted`, `date_col_name`, `error_message`. There is no `residuals`, `aic`, `bic`, or
`mse` field anywhere in this struct or in `TsForecastAggFinalize`. Yet both registered
functions' `FunctionDescription::description` (lines 667 and 682) claim: *"Returns
STRUCT(point[], lower[], upper[], fitted[], residuals[], model, aic, bic, mse)."* A caller who
reads the registered function description (e.g. via `duckdb_functions()` introspection or the
docs generated from it) will expect fields that do not exist and get a `BINDER_ERROR` on first
use. This directly touches the phase's "truthful model_name"/statistical-correctness-surface
focus: the one place this aggregate *does* carry statistical content (MSE, now computed
correctly elsewhere per D-11/D-12) is advertised but not actually returned here, and residuals
— which this phase specifically fixed the `n_residuals`/`n_fitted` aliasing bug for — aren't
exposed by this entry point at all.

**Fix:** Either extend `GetForecastAggResultType()`/`TsForecastAggFinalize` to actually
populate `residuals`, `aic`, `bic`, `mse` (mirroring `ts_forecast_native.cpp`'s struct), or
correct both `desc.description` strings to list only the fields genuinely returned
(`point[], lower[], upper[], fitted[], model`).

### WR-03: ARIMA order strings silently truncate past 31 bytes instead of erroring

**File:** `src/table_functions/ts_forecast_native.cpp:690-698`, `src/table_functions/ts_cv_forecast_native.cpp:705-716`, `src/aggregate_functions/ts_forecast_agg.cpp:392-399`

**Issue:** `arima_order`/`seasonal_order` are copied into the fixed 32-byte
`arima_order_str`/`seasonal_order_str` buffers via `strncpy`/`memcpy` with a `sizeof(...) - 1`
bound, silently truncating any input longer than 31 characters (e.g. an accidentally
pretty-printed `"[ 1, 1, 1 ]"`-style string with extra whitespace, or a copy-paste error)
instead of raising a clear "`arima_order` too long" `InvalidInputException` at bind time. The
Rust-side strict parser (`parse_order_triplet`) will usually catch the resulting malformed
string and error, but it errors with a confusing message about the *truncated* string rather
than the one the user actually passed, and in principle a sufficiently long, digit-heavy input
could truncate into a different, validly-parsed-but-wrong triplet (e.g. truncating mid-integer
into fewer digits that still satisfy `p,q <= 10`/`d <= 2`). This is a narrow edge case but
avoidable with a one-line length check at the same bind sites that already validate
`confidence_level`'s range.

**Fix:** Add an explicit length check at bind/parse time, e.g.:

```cpp
if (bind_data->arima_order.size() > sizeof(ForecastOptions::arima_order_str) - 1) {
    throw InvalidInputException("arima_order value is too long (max 31 characters)");
}
```

applied consistently across `ts_forecast_native.cpp`, `ts_cv_forecast_native.cpp`,
`ts_forecast_agg.cpp`, `ts_forecast_scalar.cpp`, and `ts_forecast.cpp`'s exog path.

---

_Reviewed: 2026-10-09T17:18:08Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
