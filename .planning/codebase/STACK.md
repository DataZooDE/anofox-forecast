# Technology Stack

**Analysis Date:** 2026-08-20

## Languages

**Primary:**
- Rust (Edition 2021) - Core forecasting logic, FFI boundary (`crates/anofox-fcst-core`, `crates/anofox-fcst-ffi`)
- C++ (C++17 standard) - DuckDB extension implementation (`src/`)
- Python (>=3.11, <3.13) - Benchmarking and validation suite (`benchmark/`)

**Secondary:**
- CMake - Build system for C++/Rust integration
- SQL - DuckDB table functions and macros

## Runtime

**Environment:**
- DuckDB v1.4.3+ (primary database engine)
- Emscripten (WASM target builds via `wasm32-unknown-emscripten`)

**Package Manager:**
- Cargo (Rust) - Workspace root at `Cargo.toml` with 2 member crates
- uv (Python) - Used for benchmark environment (`benchmark/pyproject.toml`)
- CMake 3.20+ - C++ build orchestration

## Frameworks

**Core Forecasting:**
- `anofox-forecast` v0.15.3 - Time-series forecasting library (features: `anomaly`, `serde`)
- `anofox-regression` v0.5.3 - Regression analysis and feature extraction
- `fdars-core` v0.3 - Functional data analysis for seasonality, peaks, detrending (target-conditional features: `parallel`/`linalg` for native, `js` for WASM)

**Linear Algebra & Statistics:**
- `faer` v0.23 - Matrix/linear algebra operations (features: `std`, `linalg`)
- `statrs` v0.18 - Statistical distributions and functions

**FFI & Memory:**
- `libc` v0.2 - C standard library bindings
- `cbindgen` (build-dep) - Generates C headers from Rust FFI (`crates/anofox-fcst-ffi/cbindgen.toml`)

**Date/Time:**
- `chrono` v0.4 - Date/time handling across Rust and FFI boundaries

**Error Handling:**
- `thiserror` v2.0 - Ergonomic error definitions

## Key Dependencies

**Critical for Extension:**
- `anofox-forecast` v0.15.3 - Provides all time-series algorithms; anomaly detection and serialization support required
- `anofox-regression` v0.5.3 - Global regression and per-series scaling for forecasting
- `fdars-core` v0.3 - Seasonality detection, peak analysis, period estimation (FFT/ACF/LombScargle)
- `faer` v0.23 - MSTL decomposition, feature extraction, conformal prediction intervals

**Infrastructure:**
- DuckDB v1.4.3 (submodule) - Extension host and table function framework
- OpenSSL (conditional) - Static-linked for HTTPS in PostHog telemetry (`posthog-telemetry/`)
- Corrosion v0.6.1 - CMake-Rust integration for FFI builds

## Configuration

**Environment:**
- Build configuration via `CMakeLists.txt` (root project)
  - Forced C++17 standard globally (fixes static constexpr symbol conflicts with DuckDB v1.4)
  - Platform detection: Linux GNU/musl, macOS (arm64/x86_64), Windows (MSVC/MinGW), WASM
  - Rust target auto-detection via `rustup target list --installed` with fallback to `rustc --version --verbose`
  - OpenSSL detection via `find_package(OpenSSL)` with static linking preference (`OPENSSL_USE_STATIC_LIBS ON`)
- CI environment detection (auto-disable telemetry): Checks for `CI`, `GITHUB_ACTIONS`, `GITLAB_CI`, `CIRCLECI`, `TRAVIS`, `JENKINS_URL`, `BUILDKITE`, `TEAMCITY_VERSION`, `TF_BUILD`, `CODEBUILD_BUILD_ID`
- Telemetry opt-in: `DATAZOO_DISABLE_TELEMETRY` env var respected; config setting `anofox_telemetry_enabled` (default: true)

**Build:**
- `CMakeLists.txt` - Extension compilation with Corrosion FFI integration
- `Cargo.toml` - Rust workspace with patched argmin (stable Rust 1.86 compat, `DataZooDE/argmin@fix/stable-rust-compat`)
- `Makefile` - Convenience targets for local development (rust, rust_debug, rust_test, fmt, check, header, benchmark)
- `extension_config.cmake` - DuckDB extension loader with WASM-specific `LINKED_LIBS` configuration

## Platform Requirements

**Development:**
- CMake 3.20+
- Rust 1.86+ (stable, due to argmin patch)
- C++ compiler supporting C++17 (GCC 14+ preferred on Linux due to symbol deduplication)
- Python 3.11-3.12
- DuckDB development headers (via submodule or v1.4.3 fetch)

**Production:**
- DuckDB 1.4.3 or later
- OpenSSL 1.1 or 3.x (static linking on Linux/Windows eliminates runtime .so/.dll deps)
- macOS: CoreFoundation, SystemConfiguration frameworks
- Windows: bcrypt library
- Linux: pthreads, dl, m (math) libraries

**WASM:**
- Emscripten SDK with `wasm32-unknown-emscripten` Rust target
- No OpenSSL (telemetry disabled)
- Rust std conditionally built with WASM support

---

*Stack analysis: 2026-08-20*
