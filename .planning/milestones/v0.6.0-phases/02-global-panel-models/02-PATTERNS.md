# Phase 2: Global / Panel Models - Pattern Map

**Mapped:** 2026-08-21
**Files analyzed:** 9 new/modified files
**Analogs found:** 9 / 9

---

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `crates/anofox-fcst-ffi/src/lib.rs` (append) | FFI export | request-response | `lib.rs:3343–3427` (`anofox_ts_forecast`) | exact |
| `crates/anofox-fcst-ffi/src/types.rs` (append) | model/struct | transform | `types.rs:328–368` (`ForecastResult`) | exact |
| `src/table_functions/ts_forecast_panel_native.cpp` | table function | CRUD / batch | `src/table_functions/ts_forecast_native.cpp` | exact |
| `src/include/ts_forecast_panel_native.hpp` | config/header | — | `src/include/ts_forecast_native.hpp` | role-match |
| `src/macros/ts_macros.cpp` (append entry) | macro/config | request-response | `ts_macros.cpp:575–594` (`ts_forecast_by`) | exact |
| `src/anofox_forecast_extension.cpp` (append call) | config/registration | — | `extension.cpp:168` (`RegisterTsForecastNativeFunction`) | exact |
| `benchmark/m4/global_benchmark/run.py` | utility/test | batch | `benchmark/m4/ets_benchmark/run.py` | exact |
| `benchmark/configs/global_ets.py` | config | — | `benchmark/configs/ets.py` | role-match |
| `examples/forecasting/global_panel_forecasting_examples.sql` | utility | request-response | `examples/forecasting/synthetic_forecasting_examples.sql` | role-match |

---

## Pattern Assignments

### `crates/anofox-fcst-ffi/src/lib.rs` — new `anofox_ts_forecast_panel` export

**Analog:** `crates/anofox-fcst-ffi/src/lib.rs:3343–3427` (`anofox_ts_forecast`)

**Imports pattern** (lib.rs lines 1–30, already present — no new imports needed except):
```rust
use anofox_forecast::models::exponential::{GlobalAutoETS, ModelPool};
use anofox_forecast::models::theta::GlobalTheta;
use anofox_forecast::models::intermittent::{GlobalCroston, CrostonVariant};
use anofox_fcst_core::fill_nulls_interpolate;
```

**Canonical FFI signature style** (lib.rs lines 138–179, `anofox_ts_stats` as structural template):
```rust
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_stats(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    out_result: *mut TsStatsResult,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);
    // null-check via check_null_ptrs! ...
    let result = catch_unwind(AssertUnwindSafe(|| {
        // ... Rust logic ...
    }));
    match result {
        Ok(Ok(stats)) => { *out_result = stats.into(); true }
        Ok(Err(e)) => { set_error(out_error, ErrorCode::ComputationError, &e.to_string()); false }
        Err(_) => { set_error(out_error, ErrorCode::PanicCaught, "Panic in Rust code"); false }
    }
}
```

**Forecast FFI analog** (lib.rs lines 3343–3362) — null-check + catch_unwind skeleton to copy:
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
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }
    if values.is_null() || options.is_null() || out_result.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }
    let result = catch_unwind(AssertUnwindSafe(|| {
        // ...
    }));
    // match result { ... }
}
```

**Panel-specific body pattern** (from RESEARCH.md — new code to write):
```rust
// Flat packed matrix: series_i occupies flat[i*series_len..(i+1)*series_len]
let flat = std::slice::from_raw_parts(values, n_series * series_len);
let panel: Vec<Vec<f64>> = (0..n_series)
    .map(|i| {
        let raw: Vec<Option<f64>> = flat[i * series_len..(i + 1) * series_len]
            .iter()
            .map(|&v| if v.is_nan() { None } else { Some(v) })
            .collect();
        fill_nulls_interpolate(&raw)
    })
    .collect();

let method_str = CStr::from_ptr(method).to_str().unwrap_or("");
match method_str {
    "GlobalETS" => {
        let pool = parse_model_pool(model_pool_str); // default Reduced
        let mut model = GlobalAutoETS::new(period, pool);
        model.fit(&panel)?;
        Ok(model.predict(horizon))
    }
    "GlobalTheta" => {
        let mut model = GlobalTheta::new();
        model.fit(&panel)?;
        Ok(model.predict(horizon))
    }
    "GlobalCroston" => {
        let is_sba = variant_str == "SBA";
        let mut model = if is_sba {
            GlobalCroston::sba()
        } else {
            GlobalCroston::new()
        };
        model.fit(&panel)?;
        Ok(model.predict(horizon))
    }
    other => Err(ForecastError::InvalidModel(format!("Unknown panel method: {}", other)))
}
```

**Error code mapping** (types.rs lines 16–28 — use existing codes, no new ones needed):
```rust
ErrorCode::Success = 0, NullPointer = 1, InvalidInput = 2, ComputationError = 3,
AllocationError = 4, InvalidModel = 5, InsufficientData = 6, ...
```

**Free function pattern** (lib.rs ~line 5900 — copy existing `anofox_free_forecast_result` shape):
```rust
#[no_mangle]
pub unsafe extern "C" fn anofox_free_panel_forecast_result(result: *mut PanelForecastResult) {
    if result.is_null() { return; }
    let r = &mut *result;
    if !r.forecasts.is_null() {
        anofox_free_double_array(r.forecasts);
        r.forecasts = std::ptr::null_mut();
    }
}
```

---

### `crates/anofox-fcst-ffi/src/types.rs` — append `PanelForecastResult`

**Analog:** `types.rs:328–368` (`ForecastResult` struct)

**Struct pattern** (types.rs lines 328–368):
```rust
#[repr(C)]
pub struct ForecastResult {
    pub point_forecasts: *mut c_double,
    pub lower_bounds: *mut c_double,
    pub upper_bounds: *mut c_double,
    pub fitted_values: *mut c_double,
    pub residuals: *mut c_double,
    pub n_forecasts: size_t,
    pub n_fitted: size_t,
    pub model_name: [c_char; 64],
    pub aic: c_double,
    pub bic: c_double,
    pub mse: c_double,
}

impl Default for ForecastResult {
    fn default() -> Self {
        Self {
            point_forecasts: std::ptr::null_mut(),
            // ... all ptr fields null_mut(), size fields 0, float fields NAN
        }
    }
}
```

**New struct to write** (mirrors above, panel-specific fields):
```rust
#[repr(C)]
pub struct PanelForecastResult {
    /// Flat [n_series * n_horizon] array; series-major order; allocated by Rust
    pub forecasts: *mut c_double,
    pub n_series: size_t,
    pub n_horizon: size_t,
    pub model_name: [c_char; 64],
}

impl Default for PanelForecastResult {
    fn default() -> Self {
        Self {
            forecasts: std::ptr::null_mut(),
            n_series: 0,
            n_horizon: 0,
            model_name: [0; 64],
        }
    }
}
```

---

### `src/table_functions/ts_forecast_panel_native.cpp` — new file

**Analog:** `src/table_functions/ts_forecast_native.cpp` (entire file, 824 lines)

**Includes pattern** (ts_forecast_native.cpp lines 1–14):
```cpp
#include "ts_forecast_panel_native.hpp"
#include "ts_fill_gaps_native.hpp"  // ParseFrequencyWithType, date helpers
#include "anofox_fcst_ffi.h"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <mutex>
#include <atomic>
#include <thread>
#include <cmath>
#include <cstring>
```

**BindData struct pattern** (ts_forecast_native.cpp lines 33–55 — copy and strip per-series-only fields):
```cpp
struct TsForecastPanelNativeBindData : public TableFunctionData {
    int64_t horizon = 7;
    int64_t frequency_seconds = 86400;
    bool frequency_is_raw = false;
    FrequencyType frequency_type = FrequencyType::FIXED;
    string method = "GlobalETS";
    int64_t seasonal_period = 0;
    string model_pool = "";       // "Reduced" (default) | "Complete"
    string croston_variant = "";  // "Classic" (default) | "SBA"
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
    LogicalType date_logical_type = LogicalType(LogicalTypeId::TIMESTAMP);
    LogicalType group_logical_type = LogicalType(LogicalTypeId::VARCHAR);
};
```

**Group data + output row structs** (ts_forecast_native.cpp lines 61–77 — copy verbatim):
```cpp
struct ForecastGroupData {
    Value group_value;
    vector<int64_t> dates;
    vector<double> values;
    vector<bool> validity;
};

struct PanelOutputRow {
    string group_key;
    Value group_value;
    int64_t forecast_step;
    int64_t date;
    double point_forecast;
    string model_name;
};
```

**LocalState + GlobalState pattern** (ts_forecast_native.cpp lines 83–116 — copy verbatim):
```cpp
struct TsForecastPanelNativeLocalState : public LocalTableFunctionState {
    bool owns_finalize = false;
    bool registered_collector = false;
    bool registered_finalizer = false;
};

struct TsForecastPanelNativeGlobalState : public GlobalTableFunctionState {
    idx_t MaxThreads() const override { return 999999; }
    std::mutex groups_mutex;
    std::map<string, ForecastGroupData> groups;
    vector<string> group_order;
    vector<PanelOutputRow> results;
    bool processed = false;
    idx_t output_offset = 0;
    std::atomic<bool> finalize_claimed{false};
    std::atomic<idx_t> threads_collecting{0};
    std::atomic<idx_t> threads_done_collecting{0};
};
```

**Output schema** (ts_forecast_native.cpp lines 426–452 — same but drop yhat_lower/yhat_upper):
```cpp
// Output: <group_col>, forecast_step, <date_col>, yhat, model_name
names.push_back(group_col_name);
return_types.push_back(bind_data->group_logical_type);
names.push_back("forecast_step");
return_types.push_back(LogicalType::INTEGER);
names.push_back(date_col_name);
return_types.push_back(bind_data->date_logical_type);
names.push_back("yhat");
return_types.push_back(LogicalType::DOUBLE);
names.push_back("model_name");
return_types.push_back(LogicalType::VARCHAR);
```

**InOut phase** (ts_forecast_native.cpp lines 476–553 — copy verbatim; no changes needed):
```cpp
static OperatorResultType TsForecastPanelNativeInOut(
    ExecutionContext &context, TableFunctionInput &data_p,
    DataChunk &input, DataChunk &output) {
    // ... register collector, extract batch locally, lock once/insert all ...
    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}
```

**Finalize barrier** (ts_forecast_native.cpp lines 559–584 — copy verbatim):
```cpp
// Barrier + CAS claim
if (!lstate.registered_finalizer) {
    if (lstate.registered_collector) gstate.threads_done_collecting.fetch_add(1);
    lstate.registered_finalizer = true;
}
if (!lstate.owns_finalize) {
    bool expected = false;
    if (!gstate.finalize_claimed.compare_exchange_strong(expected, true))
        return OperatorFinalizeResultType::FINISHED;
    lstate.owns_finalize = true;
    while (gstate.threads_done_collecting.load() < gstate.threads_collecting.load())
        std::this_thread::yield();
}
```

**Panel-specific Finalize processing** (new code — replaces the per-group FFI loop):
```cpp
// 1. Build shared date grid: generate regular grid from min_date to max_date
int64_t min_date = INT64_MAX, max_date = INT64_MIN;
for (const auto &key : gstate.group_order) {
    auto &grp = gstate.groups[key];
    for (auto d : grp.dates) { min_date = std::min(min_date, d); max_date = std::max(max_date, d); }
}
// Generate grid using ParseFrequencyWithType step (reuse ts_fill_gaps_native logic)
vector<int64_t> shared_grid;
// ... step from min_date to max_date by frequency_seconds (fixed) or calendar step (monthly/etc)

// 2. Align each series to shared_grid: Vec<double> with NaN for missing
vector<vector<double>> aligned; // [n_series][grid_len]
vector<string> valid_keys;
for (const auto &key : gstate.group_order) {
    auto &grp = gstate.groups[key];
    // Sort by date, build map<int64_t, double>
    // Fill grid: present → value, absent → NaN
    // Drop rule: skip if valid_count < min_len (emit DROPPED row instead)
    aligned.push_back(series_values);
    valid_keys.push_back(key);
}

// 3. Build flat matrix: double[n_series * grid_len]
size_t n_series = aligned.size();
size_t grid_len = shared_grid.size();
vector<double> flat_matrix(n_series * grid_len);
for (size_t i = 0; i < n_series; i++)
    std::copy(aligned[i].begin(), aligned[i].end(), flat_matrix.data() + i * grid_len);

// 4. Call panel FFI once
PanelForecastResult panel_result;
memset(&panel_result, 0, sizeof(panel_result));
AnofoxError error;
bool ok = anofox_ts_forecast_panel(
    flat_matrix.data(), n_series, grid_len,
    bind_data.method.c_str(), bind_data.horizon,
    bind_data.seasonal_period,
    bind_data.croston_variant.c_str(),
    &panel_result, &error);
if (!ok) throw InvalidInputException(string(error.message));

// 5. Emit output rows: n_series * horizon rows
for (size_t s = 0; s < n_series; s++) {
    for (size_t h = 0; h < panel_result.n_horizon; h++) {
        PanelOutputRow row;
        row.group_key = valid_keys[s];
        row.group_value = gstate.groups[valid_keys[s]].group_value;
        row.forecast_step = static_cast<int64_t>(h + 1);
        // Date arithmetic: reuse calendar-aware logic from ts_forecast_native.cpp:682-730
        row.point_forecast = panel_result.forecasts[s * panel_result.n_horizon + h];
        row.model_name = string(panel_result.model_name);
        gstate.results.push_back(row);
    }
}
anofox_free_panel_forecast_result(&panel_result);
```

**Date arithmetic** (ts_forecast_native.cpp lines 682–730 — copy verbatim, same calendar-aware monthly/quarterly/yearly handling).

**Output emission loop** (ts_forecast_native.cpp lines 745–799 — copy and adjust for 5-column schema):
```cpp
output.data[0].SetValue(i, row.group_value);
output.data[1].SetValue(i, Value::INTEGER(static_cast<int32_t>(row.forecast_step)));
// data[2]: date (same switch on date_col_type as ts_forecast_native.cpp:770-783)
output.data[3].SetValue(i, Value::DOUBLE(row.point_forecast));
output.data[4].SetValue(i, Value(row.model_name));
```

**Registration function** (ts_forecast_native.cpp lines 806–821 — copy and rename):
```cpp
void RegisterTsForecastPanelNativeFunction(ExtensionLoader &loader) {
    TableFunction func("_ts_forecast_panel_native",
        {LogicalType::TABLE, LogicalType::INTEGER, LogicalType::VARCHAR,
         LogicalType::VARCHAR, LogicalType::ANY},
        nullptr,
        TsForecastPanelNativeBind,
        TsForecastPanelNativeInitGlobal,
        TsForecastPanelNativeInitLocal);
    func.in_out_function = TsForecastPanelNativeInOut;
    func.in_out_function_final = TsForecastPanelNativeFinalize;
    loader.RegisterFunction(func);
}
```

---

### `src/include/ts_forecast_panel_native.hpp` — new header

**Analog:** `src/include/ts_forecast_native.hpp` (forward declaration + include guard pattern)

**Pattern:** Standard DuckDB extension header — one-liner forward declaration:
```cpp
#pragma once
#include "duckdb.hpp"
namespace duckdb {
void RegisterTsForecastPanelNativeFunction(ExtensionLoader &loader);
} // namespace duckdb
```

---

### `src/macros/ts_macros.cpp` — append `ts_forecast_panel_by` entry

**Analog:** `src/macros/ts_macros.cpp:575–594` (`ts_forecast_by`)

**TsTableMacro entry pattern** (ts_macros.cpp lines 575–594):
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
               horizon, frequency, method, params
           ), recursive := true)
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
)
)",
"...", "SELECT * FROM ts_forecast_by(...)", "forecasting"},
```

**New entry to write** (insert after line 594):
```cpp
{"ts_forecast_panel_by",
 {"source", "group_col", "date_col", "target_col", "method", "horizon", "frequency", nullptr},
 {{"params", "MAP{}"}, {nullptr, nullptr}},
R"(
SELECT group_col, forecast_step, date_col, yhat, model_name
FROM _ts_forecast_panel_native(
    query_table(source::VARCHAR),
    group_col,
    date_col,
    target_col,
    horizon,
    frequency,
    method,
    params
)
)",
"Forecasts a grouped panel using cross-series global learners (GlobalETS, GlobalTheta, GlobalCroston). "
"All series are fitted simultaneously with shared parameters. Returns one row per (group, horizon step). "
"Requires equal-length series — ragged panels are auto-aligned to a shared date grid.",
"SELECT * FROM ts_forecast_panel_by('sales', product_id, date, qty, 'GlobalETS', 14, '1d', MAP{'seasonal_period': '7'})",
"forecasting"},
```

**Registration loop** (ts_macros.cpp lines 2290–2301) — no change needed; the loop picks up the new entry automatically via the null-terminated array sentinel.

---

### `src/anofox_forecast_extension.cpp` — append registration call

**Analog:** `src/anofox_forecast_extension.cpp:168` (`RegisterTsForecastNativeFunction`)

**Pattern** (extension.cpp lines 155–183):
```cpp
// Register Native Table Functions (streaming)
RegisterTsBacktestNativeFunction(loader);
RegisterTsForecastNativeFunction(loader);     // ← insert after this line
RegisterTsForecastPanelNativeFunction(loader); // NEW (Phase 2: GLOB-01..03)
RegisterTsCvSplitNativeFunction(loader);
```

Also add the corresponding `#include`:
```cpp
#include "ts_forecast_panel_native.hpp"
```

---

### `benchmark/m4/global_benchmark/run.py` — new file

**Analog:** `benchmark/m4/ets_benchmark/run.py` (verbatim structure)

**Full pattern** (ets_benchmark/run.py lines 1–30):
```python
"""
Global panel models benchmark (GlobalETS, GlobalTheta, GlobalCroston).

Uses shared common modules and configuration files.
Run via: cd benchmark && uv run python m4/global_benchmark/run.py run
"""
import sys
from pathlib import Path

import fire

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from src.common.benchmark_runner import create_benchmark_functions
from configs import global_ets, statsforecast_global

anofox, statsforecast, evaluate, run = create_benchmark_functions(
    anofox_config=global_ets,
    statsforecast_config=statsforecast_global,
    output_dir=Path(__file__).parent / 'results'
)

if __name__ == '__main__':
    fire.Fire({
        'run': run,
        'anofox': anofox,
        'statsforecast': statsforecast,
        'evaluate': evaluate
    })
```

**anofox_runner.py modification** — the runner hardcodes `TS_FORECAST_BY` (anofox_runner.py lines 135–148). Add a `function_name` parameter defaulting to `'TS_FORECAST_BY'`, or create `run_anofox_panel_benchmark` variant that substitutes `TS_FORECAST_PANEL_BY`. Panel runner query shape:
```python
forecast_query = f"""
    SELECT *
    FROM TS_FORECAST_PANEL_BY(
        'train',
        unique_id,
        ds,
        y,
        '{model_name}',
        {horizon},
        '{freq_str}',
        {map_literal}
    )
"""
```

---

### `benchmark/configs/global_ets.py` — new file

**Analog:** `benchmark/configs/ets.py` (config module structure)

**Pattern to follow:**
```python
BENCHMARK_NAME = 'global_ets'
MODELS = [
    {
        'name': 'GlobalETS',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
    {
        'name': 'GlobalTheta',
        'params': lambda seasonality: {}
    },
    {
        'name': 'GlobalCroston',
        'params': lambda seasonality: {}
    },
]
```

---

### `examples/forecasting/global_panel_forecasting_examples.sql` — new file

**Analog:** `examples/forecasting/synthetic_forecasting_examples.sql`

**Header pattern** (from existing example files):
```sql
-- global_panel_forecasting_examples.sql
-- Demonstrates ts_forecast_panel_by with GlobalETS, GlobalTheta, GlobalCroston.
-- Run: ./build/release/duckdb < examples/forecasting/global_panel_forecasting_examples.sql

LOAD anofox_forecast;
```

**Structure:** 4 sections (CREATE TABLE with ragged panel → GlobalETS → GlobalTheta → GlobalCroston). Each section ends with a SELECT that must return rows against the built extension before it counts as done.

---

## Shared Patterns

### Finalize Barrier (single-thread processing)
**Source:** `src/table_functions/ts_forecast_native.cpp` lines 559–584
**Apply to:** `ts_forecast_panel_native.cpp` Finalize function (copy verbatim — same CAS + spin barrier)

```cpp
if (!lstate.registered_finalizer) {
    if (lstate.registered_collector) gstate.threads_done_collecting.fetch_add(1);
    lstate.registered_finalizer = true;
}
if (!lstate.owns_finalize) {
    bool expected = false;
    if (!gstate.finalize_claimed.compare_exchange_strong(expected, true))
        return OperatorFinalizeResultType::FINISHED;
    lstate.owns_finalize = true;
    while (gstate.threads_done_collecting.load() < gstate.threads_collecting.load())
        std::this_thread::yield();
}
```

### FFI catch_unwind + error mapping
**Source:** `crates/anofox-fcst-ffi/src/lib.rs` lines 3362–3380 (anofox_ts_forecast body)
**Apply to:** `anofox_ts_forecast_panel` — same match arms: `Ok(Ok(...))`, `Ok(Err(e))`, `Err(_)` (panic)

### Output batching loop
**Source:** `src/table_functions/ts_forecast_native.cpp` lines 745–799
**Apply to:** `ts_forecast_panel_native.cpp` — copy the STANDARD_VECTOR_SIZE chunk loop; adjust only column indices to match 5-column schema

### Calendar-aware date arithmetic
**Source:** `src/table_functions/ts_forecast_native.cpp` lines 682–730
**Apply to:** `ts_forecast_panel_native.cpp` Finalize forecast-date computation — copy verbatim, replacing `last_date` with the last date of `shared_grid`

### Frequency parsing
**Source:** `src/include/ts_fill_gaps_native.hpp` lines 21–28 (`ParseFrequencyWithType`, date helpers)
**Apply to:** `ts_forecast_panel_native.cpp` Bind (parse frequency string) and Finalize (generate shared date grid steps)
```cpp
ParsedFrequency ParseFrequencyWithType(const string &frequency_str);
int64_t DateToMicroseconds(date_t date);
date_t MicrosecondsToDate(int64_t micros);
```

### Params MAP parsing helpers
**Source:** `src/table_functions/ts_forecast_native.cpp` lines 343–400 (`ParseStringFromParams`, `ParseInt64FromParams`, `ValidateParamKeys`)
**Apply to:** `ts_forecast_panel_native.cpp` Bind — parse `seasonal_period`, `model_pool`, `variant` keys from the `params` MAP argument

### Macro registration loop (auto-registers all entries)
**Source:** `src/macros/ts_macros.cpp` lines 2290–2301
**Apply to:** No change needed — new `ts_forecast_panel_by` entry in the array is picked up automatically

---

## No Analog Found

All files have close analogs. No file requires falling back to RESEARCH.md-only patterns.

---

## Metadata

**Analog search scope:** `src/table_functions/`, `src/macros/`, `src/include/`, `src/anofox_forecast_extension.cpp`, `crates/anofox-fcst-ffi/src/`, `benchmark/m4/ets_benchmark/`, `benchmark/src/common/`, `examples/forecasting/`
**Files read:** 10 source files (ts_forecast_native.cpp, ts_macros.cpp, lib.rs, types.rs, extension.cpp, ets_benchmark/run.py, anofox_runner.py, ts_fill_gaps_native.hpp excerpt via RESEARCH.md)
**Pattern extraction date:** 2026-08-21
