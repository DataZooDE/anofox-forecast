---
phase: 02-global-panel-models
plan: 1
subsystem: forecasting
tags: [rust-ffi, duckdb-extension, panel-forecasting, global-ets, cpp-table-function, time-series]

requires:
  - phase: 01-diagnostics-demand-classification
    provides: Rust FFI patterns, C++ table function / macro registration conventions established in Phase 1

provides:
  - ts_forecast_panel_by SQL macro wrapping _ts_forecast_panel_native (GlobalETS, fit-once-emit-many)
  - anofox_ts_forecast_panel Rust FFI export (PanelForecastResult, anofox_free_panel_forecast_result)
  - PanelForecastResult C struct (flat forecasts buffer, n_series, n_horizon, model_name[64])
  - Ragged-panel alignment to shared date grid inside C++ Finalize barrier
  - Drop rule: series < 10 valid observations surfaced as DROPPED: too_short rows (not fatal)
  - Runnable example: examples/forecasting/global_panel_forecasting_examples.sql (verified end-to-end)

affects:
  - 02-2 (GlobalTheta + GlobalCroston expand from this tracer's architecture)
  - any future panel/global model additions to the extension

actuals:
  tokens: 15489
  tasks: 3
  commits: 3

tech-stack:
  added:
    - GlobalAutoETS + ModelPool from anofox-forecast 0.15.3 (moved from dev-dep to dep in anofox-fcst-ffi)
    - fill_nulls_interpolate from anofox-fcst-core for NaN gap imputation before FFI call
  patterns:
    - fit-once-emit-many panel pattern (single FFI call across all series, not per-group loop)
    - PanelForecastError wrapper enum for dual error-type boundary (anofox_fcst_core vs anofox_forecast)
    - subselect TABLE arg in macro SQL: (SELECT ... FROM query_table(...)) not query_table(...) directly
    - Finalize barrier + shared date grid (union of all series dates) for ragged-panel alignment

key-files:
  created:
    - crates/anofox-fcst-ffi/src/types.rs (PanelForecastResult struct added)
    - src/include/ts_forecast_panel_native.hpp
    - src/table_functions/ts_forecast_panel_native.cpp
    - examples/forecasting/global_panel_forecasting_examples.sql
  modified:
    - crates/anofox-fcst-ffi/src/lib.rs (FFI export + free + inner impl + tests)
    - crates/anofox-fcst-ffi/Cargo.toml (anofox-forecast moved to dependencies)
    - crates/anofox-fcst-ffi/cbindgen.toml (PanelForecastResult added to export include)
    - src/macros/ts_macros.cpp (ts_forecast_panel_by macro entry)
    - src/anofox_forecast_extension.cpp (include + registration)
    - CMakeLists.txt (ts_forecast_panel_native.cpp added to source list)

key-decisions:
  - "GlobalETS safe_period=1 when seasonal_period=0: GlobalAutoETS::new(0, pool) panics at t%period; mapping 0→1 gives non-seasonal candidates only (period=1 makes has_seasonal=false) without any API change"
  - "PanelForecastError wrapper enum: GlobalAutoETS::fit returns anofox_forecast::ForecastError, not anofox_fcst_core::ForecastError; ? operator requires From impl; created wrapper instead of duplicating error variants"
  - "Subselect TABLE arg pattern: query_table(source::VARCHAR) passed directly as TABLE arg to a table-in-out function causes silent parse failure at macro registration; wrapping in a subselect (SELECT col1, col2, col3 FROM query_table(...)) is the established working pattern in this codebase"
  - "Minimum 3 series for global fit: panel with fewer than 3 usable series after alignment throws InvalidInputException (global model requires multiple series for cross-series learning)"
  - "anofox-forecast moved to [dependencies]: was in [dev-dependencies], making GlobalAutoETS/ModelPool unavailable in production FFI code"

patterns-established:
  - "Panel macro → subselect TABLE arg: use (SELECT g, d, v FROM query_table(...)) not query_table(...) directly as TABLE arg"
  - "fit-once-emit-many: single anofox_ts_forecast_panel call across whole panel; C++ emits rows from flat result buffer"
  - "PanelForecastError wrapper for dual-crate error boundary in FFI crate"
  - "Drop rule surfaced as DROPPED rows: short/empty series produce forecast_step rows with NaN yhat and model_name='DROPPED: too_short'"

requirements-completed: [GLOB-01]

coverage:
  - id: D1
    description: "ts_forecast_panel_by macro registered and callable in DuckDB SQL"
    requirement: GLOB-01
    verification:
      - kind: integration
        ref: "LOAD extension; SELECT function_name FROM duckdb_functions() WHERE function_name = 'ts_forecast_panel_by' → 1 row"
        status: pass
    human_judgment: false
  - id: D2
    description: "GlobalETS panel forecast returns n_series * horizon rows for a ragged 3-series panel"
    requirement: GLOB-01
    verification:
      - kind: e2e
        ref: "examples/forecasting/global_panel_forecasting_examples.sql Section 1 — 3 series × 14 steps = 42 total rows"
        status: pass
    human_judgment: false
  - id: D3
    description: "Short series (< 10 valid observations) surfaced as DROPPED: too_short rows, not fatal"
    requirement: GLOB-01
    verification:
      - kind: e2e
        ref: "examples/forecasting/global_panel_forecasting_examples.sql Section 3 — ShortX → model_name='DROPPED: too_short'"
        status: pass
    human_judgment: false
  - id: D4
    description: "Rust FFI unit tests: happy path, NaN imputation, unknown method"
    requirement: GLOB-01
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-ffi/src/lib.rs#panel_ffi_tests — cargo test --test integration (3 tests pass)"
        status: pass
    human_judgment: false

duration: ~90min
completed: 2026-08-21
status: complete
---

# Phase 02 Plan 1: GlobalETS Panel Forecasting Tracer Summary

**GlobalETS fit-once-emit-many panel architecture proven end-to-end: Rust FFI PanelForecastResult → C++ ragged-alignment Finalize → ts_forecast_panel_by SQL macro returning per-series forecasts for a 3-series ragged panel**

## Performance

- **Duration:** ~90 min
- **Started:** 2026-08-21T18:00:00Z (approx, continued from prior session)
- **Completed:** 2026-08-21T19:44:31Z
- **Tasks:** 3
- **Files modified:** 10 source files + CMakeLists.txt = 11 total

## Accomplishments

- `anofox_ts_forecast_panel` FFI export + `PanelForecastResult` C struct + `anofox_free_panel_forecast_result` free function, all with `catch_unwind` panic safety and 3 unit tests
- `_ts_forecast_panel_native` C++ table-in-out function: Finalize barrier → shared date grid (union of dates) → alignment → drop rule (< 10 valid → `DROPPED: too_short`) → single FFI call → row emission
- `ts_forecast_panel_by` SQL macro registered and verified: `A=4, B=4, C=4` rows for a 3-series ragged panel, DROPPED rows for short series, seasonal GlobalETS (period=7) with sensible sinusoidal output
- `examples/forecasting/global_panel_forecasting_examples.sql` verified end-to-end against built extension (PR #230 rule)

## Task Commits

1. **Task 1: Rust FFI export (PanelForecastResult + anofox_ts_forecast_panel)** - `5d1be9c` (feat)
2. **Task 2: C++ table function + ts_forecast_panel_by macro** - `7a93b55` (feat)
3. **Task 3: GlobalETS panel forecasting runnable example** - `559ea2f` (feat)

## Files Created/Modified

- `crates/anofox-fcst-ffi/src/types.rs` — added `PanelForecastResult` repr(C) struct
- `crates/anofox-fcst-ffi/src/lib.rs` — added FFI export, free fn, `forecast_panel_impl` inner fn, 3 unit tests
- `crates/anofox-fcst-ffi/Cargo.toml` — moved `anofox-forecast` from dev-dep to dep
- `crates/anofox-fcst-ffi/cbindgen.toml` — added `PanelForecastResult` to export include list
- `src/include/ts_forecast_panel_native.hpp` — (new) forward declaration
- `src/table_functions/ts_forecast_panel_native.cpp` — (new) ~748-line C++ implementation
- `src/macros/ts_macros.cpp` — added `ts_forecast_panel_by` macro entry
- `src/anofox_forecast_extension.cpp` — added include + `RegisterTsForecastPanelNativeFunction` call
- `CMakeLists.txt` — added `ts_forecast_panel_native.cpp` to source list
- `examples/forecasting/global_panel_forecasting_examples.sql` — (new) 3-section runnable example

## Decisions Made

- `GlobalAutoETS::new(0, pool)` panics at `t % period` when `period=0`. Mapped `seasonal_period=0` to `safe_period=1` in `forecast_panel_impl` — with period=1, `has_seasonal = (1 > 1) = false`, so only non-seasonal candidates are selected; `t % 1 = 0` always, no panic.
- Created `PanelForecastError` wrapper enum to bridge `anofox_forecast::ForecastError` and `anofox_fcst_core::ForecastError` — `From` is not implemented cross-crate, so `?` would not compile without a wrapper.
- Minimum 3 series enforced after alignment drop: global models require cross-series learning; fewer than 3 kept series raises `InvalidInputException`.
- `anofox-forecast` moved from `[dev-dependencies]` to `[dependencies]` in the FFI crate — `GlobalAutoETS`/`ModelPool` are production imports, not test-only.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] macro SQL used `query_table(source::VARCHAR)` directly as TABLE argument**
- **Found during:** Task 2 (ts_forecast_panel_by macro registration)
- **Issue:** DuckDB silently fails to register a macro whose SQL body passes `query_table(...)` directly as the TABLE argument to a table-in-out function — the macro parse succeeds but `_ts_forecast_panel_native` returns 0 rows from `duckdb_functions()`. Every other native table function in this codebase uses a subselect `(SELECT ... FROM query_table(...))` instead.
- **Fix:** Changed macro SQL to `(SELECT group_col, date_col, target_col::DOUBLE FROM query_table(source::VARCHAR))` — the established subselect pattern.
- **Files modified:** `src/macros/ts_macros.cpp`
- **Verification:** After fix, `duckdb_functions()` returns `_ts_forecast_panel_native` (table), `ts_forecast_panel_by` (table_macro), and `anofox_fcst_ts_forecast_panel_by` (table_macro).
- **Committed in:** `7a93b55` (Task 2 commit)

**2. [Rule 1 - Bug] `GlobalAutoETS::new(0, pool).fit()` panics with period=0**
- **Found during:** Task 1 Rust FFI unit tests
- **Issue:** `t % period` integer division by zero when `seasonal_period=0`
- **Fix:** `safe_period = if seasonal_period == 0 { 1 } else { seasonal_period }` before `GlobalAutoETS::new`
- **Files modified:** `crates/anofox-fcst-ffi/src/lib.rs`
- **Committed in:** `5d1be9c` (Task 1 commit)

**3. [Rule 3 - Blocking] `anofox-forecast` not importable in FFI production code**
- **Found during:** Task 1 (`use anofox_forecast::models::exponential::{GlobalAutoETS, ModelPool}` failed)
- **Issue:** `anofox-forecast` was in `[dev-dependencies]` only, making it unavailable in production FFI exports
- **Fix:** Moved to `[dependencies]` in `crates/anofox-fcst-ffi/Cargo.toml`
- **Files modified:** `crates/anofox-fcst-ffi/Cargo.toml`
- **Committed in:** `5d1be9c` (Task 1 commit)

---

**Total deviations:** 3 auto-fixed (2 Rule 1 bugs, 1 Rule 3 blocking)
**Impact on plan:** All auto-fixes necessary for correctness. No scope creep. The subselect TABLE arg pattern is now the documented convention for future panel macros (02-2).

## Issues Encountered

- Silent macro registration failure: no exception, no log, just 0 rows in `duckdb_functions()`. Diagnosed by comparing `nm` symbol output (symbols present) vs `duckdb_functions()` output (empty), then by comparing the panel macro SQL with the working `ts_cv_forecast_by` SQL — the direct `query_table()` TABLE arg pattern is the distinguishing factor.

## Next Phase Readiness

- Panel architecture proven end-to-end — 02-2 (GlobalTheta + GlobalCroston) can expand directly from the `forecast_panel_impl` match arm pattern without any new FFI struct or C++ infrastructure
- `[02-2] GlobalTheta + GlobalCroston sections appended here` marker in the example file is the intended expansion point
- No blockers; all GLOB-01 must_haves verified

---
*Phase: 02-global-panel-models*
*Completed: 2026-08-21*
