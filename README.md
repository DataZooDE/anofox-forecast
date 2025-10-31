# Anofox Forecast - Time Series Forecasting for DuckDB

[![License: BSL 1.1](https://img.shields.io/badge/License-BSL%201.1-blue.svg)](LICENSE)
[![DuckDB](https://img.shields.io/badge/DuckDB-1.4.1+-green.svg)](https://duckdb.org)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)]()

Production-ready time series forecasting extension for DuckDB with 31 models, comprehensive data preparation, and advanced analytics—all in pure SQL.

## 🚀 Quick Start

```sql
-- Load extension
LOAD 'anofox_forecast.duckdb_extension';

-- Single series forecast
SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, {'seasonal_period': 7});

-- Multiple series with GROUP BY
SELECT * FROM TS_FORECAST_BY('sales', product_id, date, amount, 'AutoETS', 28, 
                             {'seasonal_period': 7, 'confidence_level': 0.95});
```

## ✨ Key Features

### 🎯 Forecasting (31 Models)
- **AutoML**: AutoETS, AutoARIMA, AutoMFLES, AutoMSTL, AutoTBATS
- **Statistical**: ETS, ARIMA, Theta, Holt-Winters, Seasonal Naive
- **Advanced**: TBATS, MSTL, MFLES (multiple seasonality)
- **Intermittent Demand**: Croston, ADIDA, IMAPA, TSB
- **Ensembles**: Combine multiple models

### 📊 Complete Workflow
- **EDA**: 5 macros for data quality analysis
- **Data Preparation**: 12 macros for cleaning and transformation
- **Evaluation**: 12 metrics including coverage analysis
- **Seasonality Detection**: Automatic period identification
- **Changepoint Detection**: Regime identification with probabilities

### ⚡ Performance
- **Fast**: 3-4x faster than Python/Polars for data prep
- **Parallel**: Native DuckDB parallelization on GROUP BY
- **Scalable**: Handles millions of series
- **Memory Efficient**: Columnar storage, streaming operations

### 🎨 User-Friendly API
- **Zero Setup**: All macros load automatically
- **Consistent**: MAP-based parameters
- **Composable**: Chain operations easily
- **Multi-Language**: Use from Python, R, Julia, C++, Rust, and more!

## 📋 Table of Contents

- [Installation](#installation)
- [Quick Examples](#quick-examples)
- [Multi-Language Support](#multi-language-support)
- [API Reference](#api-reference)
- [Guides](#guides)
- [Performance](#performance)
- [License](#license)

## 📦 Installation

### Prerequisites

Before building, install the required dependencies:

**Manjaro/Arch Linux**:
```bash
sudo pacman -S base-devel cmake ninja openssl eigen
```

**Ubuntu/Debian**:
```bash
sudo apt update
sudo apt install build-essential cmake ninja-build libssl-dev libeigen3-dev
```

**Fedora/RHEL**:
```bash
sudo dnf install gcc-c++ cmake ninja-build openssl-devel eigen3-devel
```

**macOS**:
```bash
brew install cmake ninja openssl eigen
```

**Windows** (Option 1 - vcpkg, recommended):
```powershell
# Install vcpkg
git clone https://github.com/Microsoft/vcpkg.git
.\vcpkg\bootstrap-vcpkg.bat

# Install dependencies
.\vcpkg\vcpkg install eigen3 openssl

# Build with vcpkg toolchain
cmake -DCMAKE_TOOLCHAIN_FILE=.\vcpkg\scripts\buildsystems\vcpkg.cmake .
cmake --build . --config Release
```

**Windows** (Option 2 - MSYS2/MinGW):
```bash
# In MSYS2 MinGW64 terminal
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
pacman -S mingw-w64-x86_64-openssl mingw-w64-x86_64-eigen3

# Then build as normal
make -j$(nproc)
```

**Windows** (Option 3 - WSL, easiest):
```bash
# Use Ubuntu in WSL
wsl --install
# Then follow Ubuntu instructions above
```

**Required**:
- C++ compiler (GCC 9+ or Clang 10+)
- CMake 3.15+
- OpenSSL (development libraries)
- Eigen3 (linear algebra library, required for ARIMA models)
- Make or Ninja (build system)

### Build from Source

```bash
# Clone with submodules
git clone --recurse-submodules https://github.com/DataZooDE/anofox-forecast.git
cd anofox-forecast

# Build (choose one)
make -j$(nproc)              # With Make
GEN=ninja make release       # With Ninja (faster)
```

### Verify Installation

```bash
# Test the extension
./build/release/duckdb -c "
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';
SELECT 'Extension loaded successfully! ✅' AS status;
"
```

### Load Extension

```sql
-- In DuckDB
LOAD 'path/to/anofox_forecast.duckdb_extension';

-- Verify all functions are available
SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 7, {'seasonal_period': 7});
```

## 🎓 Quick Examples

### Example 1: Simple Forecast

```sql
-- Create sample data
CREATE TABLE daily_sales AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 20 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 364) t(d);

-- Generate 28-day forecast
SELECT * FROM TS_FORECAST('daily_sales', date, sales, 'AutoETS', 28, 
                          {'seasonal_period': 7, 'confidence_level': 0.95});
```

### Example 2: Multiple Series

```sql
-- Forecast for multiple products
SELECT 
    product_id,
    date_col AS forecast_date,
    ROUND(point_forecast, 2) AS forecast,
    ROUND(lower, 2) AS lower_95,
    ROUND(upper, 2) AS upper_95
FROM TS_FORECAST_BY('product_sales', product_id, date, amount, 'AutoETS', 28,
                    {'seasonal_period': 7, 'confidence_level': 0.95})
WHERE forecast_step <= 7
ORDER BY product_id, forecast_step;
```

### Example 3: Complete Workflow

```sql
-- 1. Analyze data quality
CREATE TABLE sales_stats AS
SELECT * FROM TS_STATS('sales_raw', product_id, date, amount);

SELECT * FROM TS_QUALITY_REPORT('sales_stats', 30);

-- 2. Prepare data
CREATE TABLE sales_prepared AS
WITH filled AS (
    SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, amount)
)
SELECT * FROM TS_FILL_NULLS_FORWARD('filled', product_id, date, amount);

-- 3. Detect seasonality
SELECT * FROM TS_DETECT_SEASONALITY_ALL('sales_prepared', product_id, date, amount);

-- 4. Generate forecasts with diagnostics
CREATE TABLE forecasts AS
SELECT * FROM TS_FORECAST_BY('sales_prepared', product_id, date, amount, 
                             'AutoETS', 28,
                             {'seasonal_period': 7, 
                              'return_insample': true,
                              'confidence_level': 0.95});

-- 5. Evaluate accuracy (with actuals)
SELECT 
    product_id,
    TS_MAE(LIST(actual), LIST(forecast)) AS mae,
    TS_RMSE(LIST(actual), LIST(forecast)) AS rmse,
    TS_COVERAGE(LIST(actual), LIST(lower), LIST(upper)) * 100 AS coverage_pct
FROM results
GROUP BY product_id;
```

---

## 🌍 Multi-Language Support

**Write SQL once, use everywhere!** The extension works from any language with DuckDB bindings.

| Language | Status | Guide |
|----------|--------|-------|
| **Python** | ✅ | [Python Usage](guides/50_python_usage.md) |
| **R** | ✅ | [R Usage](guides/51_r_usage.md) |
| **Julia** | ✅ | [Julia Usage](guides/52_julia_usage.md) |
| **C++** | ✅ | [C++ Usage](guides/53_cpp_usage.md) |
| **Rust** | ✅ | [Rust Usage](guides/54_rust_usage.md) |
| **Node.js** | ✅ | Via DuckDB Node bindings |
| **Go** | ✅ | Via DuckDB Go bindings |
| **Java** | ✅ | Via DuckDB JDBC driver |

**See**: [Multi-Language Overview](guides/49_multi_language_overview.md) for polyglot workflows!

### Quick Examples

**Python** (pandas, matplotlib, FastAPI):
```python
forecast = con.execute("SELECT * FROM TS_FORECAST(...)").fetchdf()
```

**R** (tidyverse, ggplot2, Shiny):
```r
forecast <- dbGetQuery(con, "SELECT * FROM TS_FORECAST(...)")
```

**Julia** (DataFrames.jl, type-safe):
```julia
forecast = DataFrame(DBInterface.execute(con, "SELECT * FROM TS_FORECAST(...)"))
```

**C++** (embedded, high-performance):
```cpp
auto forecast = con.Query("SELECT * FROM TS_FORECAST(...)");
```

**Rust** (safe, async, web services):
```rust
let forecast = conn.prepare("SELECT * FROM TS_FORECAST(...)")?.query([])?;
```

**The SQL is identical across all languages!**

---

## 📚 API Reference

### Forecasting Functions

#### TS_FORECAST
Single time series forecasting.

```sql
TS_FORECAST(
    table_name: VARCHAR,
    date_col: DATE/TIMESTAMP,
    value_col: DOUBLE,
    method: VARCHAR,
    horizon: INT,
    params: MAP
) → TABLE
```

**Output**: forecast_step, date_col, point_forecast, lower, upper, model_name, insample_fitted, confidence_level

#### TS_FORECAST_BY
Multiple time series with GROUP BY.

```sql
TS_FORECAST_BY(
    table_name: VARCHAR,
    group_col: VARCHAR,
    date_col: DATE/TIMESTAMP,
    value_col: DOUBLE,
    method: VARCHAR,
    horizon: INT,
    params: MAP
) → TABLE
```

**Models** (31 total):
- AutoETS, AutoARIMA, AutoMFLES, AutoMSTL, AutoTBATS
- ETS, ARIMA, Theta, OptimizedTheta, DynamicTheta, DynamicOptimizedTheta
- Holt, HoltWinters, SES, SESOptimized
- SeasonalES, SeasonalESOptimized, SeasonalNaive, SeasonalWindowAverage
- TBATS, MSTL, MFLES
- Naive, RandomWalkDrift, SMA
- CrostonClassic, CrostonOptimized, CrostonSBA, ADIDA, IMAPA, TSB

**Common Parameters**:
```sql
{
    'seasonal_period': INT,           -- Seasonal period (e.g., 7 for weekly)
    'confidence_level': DOUBLE,       -- CI level (default: 0.90)
    'return_insample': BOOLEAN,       -- Return fitted values (default: false)
    ... model-specific parameters
}
```

### Evaluation Metrics (12 total)

```sql
TS_MAE(actual, predicted) → DOUBLE              -- Mean Absolute Error
TS_MSE(actual, predicted) → DOUBLE              -- Mean Squared Error
TS_RMSE(actual, predicted) → DOUBLE             -- Root Mean Squared Error
TS_MAPE(actual, predicted) → DOUBLE             -- Mean Absolute Percentage Error
TS_SMAPE(actual, predicted) → DOUBLE            -- Symmetric MAPE
TS_MASE(actual, predicted, baseline) → DOUBLE   -- Mean Absolute Scaled Error
TS_R2(actual, predicted) → DOUBLE               -- R-squared
TS_BIAS(actual, predicted) → DOUBLE             -- Bias
TS_RMAE(actual, pred1, pred2) → DOUBLE          -- Relative MAE
TS_QUANTILE_LOSS(actual, predicted, q) → DOUBLE -- Quantile Loss
TS_MQLOSS(actual, quantiles, levels) → DOUBLE   -- Mean Quantile Loss
TS_COVERAGE(actual, lower, upper) → DOUBLE      -- Interval Coverage
```

### EDA Functions (5 macros)

```sql
TS_STATS(table, group_col, date_col, value_col)           -- Comprehensive statistics
TS_QUALITY_REPORT(stats_table, min_length)                -- Quality checks
TS_DATASET_SUMMARY(stats_table)                           -- Overall summary
TS_GET_PROBLEMATIC(stats_table, quality_threshold)        -- Low quality series
TS_DETECT_SEASONALITY_ALL(table, group_col, date_col, value_col)  -- Seasonality
```

### Data Preparation (12 macros)

**Gap Filling**:
```sql
TS_FILL_GAPS(table, group_col, date_col, value_col)
TS_FILL_FORWARD(table, group_col, date_col, value_col, target_date)
```

**Filtering**:
```sql
TS_DROP_CONSTANT(table, group_col, value_col)
TS_DROP_SHORT(table, group_col, date_col, min_length)
TS_DROP_GAPPY(table, group_col, date_col, max_gap_pct)
```

**Edge Cleaning**:
```sql
TS_DROP_LEADING_ZEROS(table, group_col, date_col, value_col)
TS_DROP_TRAILING_ZEROS(table, group_col, date_col, value_col)
TS_DROP_EDGE_ZEROS(table, group_col, date_col, value_col)
```

**Imputation**:
```sql
TS_FILL_NULLS_CONST(table, group_col, date_col, value_col, fill_value)
TS_FILL_NULLS_FORWARD(table, group_col, date_col, value_col)
TS_FILL_NULLS_BACKWARD(table, group_col, date_col, value_col)
TS_FILL_NULLS_MEAN(table, group_col, date_col, value_col)
```

### Seasonality & Changepoints

```sql
TS_DETECT_SEASONALITY(values: DOUBLE[]) → INT[]
TS_ANALYZE_SEASONALITY(timestamps, values) → STRUCT

TS_DETECT_CHANGEPOINTS(table, date_col, value_col, params)
TS_DETECT_CHANGEPOINTS_BY(table, group_col, date_col, value_col, params)
```

## 📖 Guides

### Getting Started (2 guides)
- [Quick Start Guide](guides/01_quickstart.md) ⭐ - 5-minute introduction
- [Basic Forecasting](guides/03_basic_forecasting.md) - Complete workflow

### Technical Guides (4 guides)
- [API Reference](guides/10_api_reference.md) ⭐ - Complete API documentation
- [Model Selection](guides/11_model_selection.md) - Choosing the right model
- [Performance Tuning](guides/13_performance.md) - Optimization tips
- [EDA & Data Prep](guides/40_eda_data_prep.md) ⭐ - Data quality workflow

### Statistical Guides (1 guide)
- [Understanding Forecasts](guides/20_understanding_forecasts.md) - Statistical concepts

### Business Use Cases (3 guides)
- [Demand Forecasting](guides/30_demand_forecasting.md) ⭐ - Retail & inventory
- [Sales Prediction](guides/31_sales_prediction.md) - Revenue forecasting
- [Capacity Planning](guides/32_capacity_planning.md) - Resource allocation

### Multi-Language Guides (6 guides)
- [Multi-Language Overview](guides/49_multi_language_overview.md) ⭐ - Write once, use everywhere!
- [Python Usage](guides/50_python_usage.md) - pandas, FastAPI, Jupyter
- [R Usage](guides/51_r_usage.md) - tidyverse, ggplot2, Shiny
- [Julia Usage](guides/52_julia_usage.md) - DataFrames.jl, type-safe
- [C++ Usage](guides/53_cpp_usage.md) - Embedded, high-performance
- [Rust Usage](guides/54_rust_usage.md) - Safe, fast, production-ready

**Browse all**: [Complete Guide Index](guides/00_guide_index.md) - 17 guides, learning paths

## 🎯 Use Cases

### Retail & E-commerce
- **Demand Forecasting**: Predict product demand for inventory optimization
- **Sales Forecasting**: Revenue prediction across product lines
- **Promotions Impact**: Measure campaign effectiveness

### Operations
- **Capacity Planning**: Resource allocation and scheduling
- **Maintenance Prediction**: Preventive maintenance scheduling
- **Quality Control**: Process monitoring and anomaly detection

### Finance
- **Revenue Forecasting**: Financial planning and budgeting
- **Cash Flow Prediction**: Liquidity management
- **Cost Optimization**: Expense forecasting

### Healthcare
- **Patient Volume**: Hospital admissions forecasting
- **Resource Planning**: Staff and equipment allocation
- **Epidemic Modeling**: Disease spread prediction

## 📊 Performance

### Benchmarks

| Operation | Dataset | Python/Polars | DuckDB SQL | Speedup |
|-----------|---------|---------------|------------|---------|
| **Data Prep** | 1M rows, 1K series | 12s | 4s | 3x |
| **Per-series stats** | 1M rows, 10K series | 5s | 1.2s | 4x |
| **Forecast (AutoETS)** | 365 days, 1K series | ~120s | ~40s | 3x |
| **Gap filling** | 1M rows, 1K series | 2.5s | 0.8s | 3x |

*Benchmarks on Intel i7, 16GB RAM*

### Scalability

- ✅ **Millions of rows**: Columnar storage + streaming
- ✅ **Thousands of series**: Native parallelization
- ✅ **Large horizons**: Optimized forecasting algorithms
- ✅ **Memory efficient**: ~1GB for 1M rows, 1K series

## 🏗️ Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    DuckDB Extension API                     │
├─────────────────────────────────────────────────────────────┤
│  Forecasting  │    Metrics    │  EDA & Prep  │  Detection  │
│  (31 models)  │  (12 metrics) │  (17 macros) │ (Changepoint)│
├─────────────────────────────────────────────────────────────┤
│                   anofox-time Core Library                   │
│  • Statistical Models    • Optimization (L-BFGS, Nelder-Mead)│
│  • AutoML Selection      • SIMD Vectorization (AVX2)         │
│  • Time Series Utils     • Gradient Checkpointing            │
└─────────────────────────────────────────────────────────────┘
```

## 🔧 Development

### Documentation Build System

This project uses a template-based documentation system:

- **Templates**: Edit files in `guides/templates/*.md.in`
- **SQL Examples**: Separate SQL files in `test/sql/docs_examples/`
- **Generated Docs**: Built from templates via `make docs`

```bash
# Build documentation from templates
make docs

# Test SQL examples
make test-docs

# Lint documentation
make lint-docs

# Clean generated docs
make clean-docs

# Install git hooks (auto-build on commit)
make install-hooks
```

**Important**: Always edit template files (`*.md.in`) in `guides/templates/`, not the generated `*.md` files in `guides/`. The generated files are automatically rebuilt from templates.

### Build Options

```bash
# Debug build
make debug

# Release build with optimizations
make release

# Run tests
make test

# Clean build
make clean
```

### Project Structure

```
anofox-forecast/
├── src/                          # Extension source code
│   ├── forecast_aggregate.cpp   # Main forecasting logic
│   ├── metrics_function.cpp     # Evaluation metrics
│   ├── eda_macros.cpp           # EDA macros
│   ├── data_prep_macros.cpp     # Data preparation macros
│   └── ...
├── anofox-time/                  # Core forecasting library
│   ├── include/                  # Headers
│   └── src/                      # Implementation
├── examples/                     # SQL examples
├── guides/                       # User guides
├── test/                         # Tests
└── docs/                         # Documentation
```

## 📄 License

**Business Source License 1.1 (BSL 1.1)**

### Key Points

✅ **Free for production use** - Use internally in your business  
✅ **Free for development** - Build applications with it  
✅ **Free for research** - Academic and research use  

❌ **Cannot offer as hosted service** - No SaaS offerings to third parties  
❌ **Cannot embed in commercial product** - For third-party distribution  

🔄 **Converts to MPL 2.0** - After 5 years from first publication

See [LICENSE](LICENSE) for full terms.

### Commercial Licensing

For commercial licensing (hosted services, embedded products), contact: `license@anofox.com`

## 🤝 Contributing

We welcome contributions! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

### Development Setup

```bash
git clone --recurse-submodules https://github.com/DataZooDE/anofox-forecast.git
cd anofox-forecast
make -j$(nproc)
```

## 📞 Support

- **Documentation**: [guides/](guides/)
- **Issues**: [GitHub Issues](https://github.com/DataZooDE/anofox-forecast/issues)
- **Discussions**: [GitHub Discussions](https://github.com/DataZooDE/anofox-forecast/discussions)
- **Email**: support@anofox.com

## 🎓 Citation

If you use this extension in research, please cite:

```bibtex
@software{anofox_forecast,
  title = {Anofox Forecast: Time Series Forecasting for DuckDB},
  author = {Anofox Team},
  year = {2025},
  url = {https://github.com/DataZooDE/anofox-forecast}
}
```

## 🌟 Features Roadmap

### Coming Soon
- [ ] Machine learning 
- [ ] Probabilistic forecasting (quantile regression)
- [ ] Hierarchical reconciliation
- [ ] Cross-validation utilities
- [ ] Model explainability (SHAP values)

### Under Consideration
- [ ] Real-time forecasting updates
- [ ] External regressors support
- [ ] Causal impact analysis
- [ ] Web UI for visualization

## 📊 Stats

- **31 Models**: From simple to state-of-the-art
- **12 Metrics**: Comprehensive evaluation
- **17 Macros**: EDA + data preparation
- **60+ Functions**: Complete API
- **5,000+ Lines**: Production-ready code
- **100% SQL**: No Python dependencies

## 🏆 Acknowledgments

Built on top of:
- [DuckDB](https://duckdb.org) - Amazing analytical database
- [anofox-time](https://github.com/anofox/anofox-time) - Core forecasting library

Special thanks to the DuckDB team for making extensions possible!

---

**Made with ❤️ by the Anofox Team**

⭐ **Star us on GitHub** if you find this useful!

📢 **Follow us** for updates: [@anofox](https://twitter.com/anofox)

🚀 **Get started now**: `LOAD 'anofox_forecast.duckdb_extension';`
