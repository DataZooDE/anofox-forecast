---
phase: 02-global-panel-models
plan: 1
type: execute
wave: 1
depends_on: []
files_modified:
  - crates/anofox-fcst-ffi/src/types.rs
  - crates/anofox-fcst-ffi/src/lib.rs
  - src/include/ts_forecast_panel_native.hpp
  - src/table_functions/ts_forecast_panel_native.cpp
  - src/macros/ts_macros.cpp
  - src/anofox_forecast_extension.cpp
  - CMakeLists.txt
  - examples/forecasting/global_panel_forecasting_examples.sql
autonomous: true
requirements: [GLOB-01]
estimate:
  tokens: 118000
  raw_tokens: 59000
  tasks: 3
  confidence: low
must_haves:
  truths:
    - "ts_forecast_panel_by(source, group_col, date_col, target_col, 'GlobalETS', horizon, frequency) returns one row per (series, horizon step) for a ragged 3-series panel (D-Area1, D-Area3, GLOB-01)"
    - "GlobalETS is fit ONCE across the whole panel (single anofox_ts_forecast_panel FFI call), not once per group (D-Area1)"
    - "Ragged series are auto-aligned to a shared date grid inside the table function before the FFI call; intra-series nulls are imputed via fill_nulls_interpolate (D-Area2)"
    - "Series too short or all-null after alignment are dropped and surfaced (model_name = 'DROPPED: too_short') rather than failing the whole call (D-Area2)"
    - "The built extension loads and the GlobalETS section of the example returns rows end-to-end (PR #230 rule)"
  artifacts:
    - crates/anofox-fcst-ffi/src/types.rs
    - crates/anofox-fcst-ffi/src/lib.rs
    - src/include/ts_forecast_panel_native.hpp
    - src/table_functions/ts_forecast_panel_native.cpp
    - src/macros/ts_macros.cpp
    - src/anofox_forecast_extension.cpp
    - CMakeLists.txt
    - examples/forecasting/global_panel_forecasting_examples.sql
  key_links:
    - "ts_forecast_panel_by macro -> _ts_forecast_panel_native table function -> anofox_ts_forecast_panel FFI export -> GlobalAutoETS::fit/predict"
    - "C++ shared-grid alignment -> flat f64 matrix (NaN for missing) -> Rust fill_nulls_interpolate -> equal-length Vec<Vec<f64>>"
    - "PanelForecastResult heap buffer -> C++ emit loop -> anofox_free_panel_forecast_result (no leak)"
---

<objective>
Deliver the complete GlobalETS panel-forecasting vertical slice end-to-end: a new FFI export, a new native table function that aligns a ragged panel to a shared date grid and dispatches a single cross-series fit, a user-facing `ts_forecast_panel_by` macro, and a runnable example verified against the built extension. This is the tracer — it wires ONE method (GlobalETS) through every layer the phase touches so the architecture is proven before GlobalTheta/GlobalCroston expand out from it.

Purpose: Prove the fit-once-emit-many panel architecture (distinct from per-series `ts_forecast_by`) works end-to-end through FFI, C++ table function, macro, and SQL before adding more methods. Satisfies GLOB-01.
Output: A loadable extension where `ts_forecast_panel_by(..., 'GlobalETS', ...)` returns per-series forecasts for a ragged 3-series panel.
</objective>

<execution_context>
@$HOME/.claude/gsd-core/workflows/execute-plan.md
@$HOME/.claude/gsd-core/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/ROADMAP.md
@.planning/STATE.md
@.planning/phases/02-global-panel-models/02-CONTEXT.md
@.planning/phases/02-global-panel-models/02-RESEARCH.md
@.planning/phases/02-global-panel-models/02-PATTERNS.md
</context>

<artifacts_this_phase_produces>
New symbols introduced by this plan (exclude from drift verification — they are newly created here, not pre-existing):
- FFI: `anofox_ts_forecast_panel` (export), `anofox_free_panel_forecast_result` (export), `PanelForecastResult` (struct in types.rs)
- C++: `_ts_forecast_panel_native` (table function), `RegisterTsForecastPanelNativeFunction` (registration), struct types `TsForecastPanelNativeBindData`, `TsForecastPanelNativeGlobalState`, `TsForecastPanelNativeLocalState`, `PanelOutputRow`
- Header: `src/include/ts_forecast_panel_native.hpp`
- Macro: `ts_forecast_panel_by` (and its auto-generated `anofox_fcst_ts_forecast_panel_by` alias)
- New param MAP keys: `seasonal_period`, `model_pool` (GlobalETS); `croston_variant` reserved for 02-2
- New files: `examples/forecasting/global_panel_forecasting_examples.sql`
Phase-wide (02-2 / 02-3 add): 4 docs under docs/reference/models/*, docs/api/07-forecasting.md panel section, benchmark/configs/global_ets.py, benchmark/configs/statsforecast_global.py, benchmark/m4/global_benchmark/run.py + results/*.parquet
</artifacts_this_phase_produces>

<tasks>

<task type="tracer" tdd="true">
  <name>Task 1: GlobalETS panel FFI export + PanelForecastResult struct — one method, end to end at the FFI boundary</name>
  <files>crates/anofox-fcst-ffi/src/types.rs, crates/anofox-fcst-ffi/src/lib.rs</files>
  <read_first>
    - crates/anofox-fcst-ffi/src/types.rs:328-368 (ForecastResult struct + Default impl — the shape to mirror for PanelForecastResult)
    - crates/anofox-fcst-ffi/src/types.rs:16-28 (ErrorCode enum — reuse existing codes, add none)
    - crates/anofox-fcst-ffi/src/lib.rs:138-179 (anofox_ts_stats — canonical init_error/catch_unwind/set_error skeleton)
    - crates/anofox-fcst-ffi/src/lib.rs:3343-3427 (anofox_ts_forecast — closest analog: null checks, catch_unwind, Ok(Ok)/Ok(Err)/Err match arms)
    - crates/anofox-fcst-core/src/imputation.rs:62-116 (fill_nulls_interpolate — the imputation to call per series)
    - crates/anofox-fcst-core/src/lib.rs:75-77 (confirm fill_nulls_interpolate is re-exported)
    - .planning/phases/02-global-panel-models/02-RESEARCH.md (Research Target 1: GlobalAutoETS::new(period, ModelPool)/fit/predict; ModelPool::Reduced default; Research Target 2: panel FFI signature)
  </read_first>
  <behavior>
    Add a Rust unit test in crates/anofox-fcst-ffi (module `#[cfg(test)] mod panel_ffi_tests`) that calls anofox_ts_forecast_panel via its safe-callable body (or a thin internal helper `forecast_panel_impl`) with a small equal-length 3-series f64 panel, method="GlobalETS", horizon=4, seasonal_period=0:
    - Test 1 (happy path): returns Ok; output is a Vec<Vec<f64>> of shape [3][4]; all values finite.
    - Test 2 (NaN imputation): a series containing a NaN interior gap is interpolated (no NaN reaches fit); result still shape [3][4], finite.
    - Test 3 (unknown method): method="Nope" returns Err(InvalidModel).
    Structure the FFI export so its inner logic lives in a testable `forecast_panel_impl(flat: &[f64], n_series, series_len, method, horizon, period, variant) -> Result<Vec<Vec<f64>>>` and the `#[no_mangle]` wrapper only does pointer marshalling + catch_unwind + alloc.
  </behavior>
  <action>
    In types.rs, append `#[repr(C)] pub struct PanelForecastResult { pub forecasts: *mut c_double, pub n_series: size_t, pub n_horizon: size_t, pub model_name: [c_char; 64] }` plus a `Default` impl (forecasts=null_mut, counts=0, model_name=[0;64]) — mirror ForecastResult exactly.
    In lib.rs, add imports `use anofox_forecast::models::exponential::{GlobalAutoETS, ModelPool};` and `use anofox_fcst_core::fill_nulls_interpolate;` (GlobalTheta/GlobalCroston imports are added in 02-2 — do NOT add them now).
    Write `forecast_panel_impl(...)`: chunk `flat` into n_series slices of series_len; per slice map v -> if v.is_nan() { None } else { Some(v) } then `fill_nulls_interpolate(&raw)` to get a dense `Vec<f64>`; collect into `panel: Vec<Vec<f64>>`. Match on method: "GlobalETS" -> `let pool = if model_pool==Some("Complete") { ModelPool::Complete } else { ModelPool::Reduced }; let mut m = GlobalAutoETS::new(period, pool); m.fit(&panel)?; Ok(m.predict(horizon))`. Any other method (Theta/Croston land in 02-2) -> `Err(ForecastError::InvalidModel(format!("Unknown panel method: {}", other)))` (use the crate's existing ForecastError variant matching types.rs error mapping; if the variant name differs, use ComputationError with the message). Use `period` = seasonal_period arg (0 is a valid GlobalAutoETS input meaning non-seasonal Reduced pool).
    Write `#[no_mangle] pub unsafe extern "C" fn anofox_ts_forecast_panel(values: *const c_double, n_series: size_t, series_len: size_t, method: *const c_char, horizon: size_t, seasonal_period: size_t, variant: *const c_char, out_result: *mut PanelForecastResult, out_error: *mut AnofoxError) -> bool`. Body: `init_error(out_error)` (or the AnofoxError::success() pattern from anofox_ts_forecast); null-check values/method/out_result -> set NullPointer + return false; parse method via `CStr::from_ptr(method).to_str()`; `variant` may be null (Some/None); wrap the rest in `catch_unwind(AssertUnwindSafe(|| forecast_panel_impl(...)))`. On Ok(Ok(preds)): allocate a flat `n_series*horizon` c_double buffer (reuse the SAME allocation helper used by ForecastResult::point_forecasts, freed by anofox_free_double_array — see lib.rs alloc helper near line ~5900), copy preds[s][h] into `buf[s*horizon+h]`, write PanelForecastResult{forecasts=buf, n_series, n_horizon=horizon, model_name=b"GlobalETS" null-padded to 64}, return true. On Ok(Err(e)): `set_error(out_error, ErrorCode::ComputationError, &e.to_string())`; false. On Err(_) (panic): `set_error(out_error, ErrorCode::PanicCaught, "Panic in Rust code")`; false.
    Write `#[no_mangle] pub unsafe extern "C" fn anofox_free_panel_forecast_result(result: *mut PanelForecastResult)`: if null return; if forecasts non-null call the existing `anofox_free_double_array(forecasts)` then set to null_mut — mirror anofox_free_forecast_result.
    Do NOT edit extension_config.cmake — the symbol lives in the already-linked anofox_fcst_ffi-static archive (RESEARCH Target 2, Pitfall 6). Regenerate the C header per Task 3's build (Makefile `header` target / cbindgen) so anofox_fcst_ffi.h declares the new functions.
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast && cargo test -p anofox-fcst-ffi panel_ffi 2>&1 | tail -20</automated>
  </verify>
  <acceptance_criteria>
    - `cargo test -p anofox-fcst-ffi panel_ffi` compiles and all three panel_ffi_tests pass.
    - `cargo build -p anofox-fcst-ffi` succeeds (no warnings that break `-D warnings` if enforced).
    - types.rs contains `pub struct PanelForecastResult` with `forecasts`, `n_series`, `n_horizon`, `model_name: [c_char; 64]`.
    - lib.rs exports `anofox_ts_forecast_panel` and `anofox_free_panel_forecast_result` both with `#[no_mangle] pub unsafe extern "C"`.
    - The GlobalETS fit is called exactly once with the full `&panel` (grep confirms a single `GlobalAutoETS::new(` in forecast_panel_impl, and `.fit(&panel)` not inside a per-series loop).
  </acceptance_criteria>
  <done>anofox_ts_forecast_panel builds, unit-tests green for GlobalETS on a 3-series panel, and the free function releases the heap buffer. The Rust half of the tracer works.</done>
  <reversibility rating="costly">PanelForecastResult and the FFI signature are a published C-ABI contract; changing field order/args later forces a header + C++ recompile. Greenfield here, so record not gate.</reversibility>
</task>

<task type="tracer" tdd="true">
  <name>Task 2: _ts_forecast_panel_native table function — ragged alignment + single-fit dispatch + macro + registration</name>
  <files>src/include/ts_forecast_panel_native.hpp, src/table_functions/ts_forecast_panel_native.cpp, src/macros/ts_macros.cpp, src/anofox_forecast_extension.cpp, CMakeLists.txt</files>
  <read_first>
    - src/table_functions/ts_forecast_native.cpp (WHOLE FILE — struct layout lines 33-116, output schema 426-452, InOut 476-553, Finalize barrier 559-584, date arithmetic 682-730, emission loop 745-799, Register 806-821, param-MAP helpers 343-400)
    - src/include/ts_forecast_native.hpp (header forward-declaration + include-guard pattern to mirror)
    - src/include/ts_fill_gaps_native.hpp:21-33 (ParseFrequencyWithType, DateToMicroseconds, MicrosecondsToDate, TimestampToMicroseconds, MicrosecondsToTimestamp)
    - src/macros/ts_macros.cpp:12-20 (TsTableMacro struct), :575-594 (ts_forecast_by entry — the exact analog), :2290-2301 (auto-registration loop)
    - src/anofox_forecast_extension.cpp:155-183 (native-function registration block; add #include + call after RegisterTsForecastNativeFunction line ~168)
    - CMakeLists.txt:178 (src/table_functions/ts_forecast_native.cpp source-list line — add the new .cpp right after)
    - .planning/phases/02-global-panel-models/02-PATTERNS.md (the ts_forecast_panel_native.cpp section — copy-verbatim guidance) and 02-RESEARCH.md (Research Target 3 & 4: alignment sketch, drop rule, Pitfall 1)
  </read_first>
  <behavior>
    Add a SQLLogicTest at test/sql/ts_forecast_panel.test that:
    - Builds a ragged 3-series daily panel (series A: 12 pts, B: 9 pts with a mid-series NULL, C: 5 pts) in a temp table.
    - Calls `SELECT unique_id, count(*) FROM ts_forecast_panel_by('panel_tbl', unique_id, ds, y, 'GlobalETS', 4, '1d') GROUP BY unique_id ORDER BY unique_id;` and asserts each non-dropped series has exactly 4 forecast rows.
    - Asserts a series shorter than the drop threshold appears with model_name = 'DROPPED: too_short' (series C, len 5 < threshold 10) OR is excluded — assert the surfaced-drop behavior chosen in <action>.
    (This .test runs under the CMake LOAD_TESTS harness; it is the automated proof of the C++ layer.)
  </behavior>
  <action>
    Create src/include/ts_forecast_panel_native.hpp: `#pragma once` / `#include "duckdb.hpp"` / `namespace duckdb { void RegisterTsForecastPanelNativeFunction(ExtensionLoader &loader); }`.
    Create src/table_functions/ts_forecast_panel_native.cpp by mirroring ts_forecast_native.cpp:
    - Structs: `TsForecastPanelNativeBindData` (horizon, frequency_seconds, frequency_is_raw, frequency_type, method="GlobalETS", seasonal_period=0, model_pool="", croston_variant="", date_col_type, date_logical_type, group_logical_type). Reuse `ForecastGroupData` shape (group_value, dates, values, validity). `PanelOutputRow` = {group_key, group_value, forecast_step, date, point_forecast, model_name} (NO lower/upper). LocalState + GlobalState = copy verbatim (mutex, groups map, group_order, results, finalize_claimed/threads_collecting/threads_done_collecting atomics).
    - Bind: same positional convention as ts_forecast_native (input TABLE [group,date,value]; then horizon INTEGER, frequency VARCHAR, method VARCHAR, params ANY/MAP). Parse frequency via ParseFrequencyWithType; parse `seasonal_period`, `model_pool`, `croston_variant` from params MAP using the same ParseStringFromParams/ParseInt64FromParams/ValidateParamKeys helpers (allowed keys: seasonal_period, model_pool, croston_variant). Output schema = 5 columns: {group_col_name, "forecast_step" INTEGER, date_col_name, "yhat" DOUBLE, "model_name" VARCHAR}.
    - InOut + Finalize barrier: copy verbatim from ts_forecast_native (collect all rows under mutex; CAS-claim single-thread finalize; spin until threads_done_collecting == threads_collecting).
    - Panel Finalize processing (NEW — replaces the per-group FFI loop): build the shared date grid as the union of all dates across all groups (std::set<int64_t> over every group's dates, sorted to a vector `shared_grid`; grid_len = shared_grid.size()). For each group in group_order: build map<int64_t,double> from (dates[i],values[i]) where validity[i]; count valid points; DROP RULE (Claude's discretion, min length = 10 observations, universal): if valid_count < 10 OR all-null, DO NOT add to the fit panel — instead push horizon PanelOutputRows for that series with point_forecast = NAN and model_name = "DROPPED: too_short" so the series is surfaced (D-Area2: dropped-with-warning, do not fail the whole call). For kept series: fill a row of length grid_len with value at present dates and `std::numeric_limits<double>::quiet_NaN()` at absent dates (imputation happens in Rust). Assemble `flat_matrix` (n_kept * grid_len) and `valid_keys`. If n_kept < 3, throw InvalidInputException("panel has fewer than 3 usable series after alignment"). Call `anofox_ts_forecast_panel(flat_matrix.data(), n_kept, grid_len, bind.method.c_str(), bind.horizon, (size_t)bind.seasonal_period, bind.croston_variant.c_str(), &panel_result, &error)`; on false throw InvalidInputException(error.message). Emit: for each kept series s, for h in 0..panel_result.n_horizon: PanelOutputRow with forecast_step=h+1, date = calendar-aware step from last date of shared_grid (copy the date arithmetic from ts_forecast_native.cpp:682-730, using shared_grid.back() as the anchor), point_forecast = panel_result.forecasts[s*n_horizon+h], model_name = string(panel_result.model_name). Call `anofox_free_panel_forecast_result(&panel_result)` after copying out all values (never before).
    - Emission loop to DuckDB output: copy the STANDARD_VECTOR_SIZE chunk loop from ts_forecast_native.cpp:745-799, adjusted to the 5-column schema (data[0]=group_value, data[1]=INTEGER forecast_step, data[2]=date via the same date_col_type switch, data[3]=DOUBLE yhat, data[4]=VARCHAR model_name).
    - `RegisterTsForecastPanelNativeFunction`: TableFunction("_ts_forecast_panel_native", {TABLE, INTEGER, VARCHAR, VARCHAR, ANY}, nullptr, Bind, InitGlobal, InitLocal); set in_out_function + in_out_function_final; loader.RegisterFunction(func).
    Add the macro entry in src/macros/ts_macros.cpp immediately after the ts_forecast_by entry (~line 594): `ts_forecast_panel_by` with params {"source","group_col","date_col","target_col","method","horizon","frequency", nullptr}, named {{"params","MAP{}"}}, SQL body `SELECT group_col, forecast_step, date_col, yhat, model_name FROM _ts_forecast_panel_native(query_table(source::VARCHAR), group_col, date_col, target_col, horizon, frequency, method, params)`, description noting cross-series global learners + auto-alignment, example `SELECT * FROM ts_forecast_panel_by('sales', product_id, date, qty, 'GlobalETS', 14, '1d', MAP{'seasonal_period': '7'})`, category "forecasting". The registration loop at :2290-2301 picks it up automatically (no extra code).
    In src/anofox_forecast_extension.cpp: add `#include "ts_forecast_panel_native.hpp"` and `RegisterTsForecastPanelNativeFunction(loader);` immediately after `RegisterTsForecastNativeFunction(loader);` (~line 168).
    In CMakeLists.txt: add `src/table_functions/ts_forecast_panel_native.cpp` to the source list right after line 178 (the ts_forecast_native.cpp entry).
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast && make rust && cmake --build build/release --target anofox_forecast_loadable_extension 2>&1 | tail -30 && ./build/release/duckdb -unsigned -c "LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension'; CREATE TABLE p AS SELECT * FROM (VALUES ('A',DATE '2024-01-01',10.0),('A',DATE '2024-01-02',11.0),('A',DATE '2024-01-03',12.0),('A',DATE '2024-01-04',13.0),('A',DATE '2024-01-05',12.0),('A',DATE '2024-01-06',14.0),('A',DATE '2024-01-07',15.0),('A',DATE '2024-01-08',14.0),('A',DATE '2024-01-09',16.0),('A',DATE '2024-01-10',17.0),('A',DATE '2024-01-11',16.0),('A',DATE '2024-01-12',18.0),('B',DATE '2024-01-01',5.0),('B',DATE '2024-01-02',6.0),('B',DATE '2024-01-03',5.0),('B',DATE '2024-01-05',7.0),('B',DATE '2024-01-06',6.0),('B',DATE '2024-01-07',8.0),('B',DATE '2024-01-08',7.0),('B',DATE '2024-01-09',9.0),('B',DATE '2024-01-10',8.0),('B',DATE '2024-01-11',10.0),('B',DATE '2024-01-12',9.0)) t(unique_id,ds,y); SELECT unique_id, count(*) AS n FROM ts_forecast_panel_by('p', unique_id, ds, y, 'GlobalETS', 4, '1d') GROUP BY unique_id ORDER BY unique_id;" 2>&1 | tail -20</automated>
  </verify>
  <acceptance_criteria>
    - `make rust` builds the FFI crate; the loadable extension target builds and links (new .cpp compiled in, no unresolved `anofox_ts_forecast_panel` symbol).
    - The LOAD + query returns 2 rows: A=4, B=4 (each kept series has exactly `horizon` forecast rows). No error, no NaN in yhat for A/B.
    - `grep -c "_ts_forecast_panel_native" src/macros/ts_macros.cpp` is >= 1 (macro wired to the native function).
    - `grep -c "RegisterTsForecastPanelNativeFunction" src/anofox_forecast_extension.cpp` is >= 1 (registered on load).
    - `grep -c "ts_forecast_panel_native.cpp" CMakeLists.txt` is >= 1 (compiled into the extension).
    - A series with fewer than 10 valid points is surfaced with model_name = 'DROPPED: too_short' (verified by the SQLLogicTest / an added series-C case), not silently absent and not fatal.
  </acceptance_criteria>
  <done>The built extension loads and `ts_forecast_panel_by(..., 'GlobalETS', ...)` returns per-series forecasts for a ragged panel via a single cross-series fit. The C++ tracer layer works end-to-end against the built extension.</done>
  <reversibility rating="reversible">Internal table-function + macro wiring; the published surface name `ts_forecast_panel_by` is recorded (D-Area1) but greenfield — not gated.</reversibility>
</task>

<task type="auto">
  <name>Task 3: GlobalETS runnable example — verified end-to-end against the built extension (PR #230 rule)</name>
  <files>examples/forecasting/global_panel_forecasting_examples.sql</files>
  <read_first>
    - examples/forecasting/synthetic_forecasting_examples.sql (header + LOAD + section structure to mirror)
    - examples/forecasting/README.md (example-catalog conventions, if it lists files)
    - src/macros/ts_macros.cpp (the ts_forecast_panel_by entry written in Task 2 — mirror the exact signature/param names in the example)
  </read_first>
  <action>
    Create examples/forecasting/global_panel_forecasting_examples.sql. Header comment: file purpose + run command `./build/release/duckdb -unsigned < examples/forecasting/global_panel_forecasting_examples.sql`, then `LOAD anofox_forecast;` (or the LOAD-from-path form used by the other examples — match synthetic_forecasting_examples.sql). Sections (GlobalETS only in this plan; Theta/Croston sections added in 02-2):
    - Section 1: CREATE OR REPLACE TABLE panel_sales — a ragged multi-series daily panel (>= 3 series, at least one with an interior gap/NULL and one with a different start date) so alignment is exercised.
    - Section 2: `SELECT * FROM ts_forecast_panel_by('panel_sales', product_id, ds, y, 'GlobalETS', 14, '1d', MAP{'seasonal_period': '7'}) ORDER BY product_id, forecast_step;` — the GlobalETS panel call. Add a one-line comment noting this is a single cross-series fit (fit-once-emit-many), contrasting `ts_forecast_by` per-series dispatch.
    - Leave a clearly-commented placeholder marker `-- [02-2] GlobalTheta + GlobalCroston sections appended here` at the end so 02-2 extends this same file (file overlap is why 02-2 is a later wave).
    Every SELECT must return rows against the built extension — no eyeballing (project rule + PR #230 lesson).
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast && ./build/release/duckdb -unsigned < examples/forecasting/global_panel_forecasting_examples.sql 2>&1 | tail -30</automated>
  </verify>
  <acceptance_criteria>
    - Running the example file against the built extension exits 0 and prints forecast rows (no error, no empty result for the GlobalETS section).
    - The GlobalETS SELECT returns `n_series * 14` rows across the panel (verify with a trailing `SELECT count(*)` in the file or by inspecting output).
    - The file contains the `-- [02-2] GlobalTheta + GlobalCroston sections appended here` marker.
    - `grep -c "ts_forecast_panel_by" examples/forecasting/global_panel_forecasting_examples.sql` is >= 1.
  </acceptance_criteria>
  <done>examples/forecasting/global_panel_forecasting_examples.sql runs clean against the built extension and returns GlobalETS panel forecasts — GLOB-01 is verified end-to-end.</done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| SQL user data -> C++ table function | Panel rows (group/date/value) already inside the user's own DuckDB session; not privileged or remote |
| C++ -> Rust FFI | Flat f64 matrix + counts + method/variant C strings cross the ABI; Rust must not trust lengths/pointers blindly |
| Rust core -> heap buffer -> C++ | Rust-allocated forecast buffer handed back; C++ must free exactly once |

## STRIDE Threat Register

| Threat ID | Category | Component | Severity | Disposition | Mitigation Plan |
|-----------|----------|-----------|----------|-------------|-----------------|
| T-02-01 | Tampering | anofox_ts_forecast_panel FFI (null/short pointers, n_series*series_len overflow) | medium | mitigate | Null-check values/method/out_result before deref; compute slice via `from_raw_parts(values, n_series*series_len)` only after count validation; C++ sizes flat_matrix to exactly n_kept*grid_len |
| T-02-02 | Denial of Service | GlobalAutoETS on very large panel (ModelPool cost × N series) | low | accept | Default ModelPool::Reduced (8 candidates) per RESEARCH Pitfall 4; user opts into Complete via params; input is the user's own session data |
| T-02-03 | Denial of Service | Rust panic in fit/predict crossing FFI | medium | mitigate | catch_unwind(AssertUnwindSafe) wraps forecast_panel_impl; panic -> ErrorCode::PanicCaught -> DuckDB exception, never UB |
| T-02-04 | Information Disclosure | Use-after-free / leak of PanelForecastResult buffer | medium | mitigate | Copy all values out before anofox_free_panel_forecast_result; free exactly once; free fn null-checks and nulls the pointer |
| T-02-05 | Tampering | NaN/Inf reaching fit from imputation (multiplicative seasonal NaN) | low | mitigate | fill_nulls_interpolate densifies each series; GlobalAutoETS guards non-positive panels (RESEARCH Pitfall 3); dropped series surfaced not fed to fit |
</threat_model>

<verification>
- `cargo test -p anofox-fcst-ffi panel_ffi` — Rust FFI unit tests green (Task 1).
- `make rust && cmake --build build/release --target anofox_forecast_loadable_extension` — extension builds with the new symbol + .cpp (Task 2).
- LOAD + `ts_forecast_panel_by(..., 'GlobalETS', ...)` on a ragged 2-series panel returns 4 rows/series (Task 2).
- `./build/release/duckdb -unsigned < examples/forecasting/global_panel_forecasting_examples.sql` returns rows (Task 3).
- Single-fit invariant: grep confirms one `GlobalAutoETS::new(` + one `.fit(&panel)` (no per-series loop).
</verification>

<success_criteria>
- GLOB-01 satisfied: a grouped ragged panel forecasts via GlobalETS cross-series learning through `ts_forecast_panel_by`, verified against the built extension.
- The fit-once-emit-many panel architecture is proven end-to-end (FFI -> C++ table function -> macro -> SQL), unblocking 02-2 expansion.
- Ragged alignment + drop-with-surfacing + null imputation all exercised on a real panel.
</success_criteria>

<output>
Create `.planning/phases/02-global-panel-models/02-1-SUMMARY.md` when done.
</output>
