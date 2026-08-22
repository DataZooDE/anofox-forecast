---
phase: 03-classical-multivariate-models
plan: 02
type: execute
wave: 2
depends_on: [03-01]
files_modified:
  - crates/anofox-fcst-ffi/src/types.rs
  - crates/anofox-fcst-ffi/src/lib.rs
  - crates/anofox-fcst-ffi/cbindgen.toml
  - src/include/anofox_fcst_ffi.h
  - src/include/ts_forecast_var_native.hpp
  - src/table_functions/ts_forecast_var_native.cpp
  - src/macros/ts_macros.cpp
  - src/anofox_forecast_extension.cpp
  - CMakeLists.txt
  - examples/forecasting/classical_forecasting_examples.sql
autonomous: true
requirements: [CLAS-03]
estimate:
  tokens: 92000
  raw_tokens: 61000
  tasks: 3
  confidence: low
must_haves:
  truths:
    - "ts_forecast_var_by('src','ds',['y1','y2'],h,'1d') returns k_vars * h long-format rows {variable, forecast_step, forecast_date, forecast_value} (CLAS-03)"
    - "order:=p is honored (VAR lag order); default 1"
    - "each variable name from value_cols appears in the variable column of the output"
    - "value_cols beyond the date column are read by NAME from the input schema at Bind time"
  artifacts:
    - crates/anofox-fcst-ffi/src/lib.rs
    - crates/anofox-fcst-ffi/src/types.rs
    - src/table_functions/ts_forecast_var_native.cpp
    - src/include/ts_forecast_var_native.hpp
    - src/macros/ts_macros.cpp
    - src/anofox_forecast_extension.cpp
  key_links:
    - "ts_forecast_var_by macro (subselect) → _ts_forecast_var_native Bind (name→index) → Finalize (K columns → flat matrix) → anofox_ts_forecast_var FFI → VAR::fit/predict → long-format emit"
    - "VARForecastResult struct → cbindgen.toml export include → anofox_fcst_ffi.h"
  prohibitions:
    - "MUST NOT pass query_table(source::VARCHAR) directly as a bare TABLE arg — wrap in (SELECT date_col, * FROM query_table(...)) subselect or the macro silently fails to register (Phase-2 lesson)"
    - "MUST NOT use unwrap_or(0) or silently clamp on buffer-size multiplication — use checked_mul and propagate overflow as an error"
    - "MUST NOT pass NaN/Inf to VAR::fit() — it rejects them; impute per column first and error on unequal effective lengths"
    - "MUST NOT add a group_col in v1 — single-panel only (per CONTEXT discretion); document it"
    - "MUST NOT hand-edit anofox_fcst_ffi.h — regenerate via make header after types.rs/cbindgen.toml change"
---

<objective>
Build the NEW multivariate VAR surface `ts_forecast_var_by`: N value columns in → N×horizon per-variable forecasts out (long format). This is the flagged design-risk slice — a different I/O shape from every univariate `ts_forecast_by` method. It requires a brand-new FFI export, a new `VARForecastResult` struct, a new C++ native table function that reads K value columns by name from the input schema, a new macro, and registration.

The architecture mirrors the Phase-2 panel table function (fit-once-emit-many, Finalize barrier) with three key differences: no group_col, K value columns instead of one, and long-format `{variable, forecast_step, forecast_date, forecast_value}` output.

Purpose: Deliver CLAS-03 (VAR multivariate forecasting).
Output: anofox_ts_forecast_var FFI export + VARForecastResult struct + forecast_var_impl inner fn, _ts_forecast_var_native C++ table function, ts_forecast_var_by macro, registration, and a verified runnable example section.
</objective>

<execution_context>
@$HOME/.claude/gsd-core/workflows/execute-plan.md
@$HOME/.claude/gsd-core/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/ROADMAP.md
@.planning/STATE.md
@.planning/phases/03-classical-multivariate-models/03-RESEARCH.md
@.planning/phases/03-classical-multivariate-models/03-PATTERNS.md
@.planning/phases/03-classical-multivariate-models/03-CONTEXT.md
@.planning/phases/02-global-panel-models/02-1-SUMMARY.md
</context>

<artifacts_produced>
## Artifacts this plan produces (NEW symbols)

- `VARForecastResult` repr(C) struct (forecasts: *mut c_double, k_vars: size_t, n_horizon: size_t) + Default impl (crates/anofox-fcst-ffi/src/types.rs)
- `anofox_ts_forecast_var(flat_data, k_vars, series_len, order, horizon, out_result, out_error) -> bool` FFI export (crates/anofox-fcst-ffi/src/lib.rs)
- `anofox_free_var_forecast_result(*mut VARForecastResult)` FFI free function
- `forecast_var_impl(flat, k_vars, series_len, order, horizon) -> Result<Vec<Vec<f64>>, String>` inner testable fn
- `VARForecastResult` added to cbindgen.toml export include list; regenerated anofox_fcst_ffi.h
- `_ts_forecast_var_native` C++ table function + `TsForecastVarNativeBindData`/`GlobalState`/`Bind`/`InOut`/`Finalize`/`RegisterTsForecastVarNativeFunction` (src/table_functions/ts_forecast_var_native.cpp + src/include/ts_forecast_var_native.hpp)
- `ts_forecast_var_by` SQL macro (src/macros/ts_macros.cpp)
- VAR section appended to examples/forecasting/classical_forecasting_examples.sql
</artifacts_produced>

<tasks>

<task type="tracer" tdd="true">
  <name>Task 1: anofox_ts_forecast_var FFI export + VARForecastResult + forecast_var_impl</name>
  <read_first>
    - .planning/phases/03-classical-multivariate-models/03-RESEARCH.md (Critical Finding 3 lines 333-482 — VAR::fit(&[Vec<f64>]), predict → Vec<Vec<f64>> [k][horizon], min-obs n>p, NaN rejection; FFI export design lines 362-412)
    - .planning/phases/03-classical-multivariate-models/03-PATTERNS.md (VARForecastResult struct lines 164-183; forecast_var_impl lines 210-233; FFI export + null-check + checked_mul + alloc + free patterns lines 236-292; Shared Patterns lines 566-620)
    - crates/anofox-fcst-ffi/src/types.rs (PanelForecastResult ~414-436 as the struct-shape analog)
    - crates/anofox-fcst-ffi/src/lib.rs (anofox_ts_forecast_panel ~6968-7059, forecast_panel_impl ~6884-6947, alloc_double_array/free_double_array helpers, fill_nulls_interpolate usage ~6897-6906)
    - crates/anofox-fcst-ffi/cbindgen.toml (export include list — where PanelForecastResult was added in Phase 2)
    - Makefile (`header` target)
  </read_first>
  <behavior>
    - Rust unit test (happy path): forecast_var_impl(&flat, k_vars=2, series_len=50, order=1, horizon=5) on synthetic VAR(1) data returns Ok(Vec<Vec<f64>>) of shape [2][5].
    - Rust unit test (empty): forecast_var_impl(&[], 0, 0, 1, 5) returns Err.
    - Rust FFI unit test: anofox_ts_forecast_var with a valid flat matrix fills out_result.forecasts (non-null), k_vars=2, n_horizon=5, returns true; then anofox_free_var_forecast_result nulls the pointer.
    - Rust FFI unit test (null guard): passing null flat_data sets out_error to NullPointer and returns false (no panic).
  </behavior>
  <action>
Add the VAR FFI surface to the FFI crate.

1. types.rs: add the VARForecastResult repr(C) struct exactly per 03-PATTERNS.md lines 164-183: `forecasts: *mut c_double`, `k_vars: size_t`, `n_horizon: size_t`, plus a Default impl (null forecasts, 0 dims). Variable-major layout documented in the doc comment: forecasts[v * n_horizon + h].

2. lib.rs: add forecast_var_impl(flat, k_vars, series_len, order, horizon) -> Result<Vec<Vec<f64>>, String> per 03-PATTERNS.md lines 210-233 — reconstruct K series from the flat variable-major matrix, VAR::new(order.max(1)), fit, predict; map crate errors to String. Import `use anofox_forecast::models::var::VAR`. Before the fit, impute NaN per reconstructed column using fill_nulls_interpolate (the same helper forecast_panel_impl uses) because VAR::fit rejects NaN/Inf (Pitfall 3).

3. lib.rs: add the anofox_ts_forecast_var #[no_mangle] pub unsafe extern "C" export with signature (flat_data: *const c_double, k_vars: size_t, series_len: size_t, order: size_t, horizon: size_t, out_result: *mut VARForecastResult, out_error: *mut AnofoxError) -> bool. Follow the panel export structure: reset out_error to success; null-check flat_data and out_result (set NullPointer error, return false); wrap the body in catch_unwind(AssertUnwindSafe(...)). Inside: `let len = k_vars.checked_mul(series_len).ok_or_else(|| "VAR dimensions overflow".to_string())?;` then slice::from_raw_parts(flat_data, len). Call forecast_var_impl. Allocate output: `let total = k_vars.checked_mul(horizon).ok_or_else(|| "VAR output overflow".to_string())?;` alloc_double_array(total), write variable-major `*raw.add(v*horizon + h) = val`, set out_result fields. Map Ok(Ok)→true, Ok(Err(e))→set_error(...)+false, Err(_)→set_error(Panic)+false. Use checked_mul for BOTH input and output sizing and propagate overflow (never unwrap_or(0)).

4. lib.rs: add anofox_free_var_forecast_result(*mut VARForecastResult) per 03-PATTERNS.md lines 282-292 — null-guard, free_double_array(forecasts, k_vars * n_horizon), null the pointer. Match the alloc/free pairing exactly (alloc_double_array ↔ free_double_array).

5. cbindgen.toml: add VARForecastResult to the export include list (same as PanelForecastResult in Phase 2). Run `make header`; verify VARForecastResult and anofox_ts_forecast_var appear in src/include/anofox_fcst_ffi.h.

Add the unit tests from <behavior> to the FFI test module (use a small synthetic VAR(1) flat matrix generated inline).
  </action>
  <verify>
    <automated>make header && grep -q 'VARForecastResult' src/include/anofox_fcst_ffi.h && grep -q 'anofox_ts_forecast_var' src/include/anofox_fcst_ffi.h && cargo test -p anofox-fcst-ffi var</automated>
  </verify>
  <acceptance_criteria>
    - `grep -c 'VARForecastResult\|anofox_ts_forecast_var' src/include/anofox_fcst_ffi.h` ≥ 2 after make header.
    - `cargo test -p anofox-fcst-ffi var` passes (happy path, empty, FFI fill+free, null guard).
    - `grep -n 'checked_mul' crates/anofox-fcst-ffi/src/lib.rs` shows checked_mul used for both the input (k_vars*series_len) and output (k_vars*horizon) sizing in anofox_ts_forecast_var.
  </acceptance_criteria>
  <reversibility rating="reversible">New additive FFI export + struct; no existing FFI symbol changed.</reversibility>
  <done>anofox_ts_forecast_var is callable from Rust with correct shape, checked overflow handling, and NaN imputation; header carries the new symbols; FFI unit tests pass.</done>
</task>

<task type="auto">
  <name>Task 2: _ts_forecast_var_native C++ table function + ts_forecast_var_by macro + registration</name>
  <read_first>
    - .planning/phases/03-classical-multivariate-models/03-PATTERNS.md (full ts_forecast_var_native.cpp section lines 296-421 — BindData, GlobalState, Bind name→index, InOut K-column read, Finalize barrier + flat matrix + FFI call, output emit, RegisterTsForecastVarNativeFunction; macro section lines 426-453; registration section lines 458-467)
    - .planning/phases/03-classical-multivariate-models/03-RESEARCH.md (Critical Finding 5 lines 535-580 — multi-column input via input.input_table_names; Pitfall 4/5/6 lines 666-676)
    - src/table_functions/ts_forecast_panel_native.cpp (full structural analog: BindData ~33-49, GlobalState ~86-101, Bind ~200-282, InOut ~305-396, Finalize barrier ~404-430, output emit ~700-743, Register ~759-775)
    - src/include/ts_forecast_panel_native.hpp (header analog)
    - src/macros/ts_macros.cpp (ts_forecast_panel_by entry ~606-623 — subselect pattern, named-params registration format)
    - src/anofox_forecast_extension.cpp (~169-170 registration + include block)
    - CMakeLists.txt (ts_forecast_panel_native.cpp source-list entry)
  </read_first>
  <action>
Create the new C++ native table function and wire it in. Mirror ts_forecast_panel_native.cpp structurally; apply the three key differences (no group_col; K value columns read by name; long-format emit).

1. src/include/ts_forecast_var_native.hpp: forward-declare RegisterTsForecastVarNativeFunction (copy the panel header).

2. src/table_functions/ts_forecast_var_native.cpp: implement per 03-PATTERNS.md lines 296-421.
   - TsForecastVarNativeBindData: horizon, frequency fields (copy panel), int64_t order = 1, vector<string> value_col_names, vector<idx_t> value_col_indices.
   - Bind: parse horizon (input.inputs[1]), frequency (inputs[2]), order (inputs[3]); parse value_cols VARCHAR[] from input.inputs[4] via ListValue::GetChildren; for each name, find its index in input.input_table_names and push to value_col_indices (error if a named column is absent). Output schema: variable VARCHAR, forecast_step BIGINT, <date_col_name> (date type), forecast_value DOUBLE.
   - GlobalState: data_mutex, vector<int64_t> dates, vector<vector<double>> series_data (per column), vector<vector<bool>> series_valid, results, processed flag, atomic finalize_claimed/threads counters (copy panel barrier structure; drop the per-group map).
   - InOut (Execute): col 0 = date; for each value column index in value_col_indices read the value, push to series_data[v] (NaN on null) and series_valid[v]. Guard against nulls with the panel pattern.
   - Finalize: use the atomic finalize_claimed barrier (only one thread runs the FFI call; others spin until processed). Sort by date; pre-impute NaN per column; VERIFY all K columns have equal effective (non-null-after-impute) length — if not, throw InvalidInputException("VAR requires all value columns to have the same number of valid observations") (Pitfall 4). Add the soft under-determination check (Pitfall 5): if n_eff < k_vars * order + 1, throw a clear error. Build the flat variable-major matrix (for v in 0..k: for t in 0..n: flat.push_back(series_data[v][t])). Call anofox_ts_forecast_var(flat.data(), k_vars, n, order, horizon, &var_result, &error); throw on !ok using error message. Emit long-format rows: for each variable v, for each step h in 1..=horizon: row {value_col_names[v], h, forecast_date(h), var_result.forecasts[v*horizon + (h-1)]}. Reuse the panel forecast-date computation (frequency stepping). Call anofox_free_var_forecast_result(&var_result) after copying values out.
   - RegisterTsForecastVarNativeFunction: TableFunction("_ts_forecast_var_native", {TABLE, INTEGER, VARCHAR, INTEGER, LIST(VARCHAR), ANY}, nullptr, Bind, InitGlobal, InitLocal); set in_out_function + in_out_function_final; loader.RegisterFunction.

3. src/macros/ts_macros.cpp: add the ts_forecast_var_by macro entry per 03-PATTERNS.md lines 434-453. Required positional params: source, date_col, value_cols, horizon, frequency. Named optional: order:="1", params:="MAP{}". Body uses the subselect pattern `(SELECT date_col, * FROM query_table(source::VARCHAR))` — NEVER a bare query_table TABLE arg (Phase-2 silent-registration-failure lesson). SELECT variable, forecast_step, date_col, forecast_value FROM _ts_forecast_var_native(subselect, horizon, frequency, order, value_cols, params).

4. src/anofox_forecast_extension.cpp: add `#include "ts_forecast_var_native.hpp"` with the other table-function includes and `RegisterTsForecastVarNativeFunction(loader);` after RegisterTsForecastPanelNativeFunction.

5. CMakeLists.txt: add src/table_functions/ts_forecast_var_native.cpp to the source list.

Build the extension; fix compile errors.
  </action>
  <verify>
    <automated>build/release/duckdb -unsigned -c "LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension'; SELECT count(*) FROM duckdb_functions() WHERE function_name IN ('_ts_forecast_var_native','ts_forecast_var_by');"</automated>
  </verify>
  <acceptance_criteria>
    - Extension builds and loads with no missing-symbol errors.
    - `duckdb_functions()` returns 2 rows for ('_ts_forecast_var_native','ts_forecast_var_by') — the macro registered (proves the subselect pattern worked; 0 rows would mean silent failure).
    - The macro body in src/macros/ts_macros.cpp uses `(SELECT date_col, * FROM query_table(source::VARCHAR))`, not a bare query_table TABLE arg (grep-verify).
  </acceptance_criteria>
  <reversibility rating="reversible">Greenfield function + macro name ts_forecast_var_by; additive registration, nothing existing altered.</reversibility>
  <done>_ts_forecast_var_native and ts_forecast_var_by are registered in the built extension; the macro uses the subselect pattern; K value columns are read by name.</done>
</task>

<task type="auto">
  <name>Task 3: VAR end-to-end runnable example verified against the built extension</name>
  <read_first>
    - examples/forecasting/classical_forecasting_examples.sql (the file created in 03-1 — append the VAR section)
    - .planning/phases/03-classical-multivariate-models/03-RESEARCH.md (Pattern 5 macro usage lines 805-829; Pattern 6 synthetic VAR(1) generator lines 831-857)
    - .planning/phases/03-classical-multivariate-models/03-CONTEXT.md (long-format output {variable, forecast_date, forecast_value}; single-panel v1; order default 1)
    - .planning/phases/02-global-panel-models/02-1-SUMMARY.md (CLI subprocess verification pattern)
  </read_first>
  <action>
Append a VAR section (Section 3) to examples/forecasting/classical_forecasting_examples.sql. Build a small in-SQL multivariate table with a shared date column and ≥ 2 value columns (e.g. y1, y2 correlated over ~50 dates). Call `SELECT * FROM ts_forecast_var_by('src','ds',['y1','y2'],14,'1d')` for default order 1, plus a second call with `order:=2`. A SQL comment MUST state: output is LONG format (one row per variable × horizon step), forecast_value is a point forecast (no intervals in v1), and the function is single-panel only (no group_col in v1).

Run the full example end-to-end against the BUILT extension via the Phase-2 CLI subprocess pattern (build/release/duckdb -unsigned, LOAD built extension). Confirm the VAR call returns exactly k_vars × horizon rows (2 × 14 = 28) with both 'y1' and 'y2' appearing in the variable column. PR #230 rule — the SQL must actually execute against the built binary, no eyeballing.
  </action>
  <verify>
    <automated>build/release/duckdb -unsigned -c "LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension'; CREATE TABLE v AS SELECT (DATE '2020-01-01' + INTERVAL (i) DAY) AS ds, (0.6*sin(i*0.4)+0.1*cos(i*0.2)) AS y1, (0.05*sin(i*0.4)+0.7*cos(i*0.2)) AS y2 FROM range(60) t(i); SELECT count(*) AS n, count(DISTINCT variable) AS k FROM ts_forecast_var_by('v','ds',['y1','y2'],14,'1d');"</automated>
  </verify>
  <acceptance_criteria>
    - The VAR query returns n=28 (k_vars=2 × horizon=14) and k=2 distinct variables.
    - Both 'y1' and 'y2' appear in the variable column of the output.
    - The order:=2 call also returns 28 rows.
    - Running the full examples/forecasting/classical_forecasting_examples.sql against build/release/duckdb succeeds (exit 0) with forecast rows for the VAR section.
  </acceptance_criteria>
  <done>ts_forecast_var_by is callable from SQL against the built extension, returns k_vars × horizon long-format rows, and the example is verified end-to-end (CLAS-03 example+delivery DoD; docs/benchmark closed in 03-3).</done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| SQL value_cols VARCHAR[] → C++ Bind | User-named columns resolved against input schema |
| C++ flat matrix → Rust FFI | k_vars × series_len f64 buffer crosses the ABI (novel multivariate buffer) |
| Rust FFI → VAR crate | Reconstructed K series cross into VAR::fit |

## STRIDE Threat Register

| Threat ID | Category | Component | Severity | Disposition | Mitigation Plan |
|-----------|----------|-----------|----------|-------------|-----------------|
| T-03-04 | Denial of Service / Elevation | VAR flat-matrix buffer sizing (k_vars × series_len, k_vars × horizon) | high | mitigate | checked_mul on BOTH multiplications before slice::from_raw_parts / alloc; propagate overflow as error (never unwrap_or(0)); Task 1 step 3 |
| T-03-05 | Denial of Service | Resource exhaustion on large K / high VAR order | medium | mitigate | Pitfall-5 under-determination check (n_eff < k*order+1 → error) in C++ Finalize; VAR OLS bounded by input size collected under GROUP-BY layer only |
| T-03-06 | Tampering | value_cols naming a non-existent column | low | mitigate | Bind errors clearly when a value_cols name is absent from input.input_table_names (no silent wrong-column read) |
| T-03-07 | Tampering | NaN/Inf into VAR::fit (rejection / undefined result) | medium | mitigate | fill_nulls_interpolate per column before FFI; equal-effective-length assertion; NaN never reaches VAR::fit |
| T-03-SC | Tampering | npm/pip/cargo installs | low | accept | No new packages in this plan. Stay on anofox-forecast 0.15.3. |
</threat_model>

<verification>
- `make header` shows VARForecastResult + anofox_ts_forecast_var in src/include/anofox_fcst_ffi.h.
- `cargo test -p anofox-fcst-ffi var` passes (happy/empty/fill+free/null-guard).
- Extension builds and loads; duckdb_functions() shows both _ts_forecast_var_native and ts_forecast_var_by.
- ts_forecast_var_by returns k_vars × horizon long-format rows with all variable names present; example runs clean end-to-end.
</verification>

<success_criteria>
- CLAS-03: ts_forecast_var_by accepts multiple value columns and returns per-variable forecasts from a VAR model, verified against the built extension (roadmap success criterion 3, example+delivery portion).
- The novel multivariate I/O shape (design risk) is proven end-to-end; benchmark parity closed in 03-3.
</success_criteria>

<output>
Create `.planning/phases/03-classical-multivariate-models/03-02-SUMMARY.md` when done.
</output>