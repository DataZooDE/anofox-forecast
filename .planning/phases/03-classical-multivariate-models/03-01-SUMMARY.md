---
phase: 03-classical-multivariate-models
plan: "01"
subsystem: forecasting-models
tags: [garch, kalman, ffi-abi, ts_forecast_by, rust, cpp]
status: complete

requires:
  - 01-diagnostics-validation/01-3-SUMMARY.md
  - 02-global-panel-models/02-1-SUMMARY.md
provides:
  - GARCH conditional volatility via ts_forecast_by method='GARCH'
  - Kalman filter forecasting via ts_forecast_by method='Kalman'
  - Extended ForecastOptions ABI (garch_p, garch_q, kalman_model) across all layers
affects:
  - crates/anofox-fcst-core/src/forecast.rs
  - crates/anofox-fcst-ffi/src/types.rs
  - crates/anofox-fcst-ffi/src/lib.rs
  - src/include/anofox_fcst_ffi.h
  - src/table_functions/ts_forecast_native.cpp
  - src/scalar_functions/ts_forecast_scalar.cpp
  - examples/forecasting/classical_forecasting_examples.sql

tech-stack:
  added:
    - ModelType::GARCH — new enum variant dispatching to forecast_garch (sqrt-of-variance volatility)
    - ModelType::Kalman — new enum variant dispatching to forecast_kalman (KalmanForecaster trait)
    - ForecastOptions fields: garch_p/garch_q (usize), kalman_model (Option<String>) in Rust core
    - ForecastOptions C-struct fields: garch_p/garch_q (c_int), kalman_model ([c_char; 32]) in FFI
  patterns:
    - GARCH output is conditional volatility (std-dev = sqrt(forecast_variance(h))), not variance
    - Kalman default spec is local_level; local_linear_trend selectable via params MAP
    - ForecastOptions ABI extension is strictly additive (fields appended, no reorder, no removal)
    - cbindgen regenerates anofox_fcst_ffi.h after any types.rs edit (make header is mandatory)
    - _ts_forecast_scalar (scalar aggregate backing ts_forecast_by) requires its own ValidateParams update independent of _ts_forecast_native

key-files:
  created:
    - examples/forecasting/classical_forecasting_examples.sql
  modified:
    - crates/anofox-fcst-core/src/forecast.rs (ModelType enum, ForecastOptions, dispatch, forecast_garch, forecast_kalman, 5 tests)
    - crates/anofox-fcst-ffi/src/types.rs (3 new fields in ForecastOptions + ForecastOptionsExog)
    - crates/anofox-fcst-ffi/src/lib.rs (kalman_model CStr parsing in anofox_ts_forecast + build_core_options; 2 FFI tests)
    - src/include/anofox_fcst_ffi.h (regenerated via make header — 6 new field occurrences)
    - src/table_functions/ts_forecast_native.cpp (param plumbing for _ts_forecast_native path)
    - src/scalar_functions/ts_forecast_scalar.cpp (param plumbing for _ts_forecast_scalar / ts_forecast_by path)

decisions:
  - key: GARCH output is volatility not variance
    rationale: GARCH::predict() returns seeded simulated innovations; forecast_variance(h) gives the analytical conditional variance. The plan mandates sqrt(forecast_variance) = volatility. This is semantically correct for financial risk use cases.
  - key: Scalar function requires separate ValidateParams update
    rationale: ts_forecast_by macro routes through _ts_forecast_scalar (src/scalar_functions/ts_forecast_scalar.cpp), NOT _ts_forecast_native (src/table_functions/ts_forecast_native.cpp). Both files have independent ValidateParams logic. The plan only listed ts_forecast_native.cpp in files_modified; ts_forecast_scalar.cpp was added as a deviation (Rule 3 — blocking fix).
  - key: Kalman flat forecast for local_level is correct
    rationale: KalmanForecaster::local_level() implements random walk + noise. The optimal h-step ahead forecast under that model is a flat line at the filtered state. Observed: all 7 steps return 121.1919 (correct).
  - key: Column names in SQL examples must be unquoted identifiers
    rationale: DuckDB macro column arguments are resolved as column references, not string literals. 'asset_id' triggers "ORDER BY non-integer literal" error; asset_id resolves correctly to the column. Phase 2 lesson applied proactively per plan context.

metrics:
  duration: "~38 minutes (including previous context window)"
  completed: "2026-08-22"
  tasks_completed: 3
  commits: 2

actuals:
  tokens: 92000
  tasks: 3
  commits: 2
---

# Phase 03 Plan 01: Classical & Multivariate Models (GARCH + Kalman) Summary

GARCH conditional volatility and Kalman filter state-space forecasting wired end-to-end through Rust FFI → C++ scalar function → ts_forecast_by SQL macro, with ForecastOptions ABI extended additively across all layers.

## What Was Built

### Task 1 + 2: ForecastOptions ABI extension + GARCH/Kalman dispatch (commit 0c805f7)

Extended ForecastOptions in both the Rust core and the FFI C struct with three new fields: `garch_p`, `garch_q`, `kalman_model`. Regenerated `src/include/anofox_fcst_ffi.h` via `make header` (cbindgen). Added `ModelType::GARCH` and `ModelType::Kalman` enum variants with `FromStr` arms and `name()` methods. Implemented `forecast_garch` (using `GARCH::forecast_variance(h)` + element-wise sqrt for volatility, NOT predict()) and `forecast_kalman` (using `KalmanForecaster::local_level()` or `local_linear_trend()` based on the `kalman_model` param). FFI lib.rs parses `kalman_model` from the C char array via `CStr::from_ptr(...).to_str().ok().filter(|s| !s.is_empty())`. Param plumbing added to `ts_forecast_native.cpp` (for the `_ts_forecast_native` table function path). Five unit tests in core + two FFI tests all pass.

### Task 3: _ts_forecast_scalar param wiring + end-to-end example (commit 1b75e64)

Discovered that `ts_forecast_by` macro routes through `_ts_forecast_scalar` (in `src/scalar_functions/ts_forecast_scalar.cpp`), not `_ts_forecast_native`. That file had independent `ValidateParams` logic that rejected `kalman_model`, `garch_p`, `garch_q`. Added all three keys to its `valid_keys` set, parsed them from MAP params, and populated them into `ForecastOptions` before the FFI call. Created `examples/forecasting/classical_forecasting_examples.sql` and verified all four queries produce correct output against the built extension.

## Verification Results

All four acceptance queries pass against `build/release/duckdb -unsigned`:

| Query | Result |
|-------|--------|
| GARCH(1,1) default | 7 rows, model_name='GARCH(1,1)', volatility 0.457–0.548 (mean-reverting) |
| GARCH explicit p=1, q=1 via params | 7 identical rows (params propagated correctly) |
| Kalman local_level (default) | 7 rows, flat forecast 121.1919 (correct for random walk + noise) |
| Kalman local_linear_trend | 7 rows, increasing ~0.98/step (distinct from local_level — trend captured) |

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking Fix] ts_forecast_scalar.cpp ValidateParams not updated**

- **Found during:** Task 3 verification — `ts_forecast_by(..., params := MAP{'kalman_model':'local_linear_trend'})` threw `Unknown parameter(s): 'kalman_model'`
- **Root cause:** `ts_forecast_by` macro expands to `_ts_forecast_scalar`, a scalar aggregate function in `src/scalar_functions/ts_forecast_scalar.cpp`. This file has its own `ValidateParams` set and param parsing block, independent of `ts_forecast_native.cpp`. The plan only listed `ts_forecast_native.cpp` in `files_modified`.
- **Fix:** Added `garch_p`, `garch_q`, `kalman_model` to `ValidateParams` valid_keys; parsed them from MAP; populated `opts.garch_p`, `opts.garch_q`, `opts.kalman_model` in the per-row FFI call path; added `TsForecastScalarBindData` fields and `Copy()` entries.
- **Files modified:** `src/scalar_functions/ts_forecast_scalar.cpp`
- **Commit:** 1b75e64

**2. [Rule 1 - Bug] Example SQL used quoted column identifiers and wrong column alias**

- **Found during:** Task 3 first execution — "ORDER BY non-integer literal has no effect" + "Referenced column 'forecast_value' not found"
- **Root cause:** DuckDB macro column args must be unquoted identifiers (`asset_id`, `ds`, `y`), not quoted strings (`'asset_id'`, `'ds'`, `'y'`). The macro output column is `yhat`, not `forecast_value`.
- **Fix:** Changed all four `ts_forecast_by` calls in the example file to use unquoted column refs; changed `forecast_value` to `yhat`.
- **Files modified:** `examples/forecasting/classical_forecasting_examples.sql`
- **Commit:** 1b75e64

## Requirements Satisfied

| Requirement | Status | Evidence |
|-------------|--------|----------|
| CLAS-01: GARCH conditional volatility via ts_forecast_by | Complete | 7-row output, model_name='GARCH(1,1)', values=sqrt(variance) |
| CLAS-02: Kalman filter via ts_forecast_by | Complete | 7-row output, both local_level + local_linear_trend verified |

## Known Stubs

None. All wired through to production dispatch.

## Self-Check: PASSED

- `0c805f7` — confirmed in git log
- `1b75e64` — confirmed in git log
- `src/scalar_functions/ts_forecast_scalar.cpp` — exists and contains ValidateParams update
- `examples/forecasting/classical_forecasting_examples.sql` — exists and produces 28 output rows across 4 queries
