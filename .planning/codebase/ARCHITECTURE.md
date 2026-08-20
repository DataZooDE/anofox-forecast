<!-- refreshed: 2026-08-20 -->
# Architecture

**Analysis Date:** 2026-08-20

## System Overview

```text
┌─────────────────────────────────────────────────────────────────┐
│                     DuckDB SQL API Layer                         │
│  (Native table macros, scalar functions, aggregate functions)    │
│            `src/macros`, `src/scalar_functions`                  │
└──────────────────────────┬──────────────────────────────────────┘
                           │
┌──────────────────────────┴──────────────────────────────────────┐
│                   C++ Extension Bindings                         │
│           (DuckDB function registration & dispatch)              │
│  `src/anofox_forecast_extension.cpp`                             │
│  `src/table_functions/ts_*.cpp` (43 table functions)             │
│  `src/aggregate_functions/ts_*_agg.cpp` (8 aggregates)           │
└──────────────────────────┬──────────────────────────────────────┘
                           │
┌──────────────────────────┴──────────────────────────────────────┐
│                    Rust FFI Boundary                             │
│  (Type marshalling, memory management, panic handling)           │
│  `crates/anofox-fcst-ffi/src/lib.rs`                             │
└──────────────────────────┬──────────────────────────────────────┘
                           │
┌──────────────────────────┴──────────────────────────────────────┐
│              Native Rust Implementation Core                     │
│  (Forecasting models, statistics, feature extraction)            │
│  `crates/anofox-fcst-core/src/lib.rs`                            │
│  anofox-forecast, anofox-regression, fdars-core crates           │
└──────────────────────────┬──────────────────────────────────────┘
                           │
┌──────────────────────────┴──────────────────────────────────────┐
│               External Dependencies                              │
│  anofox-forecast 0.15.3 - Core forecasting library               │
│  anofox-regression 0.5.3 - Regression & feature extraction       │
│  fdars-core 0.3 - Functional data analysis (with WASM support)   │
└─────────────────────────────────────────────────────────────────┘
```

## Component Responsibilities

| Component | Responsibility | File |
|-----------|----------------|------|
| **SQL Macros** | High-level SQL templates for common workflows (ts_stats, ts_forecast_by, ts_cv_folds_by) | `src/macros/ts_macros.cpp` |
| **Table Functions** | DuckDB table-returning functions for data prep, forecasting, evaluation | `src/table_functions/*.cpp` (43 functions) |
| **Aggregate Functions** | DuckDB aggregate-returning functions for grouped statistics | `src/aggregate_functions/*.cpp` (8 functions) |
| **Scalar Functions** | DuckDB scalar functions for metrics, conformal, bootstrap | `src/scalar_functions/*.cpp` (5 functions) |
| **Extension Entry** | DuckDB extension loader, registration, telemetry | `src/anofox_forecast_extension.cpp` |
| **Rust FFI Boundary** | C-compatible interface, error handling, memory allocation | `crates/anofox-fcst-ffi/src/lib.rs` |
| **Rust Core** | Forecasting models (33), feature extraction (117), statistics | `crates/anofox-fcst-core/src/lib.rs` |

## Pattern Overview

**Overall:** Multi-layer extension architecture with columnar streaming and parallel execution via DuckDB's native GROUP BY support.

**Key Characteristics:**
- **SQL-native API** - Zero-setup macros automatically loaded; all functions exposed as pure SQL
- **Streaming parallel** - Native DuckDB GROUP BY + scalar functions for in-memory parallelism; no custom threading
- **Memory efficient** - Columnar storage with ListVector; O(group_size) not O(total_rows) for group-based operations
- **Rust performance** - Hot path (forecasting, feature extraction) in Rust with FFI boundary to C++
- **Layered design** - SQL macros wrap table functions; table functions dispatch to Rust via FFI; FFI marshals types

## Layers

**SQL Macro Layer:**
- Purpose: High-level, user-friendly SQL templates for common workflows
- Location: `src/macros/ts_macros.cpp`
- Contains: 20+ named parameters macros for forecasting, CV, data prep
- Depends on: Table functions (_ts_forecast_native, _ts_cv_folds_by, _ts_fill_gaps_native, etc.) and scalar functions
- Used by: Direct SQL calls from users; examples in `examples/` directory

**Table Function Layer:**
- Purpose: Implement forecasting, data prep, gap filling, feature extraction, metrics, cross-validation
- Location: `src/table_functions/` (43 files)
- Contains: _ts_forecast_native, _ts_fill_gaps_native, _ts_cv_folds_by, _ts_features_native, _ts_metrics_native, etc.
- Depends on: Rust FFI functions via anofox_fcst_ffi.h; DuckDB vectorized API
- Used by: SQL macros; called directly from SQL or R/Python bindings

**Aggregate Function Layer:**
- Purpose: Compute grouped statistics on time series (statistics, features, forecasts per group)
- Location: `src/aggregate_functions/` (8 files)
- Contains: ts_stats_agg, ts_features_agg, ts_forecast_agg, ts_changepoints_agg, etc.
- Depends on: Rust FFI; DuckDB aggregate function API
- Used by: SQL queries for GROUP BY operations; used internally by some table functions

**Scalar Function Layer:**
- Purpose: Compute point values (metrics, conformal quantiles, bootstrap) per row
- Location: `src/scalar_functions/` (5 files)
- Contains: ts_forecast_scalar (single-series wrapper), ts_forecast_inspect_scalar (model inspection), metrics, conformal, bootstrap
- Depends on: Rust FFI
- Used by: SQL for per-row operations; windowing functions

**DuckDB Extension Entry Layer:**
- Purpose: DuckDB extension lifecycle (load, register, telemetry)
- Location: `src/anofox_forecast_extension.cpp`
- Contains: LoadInternal() function that registers all 150+ functions; telemetry hooks
- Depends on: All function registration functions
- Used by: DuckDB core on LOAD anofox_forecast

**Rust FFI Boundary:**
- Purpose: Type marshalling, error handling, memory management between C++ and Rust
- Location: `crates/anofox-fcst-ffi/src/lib.rs`
- Contains: C-compatible function signatures; validation; allocation/deallocation; panic catching
- Depends on: anofox-fcst-core (via path dependency)
- Used by: All C++ table/scalar/aggregate functions

**Rust Core Implementation:**
- Purpose: Forecasting models (33), feature extraction (117), changepoint detection, statistics
- Location: `crates/anofox-fcst-core/src/lib.rs`
- Contains: Wrapper around anofox-forecast, anofox-regression, fdars-core crates
- Depends on: External crates (anofox-forecast 0.15.3, anofox-regression 0.5.3, fdars-core 0.3)
- Used by: FFI boundary layer

## Data Flow

### Primary Request Path: Forecasting

1. User calls SQL macro `ts_forecast_by('sales', product_id, ds, y, 'AutoETS', 28)` 
   - Expands to internal function `_ts_forecast_native()`
2. `_ts_forecast_native` table function called (`src/table_functions/ts_forecast_native.cpp`)
   - **Bind phase** (line 200-250): Parse parameters, extract model name, seasonal period, confidence
   - **Init phase** (line 300-350): Create global state (thread-safe groups_mutex) and local state
   - **Execute phase** (line 400+): 
     - Row-by-row collection into in-memory groups (one per unique product_id value)
     - Collect dates (microseconds), values (doubles), validity bitmap
   - **Finalize phase** (line 600+): One thread claims finalize via atomic; processes all groups sequentially
     - For each group: call Rust FFI function `forecast_one_series()` via `anofox_fcst_ffi.h`
     - Rust forecasting model selected by method name; produces point/lower/upper/fitted/residuals
     - Output rows materialized and returned
3. Rust FFI layer (`crates/anofox-fcst-ffi/src/lib.rs`):
   - `forecast_one_series()` validates inputs (null pointers, length > 0)
   - Unmarshals DuckDB vectors (raw_data + validity bitmap) to Rust Vec<Option<f64>>
   - Calls `anofox_fcst_core::forecast()` 
4. Rust core (`crates/anofox-fcst-core/src/lib.rs`):
   - Dispatches to anofox-forecast crate based on model name
   - Fits model on historical data; generates forecast for horizon steps
   - Returns point estimates, confidence intervals, residuals, AIC/BIC

### Secondary Flow: Feature Extraction

1. User calls `ts_features_native()` table function
   - Parameters: table name, group_col, date_col, value_col, feature_list (JSON/CSV/template name)
   - Bind phase: Parse feature config (defaults to tsfresh-compatible 117 features)
2. Execute/Finalize: Collect groups; for each group call Rust FFI `extract_features()`
3. Rust FFI: Unmarshals data; calls anofox-regression crate
4. Output: One row per group with 117 columns (one per feature)

### Cross-Validation Flow (ts_cv_folds_by)

1. User calls `ts_cv_folds_by()` macro → calls `_ts_cv_folds_by()` native table function
   - Parameters: table, group_col, date_col, value_col, horizon, window type, gap, embargo
2. Table function collects groups; for each group:
   - Calls Rust FFI `create_cv_folds()` which produces fold boundaries
   - Returns: GROUP BY'd rows with train_date_range, test_date_range, fold_number
3. User chains with `_ts_cv_forecast_by()` to forecast each fold
4. Cross-validation metrics computed by scalar function `ts_mse()`, `ts_mae()`, etc.

### State Management

- **Global State** (`TsForecastNativeGlobalState`): Thread-safe map of group_key → ForecastGroupData; atomic finalize barrier
- **Local State** (`TsForecastNativeLocalState`): Per-thread flags (owns_finalize, registered_collector)
- **Bind Data** (`TsForecastNativeBindData`): Immutable parameters (horizon, method, seasonal_period, etc.)
- **Row Collection**: In-memory `std::map` keyed by group value; values collect vector<dates>, vector<values>, vector<validity>
- **Result Output**: Materialized in memory as vector<ForecastOutputRow>; returned to DuckDB

## Key Abstractions

**Table Functions:**
- Purpose: Transform input rows (group_col, date_col, value_col) into output rows with computed results
- Examples: `_ts_forecast_native` (forecasts), `_ts_fill_gaps_native` (imputation), `_ts_features_native` (features)
- Pattern: Collect grouped data in Execute; process in Finalize; yield results

**SQL Macros:**
- Purpose: Hide complexity of table functions; provide friendly parameter names and defaults
- Examples: `ts_forecast_by()` wraps `_ts_forecast_native()` with default seasonal_period=0, confidence=0.90
- Pattern: Expand to SELECT from table function with positional parameter mapping

**Validity Bitmaps:**
- Represent NULL values in DuckDB columns
- 64-bit words; bit i represents row i % 64
- Passed through C++ layer to Rust FFI; Rust converts to Vec<Option<f64>>

## Entry Points

**LOAD anofox_forecast:**
- Location: `src/anofox_forecast_extension.cpp` line 16-200+ (LoadInternal)
- Triggers: ExtensionHelper::Load() when user calls LOAD anofox_forecast
- Responsibilities: Register 150+ functions (table, scalar, aggregate); auto-load json extension

**SQL Query ts_forecast_by(...):**
- Location: Macro expansion in `src/macros/ts_macros.cpp`
- Triggers: Parser recognizes ts_forecast_by as macro
- Responsibilities: Expand to SELECT from _ts_forecast_native with positional args

**Rust FFI calls (forecast_one_series, extract_features, etc.):**
- Location: `crates/anofox-fcst-ffi/src/lib.rs` lines ~100-500+
- Triggers: C++ table function calls via #include "anofox_fcst_ffi.h"
- Responsibilities: Validate pointers; unmarshal data; call Rust core; handle panics

## Architectural Constraints

- **Threading:** DuckDB handles parallelism via GROUP BY at SQL layer; C++ code uses std::atomic for finalize barrier; no custom thread pool
- **Global state:** TsForecastNativeGlobalState::groups_mutex serializes group insertion; finalize claimed via std::atomic<bool> (only one thread finalizes)
- **Circular imports:** None detected; dependency graph is strictly layered (SQL → C++ table/scalar → FFI → Rust core)
- **WASM compatibility:** Rust FFI supports WASM via conditional compilation in Cargo.toml; fdars-core features gated by target_family; DuckDB extension layer is native-only
- **Memory model:** ListVector for variable-length arrays; std::map for intermediate group data; all allocations freed in Finalize or on exception
- **DuckDB version:** Tested on v1.4.5 LTS and v1.5.4+; uses C++17 standard; constexpr static members handled with forced C++17 in CMakeLists.txt

## Anti-Patterns

### Collecting all data into memory before forecasting

**What happens:** Table functions like `_ts_forecast_native()` load entire groups into in-memory `std::map<string, ForecastGroupData>` during Execute phase before calling Rust forecasting logic in Finalize.

**Why it's wrong:** For very large groups (millions of rows per series), this causes OOM on machines with limited RAM; scales poorly with dataset size.

**Do this instead:** Investigate streaming forecasting or incremental updates in the Rust core (anofox-forecast crate); alternatively use window-based chunking for groups > threshold (file: `src/table_functions/ts_forecast_native.cpp` line 350+).

### Using Rust Vec<f64> without validity handling

**What happens:** Early FFI code assumed all values were valid and skipped NULL handling; null values silently treated as 0.0 or NaN.

**Why it's wrong:** Produces incorrect forecasts and statistics when datasets contain missing values; no clear error message to user.

**Do this instead:** Always use FFI function `build_series()` which returns Vec<Option<f64>> and respects validity bitmap (file: `crates/anofox-fcst-ffi/src/lib.rs` line 62-87).

### Grouping by timestamp directly instead of differencing for stationarity checks

**What happens:** Some period detection functions (e.g., ts_detect_periods) operate on raw data without checking if series is stationary.

**Why it's wrong:** Non-stationary series (trends, level shifts) produce misleading period estimates; can fail to detect true seasonality.

**Do this instead:** Apply differencing (ts_diff) or detrending (ts_detrend) before period detection, or use MSTL decomposition which handles non-stationary data (file: `src/table_functions/ts_periods.cpp` line 200+).

## Error Handling

**Strategy:** Two-level error handling with panics caught at FFI boundary.

**Patterns:**
- **FFI boundary** (`anofox_fcst_ffi.rs`): All exported functions wrapped in std::catch_unwind to convert Rust panics to C++ exceptions
- **Validation**: Null pointer checks; length > 0; data type matching in FFI (file: `crates/anofox-fcst-ffi/src/error_handling.rs`)
- **DuckDB integration**: C++ layer converts Rust errors to DuckDB exceptions via throw DuckDB::Exception (file: `src/table_functions/ts_forecast_native.cpp` line 600+)
- **User-facing**: SQL errors propagated as DuckDB error messages with context ("Error in ts_forecast_by: seasonal_period must be > 0")

## Cross-Cutting Concerns

**Logging:** No built-in logging. Debugging done via:
- DuckDB PRAGMA debug_print_plan;
- Printf-style debugging in C++ (disabled in release builds)
- Telemetry via PostHog if HAS_POSTHOG_TELEMETRY enabled (file: `src/anofox_forecast_extension.cpp` line 9-11)

**Validation:** 
- Type validation in FFI boundary (numeric, timestamp types)
- Domain validation in Rust core (seasonal_period > 0, confidence_level in [0,1])
- DuckDB type coercion (implicit CAST to DOUBLE for value_col)

**Authentication:** 
- None; extension assumes DuckDB user already authenticated to database
- Telemetry optionally anonymizes queries (if PostHog enabled)

---

*Architecture analysis: 2026-08-20*
