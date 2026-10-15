---
phase: 17-upstream-consumption-diagnostics-parity
reviewed: 2026-10-15T16:57:07Z
depth: standard
files_reviewed: 28
files_reviewed_list:
  - Cargo.toml
  - crates/anofox-fcst-core/src/forecast.rs
  - crates/anofox-fcst-core/src/lib.rs
  - crates/anofox-fcst-core/src/validation.rs
  - crates/anofox-fcst-core/tests/forecast_intervals.rs
  - crates/anofox-fcst-core/tests/interval_coverage_mc.rs
  - crates/anofox-fcst-core/tests/portmanteau_reference.rs
  - crates/anofox-fcst-core/tests/r_forecast_parity.rs
  - crates/anofox-fcst-core/tests/r_parity_diagnosis.rs
  - crates/anofox-fcst-core/tests/stationarity_reference.rs
  - crates/anofox-fcst-core/tests/theta_croston_fitted.rs
  - crates/anofox-fcst-ffi/src/lib.rs
  - crates/anofox-fcst-ffi/src/types.rs
  - crates/anofox-fcst-ffi/tests/core_ffi_parity.rs
  - src/include/anofox_fcst_ffi.h
  - src/macros/ts_macros.cpp
  - src/scalar_functions/diagnostics.cpp
  - test/sql/ts_diagnostics_reference.test
  - test/sql/ts_diagnostics.test
  - test/sql/ts_forecast_crate_delegation.test
  - test/sql/ts_forecast_ets_model.test
  - test/sql/ts_forecast_reference.test
  - test/sql/ts_model_distinctness.test
  - test/sql/ts_native_param_validation.test
  - validation/reference/python/gen_stationarity_reference.py
  - validation/reference/r/common.R
  - validation/reference/r/gen_diagnostics_reference.R
  - validation/reference/r/gen_forecast_reference.R
findings:
  critical: 0
  warning: 1
  info: 2
  total: 3
status: issues_found
---

# Phase 17: Code Review Report

**Reviewed:** 2026-10-15T16:57:07Z
**Depth:** standard
**Files Reviewed:** 28
**Status:** issues_found

## Summary

This phase bumps `anofox-forecast` to 0.16.1 and adds ADF/KPSS `regression := 'c'|'ct'` support
(including a locally-implemented trend KPSS not yet available upstream), the D-04 four-way
stationarity verdict, Ljung-Box `fitted_params`/`df` validation, and the `'Holt' -> ETS(A,A,N)`
remap to match R's `holt()` definition, plus a large R-parity / Monte-Carlo test suite.

The statistical-correctness surface is unusually well defended: every new numeric result
(ADF/KPSS statistics, critical values, trend-KPSS p-values, Ljung-Box `df`/p-values, the Holt
remap) is pinned against R/statsmodels fixtures with tight (1e-8..1e-12) tolerances, cross-checked
between statsmodels and R `tseries`/`urca` (`cross_tool_guard` in
`gen_stationarity_reference.py`), and independently re-derived through the FFI boundary in
`core_ffi_parity.rs::parity_holt`. The `repr(C)` FFI structs (`AnofoxStationarityResult`,
`AnofoxCombinedStationarityResult`, `AnofoxLjungBoxResult`, etc.) match the generated C header and
the C++ `STRUCT` child-list order field-for-field — no marshalling drift found. Buffer sizes for
NUL-terminated string fields (`verdict: [c_char; 32]`, `interpretation: [c_char; 32]`) comfortably
fit the longest possible values (`"difference_stationary"` = 21 chars). `copy_string_to_buffer` /
`AnofoxError::set_error` both truncate safely rather than overflow.

One functional bug was found: the Ljung-Box and residual-diagnostics scalar functions added/extended
in this phase did not carry forward the `FunctionNullHandling::SPECIAL_HANDLING` fix that `ts_adf`
and `ts_kpss` *did* receive in the same phase for the identical reason, so an explicit SQL `NULL`
for `lags`/`fitted_params`/`alpha` silently produces a NULL row instead of running with the
documented "auto" default. This does not affect the `_by` macros (which always pass literal
defaults, never `NULL`), but it does affect any direct SQL caller — and it is the kind of
regression the same file already has a documented, named fix for.

## Warnings

### WR-01: `ts_ljung_box`/`ts_residual_diagnostics` optional args missing `SPECIAL_HANDLING`, unlike their newly-fixed siblings in the same file

**File:** `src/scalar_functions/diagnostics.cpp:852-965` (registration), `598-655` and `725-778` (function bodies)

**Issue:** `RegisterTsAdfFunction`/`RegisterTsKpssFunction`/`RegisterTsStationarityFunction` set
`null_handling = FunctionNullHandling::SPECIAL_HANDLING` on every overload that has an
optional-but-NULL-meaningful argument (`max_lags`, `lags`, `regression`), with an explicit comment
explaining why: DuckDB's `DEFAULT_NULL_HANDLING` short-circuits the **entire row** to SQL `NULL`
whenever *any* argument is `NULL`, before the function body ever runs — a documented, previously-hit
bug class in this codebase (see `src/scalar_functions/conformal.cpp`'s `ts_conformal_apply_score`
fix, referenced directly in `diagnostics.cpp`'s own comments: "mirrors conformal.cpp's
`ts_conformal_intervals` fix").

`RegisterTsLjungBoxFunction`'s 2-arg (`lags`) and 3-arg (`lags, fitted_params`) overloads — the
3-arg overload is new in this phase (commit `8c85a67`, "Ljung-Box fitted_params end-to-end") — and
`RegisterTsResidualDiagnosticsFunction`'s 2-arg (`alpha`) overload do **not** set
`null_handling`. Concretely:

```sql
-- Returns a full NULL struct instead of running with the documented "auto" lags:
SELECT ts_ljung_box(LIST(resid ORDER BY ds), NULL, 2) FROM t;
-- Returns a full NULL struct instead of alpha=0.05:
SELECT ts_residual_diagnostics(LIST(resid ORDER BY ds), NULL) FROM t;
```

This contradicts the function's own documented semantics ("Default lags = min(10, n/5)... pass
fitted_params := p + q") for any caller who passes an explicit `NULL` lags/alpha value (a reasonable
thing to do when only overriding `fitted_params`, exactly as `ts_adf(series, NULL, 'ct')` is a
documented-valid pattern two functions above it in the same file). The `ts_ljung_box_by` /
`ts_residual_diagnostics_by` macros are unaffected (they always pass a literal default, e.g.
`{"lags", "-1"}`, never `NULL`), so this is reachable only via direct scalar-function calls, but it
is exactly the regression class this same phase fixed for `ts_adf`/`ts_kpss`/`ts_stationarity`.

**Fix:**
```cpp
ScalarFunction ljung_box_2arg(
    {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::INTEGER)},
    rt, TsLjungBoxFunction);
ljung_box_2arg.null_handling = FunctionNullHandling::SPECIAL_HANDLING;
s.AddFunction(ljung_box_2arg);

ScalarFunction ljung_box_3arg(
    {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::INTEGER),
     LogicalType(LogicalTypeId::INTEGER)},
    rt, TsLjungBoxFunction);
ljung_box_3arg.null_handling = FunctionNullHandling::SPECIAL_HANDLING;
s.AddFunction(ljung_box_3arg);
```
and the same for the `anofox_fcst_ts_ljung_box` alias set and `ts_residual_diagnostics`'s 2-arg
`alpha` overload. Add a `test/sql` case (mirroring `ts_diagnostics_reference.test`'s "a NULL
regression means 'c'" case at line 113-118) asserting `ts_ljung_box(series, NULL, 2)` runs with the
default lags rather than returning NULL.

## Info

### IN-01: `anofox_ts_ljung_box`'s negative-`fitted_params` validation is skipped on the `length == 0` fast path

**File:** `crates/anofox-fcst-ffi/src/lib.rs:7930-7941`

**Issue:** `anofox_ts_ljung_box` returns `true` (success, default/NaN result) immediately when
`length == 0`, before the `fitted_params < 0` check runs. A caller passing an empty residuals list
together with a negative `fitted_params` silently succeeds instead of getting the
`"fitted_params must be >= 0"` error it would get for any non-empty input. Low impact (empty-series
early-return is the established pattern across every diagnostic FFI function in this file), but
worth a one-line fix for consistency since `fitted_params` validation is otherwise unconditional.

**Fix:** Move the `fitted_params < 0` check above the `length == 0` early return (the `out_error`/
`out_result` initialization already happens first either way).

### IN-02: `kpss_trend`'s `lags.unwrap_or(0)` on the `n < 4` degenerate path doesn't reflect the resolved bandwidth

**File:** `crates/anofox-fcst-core/src/validation.rs:243-255`

**Issue:** When `n < 4`, `kpss_trend` returns `lags: lags.unwrap_or(0)` — i.e., the caller's raw
override if given, or a hardcoded `0` if not — rather than resolving through the same
`.min(n / 2).max(1)` clamp the non-degenerate path uses. Since the statistic is `NaN` either way
(and no test currently asserts `lags` for this branch), this is cosmetic, but it means
`ts_kpss(short_series, NULL, 'ct')).lags` reports `0` rather than the `1` every other
degenerate/clamped path in this file would produce (c.f. `kpss`'s own `n < 4`-adjacent behavior and
the ADF `n < 4` path, which both report a consistent sentinel). Low value fixing given the primary
signal (`NaN` statistic) is unambiguous, but flagging since a future caller sorting/filtering on
`lags` for degenerate rows could be surprised.

**Fix:** `lags: lags.unwrap_or(0).min(n.max(1) / 2).max(1)` (or simply pin a test asserting the
current `0` is intentional, if it is).

---

_Reviewed: 2026-10-15T16:57:07Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
