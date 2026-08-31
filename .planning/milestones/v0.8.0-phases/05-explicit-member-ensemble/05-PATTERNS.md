# Phase 5: Explicit-Member Ensemble — Pattern Map

**Mapped:** 2026-08-31
**Files analyzed:** 10 new/modified files
**Analogs found:** 10 / 10

---

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|---|---|---|---|---|
| `crates/anofox-fcst-core/src/forecast.rs` | core / utility | transform | itself (existing `forecast_auto_ensemble` at line 2560, `parse_combination_method` at line 2532, `extract_forecast` at line 2377) | exact — same file, additive |
| `crates/anofox-fcst-ffi/src/lib.rs` | FFI / service | request-response | `anofox_ts_forecast` at line 3386 | exact |
| `src/include/anofox_fcst_ffi.h` | config / generated | n/a | existing header (regenerated via `make header`) | generated — no code pattern needed |
| `src/table_functions/ts_forecast_ensemble_native.cpp` | table function | request-response | `src/table_functions/ts_forecast_native.cpp` | exact |
| `src/table_functions/ts_forecast_ensemble_native.hpp` | header | n/a | `src/table_functions/ts_forecast_native.hpp` | exact |
| `src/anofox_forecast_extension.cpp` | config / registration | n/a | line 176 (`RegisterTsForecastNativeFunction`) | exact |
| `src/macros/ts_macros.cpp` | macro | request-response | `ts_forecast_by` at line 575 | exact |
| `CMakeLists.txt` | config | n/a | lines 161–209 (explicit source list) | exact |
| `examples/forecasting/ensemble_explicit.sql` | example | n/a | `examples/forecasting/autoensemble.sql` | exact |
| `docs/reference/models/ensemble/ensemble_explicit.md` | docs | n/a | `docs/reference/models/ensemble/autoensemble.md` | role-match |

---

## Pattern Assignments

### `crates/anofox-fcst-core/src/forecast.rs` (core, transform) — ADDITIVE

**Two new functions** go after `forecast_auto_ensemble` (line 2580). No existing code changes.

**Existing `parse_combination_method` pattern** (lines 2532–2550) — reuse verbatim:
```rust
fn parse_combination_method(s: Option<&str>) -> Result<anofox_forecast::models::ensemble::CombinationMethod> {
    use anofox_forecast::models::ensemble::CombinationMethod;
    match s.unwrap_or("").trim().to_lowercase().as_str() {
        "" | "mean" => Ok(CombinationMethod::Mean),
        "median" => Ok(CombinationMethod::Median),
        "weighted_mse" | "weightedmse" | "weighted-mse" => Ok(CombinationMethod::WeightedMSE),
        "inverse_aic" | "inverseaic" | "inverse-aic" | "aic" => Ok(CombinationMethod::InverseAIC),
        "stacking" | "stack" => Ok(CombinationMethod::Stacking { folds: 2 }),
        "horizon_adaptive" | "horizonadaptive" | "horizon-adaptive" | "adaptive" => {
            Ok(CombinationMethod::HorizonAdaptive)
        }
        other => Err(ForecastError::InvalidParameter {
            param: "combination_method".to_string(),
            value: other.to_string(),
            reason: "expected one of: mean, median, weighted_mse, inverse_aic, stacking, horizon_adaptive"
                .to_string(),
        }),
    }
}
```

**Existing `extract_forecast` pattern** (lines 2377–2409) — used verbatim by the new `forecast_explicit_ensemble`:
```rust
fn extract_forecast(
    model: &dyn Forecaster,
    horizon: usize,
    name_override: &str,
) -> Result<ForecastOutput> {
    let forecast = model.predict(horizon).map_err(|e| {
        ForecastError::ComputationError(format!("{} predict failed: {}", name_override, e))
    })?;
    let point = forecast.primary().to_vec();
    let lower = forecast.lower().and_then(|i| i.first()).cloned().unwrap_or_default();
    let upper = forecast.upper().and_then(|i| i.first()).cloned().unwrap_or_default();
    Ok(ForecastOutput {
        point, lower, upper,
        fitted: model.fitted_values().map(|v| v.to_vec()),
        residuals: model.residuals().map(|v| v.to_vec()),
        model_name: name_override.to_string(),
        aic: None, bic: None, mse: None,
    })
}
```

**Existing `forecast_auto_ensemble` pattern** (lines 2560–2581) — `forecast_explicit_ensemble` mirrors this structure:
```rust
fn forecast_auto_ensemble(
    values: &[f64],
    horizon: usize,
    top_k: usize,
    method_str: Option<&str>,
    period: usize,
) -> Result<ForecastOutput> {
    use anofox_forecast::models::ensemble::{AutoEnsemble, AutoEnsembleConfig};
    let combination_method = parse_combination_method(method_str)?;
    let config = AutoEnsembleConfig {
        top_k,
        combination_method,
        seasonal_period: if period > 1 { Some(period) } else { None },
    };
    let ts = make_timeseries(values)?;
    let mut model = AutoEnsemble::with_config(config);
    model.fit(&ts).map_err(|e| {
        ForecastError::ComputationError(format!("AutoEnsemble fit failed: {}", e))
    })?;
    extract_forecast(&model, horizon, "AutoEnsemble")
}
```

**Existing inline dispatch per-model construction pattern** (lines 645–783) — mirrors what `build_forecaster` must replicate as boxed instances. Key arms (confirmed by reading source):
```rust
// forecast() match arms showing exact constructors — replicate as Box::new(...) in build_forecaster:
ModelType::Naive => forecast_naive(...)                   // Naive::new()
ModelType::SES => forecast_ses_fixed(...)                 // SimpleExponentialSmoothing::new(0.3)
ModelType::SESOptimized => forecast_ses_optimized(...)    // see forecast_ses_optimized() fn
ModelType::Holt => forecast_holt_lib(...)                 // HoltLinearTrend::auto()
ModelType::HoltWinters => forecast_holt_winters_lib(...)  // HoltWintersModel::new(period)
ModelType::SeasonalES => forecast_seasonal_es_lib(...)    // SeasonalESModel::new(period)
ModelType::SeasonalESOptimized => forecast_seasonal_es_optimized(...)
ModelType::Theta => forecast_theta_stm(...)               // Theta::new()
ModelType::OptimizedTheta => forecast_optimized_theta(...)
ModelType::DynamicTheta => forecast_dynamic_theta(...)    // DynamicTheta::new()
ModelType::DynamicOptimizedTheta => forecast_dynamic_optimized_theta(...)
ModelType::AutoTheta => forecast_auto_theta(...)          // AutoTheta::new() or AutoTheta::seasonal(p)
ModelType::AutoARIMA => forecast_auto_arima(...)          // AutoARIMA::with_config(cfg)
ModelType::AutoETS => forecast_auto_ets(...)              // AutoETS::with_config(cfg)
ModelType::Kalman => ...                                  // KalmanForecaster::local_level()
ModelType::CrostonClassic => ...                          // Croston::new()
ModelType::CrostonOptimized => ...                        // Croston::new().optimized()
ModelType::CrostonSBA => ...                              // Croston::new().sba()
ModelType::ADIDA => ...                                   // ADIDA::new()
ModelType::IMAPA => ...                                   // IMAPA::new()
ModelType::TSB => ...                                     // TSB::new()
```
Read the individual `forecast_*` sub-functions (lines ~1240–2460) to get the exact constructor in each case before writing `build_forecaster`.

**New `build_forecaster` placement:** insert after `forecast_auto_ensemble` (line 2581), before the exogenous-aware section (line 2583). Follows the skeleton in RESEARCH.md exactly. The `pub(crate)` visibility allows `forecast_explicit_ensemble` (also in `forecast.rs`) to call it. No `pub` export to FFI layer is needed.

**New `forecast_explicit_ensemble` placement:** immediately after `build_forecaster`. Copy the doc comment + error pattern from `forecast_auto_ensemble`. Uses `parse_combination_method` (already in scope), `make_timeseries` (already in scope), `extract_forecast` (already in scope), and the crate `Ensemble` struct.

**Imports already available** (lines 1–22, verified):
```rust
use anofox_forecast::models::arima::{AutoARIMA, AutoARIMAConfig};
use anofox_forecast::models::exponential::{
    AutoETS, AutoETSConfig, ETSSpec, HoltLinearTrend, HoltWinters as HoltWintersModel, ModelPool,
    SeasonalES as SeasonalESModel, SimpleExponentialSmoothing, ETS as ETSModel,
};
use anofox_forecast::models::garch::GARCH;
use anofox_forecast::models::intermittent::{Croston, ADIDA, IMAPA, TSB};
use anofox_forecast::models::kalman_forecaster::KalmanForecaster;
use anofox_forecast::models::laplace::LaplaceForecaster;
use anofox_forecast::models::mstl_forecaster::MSTLForecaster;
use anofox_forecast::models::tbats::{AutoTBATS, TBATS as TBATSModel};
use anofox_forecast::models::theta::{AutoTheta, DynamicTheta, OptimizedTheta, Theta};
use anofox_forecast::models::MFLES;
use anofox_forecast::prelude::Forecaster;
```
Add one import in the local scope of `build_forecaster`:
```rust
use anofox_forecast::models::BoxedForecaster;  // or Box<dyn Forecaster> directly
use anofox_forecast::models::baseline::{
    Naive, RandomWalkWithDrift, SeasonalNaive, SeasonalWindowAverage, WindowAverage,
};
use anofox_forecast::models::ensemble::Ensemble;
```
Verify which baseline types are already imported vs need adding — `baseline::*` types may not appear in the top-level `use` block if the inline functions import them locally. Check lines 1240–1400 for each `forecast_naive` / `forecast_drift` etc. to see their local imports.

---

### `crates/anofox-fcst-ffi/src/lib.rs` (FFI, request-response) — ADDITIVE

**Analog:** `anofox_ts_forecast` at lines 3386–3612.

**Function signature pattern** (lines 3386–3393):
```rust
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_forecast(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    options: *const ForecastOptions,
    out_result: *mut ForecastResult,
    out_error: *mut AnofoxError,
) -> bool {
```
New function signature:
```rust
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_forecast_ensemble(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    members_buf: *const c_char,    // null-delimited: "AutoARIMA\0AutoETS\0Theta\0"
    members_count: size_t,
    combination_method: *const c_char,  // C string, empty string = "mean"
    seasonal_period: c_int,
    horizon: c_int,
    out_result: *mut ForecastResult,
    out_error: *mut AnofoxError,
) -> bool {
```

**Null-pointer guard pattern** (lines 3394–3403):
```rust
if !out_error.is_null() {
    *out_error = AnofoxError::success();
}
if values.is_null() || options.is_null() || out_result.is_null() {
    if !out_error.is_null() {
        (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
    }
    return false;
}
```

**`catch_unwind` + `build_series` pattern** (lines 3405–3488):
```rust
let result = catch_unwind(AssertUnwindSafe(|| {
    let series = build_series(values, validity, length);
    // ... parse parameters ...
    anofox_fcst_core::forecast(&series, &core_opts)
}));
```

**Null-delimited member-list parse** (new, mirrors Rust idiomatic pattern documented in RESEARCH.md):
```rust
// Inside catch_unwind closure:
let member_bytes = std::slice::from_raw_parts(members_buf as *const u8, {
    // scan until we've consumed members_count null-terminated strings
    let mut pos = 0usize;
    let mut found = 0usize;
    while found < members_count {
        if *members_buf.add(pos) == 0 { found += 1; }
        pos += 1;
    }
    pos
});
let member_names: Vec<String> = member_bytes
    .split(|&b| b == 0)
    .filter(|s| !s.is_empty())
    .map(|s| String::from_utf8_lossy(s).into_owned())
    .collect();
```
Simpler alternative: pass `members_buf_len` (total byte length) from C++ as a `size_t` parameter, then `std::slice::from_raw_parts(members_buf as *const u8, members_buf_len)`. This is safer. Choose during execution.

**`combination_method` string parse** (mirrors `kalman_model` parse at lines 3452–3457):
```rust
let method_str = if combination_method.is_null() {
    None
} else {
    CStr::from_ptr(combination_method)
        .to_str()
        .ok()
        .filter(|s| !s.is_empty())
};
```

**ForecastResult marshalling** (lines 3492–3583) — copy verbatim: `alloc_or_error` for `point_forecasts`; `ptr::null_mut()` for `lower_bounds` and `upper_bounds` (point-only for Phase 5); `copy_string_to_buffer(&forecast.model_name, &mut (*out_result).model_name)`.

**Error handling** (lines 3585–3612) — copy verbatim: `Ok(Err(e)) => { (*out_error).set_error(...); false }` and `Err(_) => { (*out_error).set_error(ErrorCode::Panic, ...); false }`.

**Placement:** append after the last existing `#[no_mangle] pub unsafe extern "C" fn` block in `lib.rs`. Do NOT insert inside an existing function.

**After adding, run `make header`** to regenerate `src/include/anofox_fcst_ffi.h`.

---

### `src/table_functions/ts_forecast_ensemble_native.cpp` (table function, request-response) — NEW FILE

**Analog:** `src/table_functions/ts_forecast_native.cpp` (entire file).

**File header / includes pattern** (ts_forecast_native.cpp lines 1–14):
```cpp
#include "ts_forecast_ensemble_native.hpp"
#include "ts_fill_gaps_native.hpp"  // For ParseFrequencyToSeconds, DateColumnType etc.
#include "anofox_fcst_ffi.h"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <map>
#include <mutex>
#include <atomic>
#include <cmath>
#include <cstring>

namespace duckdb {
```

**Bind data struct pattern** (ts_forecast_native.cpp lines 33–62) — simplified for ensemble:
```cpp
struct TsForecastEnsembleNativeBindData : public TableFunctionData {
    // Required parameters
    int64_t horizon = 7;
    int64_t frequency_seconds = 86400;
    bool frequency_is_raw = false;
    FrequencyType frequency_type = FrequencyType::FIXED;
    // Ensemble-specific (no method string — members list replaces it)
    vector<string> member_names;         // parsed from DuckDB LIST(VARCHAR)
    string combination_method = "";      // "" → "mean" in Rust core
    int64_t seasonal_period = 0;
    double confidence_level = 0.90;
    // Type preservation
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
    LogicalType date_logical_type = LogicalType(LogicalTypeId::TIMESTAMP);
    LogicalType group_logical_type = LogicalType(LogicalTypeId::VARCHAR);
};
```

**Global + Local state pattern** (ts_forecast_native.cpp lines 90–123) — copy verbatim, rename `TsForecastNative` → `TsForecastEnsembleNative`.

**ForecastGroupData and ForecastOutputRow** (lines 68–84) — copy verbatim (same output schema).

**Bind function key difference — LIST(VARCHAR) extraction:**
```cpp
// In TsForecastEnsembleNativeBind, after extracting horizon/frequency/combination_method/seasonal_period:
// input.inputs[3] is the members LIST(VARCHAR)
auto &members_val = input.inputs[3];
if (members_val.IsNull()) {
    throw InvalidInputException(
        "ts_forecast_ensemble_by: 'members' must not be NULL. "
        "Provide a non-empty VARCHAR[] list, e.g., members := ['AutoARIMA','AutoETS','Theta'].");
}
if (members_val.type().id() == LogicalTypeId::LIST) {
    auto &list_children = ListValue::GetChildren(members_val);
    for (auto &child : list_children) {
        if (!child.IsNull()) {
            bind_data->member_names.push_back(child.GetValue<string>());
        }
    }
}
if (bind_data->member_names.size() < 2) {
    throw InvalidInputException(
        "ts_forecast_ensemble_by: at least 2 members are required. Got %zu.",
        bind_data->member_names.size());
}
```

**Null-delimited buffer construction for FFI call** (new, in Finalize):
```cpp
// Build null-delimited member name buffer
std::string members_buf;
for (const auto &name : bind_data.member_names) {
    members_buf += name;
    members_buf += '\0';
}
size_t members_count = bind_data.member_names.size();

// Call new FFI function
ForecastResult fcst_result;
memset(&fcst_result, 0, sizeof(fcst_result));
AnofoxError error;

bool success = anofox_ts_forecast_ensemble(
    sorted_values.data(),
    validity.empty() ? nullptr : validity.data(),
    sorted_values.size(),
    members_buf.c_str(),
    members_count,
    bind_data.combination_method.c_str(),
    static_cast<int>(bind_data.seasonal_period),
    static_cast<int>(bind_data.horizon),
    &fcst_result,
    &error
);
```

**Error handling after FFI call** (ts_forecast_native.cpp lines 697–703) — copy verbatim:
```cpp
if (!success) {
    if (error.code == INVALID_MODEL || error.code == INVALID_INPUT) {
        throw InvalidInputException(string(error.message));
    }
    // Skip this group on computation/data errors
    continue;
}
```

**Output row generation** (ts_forecast_native.cpp lines 706–790) — copy verbatim. `yhat_lower` and `yhat_upper` will be naturally NULL (empty arrays from FFI) — the existing pattern handles `fcst_result.lower_bounds == nullptr` by outputting NULL.

**Output schema** (ts_forecast_native.cpp lines 442–470) — copy verbatim: `group_col`, `forecast_step (INTEGER)`, `date_col`, `yhat (DOUBLE)`, `yhat_lower (DOUBLE)`, `yhat_upper (DOUBLE)`, `model_name (VARCHAR)`.

**Registration function** (ts_forecast_native.cpp lines 849–864):
```cpp
void RegisterTsForecastEnsembleNativeFunction(ExtensionLoader &loader) {
    // Positional args: TABLE(group, dates, values), members LIST(VARCHAR),
    //                  horizon INTEGER, frequency VARCHAR,
    //                  combination_method VARCHAR, seasonal_period INTEGER
    TableFunction func("_ts_forecast_ensemble_native",
        {LogicalType::TABLE, LogicalType::LIST(LogicalType::VARCHAR),
         LogicalType::INTEGER, LogicalType::VARCHAR,
         LogicalType::VARCHAR, LogicalType::INTEGER},
        nullptr,
        TsForecastEnsembleNativeBind,
        TsForecastEnsembleNativeInitGlobal,
        TsForecastEnsembleNativeInitLocal);

    func.in_out_function = TsForecastEnsembleNativeInOut;
    func.in_out_function_final = TsForecastEnsembleNativeFinalize;

    loader.RegisterFunction(func);
}
```

**CRITICAL:** This new `.cpp` file MUST be added to `CMakeLists.txt` before the first build attempt (see CMakeLists.txt section below).

---

### `src/table_functions/ts_forecast_ensemble_native.hpp` (header) — NEW FILE

**Analog:** `src/table_functions/ts_forecast_native.hpp`.

Declare `void RegisterTsForecastEnsembleNativeFunction(ExtensionLoader &loader);` with the same include guards and namespace pattern.

---

### `src/anofox_forecast_extension.cpp` (registration) — ADDITIVE

**Include pattern** (lines 5–6):
```cpp
#include "ts_forecast_panel_native.hpp"  // Phase 2: GLOB-01..03
#include "ts_forecast_var_native.hpp"    // Phase 3: CLAS-03
```
Add:
```cpp
#include "ts_forecast_ensemble_native.hpp"  // Phase 5: ENS-02
```

**Registration call pattern** (line 176–178):
```cpp
RegisterTsForecastNativeFunction(loader);
RegisterTsForecastPanelNativeFunction(loader);  // Phase 2: GLOB-01..03
RegisterTsForecastVarNativeFunction(loader);    // Phase 3: CLAS-03
```
Add after line 178:
```cpp
RegisterTsForecastEnsembleNativeFunction(loader);  // Phase 5: ENS-02
```

---

### `src/macros/ts_macros.cpp` (macro) — ADDITIVE

**Analog:** `ts_forecast_by` macro (lines 575–594).

**`ts_forecast_by` macro pattern** (lines 575–594):
```cpp
{"ts_forecast_by",
 {"source", "group_col", "date_col", "target_col", "method", "horizon", "frequency", nullptr},
 {{"params", "MAP{}"}, {nullptr, nullptr}},
R"(
SELECT group_col, forecast_step, ds, yhat, yhat_lower, yhat_upper, model_name
FROM (
    SELECT group_col,
           unnest(_ts_forecast_scalar(
               LIST(date_col ORDER BY date_col),
               LIST(target_col::DOUBLE ORDER BY date_col),
               horizon,
               frequency,
               method,
               params
           ), recursive := true)
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
)
)",
 "...",
 "...",
 "forecasting"},
```

**New `ts_forecast_ensemble_by` macro** — same per-series structure, `members` replaces `method`, `_ts_forecast_ensemble_native` replaces `_ts_forecast_scalar`, backed by a native table function (not a scalar), so uses `unnest(..., recursive := true)` differently. The macro expands directly to the `GROUP BY` aggregation:

```cpp
{"ts_forecast_ensemble_by",
 {"source", "group_col", "date_col", "target_col", "members", "horizon", "frequency", nullptr},
 {{"combination_method", "''"}, {"seasonal_period", "0"}, {nullptr, nullptr}},
R"(
SELECT group_col, forecast_step, ds, yhat, yhat_lower, yhat_upper, model_name
FROM (
    SELECT group_col,
           unnest(_ts_forecast_ensemble_native(
               group_col,
               LIST(date_col ORDER BY date_col),
               LIST(target_col::DOUBLE ORDER BY date_col),
               members,
               horizon,
               frequency,
               combination_method,
               seasonal_period
           ), recursive := true)
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
)
)",
 "Generates ensemble forecasts from explicitly named member models per series. "
 "Members is a VARCHAR[] list of model names (e.g. ['AutoARIMA','AutoETS','Theta']). "
 "Combination method: 'mean' (default), 'median', 'weighted_mse', 'inverse_aic', 'stacking', 'horizon_adaptive'. "
 "yhat_lower and yhat_upper are NULL in Phase 5; ensemble prediction intervals are Phase 6 (EPI-01).",
 "SELECT * FROM ts_forecast_ensemble_by('sales', product_id, date, qty, ['AutoARIMA','AutoETS','Theta'], 12, '1d')",
 "forecasting"},
```

**Note:** This is per-series (like `ts_forecast_by`), NOT a panel/TABLE-in macro. The `query_table(source::VARCHAR)` + `GROUP BY group_col` + `LIST(... ORDER BY ...)` pattern is identical to `ts_forecast_by`. The panel/subselect gotcha (`project_panel_macro_subselect_pattern`) does NOT apply here.

**Note on native table function vs scalar:** `ts_forecast_by` uses `_ts_forecast_scalar` (a scalar function that returns a struct). If `_ts_forecast_ensemble_native` is a true TABLE in-out function (like the Phase 2 panel function), the expansion pattern differs. RESEARCH shows it follows `ts_forecast_native` which IS an in-out table function. Verify the macro expansion matches what `unnest(_ts_forecast_ensemble_native(...), recursive := true)` expects — this is the same pattern used by `_ts_forecast_native` if the macro calls it that way. Check the existing `ts_forecast_by` expansion carefully: it calls `_ts_forecast_scalar`, not `_ts_forecast_native`. The new macro may need to follow the same approach (scalar wrapper) OR call the native function directly. **The safest approach:** follow the `_ts_forecast_native` registration pattern and verify the macro syntax during execution by testing `SELECT unnest(_ts_forecast_ensemble_native(...), recursive := true) FROM ... GROUP BY ...`.

---

### `CMakeLists.txt` (config) — ADDITIVE, BLOCKING

**Critical rule:** The source list at lines 161–209 is **explicit** (no `GLOB`). Missing the new file produces a clean build but the function is absent at runtime.

**Current list end** (lines 195–209):
```cmake
    src/table_functions/ts_metrics_native.cpp
    src/table_functions/ts_periods.cpp
    src/table_functions/ts_peaks.cpp
    src/table_functions/ts_detrend.cpp
    src/scalar_functions/metrics.cpp
    ...
    src/macros/ts_macros.cpp
)
```

**Add** after `src/table_functions/ts_forecast_panel_native.cpp` (line 179) or `ts_forecast_var_native.cpp` (line 180):
```cmake
    src/table_functions/ts_forecast_ensemble_native.cpp
```
Suggested placement — after line 180:
```cmake
    src/table_functions/ts_forecast_native.cpp
    src/table_functions/ts_forecast_panel_native.cpp
    src/table_functions/ts_forecast_var_native.cpp
    src/table_functions/ts_forecast_ensemble_native.cpp   # Phase 5: ENS-02
```

---

### `examples/forecasting/ensemble_explicit.sql` (example, DoD) — NEW FILE

**Analog:** `examples/forecasting/autoensemble.sql`.

**File structure pattern** (autoensemble.sql lines 1–60):
```sql
-- ============================================================================
-- [Title] — Phase N (REQ-ID)
-- ============================================================================
-- [Description of what the example demonstrates]
-- Run: ./build/release/duckdb -unsigned < examples/forecasting/ensemble_explicit.sql
-- ============================================================================

LOAD anofox_forecast;

.print '============================================================================='
.print '[TITLE]'
.print '============================================================================='

-- SECTION 1: Mean Combination Cross-Check (DoD)
-- [comments explaining the invariant]
CREATE OR REPLACE TABLE ae_test AS
SELECT 1 AS id,
       '2020-01-01'::DATE + INTERVAL (i - 1) DAY AS ds,
       10.0 + i * 0.5 AS y
FROM range(1, 61) t(i);
```

**Cross-check SQL pattern** (from RESEARCH.md, DoD):
```sql
-- Step 1: Ensemble mean
CREATE OR REPLACE TABLE ens_result AS
SELECT * FROM ts_forecast_ensemble_by('ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'], 5, '1d',
    combination_method := 'mean', seasonal_period := 0);

-- Step 2: Individual member forecasts
CREATE OR REPLACE TABLE m_arima AS
    SELECT forecast_step, yhat AS y_arima
    FROM ts_forecast_by('ae_test', id, ds, y, 'AutoARIMA', 5, '1d');
CREATE OR REPLACE TABLE m_ets AS
    SELECT forecast_step, yhat AS y_ets
    FROM ts_forecast_by('ae_test', id, ds, y, 'AutoETS', 5, '1d');
CREATE OR REPLACE TABLE m_theta AS
    SELECT forecast_step, yhat AS y_theta
    FROM ts_forecast_by('ae_test', id, ds, y, 'Theta', 5, '1d');

-- Step 3: Manual arithmetic mean
CREATE OR REPLACE TABLE m_manual AS
SELECT forecast_step, (y_arima + y_ets + y_theta) / 3.0 AS manual_mean
FROM m_arima JOIN m_ets USING (forecast_step) JOIN m_theta USING (forecast_step);

-- Step 4: Cross-check (all rows must have diff < 1e-6)
SELECT e.forecast_step, e.yhat AS ens_mean, m.manual_mean,
       abs(e.yhat - m.manual_mean) AS diff,
       abs(e.yhat - m.manual_mean) < 1e-6 AS match
FROM ens_result e JOIN m_manual m USING (forecast_step)
ORDER BY forecast_step;

-- Assertion: zero mismatches
SELECT count(*) AS mismatch_count
FROM ens_result e JOIN m_manual m USING (forecast_step)
WHERE abs(e.yhat - m.manual_mean) >= 1e-6;
-- Must return 0
```

**Section 2:** smoke test all six combination methods (non-NULL finite yhat, NULL yhat_lower/yhat_upper).
**Section 3:** error tests — unknown member name, < 2 members, blocked model (GARCH), NULL members.

---

## Shared Patterns

### Null-delimited string list marshalling
**New pattern (no existing analog)** — C++ → Rust for VARCHAR[].

**C++ side** (in Finalize of `ts_forecast_ensemble_native.cpp`):
```cpp
std::string members_buf;
for (const auto &name : bind_data.member_names) {
    members_buf += name;
    members_buf += '\0';
}
// Pass members_buf.c_str() (total bytes = members_buf.size()) and members_count
```

**Rust side** (in `anofox_ts_forecast_ensemble`):
```rust
let buf_len = /* total bytes of members_buf, passed as extra param */;
let member_bytes = std::slice::from_raw_parts(members_buf as *const u8, buf_len);
let member_names: Vec<String> = member_bytes
    .split(|&b| b == 0)
    .filter(|s| !s.is_empty())
    .map(|s| String::from_utf8_lossy(s).into_owned())
    .collect();
```
Recommendation: add `members_buf_len: size_t` as an explicit parameter alongside `members_count` to avoid scanning for lengths.

### `parse_combination_method` reuse
**Source:** `crates/anofox-fcst-core/src/forecast.rs` lines 2532–2550.
**Apply to:** `forecast_explicit_ensemble` (calls it directly), `anofox_ts_forecast_ensemble` FFI (passes raw string to core).
Do NOT duplicate the parse logic — it already handles all six methods, aliases, and error messages.

### `InvalidParameter` error for blocked model types
**Source:** `crates/anofox-fcst-core/src/error.rs` (ForecastError::InvalidParameter variant).
**Apply to:** Every blocked ModelType arm in `build_forecaster`.
**Pattern:**
```rust
ModelType::GARCH => Err(ForecastError::InvalidParameter {
    param: "members".to_string(),
    value: "GARCH".to_string(),
    reason: "GARCH is not supported as an ensemble member ...".to_string(),
}),
```
The 26 supported members pass through `build_forecaster`; the 10 blocked types (GARCH, Laplace, ARIMA, MFLES, AutoMFLES, MSTL, AutoMSTL, TBATS, AutoTBATS, AutoEnsemble) return `Err(InvalidParameter)` with a suggestion.

### Error propagation through FFI
**Source:** `anofox_ts_forecast` (lib.rs lines 3585–3612).
**Apply to:** `anofox_ts_forecast_ensemble`.
`Ok(Err(ForecastError::InvalidParameter { .. }))` → `ErrorCode::InvalidInput` → C++ `InvalidInputException`.
`Ok(Err(ForecastError::ComputationError(_)))` → `ErrorCode::ComputationError` → C++ silently skips group.

### Group collection + single-thread finalize
**Source:** `ts_forecast_native.cpp` (entire file structure).
**Apply to:** `ts_forecast_ensemble_native.cpp` verbatim.
`std::atomic<bool> finalize_claimed` guard + `groups_mutex` for thread-safe insertion.

---

## No Analog Found

All files have a close analog. The one genuinely new pattern is the null-delimited VARCHAR[] marshalling — no existing FFI function in the project passes a variable-length string list. The closest existing pattern is `seasonal_periods_str` (a comma-delimited string passed via a fixed-size char buffer in `ForecastOptions`), but that uses `strncpy` into a fixed-size field, not a dynamic buffer. The null-delimited approach is cleaner for a variable list and is documented fully in RESEARCH.md.

---

## Build Sequence Checklist (for planner)

Order of operations for executor:

1. Edit `crates/anofox-fcst-core/src/forecast.rs` — add `build_forecaster` + `forecast_explicit_ensemble`.
2. Edit `crates/anofox-fcst-ffi/src/lib.rs` — add `anofox_ts_forecast_ensemble` export.
3. Run `make header` — regenerate `src/include/anofox_fcst_ffi.h`.
4. Create `src/table_functions/ts_forecast_ensemble_native.hpp` (header declaration).
5. Create `src/table_functions/ts_forecast_ensemble_native.cpp` (table function).
6. Edit `CMakeLists.txt` — add new `.cpp` to `EXTENSION_SOURCES` (BLOCKING if missed).
7. Edit `src/anofox_forecast_extension.cpp` — `#include` + `Register*` call.
8. Edit `src/macros/ts_macros.cpp` — add `ts_forecast_ensemble_by` macro.
9. Build: `make rust` then build C++.
10. Create `examples/forecasting/ensemble_explicit.sql` and run it against the built extension.
11. Create `docs/reference/models/ensemble/ensemble_explicit.md`.
12. Update `docs/api/07-forecasting.md`.

---

## Metadata

**Analog search scope:** `crates/anofox-fcst-core/src/`, `crates/anofox-fcst-ffi/src/`, `src/table_functions/`, `src/macros/`, `src/anofox_forecast_extension.cpp`, `CMakeLists.txt`, `examples/forecasting/`
**Files scanned:** 8 source files read directly + grep searches
**Pattern extraction date:** 2026-08-31
