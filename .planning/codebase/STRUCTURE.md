# Codebase Structure

**Analysis Date:** 2026-08-20

## Directory Layout

```
anofox-forecast/
├── src/                           # C++ DuckDB extension code
│   ├── anofox_forecast_extension.cpp    # Extension entry point; function registration
│   ├── include/                         # C++ headers for type definitions
│   │   ├── anofox_forecast_extension.hpp # Function registration declarations
│   │   ├── ts_forecast_native.hpp       # Type definitions for table functions
│   │   ├── anofox_fcst_ffi.h            # Rust FFI C-compatible signatures
│   │   └── *.hpp                        # Type definitions for each table function
│   ├── table_functions/                 # 43 DuckDB table-returning functions
│   │   ├── ts_forecast_native.cpp       # Main forecasting engine
│   │   ├── ts_cv_folds_native.cpp       # Cross-validation fold generation
│   │   ├── ts_cv_forecast_native.cpp    # Apply forecast to CV fold
│   │   ├── ts_fill_gaps_native.cpp      # Gap filling (interpolation)
│   │   ├── ts_features_native.cpp       # Feature extraction (117 features)
│   │   ├── ts_metrics_native.cpp        # Accuracy metrics (MAE, RMSE, etc.)
│   │   ├── ts_changepoints.cpp          # Changepoint detection
│   │   ├── ts_mstl_decomposition_native.cpp  # MSTL decomposition
│   │   └── ts_*.cpp                     # Other functions (gaps, periods, seasonality, etc.)
│   ├── scalar_functions/                # 5 DuckDB scalar functions
│   │   ├── ts_forecast_scalar.cpp       # Single-series forecast wrapper
│   │   ├── ts_forecast_inspect_scalar.cpp   # Model inspection
│   │   ├── metrics.cpp                  # Scalar metrics (MAE, RMSE per row)
│   │   ├── conformal.cpp                # Conformal prediction intervals
│   │   └── bootstrap.cpp                # Bootstrap prediction intervals
│   ├── aggregate_functions/             # 8 DuckDB aggregate functions
│   │   ├── ts_forecast_agg.cpp          # Forecasts grouped by key
│   │   ├── ts_features_agg.cpp          # Features grouped by key
│   │   ├── ts_stats_agg.cpp             # Statistics grouped by key
│   │   └── ts_*_agg.cpp                 # Other aggregates
│   └── macros/
│       └── ts_macros.cpp                # 20+ high-level SQL macros (ts_forecast_by, ts_cv_folds_by, etc.)
│
├── crates/                        # Rust implementation (Cargo workspace)
│   ├── anofox-fcst-core/          # Core Rust logic (forecasting, features, statistics)
│   │   ├── Cargo.toml             # Feature flags: native, wasm
│   │   ├── src/lib.rs             # Main module; delegates to external crates
│   │   └── benches/               # Performance benchmarks
│   └── anofox-fcst-ffi/           # FFI boundary (C-compatible function definitions)
│       ├── Cargo.toml             # Produces staticlib + rlib
│       ├── src/lib.rs             # All exported C functions
│       ├── src/types.rs           # FFI-safe struct definitions
│       ├── src/error_handling.rs  # Validation, error conversion
│       ├── src/conversion.rs      # Parameter type conversion
│       └── src/allocation.rs      # Memory allocation helpers
│
├── examples/                      # SQL usage examples
│   ├── forecasting/               # Basic forecasting examples
│   ├── backtesting/               # Cross-validation examples
│   ├── feature_extraction/        # Feature engineering examples
│   ├── period_detection/          # Seasonality analysis examples
│   ├── changepoint_detection/     # Regime detection examples
│   ├── conformal_prediction/      # Uncertainty quantification examples
│   └── multi_key_hierarchy/       # Hierarchical time series examples
│
├── test/                          # Test suite
│   └── sql/                       # SQL-based tests
│       └── backtest_memory_investigation.sql  # Backtest memory profiling
│
├── benchmark/                     # Performance benchmarks
│   ├── src/                       # Python benchmark harness
│   ├── sql/                       # SQL benchmark scripts
│   ├── m4/, m5/                   # M4/M5 competition datasets
│   ├── mstl/                      # MSTL decomposition benchmarks
│   ├── timeseries_features/       # Feature extraction benchmarks
│   └── seasonality_detection/     # Period detection benchmarks
│
├── docs/                          # User documentation
│   ├── API_REFERENCE.md           # Complete function reference
│   ├── api/                       # API documentation by category
│   │   ├── 02-hierarchical.md
│   │   ├── 03-statistics.md
│   │   ├── 07-forecasting.md
│   │   ├── 09-evaluation-metrics.md
│   │   ├── 20-feature-extraction.md
│   │   └── *.md
│   ├── guides/                    # User guides
│   │   ├── 01-getting-started.md
│   │   ├── 02-model-selection.md
│   │   └── 03-cross-validation.md
│   ├── reference/                 # Model reference
│   │   └── models/                # Per-model documentation
│   │       ├── baseline/
│   │       ├── exponential-smoothing/
│   │       ├── state-space/
│   │       └── *.md
│   └── dev/                       # Developer guides
│
├── posthog-telemetry/             # Optional telemetry integration
│   ├── include/                   # Telemetry headers
│   └── src/                       # Telemetry implementation
│
├── duckdb/                        # DuckDB submodule (build dependency)
│   └── (git submodule)
│
├── extension-ci-tools/            # CI/build tools (git submodule)
│   └── (git submodule)
│
├── scripts/                       # Setup and build scripts
│   └── setup-hooks.sh             # Git hooks for cargo fmt/clippy
│
├── build/                         # Compiled output (CMake)
│   └── release/                   # Release build artifacts
│       └── extension/anofox_forecast/*.duckdb_extension
│
├── Cargo.toml                     # Workspace manifest (2 crates)
├── Cargo.lock                     # Dependency lock file
├── CMakeLists.txt                 # Main build configuration (CMake)
├── extension_config.cmake         # DuckDB extension config
├── Makefile                       # Convenience build targets
├── README.md                      # Project overview
├── LICENSE                        # BSL 1.1 license
├── CHANGELOG.md                   # Version history
├── CONTRIBUTING.md                # Development guidelines
└── THIRD_PARTY_NOTICES.md         # Attribution
```

## Directory Purposes

**`src/`** - C++ DuckDB extension wrapper layer
- Handles DuckDB-specific concerns: vector types, function registration, batch processing
- Calls Rust FFI for actual computation
- 43 table functions, 8 aggregate functions, 5 scalar functions, 20+ SQL macros
- Builds into single .duckdb_extension file

**`crates/anofox-fcst-core/`** - Rust core logic (forecasting, statistics, features)
- Pure Rust implementation of 33 forecasting models
- 117 tsfresh-compatible feature extractors
- Delegates to anofox-forecast, anofox-regression, fdars-core crates
- Compiled into static archive, linked into C++ extension

**`crates/anofox-fcst-ffi/`** - Rust FFI boundary layer
- Exports C-compatible function signatures
- Handles type marshalling (C++ types ↔ Rust types)
- Memory allocation/deallocation; error handling; panic catching
- Produces staticlib for linking into C++ extension

**`examples/`** - SQL usage examples
- Categorized by feature area (forecasting, backtesting, features, etc.)
- End-to-end workflow examples using synthetic or M5 data
- Run via: `duckdb < examples/forecasting/synthetic_forecasting_examples.sql`

**`test/sql/`** - SQL-based regression tests
- Currently minimal; manual tests done via examples/
- Backtest memory investigation script for profiling

**`benchmark/`** - Performance evaluation suite
- Python harness for running benchmarks
- M4/M5 datasets for standard time series benchmarks
- Per-feature benchmarks (MSTL, seasonality detection, feature extraction, etc.)

**`docs/`** - Complete user and developer documentation
- API reference with all function signatures
- Category-specific guides (hierarchical, statistics, forecasting, evaluation, features)
- Model reference (one page per model)
- Getting started guide and examples

**`posthog-telemetry/`** - Optional telemetry (build-time opt-in)
- Only compiled if HAS_POSTHOG_TELEMETRY defined
- Sends anonymized query patterns to PostHog (no data values)

## Key File Locations

**Entry Points:**
- `src/anofox_forecast_extension.cpp` - DuckDB extension loader (LoadInternal function)
- `Makefile` - Top-level build entry point

**Configuration:**
- `CMakeLists.txt` - Main build configuration; sets C++17, link flags, Rust target
- `Cargo.toml` - Rust workspace manifest; dependencies (anofox-forecast, fdars-core, etc.)
- `extension_config.cmake` - DuckDB extension metadata

**Core Logic:**
- `src/table_functions/ts_forecast_native.cpp` - Main forecasting engine (33 models)
- `src/table_functions/ts_features_native.cpp` - Feature extraction wrapper
- `src/table_functions/ts_cv_folds_native.cpp` - Cross-validation fold generation
- `crates/anofox-fcst-core/src/lib.rs` - Rust core module definitions
- `crates/anofox-fcst-ffi/src/lib.rs` - All exported C functions

**Testing:**
- `examples/` - SQL examples for manual testing (150+ end-to-end examples)
- `test/sql/` - Formal test suite (minimal; mostly examples-based)
- `benchmark/sql/` - SQL benchmark scripts for performance profiling

## Naming Conventions

**Files:**
- C++ source: `ts_<feature>_<type>.cpp` (e.g., `ts_forecast_native.cpp`, `ts_metrics_native.cpp`)
  - `_native` suffix indicates native table function using streaming/parallel logic
  - `_agg` suffix indicates aggregate function
  - No suffix = scalar function
- C++ headers: `ts_<feature>_native.hpp` (type definitions matching .cpp files)
- Rust: `lib.rs` (one per crate; modules defined inline or via `mod` statements)
- Macros: `ts_macros.cpp` (single file containing all 20+ macros)

**Directories:**
- Feature area organization: `table_functions/`, `scalar_functions/`, `aggregate_functions/`, `examples/<category>/`
- Crate names: `anofox-fcst-core`, `anofox-fcst-ffi` (kebab-case with domain prefix)
- Doc sections: `api/<number>-<topic>.md` (numbered for reading order)

**Functions:**
- DuckDB functions: `ts_<operation>_<variant>` (e.g., `ts_forecast_by`, `ts_fill_gaps_native`)
- Internal/native: `_ts_<operation>` (leading underscore hides from user; called by macros)
- Rust FFI: `forecast_one_series`, `extract_features`, etc. (snake_case; exported as C-compatible)
- Macros: `ts_<operation>` (same as table functions; macros expand to call internal functions)

## Where to Add New Code

**New Forecasting Model:**
- Primary implementation: `crates/anofox-fcst-core/src/lib.rs` (delegate to anofox-forecast crate)
- Model selection logic: `src/table_functions/ts_forecast_native.cpp` lines ~200-250 (bind phase)
- Model name string matching: Lines 220-250 in ts_forecast_native.cpp
- Test coverage: `examples/forecasting/synthetic_forecasting_examples.sql` (add SELECT with new model name)

**New Table Function (e.g., data prep):**
1. Create `src/table_functions/ts_<newfeature>.cpp` with:
   - Bind struct (inherit from TableFunctionData)
   - Bind function (parse parameters)
   - Init function (create state)
   - Execute function (collect data per-group)
   - Finalize function (compute results; yield rows)
2. Create header `src/include/ts_<newfeature>_native.hpp` (type definitions)
3. Register in `src/anofox_forecast_extension.cpp` line 16-200+ (add RegisterTs<NewFeature>Function call)
4. Add declaration to `src/include/anofox_forecast_extension.hpp`
5. (Optional) Wrap in SQL macro in `src/macros/ts_macros.cpp` for user-friendly API

**New Scalar Function (e.g., metric):**
1. Create `src/scalar_functions/ts_<metric>.cpp` or add to `metrics.cpp`
2. Define ScalarFunction with bind/execute logic
3. Register in `src/anofox_forecast_extension.cpp` line 119-132 (metric registration section)
4. Add test example in `examples/metrics/synthetic_metrics_examples.sql`

**New Aggregate Function:**
1. Create `src/aggregate_functions/ts_<feature>_agg.cpp`
2. Implement combine + finalize logic for GROUP BY operation
3. Register in `src/anofox_forecast_extension.cpp` line 115-117 (aggregate registration section)
4. Test via: SELECT * FROM ts_<feature>_by(table, group_col, ...)

**New Feature (117+ already exist):**
- Implementation: Handled by anofox-regression crate (external)
- To add: Modify anofox-regression dependency in Cargo.toml; wrap in `ts_features_native.cpp`
- Config: Update feature list in `examples/feature_extraction/` and docs

**New SQL Macro:**
1. Add to `src/macros/ts_macros.cpp` in ts_table_macros[] array
2. Follow pattern: Define TsTableMacro struct with name, parameters, named_params, SQL definition
3. SQL definition should call underlying _ts_*_native table function
4. Register in macro loader (handled automatically by registration code)

**New Test:**
- SQL examples: `examples/<category>/<description>.sql`
- Python benchmark: `benchmark/src/<benchmark_name>.py` (with argparse + CSV output)
- Unit test: Not currently used; testing is integration-style via SQL examples

## Special Directories

**`build/`** - Build output (generated, not committed)
- CMake-generated build artifacts
- Release extension at: `build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension`
- Clean via: `rm -rf build/`

**`target/`** - Rust build output (generated, not committed)
- Cargo incremental builds
- Release binaries at: `target/release/`
- Clean via: `cargo clean`

**`duckdb/`** - Git submodule (used for building)
- Points to DuckDB source repository
- Contains DuckDB's C++ headers and cmake files
- Updated via: `git submodule update --recursive`

**`extension-ci-tools/`** - Git submodule (build support)
- DuckDB's CI tools for extension builds
- CMake helpers, Docker templates, vcpkg integration

**`docs/`** - Published documentation
- Committed to git; synced to user-facing docs site
- Structure matches API reference categories
- Built via: `make docs` (if integrated with mkdocs/sphinx)

---

*Structure analysis: 2026-08-20*
