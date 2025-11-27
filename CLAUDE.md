# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Anofox Forecast** is a production-grade DuckDB extension that provides time series forecasting capabilities with 31 models, data preparation, exploratory data analysis, and evaluation metrics—all directly from SQL. The codebase is primarily C++ with SQLLogicTest-based tests, built on top of the anofox-time forecasting library.

**Key capabilities:**
- 31 forecasting models (AutoETS, AutoARIMA, Theta, TBATS, MSTL, MFLES, and more)
- EDA macros (5 functions for data quality analysis and statistics)
- Data preparation macros (12 functions for cleaning, gap filling, and transformation)
- Evaluation metrics (12 metrics including MAE, RMSE, MAPE, coverage, bias)
- Seasonality detection (automatic period identification with strength analysis)
- Changepoint detection (Bayesian Online Changepoint Detection with probabilities)
- Time series features calculation (comprehensive feature extraction)

## Build & Development Commands

### Quick Build with Ninja (Recommended)

**Always use Ninja for builds to significantly speed up compilation:**
```bash
# Build debug version with Ninja (parallel compilation)
GEN=ninja make debug

# Build release version with Ninja
GEN=ninja make release

# Run tests
make test_debug
```

**Why Ninja?** Ninja is a build system designed for speed and parallelization:
- Automatically uses all CPU cores
- Incremental builds are much faster than make
- Reduces build time by 50-70% compared to default make
- Highly recommended for iterative development

### Standard Build Commands

Without Ninja (slower, but works everywhere):
```bash
make debug      # Debug build
make release    # Release build
```

**Note:** Use `make clean` seldomly, it leads to long build times and is often not necessary.

### Testing

**Full test suites:**
- `make test_debug` - Run all SQL tests (SQLLogicTests)
- `make test` - Run all tests (release mode)
- `./build/debug/test/unittest "[test_category]"` - Run SQL tests by category (e.g., `"[core]"`, `"[batch]"`, `"[changepoint]"`)

**SQL tests location:** `test/sql/` (various `.test` files organized by module: core, batch, changepoint, eda, data_prep)

## Quick Iteration Development Workflow

For rapid feature development and debugging, use the debug binary directly without running full test suites:

### Direct SQL Command Execution

After building with `make debug`, execute SQL commands instantly:

```bash
# Single SQL command (fastest iteration)
./build/debug/duckdb -s "SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 14, {'seasonal_period': 7})"

# Multiple commands (just example, in debug build, extension is statically linked)
./build/debug/duckdb -s "LOAD 'build/debug/extension/anofox_forecast/anofox_forecast.duckdb_extension'; SELECT 1"

# Interactive shell (for exploring)
./build/debug/duckdb

# Execute SQL from file
./build/debug/duckdb < test_query.sql
```

### Gaining Situational Awareness with DuckDB Switches

```bash
# Enable detailed query information
./build/debug/duckdb -s ".tables" # List all tables

# Get extension info
./build/debug/duckdb -s "SELECT * FROM duckdb_functions() WHERE function_name LIKE 'TS_%'"

# Check loaded extensions
./build/debug/duckdb -s "SELECT * FROM duckdb_extensions()"

# Profile query execution
./build/debug/duckdb -s ".mode line" -s "PRAGMA explain; SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 14, {})"

# Enable verbose output
./build/debug/duckdb -verbose -s "SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 14, {})"
```

### Iterative Development Cycle

1. **Make code changes** to source files
2. **Rebuild with Ninja** (fast): `GEN=ninja make debug`
3. **Test immediately**: `./build/debug/duckdb -s "YOUR_TEST_SQL"`
4. **Repeat** until working
5. **Run full test suite** when ready: `make test_debug`

**Key advantages:**
- No waiting for full test harness to load
- Direct output and error messages
- Faster iteration = faster development
- Instant feedback loop

### Important: Rebuild After Changes

**Critical:** After modifying C++ source code, you MUST rebuild:
```bash
# Code changed? Rebuild!
GEN=ninja make debug

# Only then run your test
./build/debug/duckdb -s "SELECT * FROM your_function()"
```

The debug binary (`./build/debug/duckdb`) contains your extension code. Changes are NOT automatically reflected until you rebuild. This is the trade-off for the speed benefit of debug builds.

### Example Workflow: Adding a New Forecasting Model

```bash
# 1. Edit source code to add new model
# File: src/model_factory.cpp, anofox-time/src/models/new_model.cpp
vim src/model_factory.cpp

# 2. Quick rebuild with Ninja
GEN=ninja make debug

# 3. Test immediately without full test suite
./build/debug/duckdb -s "SELECT * FROM TS_FORECAST('sales', date, amount, 'NewModel', 14, {'seasonal_period': 7})"

# 4. If working, run full tests
make test_debug

# 5. If all pass, commit
git add .
git commit -m "feat: add NewModel forecasting algorithm"
```

### Debugging with Query Plans

Use DuckDB's query planning and profiling for maximum visibility:

```bash
./build/debug/duckdb << EOF
PRAGMA explain;
SELECT * FROM TS_FORECAST_BY('sales', product_id, date, amount, 'AutoETS', 14, {'seasonal_period': 7});
EOF
```

This produces detailed query plans showing how DuckDB parallelizes forecasting across series.
For performance debugging, use `PRAGMA enable_profiling` to see execution times per series.

## Build System & Dependencies

This extension follows the **DuckDB Extension Template** and uses **extension-ci-tools** for build management. Understanding the build architecture is essential for development.

### DuckDB Build System Architecture

The project structure consists of:
- **DuckDB Core** (submodule at `./duckdb/`) - Compiled as part of the extension build
- **Extension CI Tools** (submodule at `./extension-ci-tools/`) - Shared makefile and CMake logic for all DuckDB extensions
- **This extension** (`src/`, `test/`, `CMakeLists.txt`, `extension_config.cmake`) - Extension-specific source and configuration

**Build Flow:**
1. `make debug/release` → runs `extension-ci-tools/makefiles/duckdb_extension.Makefile`
2. CI Makefile generates CMake commands with DuckDB source directory as the root (`-S duckdb/`)
3. CMakeLists.txt in DuckDB root loads `extension_config.cmake` to configure this extension
4. `extension_config.cmake` calls `duckdb_extension_load()` to integrate this extension into the DuckDB build

### Debug vs Release Build Linking

**Critical difference in linking strategy:**

- **Debug builds** (`make debug`): Extension is **statically linked** into the DuckDB executable
  - Binary: `./build/debug/duckdb` (standalone executable with extension baked in)
  - Pros: Single binary, no loader needed, easier to debug
  - Cons: Larger binary size

- **Release builds** (`make release`): Extension is **dynamically loaded** (NOT statically linked)
  - Binary: `./build/release/duckdb` (DuckDB shell)
  - Extension: `./build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension` (loadable module)
  - Configured via `DONT_LINK` flag in `extension_config.cmake`
  - Pros: Smaller binary, distributable extension artifact, standard for production
  - Cons: Requires extension loading at runtime: `LOAD 'path/to/anofox_forecast.duckdb_extension'`

**See extension_config.cmake:**
```cmake
duckdb_extension_load(anofox_forecast
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    LOAD_TESTS
)
```

### Dependency Management with VCPKG

This extension uses **VCPKG** for external C++ dependency management. VCPKG is managed externally; we do **NOT** store it in the repository.

**Required setup:**
```bash
# Set the VCPKG_ROOT environment variable pointing to your VCPKG installation
export VCPKG_ROOT=/path/to/vcpkg

# The Makefile uses this variable:
# VCPKG_TOOLCHAIN_PATH?=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake
```

**Current dependencies** (defined in `vcpkg.json`):
- `eigen3` - Linear algebra library (required for ARIMA models, optional otherwise)
- **anofox-time** (submodule) - Core forecasting library with 31 model implementations
- **LBFGSpp** (included in anofox-time/third_party) - Optimization library for model fitting

**Build flow:**
1. `make debug` reads `vcpkg.json` from project root
2. Passes VCPKG toolchain to CMake via `-DCMAKE_TOOLCHAIN_FILE='${VCPKG_TOOLCHAIN_PATH}'`
3. CMake uses VCPKG to fetch and build dependencies locally
4. Libraries are statically linked into the extension per `EXTENSION_STATIC_BUILD=1` flag

**Adding new dependencies:**
1. Add to `vcpkg.json` dependencies array
2. Add `find_package()` in `CMakeLists.txt`
3. Link via `target_link_libraries()` in CMakeLists.txt
4. Ensure dependency is open-source and compatible with BSL 1.1 license

### Build Configuration Details

The Makefile uses these key flags (from `extension-ci-tools/makefiles/duckdb_extension.Makefile`):

```makefile
# Static linking enabled by default
BUILD_FLAGS=-DEXTENSION_STATIC_BUILD=1

# Debug build command
debug:
    cmake -DCMAKE_BUILD_TYPE=Debug -S duckdb/ -B build/debug
    cmake --build build/debug --config Debug

# Release build command
release:
    cmake -DCMAKE_BUILD_TYPE=Release -S duckdb/ -B build/release
    cmake --build build/release --config Release
```

**Available build variants:**
- `debug` - Full debug info, no optimizations
- `release` - Optimized, stripped symbols
- `relassert` - Release optimization + assertions enabled (good for testing)
- `reldebug` - Release optimization + debug info (best for profiling)

### Build Performance Optimization

The build system supports **mold linker** for faster linking (macOS/Linux):
- Configured in `CMakeLists.txt` to auto-detect and use if available
- Can reduce link time by 50-70% compared to default linker
- Falls back to default linker if mold not found

## C++ Code Standards & Best Practices

**You must act as a Senior C++ Software Engineer and produce well-readable, maintainable, and high-quality code.**

### Memory Management
- **Never** use `malloc`/`free` or `new`/`delete`—these are code smells
- Use smart pointers: `duckdb::unique_ptr` (preferred) or `duckdb::shared_ptr` (only when necessary)
- Smart pointers prevent memory leaks and make ownership explicit
- Always use `duckdb::make_unique<T>()` instead of manual `new`

### Const Correctness & References
- Apply `const` liberally throughout code (const pointers, const member functions, const parameters)
- Prefer **const references** over pointers for function arguments
- Use const references for non-trivial objects: `const std::vector<T>&`, `const std::string&`
- Use `override` or `final` keyword explicitly when overriding virtual methods—don't repeat `virtual`

### Type Usage (DuckDB Style)
- Use fixed-width types: `uint8_t`, `int16_t`, `uint32_t`, `int64_t` instead of `int`, `long`, etc.
- Use `idx_t` instead of `size_t` for offsets, indices, and counts
- Never use `using namespace std;`—always fully qualify types or use specific imports

### Naming Conventions (Match DuckDB)

| Element | Style | Example |
|---------|-------|---------|
| Files (cpp/hpp) | lowercase_with_underscores | `forecast_table_function.cpp`, `model_factory.hpp` |
| Classes/Types | CamelCase (uppercase first letter) | `ForecastTableFunction`, `ModelFactory` |
| Enums | CamelCase | `enum class ForecastMethod { AutoETS, AutoARIMA }` |
| Functions/Methods | CamelCase (uppercase first letter) | `CreateForecastTableFunction()`, `ValidateParams()` |
| Member variables | snake_case (lowercase) | `model_name`, `is_initialized` |
| Function parameters | snake_case | `const std::string& table_name`, `idx_t horizon` |
| Constants/Macros | UPPER_CASE | `const int DEFAULT_HORIZON = 14;`, `#define MAX_SERIES_LENGTH 10000` |
| Local variables | snake_case | `int forecast_step = 0;` |
| Namespace | lowercase | `namespace duckdb { }` |

### Code Structure & Formatting

Follow the `.clang-format` and `.clang-tidy` configuration:
- **Indentation:** Tabs (width 4)
- **Line length limit:** 120 characters
- **Pointer alignment:** Right (e.g., `int* ptr` not `int *ptr`)
- **Opening braces:** Always required (even for single-line if/for/while)
- **No single-line functions:** Use `AllowShortFunctionsOnASingleLine: false`
- **Spacing in templates:** No spaces (e.g., `std::vector<int>` not `std::vector< int >`)

### SOLID Principles & Clean Code (Uncle Bob)

#### Single Responsibility Principle (SRP)
- Each class/function should have one reason to change
- Example: `ModelFactory` handles model creation only; don't mix parameter validation logic
- Split responsibilities: `ModelFactory` + `ParameterValidator` + `TimeSeriesBuilder`

#### Open/Closed Principle (OCP)
- Classes should be open for extension, closed for modification
- Use abstract base classes and inheritance (e.g., `ForecastingModel` interface in anofox-time)
- Avoid modifying existing classes when adding features; extend instead

#### Liskov Substitution Principle (LSP)
- Derived classes must be substitutable for base classes
- If inheriting from a base, don't break expected behavior
- Example: `AutoETS` should work anywhere `ForecastingModel` is expected

#### Interface Segregation Principle (ISP)
- Clients should not depend on interfaces they don't use
- Create focused, minimal interfaces rather than bloated ones
- Example: separate `IForecastingModel` from `IParameterValidator`

#### Dependency Inversion Principle (DIP)
- Depend on abstractions, not concrete implementations
- Inject dependencies rather than creating them internally
- Example: pass `ModelFactory` to constructor, don't create new one inside

#### Clean Code Practices
- **Meaningful names:** Use full words, not abbreviations (e.g., `authentication_token` not `auth_tok`)
- **Small functions:** Functions should do one thing; aim for 20-30 lines maximum
- **Error handling:** Use exceptions or Result types, not error codes
- **Comments:** Only explain "why," never "what" (code should be self-explanatory)
- **No magic numbers:** Use named `constexpr` for all constants
- **DRY (Don't Repeat Yourself):** Extract common logic into shared functions
- **Early returns:** Use guard clauses to avoid deep nesting

**Example of clean code structure:**
```cpp
// Avoid
if (user_authenticated) {
    if (has_permission) {
        if (data_valid) {
            ProcessData();
        }
    }
}

// Prefer (early return)
if (!user_authenticated) { return; }
if (!has_permission) { return; }
if (!data_valid) { return; }
ProcessData();
```

### Code Reuse Strategy

Before writing new code:
1. **Search existing anofox_forecast code** for similar functionality
2. **Check DuckDB core utilities** (in `duckdb/src/`) for reusable patterns
3. **Browse other DuckDB extensions** (PostgreSQL Scanner, Parquet, JSON) for proven approaches
4. **Check anofox-time library** (in `anofox-time/src/`) for existing model implementations
5. **Minimize code footprint** by extracting and sharing utilities

Common reusable components in this extension:
- `ModelFactory` - All model creation should use this factory
- `TimeSeriesBuilder` - Time series conversion; reuse for all model inputs
- `ParameterValidator` - Parameter validation; extend for new model types
- `ForecastAggregate` - Aggregate function pattern; adapt for new aggregate functions
- DuckDB's `LIST()` aggregation - Use for collecting arrays for metrics functions

### Static Analysis & Linting

The project uses `clang-tidy` for static analysis. Key checks enabled:
- **Performance:** Flag inefficient patterns (`performance-*`)
- **Modernization:** Prefer C++11+ idioms (`modernize-*`)
- **Core guidelines:** Follow C++ core guidelines (`cppcoreguidelines-*`)
- **Readability:** Enforce naming, braces, identifier conventions (`readability-*`)
- **Bug prevention:** Flag likely bugs (`bugprone-*`)

**How to run locally:**
```bash
cd /path/to/anofox-forecast
clang-tidy -p build/debug src/**/*.cpp
```

### Example: Well-Structured C++ Class

```cpp
#pragma once

#include <memory>
#include <vector>
#include "model_factory.hpp"
#include "anofox_time_wrapper.hpp"

namespace duckdb {

// Forward declaration
class ForecastingModel;

// Abstract base for extensibility
class ParameterValidator {
public:
    virtual ~ParameterValidator() = default;
    virtual void Validate(const std::string& model_name, const std::map<std::string, duckdb::Value>& params) = 0;
};

// Concrete implementation
class ModelParameterValidator : public ParameterValidator {
public:
    explicit ModelParameterValidator(std::shared_ptr<ModelFactory> factory);

    // Override keyword required; no need to repeat virtual
    void Validate(const std::string& model_name, const std::map<std::string, duckdb::Value>& params) override;

private:
    // Private methods for internal logic
    void ValidateSeasonalPeriod(const duckdb::Value& period, idx_t series_length);

    // Member variables: snake_case, private section at bottom
    std::shared_ptr<ModelFactory> factory_;
    const idx_t min_series_length_ = 3;
};

} // namespace duckdb
```

## GitHub Actions CI/CD & Multi-Platform Builds

### Overview

This extension uses **extension-ci-tools** reusable workflows for automated multi-platform builds and distribution. The CI system builds extension binaries for Linux, macOS, and Windows through GitHub Actions.

**Primary workflow:** `.github/workflows/MainDistributionPipeline.yml`
- Calls `duckdb/extension-ci-tools/.github/workflows/_extension_distribution.yml@main`
- Builds for DuckDB v1.4.1 (configured via `duckdb_version` parameter)
- Generates platform-specific binaries as GitHub Actions artifacts
- Deploys to S3 for distribution

### Platform Matrix & Architecture Support

The build matrix is defined in `extension-ci-tools/config/distribution_matrix.json`:

| Platform | Architecture | Runner | VCPKG Triplet | CI Mode | Notes |
|----------|-------------|--------|---------------|---------|-------|
| **Linux** | linux_amd64 | ubuntu-24.04 | x64-linux-release | Reduced | Primary development platform |
| Linux | linux_arm64 | ubuntu-24.04-arm | arm64-linux-release | Full | Cross-compile or native ARM |
| Linux | linux_amd64_musl | ubuntu-24.04 | x64-linux-release | Full | Alpine/musl-libc compatibility |
| **macOS** | osx_amd64 | macos-latest | x64-osx-release | Full | Intel Mac support |
| **macOS** | osx_arm64 | macos-latest | arm64-osx-release | Reduced | Apple Silicon (primary dev) |
| **Windows** | windows_amd64 | windows-latest | x64-windows-static-md-release-vs2019comp | Reduced | MSVC build (critical for distribution) |
| Windows | windows_amd64_mingw | windows-latest | x64-mingw-static | Reduced | MinGW GCC alternative |

**Current configuration excludes** (see `exclude_archs` in MainDistributionPipeline.yml):
- `windows_amd64_rtools`, `windows_amd64_mingw` - Excluded for faster builds
- `wasm_mvp`, `wasm_eh`, `wasm_threads` - WebAssembly variants excluded
- `linux_arm64`, `linux_amd64_musl` - Excluded to reduce build time

### Platform-Specific Build Challenges

#### **Linux** (Primary Development Platform)
- **Build environment:** Docker containers with Ubuntu 24.04
- **Advantages:**
  - Fast builds on GitHub Actions
  - Consistent environment across runs
  - Native builds (no cross-compilation overhead)
  - ccache support for incremental builds
- **Challenges:**
  - Disk space management (uses `disk-cleanup` action)
  - Docker storage configuration for large builds
  - ARM64 builds require native ARM runners (expensive)
- **Dependencies:** VCPKG builds Eigen3 from source; anofox-time compiled as part of extension

#### **macOS** (Primary Development Platform)
- **Build environment:** Native macOS runners (macos-latest)
- **Advantages:**
  - Native compilation for both x86_64 and ARM64
  - Homebrew integration for build tools (ninja, ccache)
  - Python and toolchain management via actions/setup-*
- **Challenges:**
  - **Cross-architecture builds:** osx_amd64 may build on ARM runners (slower)
  - **Code signing:** Extensions may require developer signatures
  - **Runner costs:** macOS runners are 10x more expensive than Linux
  - **VCPKG compatibility:** Some packages require macOS-specific patches
  - **Linker differences:** Mach-O linking vs ELF (Linux) or PE (Windows)
- **Performance:** Builds typically take 10-15 minutes per architecture

#### **Windows** (Critical for Distribution)
- **Build environment:** Native Windows runners with MSVC 2019/2022
- **Advantages:**
  - MSVC produces optimized Windows binaries
  - Native Windows API support
  - VS2019 compatibility triplet ensures broader Windows support
- **Challenges:**
  - **Toolchain complexity:** MSVC vs MinGW vs rtools configurations
  - **Path handling:** Windows paths, backslashes, and Unix-style Makefile incompatibilities
  - **Dependency builds:** VCPKG on Windows is slower (no Docker caching)
  - **Static linking:** Windows requires careful static linking to avoid DLL dependencies
  - **CMake generators:** Must use correct generator (Ninja, Visual Studio, or NMake)
  - **Line endings:** CRLF vs LF can cause issues with scripts
  - **Case sensitivity:** Windows filesystem is case-insensitive (can hide bugs)
- **Performance:** Windows builds are typically slowest (15-25 minutes)
- **Testing:** Tests may be skipped on Windows (see `SKIP_TESTS=1` in Makefile)

**Why Windows is critical for distribution:**
- Large user base requires native Windows binaries
- Users expect seamless `INSTALL anofox_forecast FROM community` experience
- No cross-compilation from Linux/macOS (Windows requires native builds)
- MSVC compatibility ensures wide Windows version support (Windows 10+)

### Build Pipeline Stages

The CI workflow executes in these stages:

#### **1. Matrix Generation** (ubuntu-latest)
```
Parse distribution_matrix.json → Apply exclude_archs → Generate platform matrices
```
Produces separate matrices for Linux, macOS, Windows, and optionally WASM.

#### **2. Linux Build** (Docker-based)
```
Checkout repo → Setup Docker → Build in container → Test → Upload artifacts
```
- Uses ccache for compilation speed
- VCPKG dependencies cached in S3
- Produces `anofox_forecast-v1.4.1-extension-linux_amd64.duckdb_extension`

#### **3. macOS Build** (Conditional on Linux success)
```
Setup Homebrew → Install ninja/ccache → Configure VCPKG → Build → Test → Upload
```
- Builds both x86_64 and ARM64 variants
- Uses native runners (no emulation)
- Produces `anofox_forecast-v1.4.1-extension-osx_{amd64,arm64}.duckdb_extension`

#### **4. Windows Build** (Conditional on Linux success)
```
Setup MSVC → Configure VCPKG → Build with Ninja/MSBuild → Test → Upload
```
- Static linking to avoid DLL dependencies
- VS2019 compatibility mode for broader Windows support
- Produces `anofox_forecast-v1.4.1-extension-windows_amd64.duckdb_extension`

#### **5. Deployment** (S3 Upload)
```
Download artifacts → Configure AWS credentials → Upload to S3 bucket
```
- Deploys to custom S3 bucket: `DEPLOY_S3_BUCKET` variable
- Two upload modes: `latest` (overwrite) and `versioned` (permanent)
- Only triggers on `main` branch or tagged releases

### Diagnosing Build Failures with GitHub CLI

**Always use GitHub CLI (`gh`) to diagnose CI failures:**

```bash
# List recent workflow runs
gh run list --limit 10

# View specific run details
gh run view <RUN_ID>

# View run with logs
gh run view <RUN_ID> --log

# Filter by workflow
gh run list --workflow="MainDistributionPipeline.yml"

# Watch a running workflow
gh run watch <RUN_ID>

# Download artifacts from a run
gh run download <RUN_ID>

# Re-run failed jobs
gh run rerun <RUN_ID> --failed

# View job-specific logs
gh run view <RUN_ID> --job=<JOB_ID> --log
```

**Common failure patterns:**

1. **Linux build fails:**
   ```bash
   gh run view --log | grep -A 20 "linux_amd64"
   ```
   - Check VCPKG dependency compilation errors
   - Verify disk space (Docker storage issues)
   - Inspect CMake configuration output

2. **macOS build fails:**
   ```bash
   gh run view --log | grep -A 20 "osx_"
   ```
   - Check Homebrew installation logs
   - Verify cross-architecture configuration
   - Inspect linker errors (Mach-O format issues)

3. **Windows build fails:**
   ```bash
   gh run view --log | grep -A 20 "windows_amd64"
   ```
   - Check MSVC toolchain setup
   - Verify VCPKG triplet configuration
   - Inspect path-related errors (Windows vs Unix paths)

4. **Deployment fails:**
   ```bash
   gh run view --log | grep -A 20 "Deploy"
   ```
   - Check AWS credentials configuration
   - Verify S3 bucket permissions
   - Inspect artifact download step

### Git Commit Management by Agents

**Important:** When making commits, follow these practices:

#### **Commit Message Format**
Use conventional commit style WITHOUT AI attribution:

```bash
# Feature additions
git commit -m "feat: add new forecasting model (e.g., Prophet)"

# Bug fixes
git commit -m "fix: resolve AutoARIMA parameter validation edge case"

# Performance improvements
git commit -m "perf: optimize parallel forecasting for large batches"

# Refactoring
git commit -m "refactor: extract common time series validation logic"

# Documentation
git commit -m "docs: update build instructions for Windows"

# Tests
git commit -m "test: add coverage for changepoint detection probabilities"

# CI/CD changes
git commit -m "ci: exclude ARM64 builds to reduce CI time"
```

**DO NOT include:**
- ❌ "Generated with Claude Code"
- ❌ "AI-assisted changes"
- ❌ "Co-Authored-By: Claude"
- ❌ Any references to AI or automated tools in commit messages

**Why?** Commit messages should describe what changed and why, not how the change was made. The use of AI tools is an implementation detail, not relevant to the git history.

#### **Commit Management Commands**

```bash
# Stage and commit changes
git add src/new_feature.cpp src/include/new_feature.hpp
git commit -m "feat: implement new feature for XYZ"

# Amend last commit (only if not pushed)
git commit --amend -m "feat: implement feature with corrected logic"

# Create feature branch
git checkout -b feature/sac-integration
git commit -m "feat: add SAC catalog functions"
git push -u origin feature/sac-integration

# Check commit status before pushing
gh pr checks  # If on PR branch
git log --oneline -5  # Verify commit messages

# Push to remote
git push origin main
# Or for feature branch
git push origin feature/sac-integration
```

#### **Pull Request Creation**

Use GitHub CLI for PR creation:

```bash
# Create PR from current branch
gh pr create --title "feat: add Prophet forecasting model" \
             --body "Implements Prophet model with automatic seasonality detection.\n\n- Add Prophet model to model_factory\n- Add Prophet-specific parameter validation\n- Add comprehensive tests for Prophet\n- Update documentation"

# Create PR as draft
gh pr create --draft --title "wip: Prophet model integration" --body "Work in progress"

# View PR status
gh pr status

# View PR checks
gh pr checks

# Merge PR (after approval)
gh pr merge --squash  # Squash commits
gh pr merge --merge   # Regular merge
```

### Monitoring CI/CD Health

**Proactive monitoring:**

```bash
# Check latest main branch build
gh run list --branch main --limit 1

# Monitor all active runs
gh run list --status in_progress

# Check for recent failures
gh run list --status failure --limit 5

# View build duration trends
gh run list --limit 20 | awk '{print $6, $7, $8}'
```

**Performance benchmarks:**
- **Linux builds:** 8-12 minutes (target)
- **macOS builds:** 10-15 minutes per arch
- **Windows builds:** 15-25 minutes
- **Total pipeline:** ~30-45 minutes (parallel execution)

### Troubleshooting Common CI Issues

#### **Build Timeout**
```bash
# Check which job timed out
gh run view <RUN_ID> --log | grep -i "timeout"

# Solutions:
# 1. Exclude slow architectures (ARM64, musl)
# 2. Use ccache effectively
# 3. Reduce VCPKG dependency count
# 4. Split into multiple workflows
```

#### **Dependency Compilation Failures**
```bash
# Check VCPKG logs
gh run view <RUN_ID> --log | grep -A 50 "vcpkg install"

# Solutions:
# 1. Pin vcpkg_commit to known-good version
# 2. Check dependency compatibility with platform
# 3. Add vcpkg overlay ports if needed
# 4. Verify triplet configuration matches platform
```

#### **Test Failures**
```bash
# View test output
gh run view <RUN_ID> --log | grep -A 100 "unittest"

# Solutions:
# 1. Run tests locally: make test_debug
# 2. Check for platform-specific assumptions
# 3. Verify test fixtures are cross-platform
# 4. Use SKIP_TESTS if platform unsupported
```

### CI/CD Best Practices

1. **Test locally before pushing:**
   ```bash
   GEN=ninja make debug && make test_debug
   ```

2. **Use feature branches for complex changes:**
   ```bash
   git checkout -b feature/new-integration
   # Make changes, test locally
   git push -u origin feature/new-integration
   gh pr create
   ```

3. **Monitor build status:**
   ```bash
   gh run watch  # Watch current run
   ```

4. **Download and inspect artifacts:**
   ```bash
   gh run download <RUN_ID>
   ls -lh *.duckdb_extension
   ```

5. **Review platform-specific failures immediately:**
   - Windows failures often indicate path or linking issues
   - macOS failures may indicate code signing or cross-arch problems
   - Linux failures are usually dependency or disk space issues

## Extension Distribution & Community Integration

This extension follows **DuckDB community extension best practices**:

### Distribution Models
1. **Community Extensions Repository** (recommended)
   - Submit to https://github.com/duckdb/community-extensions
   - Enables installation via `INSTALL anofox_forecast FROM community`
   - Automatic CI/CD for all platforms
   - Recommended for long-term maintenance

2. **GitHub Releases**
   - Attach `.duckdb_extension` binaries to releases
   - Manual `INSTALL anofox_forecast FROM 'file://path'`
   - Suitable for early-stage/experimental extensions

3. **Custom Distribution**
   - Host on custom server or CDN
   - Requires `allow_unsigned_extensions` setting
   - Full control but higher maintenance burden

### Building for Distribution

Release builds create distributable artifacts:
```bash
make release
# Produces: ./build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension
```

**Distribution packages include:**
- Multiple platform binaries (linux_amd64, windows_amd64, osx_amd64, osx_arm64)
- Built via GitHub Actions or locally
- Tested across platforms before release

### CI/CD Best Practices from DuckDB Ecosystem

The extension-ci-tools repository provides centralized CI/CD:
- **Latest 2 DuckDB versions** actively supported
- **Multiple architecture builds:** linux_amd64, windows_amd64, osx_amd64, osx_arm64
- **Automatic updates** when DuckDB changes build system
- **Phased deprecation:** Old versions removed systematically

This project currently targets **DuckDB v1.4.1** (check `.github/workflows/` and `extension-ci-tools` branch).

## Codebase Architecture

### Extension Entry Point
- **src/anofox_forecast_extension.cpp** - Main extension initialization via `DUCKDB_EXTENSION_ENTRY`
- Registers all SQL functions, table macros, and aggregate functions
- Configures forecasting models, metrics, seasonality, and changepoint detection

### Core Module Organization

#### 1. **Forecasting Functions** (`forecast_table_function.*`, `forecast_aggregate.*`)
- **forecast_table_function.cpp/hpp** - Legacy `FORECAST()` table function (for compatibility)
- **forecast_aggregate.cpp/hpp** - `TS_FORECAST_AGG()` aggregate function (enables GROUP BY parallelization)
- Handles model selection, parameter validation, and result formatting
- Integrates with model_factory to dispatch to appropriate forecasting algorithm

#### 2. **Model Factory** (`model_factory.*`)
- **model_factory.cpp/hpp** - Central registry for 31 forecasting models
- Maps model names to anofox-time implementations
- Handles model-specific parameter validation and defaults
- Supports AutoML models (AutoETS, AutoARIMA, AutoMFLES, AutoMSTL, AutoTBATS)

#### 3. **Time Series Builder** (`time_series_builder.*`)
- **time_series_builder.cpp/hpp** - Converts DuckDB data to anofox-time time series format
- Handles date/timestamp conversion, gap detection, and data validation
- Supports DATE, TIMESTAMP, and INTEGER date types

#### 4. **Anofox Time Wrapper** (`anofox_time_wrapper.*`)
- **anofox_time_wrapper.cpp/hpp** - Bridge between DuckDB extension and anofox-time library
- Wraps anofox-time model interfaces for DuckDB integration
- Handles memory management and error translation

#### 5. **Metrics Functions** (`metrics_function.*`)
- **metrics_function.cpp/hpp** - 12 evaluation metrics (MAE, RMSE, MAPE, SMAPE, MASE, R², Bias, etc.)
- Scalar functions that accept DOUBLE[] arrays (use with LIST() aggregation)
- Supports coverage analysis for prediction intervals

#### 6. **Seasonality Detection** (`seasonality_function.*`)
- **seasonality_function.cpp/hpp** - Automatic seasonal period detection
- `TS_DETECT_SEASONALITY()` - Returns array of detected periods sorted by strength
- `TS_ANALYZE_SEASONALITY()` - Returns detailed seasonality analysis structure
- Uses FFT and autocorrelation analysis from anofox-time

#### 7. **Changepoint Detection** (`changepoint_function.*`)
- **changepoint_function.cpp/hpp** - Bayesian Online Changepoint Detection (BOCPD)
- `TS_DETECT_CHANGEPOINTS_AGG()` - Aggregate function for single series
- Detects level shifts, trend changes, variance shifts, and regime changes
- Supports probabilistic detection with confidence scores

#### 8. **EDA Macros** (`eda_macros.*`)
- **eda_macros.cpp/hpp** - 5 SQL macros for exploratory data analysis
- `TS_STATS()` - Per-series statistics (count, mean, std, min, max, nulls, gaps)
- `TS_QUALITY_REPORT()` - Quality assessment with configurable thresholds
- `TS_STATS_SUMMARY()` - Aggregate statistics across all series
- `()` - Identifies series below quality threshold
- `()` - Seasonality detection for all series

#### 9. **Data Preparation Macros** (`data_prep_macros.*`)
- **data_prep_macros.cpp/hpp** - 12 SQL macros for data cleaning and transformation
- Gap filling: `TS_FILL_GAPS()`, `TS_FILL_FORWARD()` (with INTEGER variants)
- Filtering: `TS_DROP_CONSTANT()`, `TS_DROP_SHORT()`, `TS_DROP_GAPPY()`
- Edge cleaning: `TS_DROP_LEADING_ZEROS()`, `TS_DROP_TRAILING_ZEROS()`, `TS_DROP_EDGE_ZEROS()`
- Imputation: `TS_FILL_NULLS_CONST()`, `TS_FILL_NULLS_FORWARD()`, `TS_FILL_NULLS_BACKWARD()`, `TS_FILL_NULLS_MEAN()`

#### 10. **Time Series Features** (`ts_features_function.*`)
- **ts_features_function.cpp/hpp** - Comprehensive time series feature extraction
- Aggregate/window function for calculating statistical, temporal, and spectral features
- Integrates with anofox-time feature calculators

#### 11. **Table Macros** (defined in `anofox_forecast_extension.cpp`)
- `TS_FORECAST()` - Single series forecasting (no GROUP BY)
- `TS_FORECAST_BY()` - Multiple series forecasting (1 group column)
- `TS_DETECT_CHANGEPOINTS()` - Single series changepoint detection
- `TS_DETECT_CHANGEPOINTS_BY()` - Multiple series changepoint detection
- All macros handle UNNEST internally for user-friendly table output

### Configuration & Extension Loading
- **extension_config.cmake** - Specifies extension loading and test loading
- **CMakeLists.txt** - C++ build configuration with Eigen3 dependency and anofox-time source inclusion
- External dependencies managed via VCPKG (Eigen3)
- anofox-time compiled as part of extension (submodule)

## Key Design Patterns

### 1. **Native DuckDB Parallelization**
Multi-series forecasting leverages DuckDB's GROUP BY parallelization. When using `TS_FORECAST_BY()`, DuckDB automatically distributes series across CPU cores, with each thread processing independent series in parallel. This provides linear speedup for large batches without explicit thread management.

### 2. **Model Factory Pattern**
The `ModelFactory` class provides a centralized registry for 31 forecasting models. New models are added by registering a factory function that creates model instances. This enables easy extension without modifying existing code.

### 3. **Aggregate Function Pattern**
`TS_FORECAST_AGG()` is implemented as an aggregate function, enabling native GROUP BY support. This allows DuckDB's query optimizer to parallelize forecasting across series automatically. The aggregate returns structured data (arrays) that are then UNNESTed by table macros.

### 4. **Table Macro Pattern**
User-facing functions like `TS_FORECAST()` and `TS_FORECAST_BY()` are implemented as table macros that wrap the aggregate function. Macros handle UNNEST operations internally, providing clean table output without requiring users to manually unnest arrays.

### 5. **Columnar Data Processing**
Time series data is processed in DuckDB's columnar format, enabling efficient streaming operations. The `TimeSeriesBuilder` converts columnar data to the format expected by anofox-time models, minimizing data copying and memory allocation.

## Common Development Tasks

### Adding a New Forecasting Model
1. Implement model in `anofox-time/src/models/` following existing model patterns
2. Add model registration in `src/model_factory.cpp` via `RegisterModel()`
3. Add parameter validation in `ModelFactory::ValidateParams()` for model-specific parameters
4. Add model to `CMakeLists.txt` ANOFOX_TIME_SOURCES list
5. Add SQL tests in `test/sql/core/test_all_models.test` or create model-specific test
6. Update documentation in `guides/` and `README.md` with model description and parameters
7. Follow C++ standards above (const correctness, smart pointers, naming conventions)

### Adding a New Evaluation Metric
1. Implement metric calculation in `src/metrics_function.cpp`
2. Add scalar function registration in `RegisterMetricsFunction()`
3. Ensure function accepts `DOUBLE[]` arrays (for use with LIST() aggregation)
4. Add SQL tests in `test/sql/core/` with various input scenarios
5. Document metric formula and usage in `guides/50_evaluation_metrics.md`
6. Consider edge cases (empty arrays, NaN values, division by zero)

### Adding a New EDA or Data Prep Macro
1. Implement macro SQL in `src/eda_macros.cpp` or `src/data_prep_macros.cpp`
2. Use `QUERY_TABLE()` to reference input table
3. Register macro in `RegisterEDAMacros()` or `RegisterDataPrepMacros()`
4. Add SQL tests in `test/sql/eda/` or `test/sql/data_prep/`
5. Handle edge cases (empty tables, NULL values, invalid date types)
6. Document macro in appropriate guide file

### Extending Seasonality Detection
1. Modify `anofox-time/src/seasonality/detector.cpp` for new detection algorithms
2. Update `src/seasonality_function.cpp` if new function signatures needed
3. Add tests for new detection methods
4. Consider performance implications for large datasets

### Adding New Changepoint Detection Algorithms
1. Implement algorithm in `anofox-time/src/changepoint/` (currently BOCPD)
2. Add aggregate function in `src/changepoint_function.cpp` if new algorithm type
3. Create table macro wrapper if user-facing function needed
4. Add comprehensive tests with known changepoint locations
5. Document algorithm parameters and behavior

## Testing Considerations

- SQL tests use SQLLogicTest format; see `test/README.md`
- Test files organized by module: `test/sql/core/`, `test/sql/batch/`, `test/sql/changepoint/`, `test/sql/eda/`, `test/sql/data_prep/`
- Forecasting model tests validate accuracy against known expected outputs
- Metric tests verify correctness with hand-calculated examples
- Data prep tests ensure transformations preserve data integrity
- Batch consistency tests verify parallel execution produces same results as sequential
- Use synthetic data generators for reproducible tests
- Test edge cases: empty series, constant series, series with gaps, very short series

## Current Modified Files (from git status)
- Check `git status` for current modified files
- Common areas of recent work: model implementations, feature calculations, parameter validation

## When Claude Code Gets Stuck: Consulting Codex

### Philosophy: AI Agent Collaboration

As an AI coding agent, you (Claude Code) have powerful capabilities but also limitations. When facing complex architectural decisions, ambiguous requirements, or deeply technical issues, **consult OpenAI Codex** for a second opinion.

This mirrors successful engineering practices: senior engineers consult each other when stuck. Two AI agents with different strengths can solve problems faster than either alone.

### When to Consult Codex

**Consult Codex when you encounter:**

1. **Architectural Ambiguity**
   - Multiple valid approaches with significant trade-offs
   - Design patterns that could affect long-term maintenance
   - Performance optimization requiring domain expertise
   - Example: "Should we use per-thread model instances or shared model instances for parallel forecasting?"

2. **Deep Technical Unknowns**
   - Behavior of external APIs not documented in training data
   - Platform-specific quirks (e.g., Windows VCPKG linking edge cases)
   - Complex C++ template metaprogramming issues
   - Example: "How does DuckDB's vector processing work with custom table functions?"

3. **Security or Correctness Critical Decisions**
   - Numerical stability in forecasting algorithms
   - Thread safety in parallel model execution
   - Memory management in large batch forecasting
   - Example: "Is it safe to share model instances across threads, or should each thread have its own?"

4. **Build System Configuration Issues**
   - VCPKG dependency resolution failures with unclear root cause
   - CMake configuration that works locally but fails in CI
   - Platform-specific compilation errors requiring deep toolchain knowledge
   - Example: "Why does Eigen3 link correctly on Linux but fail on Windows with MSVC?"

5. **Complex Debugging Scenarios**
   - Segmentation faults with unclear stack traces
   - Race conditions in multi-threaded code
   - Performance bottlenecks requiring profiling interpretation
   - Example: "Why does this code crash only on ARM64 but not x86_64?"

### How to Consult Codex

When stuck, prepare a context file and invoke Codex via bash:

#### **Step 1: Prepare Context File**

Create a consultation file in `.ai/codex_context/` with all relevant information:

```bash
# Create context directory if it doesn't exist
mkdir -p .ai/codex_context

# Write consultation request
cat > .ai/codex_context/$(date +%Y%m%d_%H%M%S)_parallel_forecasting.md << 'EOF'
# Codex Consultation: Parallel Forecasting Model Instance Strategy

## Context
Working on anofox_forecast DuckDB extension implementing parallel forecasting
for multiple time series. DuckDB automatically parallelizes GROUP BY operations,
and we need to decide how to manage forecasting model instances across threads.

## Current Implementation
File: src/forecast_aggregate.cpp
- Each series gets its own model instance created in aggregate function
- Models are created per-thread when DuckDB parallelizes GROUP BY
- No sharing of model instances across threads

## Problem
Not sure if we should share model instances (stateless models) or create
per-thread instances. Some models maintain internal state during fitting.

## Options Considered

### Option 1: Shared Model Instances (Stateless)
Single model instance shared across all threads with mutex protection
- Pros: Memory efficient, models are typically stateless after fitting
- Cons: Requires synchronization, potential contention

### Option 2: Per-Thread Model Instances
Each thread creates its own model instances
- Pros: No synchronization overhead, thread-safe by design
- Cons: More memory usage, model creation overhead

### Option 3: Model Instance Pool
Pool of model instances allocated per thread
- Pros: Balance between memory and performance
- Cons: Complex lifecycle management

## Question for Codex
Which approach aligns best with DuckDB's parallel execution model? How do
other DuckDB extensions handle stateful objects in parallel aggregates?

## Constraints
- Must be thread-safe (DuckDB uses parallel execution)
- Should follow RAII principles (no manual cleanup)
- Must not leak resources on query cancellation
- Should follow DuckDB extension best practices
- Models may have internal state during fitting phase

## Relevant Code Snippets

### Current Forecast Aggregate (src/forecast_aggregate.cpp)
```cpp
void TSForecastAggregate::Execute(DataChunk &args, AggregateState &state) {
    // Creates new model instance for each series
    auto model = ModelFactory::CreateModel(model_name, params);
    // ... fit and forecast
}
```

### DuckDB Aggregate State Pattern
```cpp
struct ForecastState : public AggregateState {
    unique_ptr<ForecastingModel> model;
    // Model lives for aggregate state lifetime
};
```

## References
- src/forecast_aggregate.cpp
- src/model_factory.cpp
- anofox-time/src/models/ (model implementations)
EOF
```

#### **Step 2: Invoke Codex**

Use Codex CLI to get consultation:

```bash
# Basic consultation (interactive)
codex chat --file .ai/codex_context/20250129_143022_connection_pooling.md

# Non-interactive consultation (get direct answer)
codex exec --file .ai/codex_context/20250129_143022_connection_pooling.md \
           --prompt "Based on this context, recommend the best approach and explain why." \
           > .ai/codex_context/20250129_143022_connection_pooling_response.md

# View the response
cat .ai/codex_context/20250129_143022_connection_pooling_response.md
```

#### **Step 3: Implement Based on Consultation**

After receiving Codex's response:

1. **Document the consultation**
   ```cpp
   // Using per-thread model instances (Option 2) per Codex consultation
   // Date: 2025-01-29 14:30
   // Rationale: Models may have internal state during fitting, per-thread
   // instances avoid synchronization overhead and ensure thread safety.
   // See: .ai/codex_context/20250129_143022_parallel_forecasting_response.md
   ```

2. **Implement the solution**
   ```cpp
   struct ForecastState : public AggregateState {
       unique_ptr<ForecastingModel> model;

       ForecastState() {
           // Each thread creates its own model instance
           model = ModelFactory::CreateModel(model_name, params);
       }
   };
   ```

3. **Reference in commit message**
   ```bash
   git commit -m "feat: implement per-thread model instances for parallel forecasting

   Use per-thread model instances per Codex consultation.
   This ensures thread safety and avoids synchronization overhead.

   Consultation: .ai/codex_context/20250129_143022_parallel_forecasting.md"
   ```

### Automation: Codex Consultation Helper Script

Create a helper script for quick consultations:

```bash
cat > scripts/consult_codex.sh << 'EOF'
#!/bin/bash
# Helper script to consult Codex when stuck

CONTEXT_DIR=".ai/codex_context"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

# Ensure context directory exists
mkdir -p "$CONTEXT_DIR"

# Get consultation topic
TOPIC="$1"
if [ -z "$TOPIC" ]; then
    echo "Usage: $0 <topic_slug>"
    echo "Example: $0 connection_pooling"
    exit 1
fi

CONTEXT_FILE="$CONTEXT_DIR/${TIMESTAMP}_${TOPIC}.md"
RESPONSE_FILE="$CONTEXT_DIR/${TIMESTAMP}_${TOPIC}_response.md"

# Check if Codex is available
if ! command -v codex &> /dev/null; then
    echo "Error: Codex CLI not found. Install with: npm install -g @openai/codex"
    exit 1
fi

echo "Context file created: $CONTEXT_FILE"
echo "Please fill in the context file, then run:"
echo "  codex exec --file $CONTEXT_FILE --prompt 'Analyze and recommend' > $RESPONSE_FILE"
EOF

chmod +x scripts/consult_codex.sh
```

Usage:
```bash
# Create context file template
./scripts/consult_codex.sh thread_safety

# Edit the generated file with your question
vim .ai/codex_context/20250129_150000_thread_safety.md

# Get Codex response
codex exec --file .ai/codex_context/20250129_150000_thread_safety.md \
           --prompt "Analyze the problem and recommend the best solution with rationale." \
           > .ai/codex_context/20250129_150000_thread_safety_response.md
```

### Codex Context Template

Standard template for context files:

```markdown
# Codex Consultation: [TOPIC]

## Context
[Brief description of what you're working on]

## Current Implementation
[Current approach, relevant file paths]

## Problem
[What you're stuck on, what you've tried]

## Options Considered
1. **Option A**: [Description]
   - Pros: ...
   - Cons: ...

2. **Option B**: [Description]
   - Pros: ...
   - Cons: ...

## Question for Codex
[Specific question requiring architectural/technical guidance]

## Constraints
- [Constraint 1]
- [Constraint 2]
- Must follow DuckDB extension best practices
- Must follow C++ code standards in this repo

## Relevant Code Snippets
[Include 10-30 lines of most relevant code]

## References
- [File paths]
- [Documentation links]
- [Similar extensions or patterns]
```

### Red Flags: When NOT to Consult Codex

**Don't consult for:**
- ❌ Syntax errors (you can fix these)
- ❌ Simple refactoring tasks
- ❌ Straightforward bug fixes with clear root cause
- ❌ Adding tests for existing functionality
- ❌ Documentation updates
- ❌ Formatting or style issues
- ❌ Tasks that can be solved by reading existing code

**Do consult for:**
- ✅ "Should we...?" architectural questions
- ✅ "How does X work in DuckDB?" when docs are unclear
- ✅ "What's the best practice for...?" design patterns
- ✅ "Why does this fail only on Windows?" platform mysteries
- ✅ "Is this approach thread-safe?" concurrency concerns
- ✅ Complex performance optimization decisions

### Effective Consultation Protocol

1. **Attempt resolution first** (15-20 minutes)
   - Search codebase for similar patterns
   - Review DuckDB core and other extensions
   - Check documentation and GitHub issues
   - Try 2-3 different approaches

2. **Prepare comprehensive context**
   - Document what you've tried
   - Include error messages
   - Note partial successes
   - List constraints clearly

3. **Formulate specific question**
   - Avoid "How do I do X?"
   - Prefer "Given constraints A, B, C, should I do X or Y?"
   - Include code snippets (10-30 lines)

4. **Invoke Codex non-interactively**
   ```bash
   codex exec --file .ai/codex_context/TIMESTAMP_topic.md \
              --prompt "Recommend the best approach with detailed rationale" \
              > .ai/codex_context/TIMESTAMP_topic_response.md
   ```

5. **Review and implement**
   - Read Codex's response carefully
   - Validate against project constraints
   - Document the decision in code
   - Reference consultation in commit

### Consultation Log

Maintain a log in `.ai/codex_consultations.md`:

```markdown
# Codex Consultations Log

## 2025-01-29 14:30: Parallel Forecasting Model Instance Strategy
**Question:** Per-thread vs shared model instances for parallel forecasting
**Codex Recommendation:** Per-thread model instances
**Rationale:** Models may have internal state during fitting, per-thread instances ensure thread safety without synchronization overhead
**Files Affected:** src/forecast_aggregate.cpp, src/model_factory.cpp
**Context:** .ai/codex_context/20250129_143022_parallel_forecasting.md
**Response:** .ai/codex_context/20250129_143022_parallel_forecasting_response.md

## 2025-01-30 09:15: Numerical Stability in ARIMA Fitting
**Question:** How to handle numerical instability in ARIMA parameter estimation with Eigen3
**Codex Recommendation:** Use QR decomposition with column pivoting and condition number checking
**Rationale:** ARIMA fitting involves matrix inversion which can be numerically unstable; QR decomposition is more stable
**Files Affected:** anofox-time/src/models/arima.cpp, anofox-time/src/optimization/
**Context:** .ai/codex_context/20250130_091500_arima_numerical_stability.md
**Response:** .ai/codex_context/20250130_091500_arima_numerical_stability_response.md
```

### Success Metrics

Good Codex consultation practices lead to:
- ✅ Fewer architectural reworks
- ✅ More maintainable code
- ✅ Better alignment with DuckDB patterns
- ✅ Clearer documentation of design decisions
- ✅ Faster resolution of complex issues
- ✅ Knowledge capture in consultation logs

### Example: Full Consultation Workflow

```bash
# 1. You're stuck on optimizing model selection for large datasets
mkdir -p .ai/codex_context

# 2. Create context file
cat > .ai/codex_context/20250130_091500_model_selection.md << 'EOF'
# Codex Consultation: Optimizing Model Selection for Large Datasets

## Context
Implementing automatic model selection in AutoETS. When forecasting thousands
of series, trying all ETS model variants becomes prohibitively slow. Need to
optimize the selection process without sacrificing accuracy.

## Problem
Current implementation tries all 30 ETS model variants for each series.
For 1000 series, this means 30,000 model fits. Even with parallelization,
this is too slow for production use.

## Options Considered
1. **Early stopping on AIC threshold** - Stop trying variants once AIC is below threshold
2. **Heuristic pre-filtering** - Use simple heuristics to eliminate unlikely models
3. **Cached model fits** - Cache fits for similar series (but series are usually unique)
4. **Reduced model space** - Only try most common variants (AAA, AAN, etc.)

## Question for Codex
What's the best strategy for optimizing model selection without sacrificing
forecast accuracy? Are there proven heuristics from time series literature?

## Constraints
- Must maintain forecast accuracy (can't sacrifice quality for speed)
- Should work with DuckDB's parallel execution model
- Must use C++17 (DuckDB requirement)
- Should be deterministic (same input produces same output)

## Code Snippet
```cpp
class AutoETS {
    std::string SelectBestModel(const std::vector<double>& data) {
        double best_aic = std::numeric_limits<double>::max();
        std::string best_model;
        
        for (const auto& variant : all_ets_variants) {
            auto model = FitETS(data, variant);
            if (model.aic < best_aic) {
                best_aic = model.aic;
                best_model = variant;
            }
            // OPTIMIZATION OPPORTUNITY HERE
        }
        return best_model;
    }
};
```

## References
- anofox-time/src/models/auto_ets.cpp
- StatsForecast Python implementation (reference)
- Hyndman & Athanasopoulos "Forecasting: principles and practice"
EOF

# 3. Consult Codex
codex exec --file .ai/codex_context/20250130_091500_model_selection.md \
           --prompt "Recommend optimization strategy with rationale from time series literature" \
           > .ai/codex_context/20250130_091500_model_selection_response.md

# 4. Review response
cat .ai/codex_context/20250130_091500_model_selection_response.md

# 5. Implement based on recommendation
# (Codex likely recommends early stopping or heuristic pre-filtering)

# 6. Log consultation
echo "## $(date +%Y-%m-%d\ %H:%M): Model Selection Optimization" >> .ai/codex_consultations.md
echo "**Codex Recommendation:** Early stopping with AIC threshold" >> .ai/codex_consultations.md
echo "**Files Affected:** anofox-time/src/models/auto_ets.cpp" >> .ai/codex_consultations.md
echo "" >> .ai/codex_consultations.md

# 7. Commit with reference
git add anofox-time/src/models/auto_ets.cpp .ai/codex_context/20250130_091500_model_selection*
git commit -m "perf: optimize AutoETS model selection with early stopping

Implement early stopping per Codex consultation.
Reduces model selection time by 60% without sacrificing accuracy.

Consultation: .ai/codex_context/20250130_091500_model_selection.md"
```

**Remember:** Consulting Codex when stuck is good engineering practice. Two AI perspectives are better than one, and documenting these consultations improves the project's knowledge base.

## Key References

- **README.md** - Complete feature documentation and SQL examples
- **docs/UPDATING.md** - Process for updating DuckDB version dependencies
- **.ai/README.md** - Extension analysis and GitHub Actions documentation
- **DuckDB Extension Template** - https://github.com/duckdb/extension-template
- **Extension CI Tools** - https://github.com/duckdb/extension-ci-tools (supports 2 latest DuckDB versions)
- **DuckDB CONTRIBUTING.md** - https://github.com/duckdb/duckdb/blob/main/CONTRIBUTING.md
- **Community Extensions List** - https://duckdb.org/community_extensions/list_of_extensions (reference implementations)
- **OpenAI Codex** - https://github.com/openai/codex (consult when stuck on complex problems)