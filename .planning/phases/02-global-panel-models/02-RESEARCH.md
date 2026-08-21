# Phase 2: Global / Panel Models - Research

**Researched:** 2026-08-21
**Domain:** Global cross-series forecasting (GlobalETS, GlobalTheta, GlobalCroston), DuckDB panel table function, ragged→dense alignment
**Confidence:** HIGH

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**Area 1 — Panel Forecast SQL Surface Shape**
- New dedicated surface, not an extension of `ts_forecast_by`. The per-series dispatch is incompatible with fit-once-emit-many global models.
- Delivery: new FFI export (`crates/anofox-fcst-ffi`) → new native table function `_ts_forecast_panel_native` (`src/table_functions/`) → user-facing macro **`ts_forecast_panel_by`** (`src/macros/ts_macros.cpp`).
- Model selection via a **`method` string**: `'GlobalETS'`, `'GlobalTheta'`, `'GlobalCroston'` — mirrors `ts_forecast_by`.
- Signature mirrors `ts_forecast_by`: `ts_forecast_panel_by(source, group_col, date_col, target_col, method, horizon, frequency, params := MAP{})`.

**Area 2 — Ragged Panel Handling**
- The crate requires all series to have equal length. Alignment happens inside the table function before the FFI call.
- Auto-align every series to a shared date grid (union of dates across the panel, on the declared `frequency`).
- Gap-fill / leading-fill each series up to the common length.
- Series that are too short or all-null are dropped with a surfaced warning.
- Intra-series nulls are imputed (interpolation) before the global fit.

**Area 3 — Output Shape & Intervals**
- Long format: one row per (series, horizon step) — identical shape to `ts_forecast_by`.
- Point forecasts only for v1. `predict(horizon)` returns `Vec<Vec<f64>>` (points). Prediction intervals deferred.
- Output columns match the existing surface: `{group_col}, forecast_date, forecast_value, model`.
- No per-series fitted-spec metadata in the output for v1.

**Area 4 — Benchmark & Docs**
- Parity baseline = statsforecast.
- Reuse the M4 subset already present under `benchmark/m4/`.
- Parity criterion is behavioral / approximate (relative MASE within tolerance).
- Docs delivered in `docs/api/` and `docs/reference/models/`, plus a runnable `examples/*.sql` snippet verified end-to-end.

### Claude's Discretion
- Exact imputation/interpolation algorithm for intra-series nulls.
- Minimum-series-length threshold for the drop-with-warning rule.
- Internal FFI marshalling shape for the ragged→dense panel (row offsets vs. equal-length matrix), provided the crate's equal-length contract is met.

### Deferred Ideas (OUT OF SCOPE)
- Prediction intervals for global/panel forecasts — route through the existing conformal prediction surface in a later increment.
- Per-series fitted-spec / model-metadata output columns — omitted from v1 to keep the output lean.
- The `batch::auto_ets/ets/mfles` shared-compute per-series batch path (distinct from Global* cross-learning) — not exposed in this phase.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| GLOB-01 | User can forecast a grouped panel with GlobalETS (cross-series learning) via the panel-aware forecast surface. | GlobalETS API fully documented below; new FFI + table function + macro pattern verified from existing code |
| GLOB-02 | User can forecast a grouped panel with GlobalTheta. | GlobalTheta API documented; simpler than ETS (no spec/period required) |
| GLOB-03 | User can forecast a grouped panel with GlobalCroston (intermittent panel). | GlobalCroston API documented; equal-length contract same as ETS/Theta despite extracting demand sub-sequences internally |
</phase_requirements>

---

## Summary

Phase 2 exposes GlobalETS, GlobalTheta, and GlobalCroston — the upstream crate's true cross-series learners — through a new panel-aware SQL surface `ts_forecast_panel_by`. These models pool smoothing parameters across the entire panel and predict per-series, requiring all series to be equal-length dense `f64` arrays at the FFI boundary. The core challenge is ragged→dense alignment inside the C++ table function, before any Rust call.

The upstream API is simple: each model has `new(...)`, `fit(&[Vec<f64>])`, and `predict(horizon) -> Vec<Vec<f64>>`. GlobalTheta takes no arguments (`::new()`), GlobalCroston takes no arguments (`::new()` or `::sba()`), GlobalETS takes `(ETSSpec, period)` but the recommended user entry point is `GlobalAutoETS::new(period, ModelPool)` which does spec selection internally. Re-export paths are confirmed in the crate's module `pub use` declarations.

The delivery pattern is a straight extension of the Phase 1 diagnostic pattern and the existing `_ts_forecast_native` table function: collect the whole panel in-memory across the Finalize barrier, align series to a shared date grid, impute nulls with linear interpolation (using the existing `fill_nulls_interpolate` from core), drop invalid series with a warning, call a new `anofox_ts_forecast_panel` FFI export, then emit long-format rows.

**Primary recommendation:** Model the new table function exactly on `ts_forecast_native.cpp`. The only structural change is that Finalize fits once across all series instead of once per group, and the output loop emits N_series × horizon rows.

---

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Global model fit (cross-learning) | Rust Core (via FFI) | — | Math lives in anofox-forecast 0.15.3; FFI exports it |
| Ragged→dense alignment | C++ table function Finalize | — | Table function already owns in-memory collection; alignment is a pre-processing step before the FFI call |
| Intra-series null imputation | Rust Core (`fill_nulls_interpolate`) called from FFI wrapper | — | Existing utility in `anofox-fcst-core::imputation` |
| Date grid / frequency step | C++ (`ParseFrequencyWithType`) | — | Already in `ts_fill_gaps_native.hpp`, reusable |
| Panel dispatch (method string) | C++ bind / Finalize | — | Same pattern as `ts_forecast_native.cpp` |
| SQL surface / named params | SQL macro (`ts_macros.cpp`) | — | Macro wraps native function; same TsTableMacro pattern |
| Benchmark | Python benchmark harness (`benchmark/m4/`) | statsforecast | Existing `create_benchmark_functions` factory + new config modules |

---

## Standard Stack

### Core (all existing — no new dependencies)
| Component | Version | Purpose | Source |
|-----------|---------|---------|--------|
| `anofox-forecast` | 0.15.3 (locked) | GlobalETS, GlobalTheta, GlobalCroston models | `Cargo.toml` workspace |
| `anofox-fcst-core` | in-workspace | Core wrapper, imputation utilities | `crates/anofox-fcst-core/` |
| `anofox-fcst-ffi` | in-workspace | FFI boundary, `#[no_mangle]` exports | `crates/anofox-fcst-ffi/` |
| DuckDB C++ extension API | 1.4.3+ | Table function, macro, registration | submodule / cmake |
| `ParseFrequencyWithType` / date helpers | in-extension | Frequency string parsing, date arithmetic | `src/include/ts_fill_gaps_native.hpp` |
| `fill_nulls_interpolate` | in-workspace | Linear interpolation for intra-series nulls | `crates/anofox-fcst-core/src/imputation.rs:62-116` |

No new Cargo or npm dependencies. Stay on existing locked versions.

---

## Research Target 1: Upstream Global* API Surface

### GlobalETS
**File:** `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/exponential/global_ets.rs`

**Re-export path** [VERIFIED: `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/exponential/mod.rs:21`]:
```
pub use global_ets::{GlobalAutoETS, GlobalETS};
```
So the import path in Rust is `anofox_forecast::models::exponential::{GlobalETS, GlobalAutoETS, ETSSpec, ModelPool}`.

**Constructor** [VERIFIED: `global_ets.rs:64`]:
```rust
pub fn new(spec: ETSSpec, period: usize) -> Self
```
`ETSSpec` is `(ErrorType, TrendType, SeasonalType)`. Convenience constructor `ETSSpec::ann()` gives `ETS(A,N,N)` [VERIFIED: `ets.rs:74`].

**Auto-selecting variant** [VERIFIED: `global_ets.rs:616-624`]:
```rust
pub struct GlobalAutoETS { period: usize, pool: ModelPool, ... }
pub fn new(period: usize, pool: ModelPool) -> Self
```
`GlobalAutoETS` fits every candidate `ETSSpec` as a `GlobalETS`, then picks the best spec per series by per-series NLL. This is the recommended default — it avoids requiring the user to specify a spec. `ModelPool::Reduced` (8 candidates) is the recommended default for panel use: fastest, comparable accuracy [VERIFIED: `auto_ets.rs:63-67`]:
```
Reduced — 8 models: ANN, MNN, AAdN, MAdN, ANA, MNM, AAdA, MAdM.
Recommended for large-scale forecasting (fastest, comparable accuracy).
```

**Fit contract** [VERIFIED: `global_ets.rs:81-91`]:
```rust
pub fn fit(&mut self, all_series: &[Vec<f64>]) -> Result<()>
// Enforces: all_series[i].len() == all_series[0].len()
// Enforces: len > period + 2 (for seasonal specs)
// Returns: Err(ForecastError::InsufficientData) if any constraint violated
```
The code uses `all_series[0].len()` as the canonical length — it does NOT check that ALL series have the same length. However, the objective function iterates `all_series.iter().zip(states.iter())` which assumes correspondence, so unequal lengths cause silent wrong results or panics. **Must align before calling.**

**Predict** [VERIFIED: `global_ets.rs:202-212`]:
```rust
pub fn predict(&self, horizon: usize) -> Vec<Vec<f64>>
// Returns Vec<Vec<f64>> — outer index is series, inner index is horizon step
// Returns vec![] if not fitted
```
Output shape: `[n_series][horizon]`. Point forecasts only — no intervals.

**Minimum length for GlobalETS** [VERIFIED: `global_ets.rs:95-100`]:
```rust
if n <= start_idx + 2 {
    return Err(ForecastError::InsufficientData {
        needed: start_idx + 3, ...
    });
}
```
Where `start_idx = if spec.has_seasonal() { period } else { 0 }`. For `ANN` (non-seasonal, default), minimum length = 3. For seasonal spec with `period=7`, minimum length = 10. In practice, recommend dropping series shorter than `max(10, 2*period)` before panel fit.

**NaN in output:** `predict()` calls `forecast_from_state()` [VERIFIED: `global_ets.rs:484-529`]. The multiplicative seasonal path can produce `NaN` if `seasonals` is empty and `SeasonalType::Multiplicative` returns `1.0` as a fallback. For `ANN` spec, output is always finite given finite states.

### GlobalTheta
**File:** `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/theta/global_theta.rs`

**Re-export path** [VERIFIED: `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/theta/mod.rs:25`]:
```
pub use global_theta::GlobalTheta;
```
Import: `anofox_forecast::models::theta::GlobalTheta`.

**Constructor** [VERIFIED: `global_theta.rs:43-49`]:
```rust
pub fn new() -> Self          // theta=2.0 (Standard Theta Method)
pub fn with_theta(theta: f64) -> Self   // custom theta
```
No period parameter. No seasonal decomposition.

**Fit contract** [VERIFIED: `global_theta.rs:66-109`]:
```rust
pub fn fit(&mut self, all_series: &[Vec<f64>]) -> Result<()>
// Requires: all_series.len() >= 1
// Series with len < 2 are silently skipped in SSE computation
// Equal length NOT verified by the code, but OLS slope is per-series so unequal lengths are safe
```
Note: GlobalTheta does NOT enforce equal length in the optimizer — `total_sse` skips series with `len < 2`. However equal-length alignment is still required for the panel contract and for meaningful cross-series pooling.

**Predict** [VERIFIED: `global_theta.rs:113-129`]:
```rust
pub fn predict(&self, horizon: usize) -> Vec<Vec<f64>>
// Returns vec![] if not fitted
// Point forecasts: linear extrapolation with shared alpha, per-series level+slope
```
Output is always finite (no NaN risk) if states were computed from non-empty series.

**Minimum length:** Series with `len < 2` are silently skipped in SSE; states are still computed if `len >= 1` (level = `values[0]`, slope = 0). Recommend minimum 3 observations.

### GlobalCroston
**File:** `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/intermittent/global_croston.rs`

**Re-export path** [VERIFIED: `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/intermittent/mod.rs:21`]:
```
pub use global_croston::GlobalCroston;
```
Import: `anofox_forecast::models::intermittent::GlobalCroston`.

**Constructor** [VERIFIED: `global_croston.rs:44-66`]:
```rust
pub fn new() -> Self                        // Classic variant (no bias correction)
pub fn sba() -> Self                        // Syntetos-Boylan Approximation (multiply by 1 - α/2)
pub fn with_variant(variant: CrostonVariant) -> Self
// CrostonVariant enum: Classic | SBA
```
No period parameter. Croston operates on demand occurrences, not on calendar positions — `period` is irrelevant.

**Fit contract** [VERIFIED: `global_croston.rs:76-138`]:
```rust
pub fn fit(&mut self, all_series: &[Vec<f64>]) -> Result<()>
// Requires: at least one series with >= 2 demand occurrences (non-zero values)
// Series with < 2 demands get fallback state (0.0, 1.0)
// Equal length NOT enforced; each series extracts its own demand subsequence
```
IMPORTANT: `extract_demands` [VERIFIED: `global_croston.rs:200-215`] operates on the raw value array: any value `> 0.0` is a demand. Leading zeros are counted as inter-demand intervals. The equal-length alignment rule is for consistency, not a hard code constraint for Croston — but we still align to a shared date grid to ensure series cover the same time window.

**Predict** [VERIFIED: `global_croston.rs:142-153`]:
```rust
pub fn predict(&self, horizon: usize) -> Vec<Vec<f64>>
// Returns flat forecasts: vec![fc; horizon] per series
// fc = demand_level / interval_level with optional SBA correction
// demand_level / interval_level.max(0.001) avoids division by zero
```
Output is non-negative and finite. All horizon steps get the same value (Croston is a flat/constant forecast).

**All-zero series:** A series with zero demands yields fallback state `(0.0, 1.0)` and predicts `0.0` for all horizons. This is correct behavior for a zero-demand series. No NaN risk.

**Variant for params MAP:** Expose `variant` param key: `'Classic'` (default) or `'SBA'`. Parse via CrostonVariant.

### Re-export Summary

| Model | Full import path | Constructor | Fit signature | Predict return |
|-------|-----------------|-------------|---------------|----------------|
| `GlobalETS` | `anofox_forecast::models::exponential::GlobalETS` | `new(ETSSpec, usize)` | `fit(&[Vec<f64>]) -> Result<()>` | `Vec<Vec<f64>>` |
| `GlobalAutoETS` | `anofox_forecast::models::exponential::GlobalAutoETS` | `new(usize, ModelPool)` | `fit(&[Vec<f64>]) -> Result<()>` | `Vec<Vec<f64>>` |
| `GlobalTheta` | `anofox_forecast::models::theta::GlobalTheta` | `new()` | `fit(&[Vec<f64>]) -> Result<()>` | `Vec<Vec<f64>>` |
| `GlobalCroston` | `anofox_forecast::models::intermittent::GlobalCroston` | `new()` or `sba()` | `fit(&[Vec<f64>]) -> Result<()>` | `Vec<Vec<f64>>` |

All [VERIFIED: respective module `mod.rs` `pub use` lines].

---

## Research Target 2: Existing FFI Export Pattern

**Primary reference:** `crates/anofox-fcst-ffi/src/lib.rs` (6837 lines total)

### Canonical FFI signature style [VERIFIED: `crates/anofox-fcst-ffi/src/lib.rs:138-179`]
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
    // null-check ...
    let result = catch_unwind(AssertUnwindSafe(|| {
        let series = build_series(values, validity, length);
        anofox_fcst_core::compute_ts_stats(&series)
    }));
    match result {
        Ok(Ok(stats)) => { *out_result = stats.into(); true }
        Ok(Err(e)) => { set_error(out_error, ErrorCode::ComputationError, &e.to_string()); false }
        Err(_) => { set_error(out_error, ErrorCode::PanicCaught, "Panic in Rust code"); false }
    }
}
```

Key rules for the new panel FFI function:
1. `#[no_mangle] pub unsafe extern "C"` — mandatory
2. All pointer args; return `bool` (true = success)
3. `catch_unwind(AssertUnwindSafe(...))` wraps all Rust logic — **mandatory** for panic safety
4. `init_error(out_error)` at top; `set_error(...)` on failure
5. Use `core::ffi` types (`c_double`, `c_char`, `c_int`) — NOT `libc` types, for WASM compat [VERIFIED: `lib.rs:19-20`]

### Forecast FFI function [VERIFIED: `crates/anofox-fcst-ffi/src/lib.rs:3344-3427`]
```rust
pub unsafe extern "C" fn anofox_ts_forecast(
    values: *const c_double,     // single series data
    validity: *const u64,         // DuckDB bitmask (nullable)
    length: size_t,
    options: *const ForecastOptions,  // model name, horizon, period, etc.
    out_result: *mut ForecastResult,
    out_error: *mut AnofoxError,
) -> bool
```

### ForecastResult struct [VERIFIED: `crates/anofox-fcst-ffi/src/types.rs:328-351`]
```rust
pub struct ForecastResult {
    pub point_forecasts: *mut c_double,  // heap-alloc; caller frees via anofox_free_forecast_result
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
```

### New panel FFI signature — recommended design

The panel function differs from single-series: it receives N series and returns N×horizon forecasts. The cleanest approach (consistent with the existing pattern) is:

```rust
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_forecast_panel(
    // Flat packed matrix: series_0[0..len], series_1[0..len], ..., series_{n-1}[0..len]
    // All series have identical length (already aligned by C++)
    values: *const c_double,
    n_series: size_t,
    series_len: size_t,
    // Method: "GlobalETS" | "GlobalTheta" | "GlobalCroston"
    method: *const c_char,
    horizon: size_t,
    seasonal_period: size_t,     // 0 = use GlobalAutoETS default; ignored for Theta/Croston
    // params_json: optional JSON for ets_spec, model_pool, croston_variant
    params_json: *const c_char,  // null = defaults
    // Output: flat matrix, horizon values per series: out[series_i * horizon + h]
    out_forecasts: *mut *mut c_double,   // allocated by Rust; caller frees via anofox_free_double_array
    out_n_forecasts: *mut size_t,        // total count = n_series * horizon
    out_error: *mut AnofoxError,
) -> bool
```

Alternative for result packaging (simpler for C++ side): use a `PanelForecastResult` struct analogous to `ForecastResult`:
```rust
#[repr(C)]
pub struct PanelForecastResult {
    pub forecasts: *mut c_double,   // flat [n_series * horizon]; series-major order
    pub n_series: size_t,
    pub n_horizon: size_t,
    pub model_name: [c_char; 64],
}
```

**Free function** needed: `anofox_free_panel_forecast_result(result: *mut PanelForecastResult)`.

### build_series helper [VERIFIED: `crates/anofox-fcst-ffi/src/lib.rs:62-87`]
```rust
unsafe fn build_series(data: *const c_double, validity: *const u64, length: size_t) -> Vec<Option<f64>>
```
The panel FFI receives pre-imputed `Vec<f64>` (all dense, no nulls) because alignment + imputation happens in C++ before the FFI call. So validity masks are not needed in the panel FFI — the input is always fully valid.

### Error→code mapping [VERIFIED: `crates/anofox-fcst-ffi/src/types.rs:16-28`]
```rust
ErrorCode::Success = 0, NullPointer = 1, InvalidInput = 2, ComputationError = 3,
AllocationError = 4, InvalidModel = 5, InsufficientData = 6, InvalidDateFormat = 7,
InvalidFrequency = 8, PanicCaught = 9, InternalError = 10
```

### WASM constraint
The new FFI symbol must be referenced in `extension_config.cmake`'s `LINKED_LIBS` via the `-static` Corrosion target. Since `anofox_ts_forecast_panel` will live in the same `anofox-fcst-ffi` crate that is already in `LINKED_LIBS "$<TARGET_FILE:anofox_fcst_ffi-static>"` [VERIFIED: `extension_config.cmake:17`], no additional cmake change is needed — the symbol is automatically included in the existing archive. **No cmake change required for WASM.**

---

## Research Target 3: C++ Native Table Function Pattern

**Primary reference:** `src/table_functions/ts_forecast_native.cpp` (full file read)

### Struct layout [VERIFIED: `ts_forecast_native.cpp:33-116`]

```cpp
// Bind data — parsed at query-plan time, immutable during execution
struct TsForecastNativeBindData : public TableFunctionData {
    int64_t horizon = 7;
    int64_t frequency_seconds = 86400;
    FrequencyType frequency_type = FrequencyType::FIXED;
    string method = "AutoETS";
    int64_t seasonal_period = 0;
    // ... other params
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
    LogicalType date_logical_type;
    LogicalType group_logical_type;
};

// Per-group intermediate storage
struct ForecastGroupData {
    Value group_value;
    vector<int64_t> dates;    // microseconds
    vector<double> values;
    vector<bool> validity;
};

// Output row
struct ForecastOutputRow {
    string group_key;
    Value group_value;
    int64_t forecast_step;
    int64_t date;             // microseconds
    double point_forecast;
    double lower_90;
    double upper_90;
    string model_name;
};

// Local state — per-thread flags only (no data)
struct TsForecastNativeLocalState : public LocalTableFunctionState {
    bool owns_finalize = false;
    bool registered_collector = false;
    bool registered_finalizer = false;
};

// Global state — thread-safe collection + single-thread finalize
struct TsForecastNativeGlobalState : public GlobalTableFunctionState {
    idx_t MaxThreads() const override { return 999999; }
    std::mutex groups_mutex;
    std::map<string, ForecastGroupData> groups;
    vector<string> group_order;
    vector<ForecastOutputRow> results;
    bool processed = false;
    idx_t output_offset = 0;
    std::atomic<bool> finalize_claimed{false};
    std::atomic<idx_t> threads_collecting{0};
    std::atomic<idx_t> threads_done_collecting{0};
};
```

### InOut phase [VERIFIED: `ts_forecast_native.cpp:476-553`]
Rows are buffered into `gstate.groups` under mutex. Thread registers itself via `threads_collecting`. Output cardinality is always 0 during input phase — `OperatorResultType::NEED_MORE_INPUT`.

### Finalize phase [VERIFIED: `ts_forecast_native.cpp:559-584`]
```
1. Thread decrements collecting counter, registers as done
2. First thread to CAS `finalize_claimed` false→true owns finalize
3. Barrier: spin until threads_done_collecting == threads_collecting
4. Owner thread processes all groups; other threads return FINISHED immediately
```
The panel function uses the same barrier. The key difference: instead of looping over groups and calling `anofox_ts_forecast` per group, we call `anofox_ts_forecast_panel` once with all aligned series.

### Output schema [VERIFIED: `ts_forecast_native.cpp:426-452`]
```cpp
names: [group_col_name, "forecast_step", date_col_name, "yhat", "yhat_lower", "yhat_upper", "model_name"]
types: [group_logical_type, INTEGER, date_logical_type, DOUBLE, DOUBLE, DOUBLE, VARCHAR]
```
The panel function emits the same schema except `yhat_lower` / `yhat_upper` are always `NaN` (no intervals in v1). The macro `ts_forecast_panel_by` will SELECT only `{group_col}, {date_col}, yhat, model_name` unless the user requests the raw native output.

### Date arithmetic [VERIFIED: `ts_forecast_native.cpp:682-730`]
Calendar-aware date arithmetic for monthly/quarterly/yearly frequencies reuses `ParseFrequencyWithType` from `ts_fill_gaps_native.hpp`. This is fully reusable in the panel function.

### Bind inputs convention [VERIFIED: `ts_forecast_native.cpp:320-400`]
Input table has columns: `[group_col, date_col, value_col]`. After the table: `horizon, frequency, method, params`. The panel function has the same positional convention.

### What changes in `_ts_forecast_panel_native`

| Aspect | `_ts_forecast_native` | `_ts_forecast_panel_native` |
|--------|----------------------|----------------------------|
| Finalize FFI call | `anofox_ts_forecast()` per group in a loop | `anofox_ts_forecast_panel()` once, passing aligned matrix |
| FFI input | Single series `values[]` + validity bitmask | Flat `double[]` matrix `[n_series × series_len]` (pre-imputed) |
| Pre-call processing | Sort by date only | Sort + align to shared grid + linear interpolate + drop invalid |
| Output | `n_groups × horizon` rows | Same |
| Intervals | Filled from FFI result | `NaN` / 0.0 (deferred) |

---

## Research Target 4: Ragged→Dense Alignment

### Shared date grid construction

The panel function must build a union of all dates across the panel, then pad each series to that full grid on the declared `frequency`. The C++ layer already owns all the dates in `ForecastGroupData.dates` (vector of int64 microseconds). Algorithm:

```
1. Collect union of all dates: std::set<int64_t> all_dates.
2. For each group: fill all_dates with its dates vector.
3. Convert to sorted vector: shared_grid.
4. For each group: iterate shared_grid; if date present → use value; if absent → None (later imputed).
5. Result: each series is now a Vec<Option<f64>> of length shared_grid.size().
```

In practice the "union of dates on declared frequency" is equivalent to generating a regular date grid from `min_date` to `max_date` stepping by `frequency`, then checking which dates each series has. This avoids needing sparse lookup for large grids.

### Existing helpers reusable [VERIFIED]

**Frequency parsing** [VERIFIED: `src/include/ts_fill_gaps_native.hpp:21-28`]:
```cpp
struct ParsedFrequency {
    int64_t seconds;    // seconds per step (for fixed frequencies)
    bool is_raw;
    FrequencyType type; // FIXED | MONTHLY | QUARTERLY | YEARLY
};
ParsedFrequency ParseFrequencyWithType(const string &frequency_str);
```
Already `#include`d in `ts_forecast_native.cpp` (line 2: `#include "ts_fill_gaps_native.hpp"`).

**Date helpers** [VERIFIED: `src/include/ts_fill_gaps_native.hpp:29-33`]:
```cpp
int64_t DateToMicroseconds(date_t date);
int64_t TimestampToMicroseconds(timestamp_t ts);
date_t MicrosecondsToDate(int64_t micros);
timestamp_t MicrosecondsToTimestamp(int64_t micros);
```

**Interpolation** [VERIFIED: `crates/anofox-fcst-core/src/imputation.rs:62-116`]:
```rust
pub fn fill_nulls_interpolate(values: &[Option<f64>]) -> Vec<f64>
// Leading gaps filled with first observed value
// Trailing gaps filled with last observed value
// Interior gaps: linear interpolation
// All-null series returns all NaN
```
This is already exported from `anofox-fcst-core` lib.rs [VERIFIED: `crates/anofox-fcst-core/src/lib.rs:75-77`]:
```rust
pub use imputation::{
    fill_nulls_backward, fill_nulls_const, fill_nulls_forward, fill_nulls_interpolate,
    fill_nulls_mean,
```

**Strategy for leading-fill before first observation:** The standard `fill_nulls_interpolate` fills leading gaps with the first observed value. This is appropriate for GlobalETS and GlobalTheta. For GlobalCroston, leading zeros are valid (they're just pre-demand-start interval counts). Use `fill_nulls_const(series, 0.0)` as fallback for all-null Croston series before dropping.

### Imputation placement

The imputation happens in the Rust FFI function, not in C++:
- C++ collects aligned `Vec<Option<f64>>` for each series, passes them as a flat array (using `f64::NAN` for None)
- Rust FFI calls `fill_nulls_interpolate` on each series before building the panel matrix
- This keeps C++ free of Rust imputation logic and matches the existing pattern

Alternatively (simpler): impute in C++ before passing to Rust:
- C++ calls the Rust helper via a separate FFI for each series
- OR just reimplement linear interpolation in C++ (it's 30 lines)

**Recommended:** Impute inside the panel FFI, keeping C++ thin. Pass a flag or count of NaNs in the flat array; Rust iterates and imputes before `fit()`.

### Drop rule [Claude's discretion]

Minimum series length: `max(period + 3, 10)` for seasonal; `3` for non-seasonal GlobalETS. `3` for GlobalTheta. `1` observation with demand for GlobalCroston. Recommended conservative threshold for panel use: **10 observations** (universal). Series with all-null values after alignment are always dropped.

**Warning mechanism:** Since DuckDB table functions can only throw exceptions (not warnings), use `DuckDB::InvalidInputException` for hard errors and a `// WARNING` comment in the `model_name` column for dropped series. Better: emit a row with `yhat = NULL` and `model_name = 'DROPPED: too_short'`. This preserves the series in output while flagging it, letting the caller filter.

---

## Research Target 5: Macro Registration and Naming

### TsTableMacro structure [VERIFIED: `src/macros/ts_macros.cpp:12-20`]
```cpp
struct TsTableMacro {
    const char *name;
    const char *parameters[MAX_PARAMS];
    struct { const char *name; const char *default_value; } named_params[MAX_NAMED];
    const char *macro;       // SQL body (SELECT ... FROM ...)
    const char *description;
    const char *example;
    const char *category;
};
```

### Existing `ts_forecast_by` definition [VERIFIED: `src/macros/ts_macros.cpp:575-594`]
```cpp
{"ts_forecast_by",
 {"source", "group_col", "date_col", "target_col", "method", "horizon", "frequency", nullptr},
 {{"params", "MAP{}"}, {nullptr, nullptr}},
R"(
SELECT group_col, forecast_step, ds, yhat, yhat_lower, yhat_upper, model_name
FROM (
    SELECT group_col,
           unnest(_ts_forecast_scalar(...), recursive := true)
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
)
)", ...}
```

### New `ts_forecast_panel_by` macro

Add to the array immediately after `ts_forecast_by` (around line 595). The SQL body wraps `_ts_forecast_panel_native`:

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
"All series are fitted simultaneously with shared parameters. Returns one row per (group, horizon step).",
"SELECT * FROM ts_forecast_panel_by('sales', product_id, date, qty, 'GlobalETS', 12, '1d')",
"forecasting"}
```

Note: The `query_table(source::VARCHAR)` pattern is used by other table macros [VERIFIED: `ts_macros.cpp:588`]. The native function receives the table via DuckDB's table-in-out mechanism.

### Registration loop [VERIFIED: `src/macros/ts_macros.cpp:2290-2301`]
```cpp
void RegisterTsTableMacros(ExtensionLoader &loader) {
    for (idx_t i = 0; ts_table_macros[i].name != nullptr; i++) {
        auto info = CreateTableMacro(ts_table_macros[i]);
        loader.RegisterFunction(*info);
        // Also registers "anofox_fcst_" prefix alias
        auto alias_info = CreateTableMacro(ts_table_macros[i]);
        alias_info->name = "anofox_fcst_" + string(ts_table_macros[i].name);
        alias_info->alias_of = string(ts_table_macros[i].name);
        loader.RegisterFunction(*alias_info);
    }
}
```
The new macro is registered automatically by the loop — no additional registration code needed.

### Extension LoadInternal registration [VERIFIED: `src/anofox_forecast_extension.cpp:163-179`]
Add between the existing native function registrations:
```cpp
// Register Global / Panel forecast function (Phase 2: GLOB-01..03)
RegisterTsForecastPanelNativeFunction(loader);
```
Placed after line 168 (after `RegisterTsForecastNativeFunction`).

---

## Research Target 6: Benchmark Harness

### Existing M4 harness structure [VERIFIED: `benchmark/` directory listing]
```
benchmark/
├── configs/               # Model configs (ets.py, theta.py, etc.)
├── src/common/
│   ├── benchmark_runner.py   # create_benchmark_functions factory
│   ├── anofox_runner.py      # run_anofox_benchmark (calls ts_forecast_by)
│   ├── statsforecast_runner.py
│   └── evaluation.py
└── m4/
    ├── ets_benchmark/run.py
    └── ...
```

### How to add global model benchmark

**Step 1 — New config files:**
```
benchmark/configs/global_ets.py          # MODELS for anofox
benchmark/configs/statsforecast_global.py  # statsforecast reference
```

`global_ets.py`:
```python
BENCHMARK_NAME = 'global_ets'
MODELS = [
    {
        'name': 'GlobalETS',      # method string in ts_forecast_panel_by
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

**Step 2 — New benchmark dir:**
```
benchmark/m4/global_benchmark/
├── run.py          # uses create_benchmark_functions factory
└── results/        # parquet output goes here
```

**Step 3 — anofox_runner needs to call `ts_forecast_panel_by` instead of `ts_forecast_by`.**
The runner currently hardcodes `TS_FORECAST_BY` [VERIFIED: `benchmark/src/common/anofox_runner.py:135-148`]. Options:
- Pass a `function_name` parameter to `run_anofox_benchmark`
- Or create a parallel `run_anofox_panel_benchmark` variant

**Step 4 — statsforecast reference models:**
statsforecast provides `GlobalETS` (via `statsforecast.models.GlobalETS`), `Theta` (not exactly GlobalTheta but close). For GlobalCroston, statsforecast has `CrostonOptimized` / `ADIDA` as references.

**Python venv rule** [VERIFIED: `STATE.md:90`]:
> statsmodels/statsforecast cross-check scripts MUST run under `benchmark/.venv/bin/python` (or `cd benchmark && uv run python ...`), NOT system python3.

**Results format:** committed parquet files:
- `benchmark/m4/global_benchmark/results/anofox-global_ets-Daily.parquet`
- `benchmark/m4/global_benchmark/results/anofox-global_ets-Daily-metrics.parquet`
- `benchmark/m4/global_benchmark/results/statsforecast-GlobalETS-Daily.parquet`

---

## Research Target 7: Docs Layout

### Per-model doc template [VERIFIED: `docs/reference/models/theta/auto_theta.md`]
```markdown
# ModelName
> One-line description

## Signature
```sql
-- Single series (ts_forecast_by for per-series; ts_forecast_panel_by for panel)
```

## Description
## Parameters  (table: Parameter | Type | Required | Default | Description)
## Returns     (table: Column | Type | Description)
## SQL Example
## Best For
```

**New files to create:**
- `docs/reference/models/exponential/global_ets.md`
- `docs/reference/models/exponential/global_auto_ets.md`
- `docs/reference/models/theta/global_theta.md`
- `docs/reference/models/intermittent/global_croston.md`
- `docs/api/07-forecasting.md` — add panel section (currently exists, covers `ts_forecast_by`)

### Example SQL template [VERIFIED: `examples/forecasting/synthetic_forecasting_examples.sql`]
New file: `examples/forecasting/global_panel_forecasting_examples.sql`
- Section 1: Create synthetic multi-series panel with ragged lengths
- Section 2: `ts_forecast_panel_by` with GlobalETS
- Section 3: GlobalTheta
- Section 4: GlobalCroston (sparse/intermittent panel)
- Section 5: Mixed method comparison

Run command in header: `./build/release/duckdb < examples/forecasting/global_panel_forecasting_examples.sql`

---

## Architecture Patterns

### System Architecture Diagram

```
SQL user
  │  ts_forecast_panel_by(source, group_col, date_col, target_col, method, horizon, freq, params)
  ▼
ts_macros.cpp — TsTableMacro entry
  │  expands to: SELECT ... FROM _ts_forecast_panel_native(query_table(source), ...)
  ▼
_ts_forecast_panel_native (new C++ table function)
  │  InOut: collect all rows into groups (same as ts_forecast_native)
  │  Finalize (single thread):
  │    1. Build shared date grid (union of all dates, freq-stepped)
  │    2. Align each series to grid → Vec<Option<f64>> per series
  │    3. Drop too-short / all-null series (record as DROPPED in output)
  │    4. Build flat f64 matrix (imputation happens in Rust FFI)
  │    5. Call anofox_ts_forecast_panel(matrix, n_series, len, method, horizon, period, params)
  │    6. Emit long-format output rows
  ▼
anofox_ts_forecast_panel (new Rust FFI export in anofox-fcst-ffi/src/lib.rs)
  │  catch_unwind wrapper
  │  for each series: fill_nulls_interpolate → Vec<f64>
  │  match method:
  │    "GlobalETS"    → GlobalAutoETS::new(period, ModelPool::Reduced).fit(&panel)
  │    "GlobalTheta"  → GlobalTheta::new().fit(&panel)
  │    "GlobalCroston"→ GlobalCroston::new() or sba().fit(&panel)
  │  model.predict(horizon) → Vec<Vec<f64>>
  │  alloc flat output buffer; copy results
  ▼
PanelForecastResult { forecasts: *mut f64, n_series, n_horizon, model_name }
  │  freed by anofox_free_panel_forecast_result
  ▼
C++ Finalize: emit rows from result buffer
  ▼
DuckDB result set
```

### Recommended Project Structure

New files:
```
src/table_functions/
└── ts_forecast_panel_native.cpp     # new — mirrors ts_forecast_native.cpp structure
src/include/
└── ts_forecast_panel_native.hpp     # new — forward declares RegisterTsForecastPanelNativeFunction
crates/anofox-fcst-ffi/src/lib.rs   # append anofox_ts_forecast_panel + free function
crates/anofox-fcst-ffi/src/types.rs # append PanelForecastResult struct
benchmark/configs/
└── global_ets.py                    # new anofox model config
└── statsforecast_global.py          # new statsforecast reference
benchmark/m4/global_benchmark/
├── run.py                           # new benchmark entry point
└── results/                         # committed parquet output
docs/reference/models/exponential/
└── global_ets.md                    # new
docs/reference/models/theta/
└── global_theta.md                  # new
docs/reference/models/intermittent/
└── global_croston.md                # new
examples/forecasting/
└── global_panel_forecasting_examples.sql  # new, verified against built extension
```

CMakeLists.txt: add `src/table_functions/ts_forecast_panel_native.cpp` to the source list.

### Pattern 1: Flat Matrix Panel FFI

**What:** Pass N series as a flat `double[n_series * series_len]` matrix (row-major, series-first).
**When to use:** When all series are equal length (post-alignment). Avoids pointer-of-pointers complexity.

```rust
// Source: anofox-fcst-ffi/src/lib.rs (new)
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_forecast_panel(
    values: *const c_double,   // flat [n_series * series_len], row-major
    n_series: size_t,
    series_len: size_t,
    method: *const c_char,     // "GlobalETS" | "GlobalTheta" | "GlobalCroston"
    horizon: size_t,
    seasonal_period: size_t,   // 0 = use Reduced pool default for GlobalAutoETS
    variant: *const c_char,    // for Croston: "Classic" | "SBA"; others: ignored
    out_result: *mut PanelForecastResult,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);
    // null checks ...
    let result = catch_unwind(AssertUnwindSafe(|| {
        let flat = std::slice::from_raw_parts(values, n_series * series_len);
        // Build Vec<Vec<f64>> by chunking and imputing
        let panel: Vec<Vec<f64>> = (0..n_series)
            .map(|i| {
                let raw: Vec<Option<f64>> = flat[i*series_len..(i+1)*series_len]
                    .iter().map(|&v| if v.is_nan() { None } else { Some(v) })
                    .collect();
                anofox_fcst_core::fill_nulls_interpolate(&raw)
            })
            .collect();
        // dispatch ...
    }));
    // ...
}
```

### Pattern 2: Collect-all-in-Finalize (existing pattern, no change)

The panel table function uses **the same barrier pattern** as `ts_forecast_native.cpp` [VERIFIED: `ts_forecast_native.cpp:559-584`]. No change to the collection phase. The difference is solely in Finalize where one FFI call replaces N per-group calls.

### Anti-Patterns to Avoid

- **Calling `GlobalETS::fit` per group in a loop:** Defeats the cross-learning purpose. Use a single `fit(&panel)` call.
- **Passing ragged (unequal-length) arrays to fit:** Causes `zip` misalignment in the optimizer. Always align first.
- **Using `batch::auto_ets` instead of `GlobalAutoETS`:** `batch::auto_ets` [VERIFIED: `batch.rs:51-80`] fits N independent models in parallel — correct per-series dispatch, not shared-parameter cross-learning. These are different concepts.
- **Not calling `anofox_free_panel_forecast_result` in C++:** Memory leak on the heap-allocated forecasts buffer.
- **Propagating GlobalETS `fit` failure as a full-query failure:** If the panel has `< 3` series after alignment, fall back to an error. Otherwise log dropped series via model_name column.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Linear interpolation for null imputation | Custom C++ interpolation loop | `anofox_fcst_core::fill_nulls_interpolate` | Already in crate, battle-tested, handles leading/trailing/interior gaps correctly |
| Frequency string parsing | Custom parser | `ParseFrequencyWithType()` from `ts_fill_gaps_native.hpp` | Handles all DuckDB + Polars-style formats; already handles calendar (monthly/quarterly/yearly) |
| Date arithmetic (calendar-aware) | Custom date math | Existing calendar date logic from `ts_forecast_native.cpp:682-730` | Monthly/quarterly/yearly require year-rollover logic that's already correct in the codebase |
| Global ETS model | Custom ETS implementation | `GlobalAutoETS` from `anofox_forecast::models::exponential` | Correct Nelder-Mead optimization, handles all 8+ ETS specs |
| Flat matrix allocation in Rust FFI | `Vec<*mut f64>` or nested structs | Flat `*mut c_double` buffer + `anofox_free_double_array` | Same pattern as `ForecastResult::point_forecasts`; consistent with existing free functions [VERIFIED: `lib.rs:5900`] |
| Statsforecast benchmark runner | New Python harness | `create_benchmark_functions` factory [VERIFIED: `benchmark/src/common/benchmark_runner.py`] | Already handles M4 data loading, timing, parquet output, evaluation |

---

## Common Pitfalls

### Pitfall 1: Equal-length contract violation
**What goes wrong:** `GlobalETS::fit` uses `all_series[0].len()` as the canonical length and iterates `zip(states)` which assumes correspondence. Passing series of different lengths silently produces wrong optimizer gradients and wrong final states.
**Why it happens:** C++ collects groups independently; series arrive at different rates and may have gaps at different positions.
**How to avoid:** Build the shared date grid in Finalize before any FFI call. Verify `all aligned_series[i].len() == series_len` with an assertion before passing to Rust.
**Warning signs:** Optimizer converges to edge values (alpha=0.9999 or 0.0001) — symptom of objective function getting misaligned data.

### Pitfall 2: GlobalCroston with all-zero panel
**What goes wrong:** `GlobalCroston::fit` returns `Err(ConvergenceFailure)` if NO series has >= 2 demand occurrences. With an intermittent panel where all demand events fall outside the aligned window, the whole fit fails.
**Why it happens:** Panel alignment and leading-fill with zeros can push demand events outside the common grid window.
**How to avoid:** Before calling Croston, count demand occurrences per series; if all series have < 2 demands, emit a clear error. The Rust error is `ForecastError::ConvergenceFailure("No series with at least 2 demand occurrences")` [VERIFIED: `global_croston.rs:98-103`].
**Warning signs:** Empty forecast output for an intermittent panel.

### Pitfall 3: GlobalETS NaN output for multiplicative seasonal spec
**What goes wrong:** If `GlobalAutoETS` selects a multiplicative seasonal spec (e.g., `MNM`) for a panel containing zero or negative values, `forecast_from_state` can return `NaN` because the seasonal fallback for multiplicative returns `1.0` when `seasonals` is empty.
**Why it happens:** `GlobalAutoETS::generate_candidates` guards against non-positive values [VERIFIED: `global_ets.rs:641-643`]:
```rust
let has_non_positive = all_series.iter().any(|s| s.iter().any(|&v| v <= 0.0));
```
But after imputation, interpolated values could be negative (e.g., if leading values are negative). The guard is applied at fit time, so this should be safe — but verify after imputation.
**How to avoid:** After `fill_nulls_interpolate`, scan for `<= 0` values; if present, do NOT use multiplicative specs. Pass a `model_pool` hint that excludes multiplicative error (`ModelPool::DampedTrendOnly` or `Reduced`). For safety, clamp predictions to 0.0 for Croston (already guaranteed by `demand_level/interval_level.max(0.001)`).

### Pitfall 4: Large ModelPool cost on large panels
**What goes wrong:** `GlobalAutoETS` with `ModelPool::Complete` (19 candidates) × N series = 19 NM optimizations each evaluating across all series. On a 1000-series panel with period=7 and 200 observations each, `Complete` can take minutes.
**Why it happens:** Each candidate spec requires a full NM optimization of 1-4 params with `max_iter=500`.
**How to avoid:** Default to `ModelPool::Reduced` (8 candidates) for `GlobalETS` method. Let users override via `params := MAP{'model_pool': 'Complete'}` if needed. Document the tradeoff.

### Pitfall 5: GlobalTheta on panels with constant series
**What goes wrong:** OLS slope `ols_slope` returns 0.0 for constant series [VERIFIED: `global_theta.rs:151-174`]. This is correct behavior. But a fully-constant panel means the optimizer trivially sets alpha to any value (SSE = 0 for any alpha with constant series). The result is a valid constant forecast.
**Why it happens:** Degenerate panel data.
**How to avoid:** No special handling needed — GlobalTheta handles this gracefully. Document as a known edge case.

### Pitfall 6: WASM LINKED_LIBS — no action needed (but verify)
**What goes wrong:** New FFI symbols silently dropped on WASM builds if not in LINKED_LIBS.
**Why it happens:** Emscripten post-build step only links archives listed in `DUCKDB_EXTENSION_*_LINKED_LIBS`.
**How to avoid:** The new `anofox_ts_forecast_panel` symbol is in `anofox-fcst-ffi` crate, which is already covered by `LINKED_LIBS "$<TARGET_FILE:anofox_fcst_ffi-static>"` [VERIFIED: `extension_config.cmake:17`]. No cmake change needed — but a WASM test build is recommended as a verification step.

### Pitfall 7: Benchmark uses system python3 instead of venv
**What goes wrong:** `statsforecast` and `pandas` versions differ; benchmark produces wrong or no results.
**Why it happens:** System python3 doesn't have the benchmark venv packages installed [VERIFIED: `STATE.md:90`].
**How to avoid:** All benchmark / evaluation scripts must use `benchmark/.venv/bin/python` or `cd benchmark && uv run python ...`. Add this rule to the benchmark `run.py` header comment.

---

## Code Examples

### GlobalETS panel fit — Rust (new FFI body)

```rust
// In crates/anofox-fcst-ffi/src/lib.rs (new function)
use anofox_forecast::models::exponential::{GlobalAutoETS, ModelPool};
use anofox_forecast::models::theta::GlobalTheta;
use anofox_forecast::models::intermittent::{GlobalCroston, CrostonVariant};
use anofox_fcst_core::fill_nulls_interpolate;

let panel: Vec<Vec<f64>> = (0..n_series)
    .map(|i| {
        let slice = &flat_values[i * series_len..(i + 1) * series_len];
        let with_opts: Vec<Option<f64>> = slice.iter()
            .map(|&v| if v.is_nan() { None } else { Some(v) })
            .collect();
        fill_nulls_interpolate(&with_opts)
    })
    .collect();

match method_str {
    "GlobalETS" => {
        let pool = parse_model_pool(model_pool_str); // defaults to Reduced
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
        let variant = if sba { CrostonVariant::SBA } else { CrostonVariant::Classic };
        let mut model = GlobalCroston::with_variant(variant);
        model.fit(&panel)?;
        Ok(model.predict(horizon))
    }
    other => Err(ForecastError::InvalidModel(format!("Unknown panel method: {}", other)))
}
```

### ts_forecast_panel_by SQL call

```sql
-- GlobalETS panel (auto spec selection, Reduced pool, period=7)
SELECT * FROM ts_forecast_panel_by(
    'daily_sales',
    product_id,
    ds,
    y,
    'GlobalETS',
    14,
    '1d',
    MAP{'seasonal_period': '7'}
);

-- GlobalTheta panel (no period needed)
SELECT * FROM ts_forecast_panel_by(
    'daily_sales', product_id, ds, y,
    'GlobalTheta', 14, '1d'
);

-- GlobalCroston with SBA variant
SELECT * FROM ts_forecast_panel_by(
    'spare_parts', item_id, date, qty,
    'GlobalCroston', 6, '1mo',
    MAP{'croston_variant': 'SBA'}
);
```

### Shared date grid alignment (C++ sketch)

```cpp
// In TsForecastPanelNativeFinalize:

// 1. Build shared date grid
std::set<int64_t> date_union;
for (const auto &key : gstate.group_order) {
    for (int64_t d : gstate.groups[key].dates) {
        date_union.insert(d);
    }
}
std::vector<int64_t> shared_grid(date_union.begin(), date_union.end());
std::sort(shared_grid.begin(), shared_grid.end());
size_t grid_len = shared_grid.size();

// 2. Align each series and build flat matrix (NaN for missing positions)
std::vector<double> flat_matrix(n_valid_series * grid_len, std::numeric_limits<double>::quiet_NaN());
size_t series_idx = 0;
for (const auto &key : valid_group_order) {
    auto &grp = gstate.groups[key];
    std::map<int64_t, double> date_to_value;
    for (size_t i = 0; i < grp.dates.size(); i++) {
        if (grp.validity[i]) {
            date_to_value[grp.dates[i]] = grp.values[i];
        }
    }
    double *series_row = flat_matrix.data() + series_idx * grid_len;
    for (size_t j = 0; j < grid_len; j++) {
        auto it = date_to_value.find(shared_grid[j]);
        series_row[j] = (it != date_to_value.end()) ? it->second
                        : std::numeric_limits<double>::quiet_NaN();
    }
    series_idx++;
}
// 3. Call FFI with flat_matrix
```

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Per-series independent ETS (N fits) | GlobalETS shared parameters (1 fit, N predictions) | anofox-forecast 0.11+ | 10-100x faster on large panels; slightly different accuracy (pooled params) |
| SQL GROUP BY per-series dispatch | Single FFI call with panel matrix | This phase | Eliminates per-group FFI overhead for panel workloads |
| Ragged series alignment outside DuckDB | In-Finalize alignment inside table function | This phase | No pre-processing SQL needed |

---

## Package Legitimacy Audit

No new external packages are introduced in this phase. All dependencies are locked versions already in `Cargo.toml` and the DuckDB submodule. No package legitimacy gate needed.

---

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `GlobalAutoETS::generate_candidates` guards against multiplicative specs when panel contains non-positive values | Pitfall 3 | If the guard is bypassed by imputed values, predictions could contain NaN. Mitigation: check output for NaN before emitting. |
| A2 | The optimal `params_json` / string-based param passing for the panel FFI variant/pool selection is via C strings | Target 2 | Could switch to a struct; either works but consistency with existing `ForecastOptions` pattern favors struct. Discretionary choice. |
| A3 | statsforecast provides `GlobalETS` as a comparable reference for GLOB-01 parity benchmark | Target 6 | If statsforecast GlobalETS uses a different pooling approach, MASE parity might not be achievable. Use behavioral tolerance criterion (within 5% MASE). |
| A4 | Minimum series length threshold of 10 observations is appropriate for a panel of daily data | Target 4 | Could be too conservative (drops short but valid series) or too lenient (allows very short series to bias the global fit). Start with 10, make it configurable via params. |

**If this table is empty for a claim:** All other claims in this research were verified against source files opened this session.

---

## Open Questions

1. **PanelForecastResult struct vs flat output params**
   - What we know: existing `ForecastResult` uses a struct; `anofox_free_double_array` exists for plain arrays
   - What's unclear: Whether to add a new `PanelForecastResult` type to `types.rs` (cleaner) or use flat out-params (simpler)
   - Recommendation: Add `PanelForecastResult` to `types.rs` for consistency; add matching `anofox_free_panel_forecast_result` free function

2. **Croston variant param key name**
   - What we know: `CrostonVariant::Classic` and `CrostonVariant::SBA` exist [VERIFIED: `global_croston.rs:25-31`]
   - What's unclear: Whether to use `croston_variant` or `variant` as the params MAP key
   - Recommendation: Use `croston_variant` to avoid collision with other params

3. **Table-in-out vs full table collection for the panel native function**
   - What we know: `ts_forecast_native` uses InOut (push rows, collect in Execute); the panel needs ALL data before fitting
   - What's unclear: Can the InOut pattern collect all data before Finalize in every DuckDB execution mode?
   - Recommendation: Yes — the existing `ts_forecast_native` already does exactly this (collect in InOut / Execute, process in Finalize). Mirror the pattern exactly.

---

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Rust 1.86+ | FFI crate build | ✓ | From Cargo.toml constraint | — |
| CMake 3.20+ | C++ build | ✓ | Project already builds | — |
| `benchmark/.venv` | Python benchmark | Likely present (Phase 1 used it) | `uv` managed | `cd benchmark && uv sync` |
| statsforecast | Python benchmark | In venv (Phase 1 confirmed) | Via pyproject.toml | `cd benchmark && uv sync` |

---

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | DuckDB SQL tests (.test files) + Python benchmark scripts |
| Config file | `CMakeLists.txt` LOAD_TESTS; `benchmark/pyproject.toml` |
| Quick run command | `make rust_test` (Rust unit tests) |
| SQL test run | `./build/release/duckdb < examples/forecasting/global_panel_forecasting_examples.sql` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| GLOB-01 | `ts_forecast_panel_by` with GlobalETS returns N*horizon rows | Integration | `./build/release/duckdb < examples/forecasting/global_panel_forecasting_examples.sql` | ❌ Wave 0 |
| GLOB-02 | GlobalTheta panel forecast returns correct row count | Integration | Same SQL example file | ❌ Wave 0 |
| GLOB-03 | GlobalCroston panel forecast on intermittent data | Integration | Same SQL example file | ❌ Wave 0 |
| GLOB-01..03 | MASE within 5% of statsforecast reference | Benchmark | `cd benchmark && uv run python m4/global_benchmark/run.py run` | ❌ Wave 0 |

### Wave 0 Gaps
- [ ] `examples/forecasting/global_panel_forecasting_examples.sql` — covers GLOB-01, GLOB-02, GLOB-03
- [ ] `benchmark/m4/global_benchmark/run.py` — covers parity test
- [ ] `benchmark/configs/global_ets.py` — anofox config
- [ ] `benchmark/configs/statsforecast_global.py` — statsforecast config

---

## Security Domain

This phase adds no authentication, session management, or cryptographic operations. Input validation is handled at the FFI boundary via existing `check_null_pointers` and `catch_unwind` patterns. V5 (input validation) is the only applicable ASVS category and is covered by the existing FFI validation pattern.

---

## Sources

### Primary (HIGH confidence)
- `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/exponential/global_ets.rs` — GlobalETS and GlobalAutoETS API, fit contract, predict return shape
- `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/theta/global_theta.rs` — GlobalTheta API
- `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/intermittent/global_croston.rs` — GlobalCroston API, CrostonVariant enum
- `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/exponential/mod.rs` — re-export paths for GlobalETS/GlobalAutoETS
- `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/theta/mod.rs` — re-export for GlobalTheta
- `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/intermittent/mod.rs` — re-export for GlobalCroston
- `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/exponential/auto_ets.rs` — ModelPool enum values
- `crates/anofox-fcst-ffi/src/lib.rs` — FFI pattern, signature style, catch_unwind, alloc helpers
- `crates/anofox-fcst-ffi/src/types.rs` — ForecastOptions, ForecastResult, ErrorCode, FrequencyType
- `crates/anofox-fcst-core/src/imputation.rs` — fill_nulls_interpolate implementation
- `crates/anofox-fcst-core/src/lib.rs` — imputation re-exports
- `src/table_functions/ts_forecast_native.cpp` — collect/finalize pattern, output schema, date arithmetic
- `src/macros/ts_macros.cpp` — TsTableMacro struct, ts_forecast_by definition, registration loop
- `src/anofox_forecast_extension.cpp` — LoadInternal registration calls
- `src/include/ts_fill_gaps_native.hpp` — ParsedFrequency, ParseFrequencyWithType, date helpers
- `extension_config.cmake` — LINKED_LIBS for WASM
- `benchmark/src/common/benchmark_runner.py` — create_benchmark_functions factory
- `benchmark/src/common/anofox_runner.py` — ts_forecast_by call pattern, parquet output convention

### Secondary (MEDIUM confidence)
- `benchmark/configs/ets.py` / `statsforecast_ets.py` — config pattern to replicate for global models
- `docs/reference/models/theta/auto_theta.md` — model doc template
- `examples/forecasting/synthetic_forecasting_examples.sql` — SQL example file pattern

---

## Metadata

**Confidence breakdown:**
- Upstream API (Global* constructors, fit, predict): HIGH — read source files directly this session
- FFI pattern: HIGH — read `lib.rs` and `types.rs` directly
- C++ table function pattern: HIGH — read `ts_forecast_native.cpp` directly
- Benchmark harness: HIGH — read Python source files directly
- Docs/examples pattern: HIGH — read existing files directly
- Alignment algorithm details (C++ implementation): MEDIUM — designed from first principles matching existing patterns; will need verification during implementation

**Research date:** 2026-08-21
**Valid until:** 2026-09-20 (stable — locked crate version 0.15.3, no upstream changes expected)
