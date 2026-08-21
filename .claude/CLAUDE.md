<!-- GSD:project-start source:PROJECT.md -->

## Project

**anofox-forecast — Milestone: Close the Crate→Extension Gap (Diagnostics + Model Coverage)**

`anofox-forecast` is a DuckDB extension that exposes SQL-native time-series forecasting, backed by the `anofox-forecast` Rust crate (v0.15.3) via an FFI boundary. It already surfaces 36 forecasting models, 117 features, cross-validation, conformal prediction intervals, seasonality/period/changepoint/peak detection, and data-prep utilities as SQL functions and `ts_*_by` macros.

This milestone extends that SQL surface to reach crate capabilities that are currently unreachable from SQL: statistical **diagnostics & validation**, and additional **forecasting models** (global/panel and classical). It is a brownfield capability-exposure milestone, not a rewrite — the delivery pattern is the established one: Rust FFI export → C++ table/scalar/aggregate function → `ts_*_by` SQL macro → runnable example → docs.

**Core Value:** SQL users can validate whether a series/model is statistically sound (stationarity, residual adequacy, demand regime) and can reach the crate's higher-coverage models (global + classical) — all without leaving DuckDB.

### Constraints

- **Tech stack**: DuckDB v1.4.3+ extension; Rust 1.86+ core via FFI; C++17. No new languages.
- **Architecture**: Parallelism stays at the DuckDB GROUP BY / scalar-function layer — no custom threading or table-in/table-out (established project rule).
- **Dependencies**: Stay on `anofox-forecast` 0.15.3 unless a required capability is missing; global-model steady-state ARIMA optimization tracked separately (awaiting 0.5.4-class improvements).
- **Compatibility**: Must build and load across Linux/macOS/Windows and WASM; OpenSSL stays statically linked; verify clean-machine load (not just green CI).
- **Verification**: Every new SQL function must be exercised by a runnable example against the built extension before it counts as done.

<!-- GSD:project-end -->

<!-- GSD:stack-start source:codebase/STACK.md -->

## Technology Stack

## Languages

- Rust (Edition 2021) - Core forecasting logic, FFI boundary (`crates/anofox-fcst-core`, `crates/anofox-fcst-ffi`)
- C++ (C++17 standard) - DuckDB extension implementation (`src/`)
- Python (>=3.11, <3.13) - Benchmarking and validation suite (`benchmark/`)
- CMake - Build system for C++/Rust integration
- SQL - DuckDB table functions and macros

## Runtime

- DuckDB v1.4.3+ (primary database engine)
- Emscripten (WASM target builds via `wasm32-unknown-emscripten`)
- Cargo (Rust) - Workspace root at `Cargo.toml` with 2 member crates
- uv (Python) - Used for benchmark environment (`benchmark/pyproject.toml`)
- CMake 3.20+ - C++ build orchestration

## Frameworks

- `anofox-forecast` v0.15.3 - Time-series forecasting library (features: `anomaly`, `serde`)
- `anofox-regression` v0.5.3 - Regression analysis and feature extraction
- `fdars-core` v0.3 - Functional data analysis for seasonality, peaks, detrending (target-conditional features: `parallel`/`linalg` for native, `js` for WASM)
- `faer` v0.23 - Matrix/linear algebra operations (features: `std`, `linalg`)
- `statrs` v0.18 - Statistical distributions and functions
- `libc` v0.2 - C standard library bindings
- `cbindgen` (build-dep) - Generates C headers from Rust FFI (`crates/anofox-fcst-ffi/cbindgen.toml`)
- `chrono` v0.4 - Date/time handling across Rust and FFI boundaries
- `thiserror` v2.0 - Ergonomic error definitions

## Key Dependencies

- `anofox-forecast` v0.15.3 - Provides all time-series algorithms; anomaly detection and serialization support required
- `anofox-regression` v0.5.3 - Global regression and per-series scaling for forecasting
- `fdars-core` v0.3 - Seasonality detection, peak analysis, period estimation (FFT/ACF/LombScargle)
- `faer` v0.23 - MSTL decomposition, feature extraction, conformal prediction intervals
- DuckDB v1.4.3 (submodule) - Extension host and table function framework
- OpenSSL (conditional) - Static-linked for HTTPS in PostHog telemetry (`posthog-telemetry/`)
- Corrosion v0.6.1 - CMake-Rust integration for FFI builds

## Configuration

- Build configuration via `CMakeLists.txt` (root project)
- CI environment detection (auto-disable telemetry): Checks for `CI`, `GITHUB_ACTIONS`, `GITLAB_CI`, `CIRCLECI`, `TRAVIS`, `JENKINS_URL`, `BUILDKITE`, `TEAMCITY_VERSION`, `TF_BUILD`, `CODEBUILD_BUILD_ID`
- Telemetry opt-in: `DATAZOO_DISABLE_TELEMETRY` env var respected; config setting `anofox_telemetry_enabled` (default: true)
- `CMakeLists.txt` - Extension compilation with Corrosion FFI integration
- `Cargo.toml` - Rust workspace with patched argmin (stable Rust 1.86 compat, `DataZooDE/argmin@fix/stable-rust-compat`)
- `Makefile` - Convenience targets for local development (rust, rust_debug, rust_test, fmt, check, header, benchmark)
- `extension_config.cmake` - DuckDB extension loader with WASM-specific `LINKED_LIBS` configuration

## Platform Requirements

- CMake 3.20+
- Rust 1.86+ (stable, due to argmin patch)
- C++ compiler supporting C++17 (GCC 14+ preferred on Linux due to symbol deduplication)
- Python 3.11-3.12
- DuckDB development headers (via submodule or v1.4.3 fetch)
- DuckDB 1.4.3 or later
- OpenSSL 1.1 or 3.x (static linking on Linux/Windows eliminates runtime .so/.dll deps)
- macOS: CoreFoundation, SystemConfiguration frameworks
- Windows: bcrypt library
- Linux: pthreads, dl, m (math) libraries
- Emscripten SDK with `wasm32-unknown-emscripten` Rust target
- No OpenSSL (telemetry disabled)
- Rust std conditionally built with WASM support

<!-- GSD:stack-end -->

<!-- GSD:conventions-start source:CONVENTIONS.md -->

## Conventions

## Naming Patterns

- Rust source files use snake_case: `decomposition.rs`, `detrending.rs`, `imputation.rs`
- Test modules are inline within source files using `#[cfg(test)] mod tests { }`
- Benchmark files use snake_case in `benches/` directory: `mstl_perf.rs`
- SQL test files use lowercase with underscores: `ts_diff.test`, `ts_features.test`
- Public functions use snake_case: `mstl_decompose()`, `extract_features()`, `detect_changepoints()`
- Utility functions follow verb_noun pattern: `is_constant()`, `drop_edge_zeros()`, `fill_gaps()`
- Methods that validate/detect use verb prefixes: `detect_*`, `classify_*`, `compute_*`, `extract_*`, `analyze_*`
- Builder/conversion methods use `from_*` or `to_*` patterns
- Local variables and parameters use snake_case: `non_null_count`, `seasonal_period`, `hazard_rate`
- Constants and type parameters use UPPER_SNAKE_CASE: `DEFAULT_TOLERANCE`, `HORIZON`, `SEASONAL_PERIOD`
- Generic type parameters use uppercase single letters: `T`, `F` (for function types)
- Structs use PascalCase: `MstlDecomposition`, `ForecastError`, `ConformalResult`
- Enums use PascalCase with variants as PascalCase: `PeriodMethod::Fft`, `InsufficientDataMode::Trend`
- Type aliases use PascalCase: `Result<T>` is defined as `type Result<T> = std::result::Result<T, ForecastError>`
- Result wrappers follow convention: `pub type Result<T> = std::result::Result<T, ForecastError>`

## Code Style

- Use standard Rust formatting (implied rustfmt defaults — no rustfmt.toml found)
- 4-space indentation (Rust default)
- Line length: standard (no specific limit enforced)
- Opening braces on same line: `fn foo() {` (Rust convention)
- Standard Clippy lints apply (no custom configuration)
- Code follows idiomatic Rust patterns

## Import Organization

- No path aliases observed in crates (standard module system used)
- Crate-relative paths use `crate::` prefix explicitly

## Error Handling

- All fallible operations return `Result<T>` which is `std::result::Result<T, ForecastError>`
- Custom error types defined with `#[derive(Error, Debug)]` using `thiserror` crate
- Error variants include contextual information: `InvalidParameter { param, value, reason }`
- Errors map to numeric codes for FFI: `to_code()` method on `ForecastError`
- Example from `crates/anofox-fcst-core/src/error.rs`:

## Logging

- Errors propagated via `Result<T>` type, not logged
- FFI layer converts errors to numeric codes for caller interpretation
- No `println!` or `eprintln!` in library code (only in benchmarks)

## Comments

- Module-level documentation with `//!` explaining purpose and usage
- Complex algorithms documented with multi-line comments
- Examples included in doc comments with code blocks
- Individual functions have doc comments with purpose, arguments, returns sections
- Rust uses `///` for doc comments on public items
- Format: Summary line, then Arguments section, Returns section, Example section if applicable
- Example from `crates/anofox-fcst-core/src/filter.rs`:

## Function Design

- Functions kept to 30-50 lines for core algorithms; helpers smaller (5-20 lines)
- Long functions (100+ lines) contain clear section comments for major steps
- Slices preferred over references to vectors: `fn foo(&[f64])`
- Optional values use `Option<f64>` and `Option<T>` patterns
- Return complex results via struct: `struct DetrendResult { detrended: Vec<f64>, ... }`
- No builder patterns observed; configuration via direct struct construction or default traits
- Simple values returned directly
- Multiple values wrapped in structs with named fields
- Errors wrapped in `Result<T>`
- Option used for nullable returns (e.g., trend component might be `Option<Vec<f64>>`)

## Module Design

- Public types and functions explicitly declared as `pub`
- Re-exports in `lib.rs` to stabilize API: `pub use bootstrap::{...};`
- Private helpers marked implicitly without `pub` keyword
- Single `lib.rs` in `crates/anofox-fcst-core/src/lib.rs` re-exports all public items
- Allows users to import from crate root: `use anofox_fcst_core::mstl_decompose;`
- Pattern keeps internal module structure hidden while exposing clean API

<!-- GSD:conventions-end -->

<!-- GSD:architecture-start source:ARCHITECTURE.md -->

## Architecture

## System Overview

```text

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

- **SQL-native API** - Zero-setup macros automatically loaded; all functions exposed as pure SQL
- **Streaming parallel** - Native DuckDB GROUP BY + scalar functions for in-memory parallelism; no custom threading
- **Memory efficient** - Columnar storage with ListVector; O(group_size) not O(total_rows) for group-based operations
- **Rust performance** - Hot path (forecasting, feature extraction) in Rust with FFI boundary to C++
- **Layered design** - SQL macros wrap table functions; table functions dispatch to Rust via FFI; FFI marshals types

## Layers

- Purpose: High-level, user-friendly SQL templates for common workflows
- Location: `src/macros/ts_macros.cpp`
- Contains: 20+ named parameters macros for forecasting, CV, data prep
- Depends on: Table functions (_ts_forecast_native, _ts_cv_folds_by, _ts_fill_gaps_native, etc.) and scalar functions
- Used by: Direct SQL calls from users; examples in `examples/` directory
- Purpose: Implement forecasting, data prep, gap filling, feature extraction, metrics, cross-validation
- Location: `src/table_functions/` (43 files)
- Contains: _ts_forecast_native, _ts_fill_gaps_native, _ts_cv_folds_by, _ts_features_native, _ts_metrics_native, etc.
- Depends on: Rust FFI functions via anofox_fcst_ffi.h; DuckDB vectorized API
- Used by: SQL macros; called directly from SQL or R/Python bindings
- Purpose: Compute grouped statistics on time series (statistics, features, forecasts per group)
- Location: `src/aggregate_functions/` (8 files)
- Contains: ts_stats_agg, ts_features_agg, ts_forecast_agg, ts_changepoints_agg, etc.
- Depends on: Rust FFI; DuckDB aggregate function API
- Used by: SQL queries for GROUP BY operations; used internally by some table functions
- Purpose: Compute point values (metrics, conformal quantiles, bootstrap) per row
- Location: `src/scalar_functions/` (5 files)
- Contains: ts_forecast_scalar (single-series wrapper), ts_forecast_inspect_scalar (model inspection), metrics, conformal, bootstrap
- Depends on: Rust FFI
- Used by: SQL for per-row operations; windowing functions
- Purpose: DuckDB extension lifecycle (load, register, telemetry)
- Location: `src/anofox_forecast_extension.cpp`
- Contains: LoadInternal() function that registers all 150+ functions; telemetry hooks
- Depends on: All function registration functions
- Used by: DuckDB core on LOAD anofox_forecast
- Purpose: Type marshalling, error handling, memory management between C++ and Rust
- Location: `crates/anofox-fcst-ffi/src/lib.rs`
- Contains: C-compatible function signatures; validation; allocation/deallocation; panic catching
- Depends on: anofox-fcst-core (via path dependency)
- Used by: All C++ table/scalar/aggregate functions
- Purpose: Forecasting models (33), feature extraction (117), changepoint detection, statistics
- Location: `crates/anofox-fcst-core/src/lib.rs`
- Contains: Wrapper around anofox-forecast, anofox-regression, fdars-core crates
- Depends on: External crates (anofox-forecast 0.15.3, anofox-regression 0.5.3, fdars-core 0.3)
- Used by: FFI boundary layer

## Data Flow

### Primary Request Path: Forecasting

### Secondary Flow: Feature Extraction

### Cross-Validation Flow (ts_cv_folds_by)

### State Management

- **Global State** (`TsForecastNativeGlobalState`): Thread-safe map of group_key → ForecastGroupData; atomic finalize barrier
- **Local State** (`TsForecastNativeLocalState`): Per-thread flags (owns_finalize, registered_collector)
- **Bind Data** (`TsForecastNativeBindData`): Immutable parameters (horizon, method, seasonal_period, etc.)
- **Row Collection**: In-memory `std::map` keyed by group value; values collect vector<dates>, vector<values>, vector<validity>
- **Result Output**: Materialized in memory as vector<ForecastOutputRow>; returned to DuckDB

## Key Abstractions

- Purpose: Transform input rows (group_col, date_col, value_col) into output rows with computed results
- Examples: `_ts_forecast_native` (forecasts), `_ts_fill_gaps_native` (imputation), `_ts_features_native` (features)
- Pattern: Collect grouped data in Execute; process in Finalize; yield results
- Purpose: Hide complexity of table functions; provide friendly parameter names and defaults
- Examples: `ts_forecast_by()` wraps `_ts_forecast_native()` with default seasonal_period=0, confidence=0.90
- Pattern: Expand to SELECT from table function with positional parameter mapping
- Represent NULL values in DuckDB columns
- 64-bit words; bit i represents row i % 64
- Passed through C++ layer to Rust FFI; Rust converts to Vec<Option<f64>>

## Entry Points

- Location: `src/anofox_forecast_extension.cpp` line 16-200+ (LoadInternal)
- Triggers: ExtensionHelper::Load() when user calls LOAD anofox_forecast
- Responsibilities: Register 150+ functions (table, scalar, aggregate); auto-load json extension
- Location: Macro expansion in `src/macros/ts_macros.cpp`
- Triggers: Parser recognizes ts_forecast_by as macro
- Responsibilities: Expand to SELECT from _ts_forecast_native with positional args
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

### Using Rust Vec<f64> without validity handling

### Grouping by timestamp directly instead of differencing for stationarity checks

## Error Handling

- **FFI boundary** (`anofox_fcst_ffi.rs`): All exported functions wrapped in std::catch_unwind to convert Rust panics to C++ exceptions
- **Validation**: Null pointer checks; length > 0; data type matching in FFI (file: `crates/anofox-fcst-ffi/src/error_handling.rs`)
- **DuckDB integration**: C++ layer converts Rust errors to DuckDB exceptions via throw DuckDB::Exception (file: `src/table_functions/ts_forecast_native.cpp` line 600+)
- **User-facing**: SQL errors propagated as DuckDB error messages with context ("Error in ts_forecast_by: seasonal_period must be > 0")

## Cross-Cutting Concerns

- DuckDB PRAGMA debug_print_plan;
- Printf-style debugging in C++ (disabled in release builds)
- Telemetry via PostHog if HAS_POSTHOG_TELEMETRY enabled (file: `src/anofox_forecast_extension.cpp` line 9-11)
- Type validation in FFI boundary (numeric, timestamp types)
- Domain validation in Rust core (seasonal_period > 0, confidence_level in [0,1])
- DuckDB type coercion (implicit CAST to DOUBLE for value_col)
- None; extension assumes DuckDB user already authenticated to database
- Telemetry optionally anonymizes queries (if PostHog enabled)

<!-- GSD:architecture-end -->

<!-- GSD:skills-start source:skills/ -->

## Project Skills

| Skill | Description | Path |
|-------|-------------|------|
| anofox-forecast-backtest | > Backtesting, cross-validation, evaluation metrics, and conformal prediction intervals for the anofox_forecast DuckDB extension. Use when evaluating forecast accuracy, comparing models with time-series-aware CV, computing metrics (MAE / RMSE / MAPE / MASE / coverage), or attaching distribution-free prediction intervals to forecasts. | `.claude/skills/anofox-forecast-backtest/SKILL.md` |
| anofox-forecast-data-prep | > Data preparation for the anofox_forecast DuckDB extension — filling gaps, imputing nulls, dropping bad series, differencing, detrending, hierarchical key operations. Use when preparing raw time series for downstream forecasting or backtesting with `ts_forecast_by` / `ts_cv_folds_by`. | `.claude/skills/anofox-forecast-data-prep/SKILL.md` |
| anofox-forecast-detection | > Seasonality, changepoint, peak, and decomposition detection for the anofox_forecast DuckDB extension. Use when identifying seasonal periods before configuring seasonal forecasting models, detecting structural breaks, analysing peak timing regularity, or decomposing a series into trend / seasonal / residual components. | `.claude/skills/anofox-forecast-detection/SKILL.md` |
| anofox-forecast-eda | > Exploratory data analysis and data quality for the anofox_forecast DuckDB extension — 34 per-series statistics, data-quality scoring, quality-report summaries, and 117 tsfresh-compatible feature extraction. Use before forecasting to understand series characteristics (length, gaps, trend, seasonality strength, intermittency) or to build ML feature vectors for downstream models. | `.claude/skills/anofox-forecast-eda/SKILL.md` |
| anofox-forecast-models | > Forecasting models and the `ts_forecast_by` API surface of the anofox_forecast DuckDB extension. Covers 33 models (baseline, exponential smoothing, state-space, ARIMA, Theta, multi-seasonal, intermittent-demand, distributional Laplace with three variants), parameter surfaces (MAP + STRUCT), model selection guidance, and common workflow gotchas. Use when picking a model or writing `ts_forecast_by` / `ts_forecast_agg` calls. | `.claude/skills/anofox-forecast-models/SKILL.md` |
<!-- GSD:skills-end -->

<!-- GSD:workflow-start source:GSD defaults -->

## GSD Workflow Enforcement

Before using Edit, Write, or other file-changing tools, start work through a GSD command so planning artifacts and execution context stay in sync.

Use these entry points:

- `/gsd-quick` for small fixes, doc updates, and ad-hoc tasks
- `/gsd-debug` for investigation and bug fixing
- `/gsd-execute-phase` for planned phase work

Do not make direct repo edits outside a GSD workflow unless the user explicitly asks to bypass it.
<!-- GSD:workflow-end -->

<!-- GSD:profile-start -->

## Developer Profile

> Profile not yet configured. Run `/gsd-profile-user` to generate your developer profile.
> This section is managed by `generate-claude-profile` -- do not edit manually.
<!-- GSD:profile-end -->
