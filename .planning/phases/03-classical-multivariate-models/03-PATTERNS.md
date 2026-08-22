# Phase 3: Classical & Multivariate Models — Pattern Map

**Mapped:** 2026-08-21
**Files analyzed:** 14 new/modified files
**Analogs found:** 13 / 14

---

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `crates/anofox-fcst-core/src/forecast.rs` | model-dispatch | request-response | same file (existing arms) | exact — new variants added to existing match |
| `crates/anofox-fcst-ffi/src/types.rs` | FFI types | request-response | same file (`ForecastOptions`, `PanelForecastResult`) | exact — extend existing struct + add sibling struct |
| `crates/anofox-fcst-ffi/src/lib.rs` (GARCH/Kalman wire) | FFI export | request-response | same file (`anofox_ts_forecast`, lines ~3400-3450) | exact |
| `crates/anofox-fcst-ffi/src/lib.rs` (VAR export) | FFI export | batch | same file (`anofox_ts_forecast_panel`, lines 6968-7059) | exact |
| `src/include/anofox_fcst_ffi.h` | config/header | — | regenerated via `make header` / cbindgen | n/a (auto-generated) |
| `src/table_functions/ts_forecast_var_native.cpp` | table-function | batch | `src/table_functions/ts_forecast_panel_native.cpp` | role-match (structural analog; key difference: K value cols, no group_col, long-format variable emit) |
| `src/include/ts_forecast_var_native.hpp` | config/header | — | `src/include/ts_forecast_panel_native.hpp` | exact |
| `src/anofox_forecast_extension.cpp` | config/entry | — | same file, line 170 (`RegisterTsForecastPanelNativeFunction`) | exact |
| `src/macros/ts_macros.cpp` | config/macro | request-response | same file, lines 606-623 (`ts_forecast_panel_by`) | exact (subselect pattern + named-params registration) |
| `CMakeLists.txt` | config | — | existing `.cpp` source list entries | exact |
| `examples/forecasting/classical_forecasting_examples.sql` | test/example | request-response | `examples/forecasting/global_panel_forecasting_examples.sql` | role-match |
| `benchmark/m4/garch_benchmark/run.py`, `kalman_benchmark/run.py`, `var_benchmark/run.py` | test/benchmark | batch | `benchmark/m4/global_benchmark/run.py` | exact (same `create_benchmark_functions` harness) |
| `benchmark/configs/garch.py`, `kalman.py`, `var.py` | config/benchmark | — | `benchmark/configs/global_ets.py` (inferred) | role-match |
| `docs/reference/models/classical/garch.md`, `state-space/kalman.md`, `multivariate/var.md` | docs | — | `docs/reference/models/state-space/` existing pages | role-match |

---

## Pattern Assignments

---

### `crates/anofox-fcst-core/src/forecast.rs` (model-dispatch, request-response)

**Analog:** Same file — add `GARCH` and `Kalman` to all three match blocks and `ForecastOptions`.

**ModelType enum pattern** (`forecast.rs` lines 92–146):
```rust
// Add after ModelType::Laplace (line 145):
// Classical Models
GARCH,
Kalman,
```

**FromStr exact-match block pattern** (`forecast.rs` lines 152–196):
```rust
// Add in the exact-match block (before the _ => {} fallback at line 196):
"GARCH" => return Ok(ModelType::GARCH),
"Kalman" => return Ok(ModelType::Kalman),
```

**FromStr case-insensitive fallback pattern** (`forecast.rs` lines 200–255):
```rust
// Add in the lowercase match before the final _ arm:
"garch" => Ok(ModelType::GARCH),
"kalman" => Ok(ModelType::Kalman),
```

**ModelType::name() pattern** (`forecast.rs` lines 259–306):
```rust
// Add alongside the other name arms:
ModelType::GARCH  => "GARCH",
ModelType::Kalman => "Kalman",
```

**ForecastOptions struct extension** (`forecast.rs` lines 309–347):
```rust
// Copy pattern from laplace_variant / laplace_seasonal_batch_init (lines 337-346);
// add after laplace_seasonal_batch_init:
/// GARCH p order (0 = use default 1). Only consulted when model is GARCH.
pub garch_p: usize,
/// GARCH q order (0 = use default 1). Only consulted when model is GARCH.
pub garch_q: usize,
/// Kalman state-space spec ("local_level" | "local_linear_trend").
/// None = "local_level". Only consulted when model is Kalman.
pub kalman_model: Option<String>,
```

**ForecastOptions::default() pattern** (`forecast.rs` lines 349–367):
```rust
// Extend Default impl — copy pattern from laplace_seasonal_batch_init: false
garch_p: 0,
garch_q: 0,
kalman_model: None,
```

**forecast() dispatch match pattern** (`forecast.rs` lines 570–681):
```rust
// Copy Laplace pattern (lines 673-680); add before closing `}?;`:
ModelType::GARCH => forecast_garch(
    &clean_values,
    options.horizon,
    if options.garch_p == 0 { 1 } else { options.garch_p },
    if options.garch_q == 0 { 1 } else { options.garch_q },
),
ModelType::Kalman => forecast_kalman(
    &clean_values,
    options.horizon,
    options.kalman_model.as_deref(),
),
```

**New `forecast_garch` helper** (new function, model on `forecast_laplace` shape):
```rust
use anofox_forecast::models::garch::GARCH;

fn forecast_garch(values: &[f64], horizon: usize, p: usize, q: usize) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = GARCH::new(p, q);
    model.fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("GARCH fit failed: {}", e)))?;
    // IMPORTANT: use forecast_variance(), NOT predict() — predict() returns simulated innovations
    let variance = model.forecast_variance(horizon)
        .map_err(|e| ForecastError::ComputationError(format!("GARCH forecast failed: {}", e)))?;
    // Output is volatility (std-dev), not variance — take sqrt of each element
    let volatility: Vec<f64> = variance.iter().map(|&v| v.sqrt()).collect();
    Ok(ForecastOutput {
        point: volatility,
        lower: vec![], upper: vec![],
        fitted: None, residuals: None,
        model_name: format!("GARCH({},{})", p, q),
        aic: None, bic: None, mse: None,
    })
}
```

**New `forecast_kalman` helper** (uses `extract_forecast` — same path as all Forecaster-trait models):
```rust
use anofox_forecast::models::kalman_forecaster::KalmanForecaster;

fn forecast_kalman(values: &[f64], horizon: usize, spec: Option<&str>) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = match spec.unwrap_or("local_level") {
        "local_linear_trend" => KalmanForecaster::local_linear_trend(),
        _ => KalmanForecaster::local_level(),
    };
    model.fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("Kalman fit failed: {}", e)))?;
    // KalmanForecaster implements Forecaster — extract_forecast works directly
    extract_forecast(&model, horizon, "Kalman")
}
```

---

### `crates/anofox-fcst-ffi/src/types.rs` (FFI types, request-response)

**Analog:** Same file — `ForecastOptions` (lines 372–406), `PanelForecastResult` (lines 414–436).

**`ForecastOptions` C struct extension** (`types.rs` lines 372–406):
```rust
// Add after laplace_seasonal_batch_init (line 405):
/// GARCH p order (0 → default 1).
pub garch_p: c_int,
/// GARCH q order (0 → default 1).
pub garch_q: c_int,
/// Kalman state-space spec. Empty = "local_level".
pub kalman_model: [c_char; 32],
```

**`ForecastOptions::default()` extension** — set `garch_p: 0`, `garch_q: 0`, `kalman_model: [0; 32]`.

**New `VARForecastResult` struct** (copy `PanelForecastResult` shape, lines 414–436):
```rust
/// VAR multivariate forecast result — returned by `anofox_ts_forecast_var`.
///
/// `forecasts` is a flat `[k_vars * n_horizon]` array in variable-major order:
/// `forecasts[v * n_horizon + h]` is the forecast for variable `v` at horizon step `h`.
/// Allocated by Rust; freed by `anofox_free_var_forecast_result`.
#[repr(C)]
pub struct VARForecastResult {
    pub forecasts: *mut c_double,
    pub k_vars: size_t,
    pub n_horizon: size_t,
}

impl Default for VARForecastResult {
    fn default() -> Self {
        Self { forecasts: std::ptr::null_mut(), k_vars: 0, n_horizon: 0 }
    }
}
```

**AFTER modifying `types.rs`:** run `make header` to regenerate `src/include/anofox_fcst_ffi.h` via cbindgen. Do NOT hand-edit the header — verify new fields appear before compiling C++ (Pitfall 2 from RESEARCH.md).

---

### `crates/anofox-fcst-ffi/src/lib.rs` — GARCH/Kalman wiring in `anofox_ts_forecast`

**Analog:** Same file, params-reading block that populates `ForecastOptions` core struct (around line 3416–3431, where `laplace_variant` is read from the C struct and converted to `Option<LaplaceVariant>`).

Read the new FFI fields and wire them into the core `ForecastOptions`:
```rust
// After reading laplace_seasonal_batch_init (~line 3431):
options.garch_p  = opts.garch_p as usize;
options.garch_q  = opts.garch_q as usize;
options.kalman_model = {
    let s = CStr::from_ptr(opts.kalman_model.as_ptr()).to_str().unwrap_or("");
    if s.is_empty() { None } else { Some(s.to_owned()) }
};
```

---

### `crates/anofox-fcst-ffi/src/lib.rs` — New `anofox_ts_forecast_var` export

**Analog:** `anofox_ts_forecast_panel` (lines 6968–7059) and `forecast_panel_impl` inner function (lines 6884–6947).

**Inner function** (testable without pointer marshalling):
```rust
// Source: crates/anofox-fcst-ffi/src/lib.rs — mirrors forecast_panel_impl pattern
use anofox_forecast::models::var::VAR;

pub(crate) fn forecast_var_impl(
    flat: &[f64],
    k_vars: usize,
    series_len: usize,
    order: usize,
    horizon: usize,
) -> std::result::Result<Vec<Vec<f64>>, String> {
    if k_vars == 0 || series_len == 0 {
        return Err("Empty VAR data".into());
    }
    // Reconstruct K series from flat variable-major matrix
    let data: Vec<Vec<f64>> = (0..k_vars)
        .map(|v| flat[v * series_len..(v + 1) * series_len].to_vec())
        .collect();
    let mut model = VAR::new(order.max(1))
        .map_err(|e| format!("VAR::new failed: {}", e))?;
    model.fit(&data).map_err(|e| format!("VAR fit failed: {}", e))?;
    model.predict(horizon).map_err(|e| format!("VAR predict failed: {}", e))
}
```

**FFI export signature** (copy `anofox_ts_forecast_panel` structure, lines 6968–7059):
```rust
/// Safety doc mirrors anofox_ts_forecast_panel (line 6962-6967).
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_forecast_var(
    flat_data: *const c_double,  // flat [k_vars * series_len], variable-major; NaN = missing
    k_vars: size_t,
    series_len: size_t,
    order: size_t,               // lag order p (0 → 1)
    horizon: size_t,
    out_result: *mut VARForecastResult,
    out_error: *mut AnofoxError,
) -> bool
```

**Null-check pattern** (lines 6986–6992 of panel export):
```rust
if flat_data.is_null() || out_result.is_null() {
    if !out_error.is_null() { (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument"); }
    return false;
}
```

**checked_mul safety pattern** (lines 7012–7016 — Phase-2 lesson):
```rust
let len = k_vars.checked_mul(series_len)
    .ok_or_else(|| "VAR dimensions overflow (k_vars * series_len > usize::MAX)".to_string())?;
let flat = std::slice::from_raw_parts(flat_data, len);
```

**Output buffer allocation + fill** (lines 7046–7061):
```rust
let total = k_vars.checked_mul(horizon)
    .ok_or_else(|| "VAR output overflow (k_vars * horizon > usize::MAX)".to_string())?;
let raw = alloc_double_array(total);
for (v, var_preds) in preds.iter().enumerate() {
    for (h, &val) in var_preds.iter().enumerate() {
        *raw.add(v * horizon + h) = val;
    }
}
(*out_result).forecasts = raw;
(*out_result).k_vars    = k_vars;
(*out_result).n_horizon = horizon;
```

**Free function** (copy `anofox_free_panel_forecast_result` pattern):
```rust
#[no_mangle]
pub unsafe extern "C" fn anofox_free_var_forecast_result(result: *mut VARForecastResult) {
    if result.is_null() { return; }
    let r = &mut *result;
    if !r.forecasts.is_null() {
        free_double_array(r.forecasts, r.k_vars * r.n_horizon);
        r.forecasts = std::ptr::null_mut();
    }
}
```

---

### `src/table_functions/ts_forecast_var_native.cpp` (table-function, batch)

**Analog:** `src/table_functions/ts_forecast_panel_native.cpp` (777 lines). Mirror the full structural pattern. Key differences from panel:

1. **No `group_col`** — VAR is single-panel; date_col is index 0, value columns are indices 1..K in the subselect.
2. **`value_cols` Bind param** — `VARCHAR[]` at `input.inputs[3]`; Bind uses `input.input_table_names` to find column indices 1..K.
3. **Finalize collects K value columns** (not one) into `vector<vector<double>> series_data(k_vars)`.
4. **Output schema:** `variable VARCHAR, forecast_step BIGINT, forecast_date TIMESTAMP, forecast_value DOUBLE` (long format, not group/yhat/model_name).
5. **FFI call:** `anofox_ts_forecast_var(flat, k_vars, n, order, horizon, &var_result, &error)`.

**BindData struct** (copy `TsForecastPanelNativeBindData`, lines 33–49):
```cpp
struct TsForecastVarNativeBindData : public TableFunctionData {
    int64_t horizon = 7;
    int64_t frequency_seconds = 86400;
    bool frequency_is_raw = false;
    FrequencyType frequency_type = FrequencyType::FIXED;
    int64_t order = 1;

    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
    LogicalType date_logical_type = LogicalType(LogicalTypeId::TIMESTAMP);

    vector<string> value_col_names;   // from value_cols VARCHAR[] arg
    vector<idx_t>  value_col_indices; // resolved in Bind from input.input_table_names
};
```

**GlobalState struct** (copy `TsForecastPanelNativeGlobalState`, lines 86–101; remove `groups_mutex` / per-group map — use flat per-column vectors instead):
```cpp
struct TsForecastVarNativeGlobalState : public GlobalTableFunctionState {
    idx_t MaxThreads() const override { return 999999; }
    std::mutex data_mutex;
    vector<int64_t> dates;
    vector<vector<double>> series_data;  // [k_vars][n_obs]
    vector<vector<bool>>   series_valid; // [k_vars][n_obs]
    vector<VarOutputRow> results;
    bool processed = false;
    idx_t output_offset = 0;
    std::atomic<bool> finalize_claimed{false};
    std::atomic<idx_t> threads_collecting{0};
    std::atomic<idx_t> threads_done_collecting{0};
};
```

**Bind function** (copy `TsForecastPanelNativeBind`, lines 200–282; adapt for VAR):
```cpp
// Parse horizon (input.inputs[1]), frequency (input.inputs[2]), order (input.inputs[3])
// Parse value_cols (input.inputs[4]) — a LIST Value:
auto value_cols_val = input.inputs[4];
auto &cols_list = ListValue::GetChildren(value_cols_val);
for (auto &col_val : cols_list) {
    string col_name = col_val.GetValue<string>();
    bind_data->value_col_names.push_back(col_name);
    for (idx_t i = 0; i < input.input_table_names.size(); i++) {
        if (input.input_table_names[i] == col_name) {
            bind_data->value_col_indices.push_back(i);
            break;
        }
    }
}
// Output schema:
names.push_back("variable");       return_types.push_back(LogicalType::VARCHAR);
names.push_back("forecast_step");  return_types.push_back(LogicalType::BIGINT);
names.push_back(date_col_name);    return_types.push_back(bind_data->date_logical_type);
names.push_back("forecast_value"); return_types.push_back(LogicalType::DOUBLE);
```

**InOut (Execute) phase** (copy `TsForecastPanelNativeInOut`, lines 305–396; adapt column reads):
```cpp
// col 0 = date, col 1..K = value columns (by bind_data->value_col_indices)
Value date_val = input.data[0].GetValue(i);
for (idx_t v = 0; v < bind_data->value_col_names.size(); v++) {
    idx_t col_idx = bind_data->value_col_indices[v];
    Value val = input.data[col_idx].GetValue(i);
    gstate.series_data[v].push_back(val.IsNull() ? NaN : val.GetValue<double>());
    gstate.series_valid[v].push_back(!val.IsNull());
}
```

**Finalize barrier pattern** (copy panel Finalize barrier, lines 404–430):
```cpp
// Same atomic finalize_claimed pattern; one thread runs the FFI call.
// Fill NaN pre-impute using fill_nulls_interpolate equivalent before building flat matrix.
// Build flat variable-major matrix:
vector<double> flat;
flat.reserve(k_vars * n);
for (size_t v = 0; v < k_vars; v++)
    for (size_t t = 0; t < n; t++)
        flat.push_back(series_data[v][t]);  // NaN = missing (FFI will fail; pre-impute first)

VARForecastResult var_result;
memset(&var_result, 0, sizeof(var_result));
AnofoxError error;
bool ok = anofox_ts_forecast_var(flat.data(), k_vars, n, order, horizon, &var_result, &error);
```

**Output emit pattern** (copy panel output loop, lines 700–743; adapt for long format):
```cpp
// variable, forecast_step, forecast_date, forecast_value
for (size_t v = 0; v < k_vars; v++) {
    for (int64_t h = 1; h <= horizon; h++) {
        output.data[0].SetValue(i, Value(value_col_names[v]));         // variable
        output.data[1].SetValue(i, Value::BIGINT(h));                   // forecast_step
        output.data[2].SetValue(i, Value::TIMESTAMP(forecast_date));    // date
        output.data[3].SetValue(i, Value::DOUBLE(var_result.forecasts[v * horizon + (h-1)]));
    }
}
anofox_free_var_forecast_result(&var_result);
```

**Registration function** (copy `RegisterTsForecastPanelNativeFunction`, lines 759–775):
```cpp
void RegisterTsForecastVarNativeFunction(ExtensionLoader &loader) {
    // Input TABLE has: date_col, then all K value cols (via subselect in macro)
    // Args after TABLE: horizon BIGINT, frequency VARCHAR, order BIGINT, value_cols VARCHAR[], params ANY
    TableFunction func("_ts_forecast_var_native",
        {LogicalType::TABLE, LogicalType::INTEGER, LogicalType::VARCHAR,
         LogicalType::INTEGER, LogicalType::LIST(LogicalType::VARCHAR), LogicalType::ANY},
        nullptr,
        TsForecastVarNativeBind,
        TsForecastVarNativeInitGlobal,
        TsForecastVarNativeInitLocal);
    func.in_out_function       = TsForecastVarNativeInOut;
    func.in_out_function_final = TsForecastVarNativeFinalize;
    loader.RegisterFunction(func);
}
```

---

### `src/macros/ts_macros.cpp` — `ts_forecast_var_by` macro

**Analog:** `ts_forecast_panel_by` entry (lines 606–623). Copy macro registration pattern exactly.

**CRITICAL — subselect pattern** (Phase-2 lesson; line 610): Always wrap `query_table(source::VARCHAR)` in a subselect. Never pass it as a bare TABLE arg.

```cpp
// Add after ts_forecast_panel_by entry (after line 623):
{"ts_forecast_var_by",
 {"source", "date_col", "value_cols", "horizon", "frequency", nullptr},
 {{"order", "1"}, {"params", "MAP{}"}, {nullptr, nullptr}},
R"(
SELECT variable, forecast_step, date_col, forecast_value
FROM _ts_forecast_var_native(
    (SELECT date_col, * FROM query_table(source::VARCHAR)),
    horizon,
    frequency,
    order,
    value_cols,
    params
)
)",
"VAR multivariate forecasting. Returns one row per (variable, horizon step) in long format. "
"value_cols is a VARCHAR[] of column names. order is the lag order p (default 1). "
"forecast_value for all variables is a point forecast (no prediction intervals in v1). "
"v1 is single-panel only (no group_col).",
"SELECT * FROM ts_forecast_var_by('returns', 'ds', ['equity','bond','fx'], 12, '1d', order:=2)",
"forecasting"},
```

---

### `src/anofox_forecast_extension.cpp` (registration)

**Analog:** Lines 169–170.

```cpp
// Add after RegisterTsForecastPanelNativeFunction (line 170):
RegisterTsForecastVarNativeFunction(loader);   // Phase 3: CLAS-03
```

Also add `#include "ts_forecast_var_native.hpp"` at the top with the other table-function includes.

---

### `src/table_functions/ts_forecast_native.cpp` — param plumbing for GARCH/Kalman

**Analog:** Same file. Three touch points:

**1. `ValidateParamKeys` set** (lines 270–274):
```cpp
// Add to valid_keys set:
"garch_p", "garch_q", "kalman_model"
```

**2. `TsForecastNativeBindData` struct** (find struct definition, add fields):
```cpp
int64_t garch_p = 0;
int64_t garch_q = 0;
string kalman_model = "";
```

**3. `TsForecastNativeBind` param parsing** (lines 342–354 pattern):
```cpp
bind_data->garch_p       = ParseInt64FromParams(params, "garch_p", 0);
bind_data->garch_q       = ParseInt64FromParams(params, "garch_q", 0);
bind_data->kalman_model  = ParseStringFromParams(params, "kalman_model", "");
```

**4. `TsForecastNativeFinalize` opts population** (lines 617–651 pattern):
```cpp
// Copy laplace_variant strncpy pattern (lines 645-649); add after opts.laplace_seasonal_batch_init:
opts.garch_p = static_cast<int>(bind_data.garch_p);
opts.garch_q = static_cast<int>(bind_data.garch_q);
if (!bind_data.kalman_model.empty()) {
    strncpy(opts.kalman_model, bind_data.kalman_model.c_str(), sizeof(opts.kalman_model) - 1);
    opts.kalman_model[sizeof(opts.kalman_model) - 1] = '\0';
}
```

---

### `benchmark/m4/garch_benchmark/run.py`, `kalman_benchmark/run.py`, `var_benchmark/run.py`

**Analog:** `benchmark/m4/global_benchmark/run.py` (lines 1–50 shown above).

All three use the same `create_benchmark_functions` harness from `src/common/benchmark_runner`:
```python
# Template for all three run.py files:
import sys
from pathlib import Path
import fire
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from src.common.benchmark_runner import create_benchmark_functions
from configs import <model_config>, <reference_config>

anofox, reference, evaluate, run = create_benchmark_functions(
    anofox_config=<model_config>,
    statsforecast_config=<reference_config>,
    output_dir=Path(__file__).parent / 'results'
)

if __name__ == '__main__':
    fire.Fire({'run': run, 'anofox': anofox, 'statsforecast': reference, 'evaluate': evaluate})
```

**GARCH:** reference = `arch.arch_model` (requires adding `arch>=5.3.0` to `benchmark/pyproject.toml` optional `comparison` group — currently missing; fallback = variance-convergence self-check).

**Kalman:** reference = `statsmodels.tsa.statespace.structural.UnobservedComponents` (already installed).

**VAR:** reference = `statsmodels.tsa.api.VAR` (already installed). Source data = synthetic VAR(1) generated in Python (not M4 — no suitable multivariate M4 dataset), mirroring the `generate_var1_data` pattern from `var.rs` lines 439–459.

**Run all benchmarks via** `benchmark/.venv/bin/python` (never system `python3`) — Phase-1 precedent.

---

### `examples/forecasting/classical_forecasting_examples.sql`

**Analog:** `examples/forecasting/global_panel_forecasting_examples.sql` (verified structure, similar layout).

Pattern: runnable SQL snippets against the built extension, each exercising the new method. Must be verified end-to-end before merge (PR #230 rule). Three sections:
1. GARCH — note that `forecast_value` is volatility (std-dev), not variance.
2. Kalman — show both `local_level` (default) and `local_linear_trend`.
3. VAR — show `['y1','y2']` multi-column call, long-format output.

---

### Docs files

**Analog:** Existing `docs/reference/models/state-space/` pages (for Kalman); existing model doc pages (for GARCH, VAR layout).

New directories and files:
- `docs/reference/models/classical/garch.md` — new dir
- `docs/reference/models/multivariate/var.md` — new dir
- `docs/reference/models/state-space/kalman.md` — existing dir, new file
- `docs/api/07-forecasting.md` — extend, add Classical subsection after Panel section (line 318+)

---

## Shared Patterns

### Rust FFI: `catch_unwind` + null-check wrapper
**Source:** `crates/anofox-fcst-ffi/src/lib.rs` lines 6982–7022 (`anofox_ts_forecast_panel`)
**Apply to:** `anofox_ts_forecast_var` (new export)
```rust
if !out_error.is_null() { *out_error = AnofoxError::success(); }
if values.is_null() || out_result.is_null() {
    if !out_error.is_null() { (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument"); }
    return false;
}
let result = catch_unwind(AssertUnwindSafe(|| { /* inner logic */ }));
match result {
    Ok(Ok(...)) => { /* fill out_result */ true }
    Ok(Err(e))  => { if !out_error.is_null() { (*out_error).set_error(..., ...) } false }
    Err(_)      => { if !out_error.is_null() { (*out_error).set_error(ErrorCode::Panic, "Rust panic") } false }
}
```

### C++ Finalize barrier (single-thread finalize + atomic claim)
**Source:** `src/table_functions/ts_forecast_panel_native.cpp` lines 404–430
**Apply to:** `ts_forecast_var_native.cpp` Finalize function
```cpp
// Only one thread runs the FFI call:
if (gstate.finalize_claimed.exchange(true)) {
    // Another thread is finalizing — spin until processed
    while (!gstate.processed) { std::this_thread::yield(); }
    // Fall through to the output-batching block
} else {
    // This thread owns finalization
    // ... sort, impute, build flat matrix, call anofox_ts_forecast_var ...
    gstate.processed = true;
}
```

### checked_mul before `slice::from_raw_parts`
**Source:** `crates/anofox-fcst-ffi/src/lib.rs` lines 7012–7017
**Apply to:** `anofox_ts_forecast_var` — both input size and output size
```rust
let len = k_vars.checked_mul(series_len)
    .ok_or_else(|| "dimensions overflow".to_string())?;
let flat = std::slice::from_raw_parts(flat_data, len);
```

### C++ params parsing (`ParseStringFromParams`, `ParseInt64FromParams`)
**Source:** `src/table_functions/ts_forecast_native.cpp` lines 200–263
**Apply to:** `ts_forecast_native.cpp` (GARCH/Kalman new keys) and `ts_forecast_var_native.cpp` (order param)
```cpp
// Copy the helpers already in ts_forecast_native.cpp or ts_forecast_panel_native.cpp;
// for VAR create local analogues `ParseStringFromVarParams` / `ParseInt64FromVarParams`
// following the same MAP/STRUCT dual-branch pattern (lines 200-263).
```

### `fill_nulls_interpolate` before FFI
**Source:** `crates/anofox-fcst-ffi/src/lib.rs` lines 6897–6906 (inside `forecast_panel_impl`)
**Apply to:** `forecast_var_impl` — must impute NaN per column before passing to `VAR::fit()` because VAR rejects NaN (var.rs:119-124). Convert `series_data[v]` using the same `fill_nulls_interpolate` call.

### `make header` after types.rs change
**Source:** Phase-2 RESEARCH.md Pitfall 2; `Makefile` `header` target
**Apply to:** After any change to `crates/anofox-fcst-ffi/src/types.rs`
```bash
make header   # runs cbindgen; updates src/include/anofox_fcst_ffi.h
```
Verify new fields (`garch_p`, `garch_q`, `kalman_model`, `VARForecastResult`) appear in the header before compiling C++.

---

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| `benchmark/configs/garch.py`, `kalman.py`, `var.py` | config/benchmark | batch | No existing benchmark config files were directly read; pattern inferred from `global_benchmark/run.py` imports. Planner should read `benchmark/configs/global_ets.py` to confirm the exact config shape before writing these. |

---

## Metadata

**Analog search scope:** `crates/anofox-fcst-core/src/`, `crates/anofox-fcst-ffi/src/`, `src/table_functions/`, `src/macros/`, `src/`, `benchmark/m4/global_benchmark/`
**Files read this session:** 12 source files (forecast.rs, types.rs, lib.rs ×2 ranges, ts_forecast_native.cpp ×3 ranges, ts_forecast_panel_native.cpp ×4 ranges, ts_macros.cpp, anofox_forecast_extension.cpp, global_benchmark/run.py)
**Pattern extraction date:** 2026-08-21
