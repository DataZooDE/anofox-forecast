# Anofox Forecast - Time Series Forecasting for DuckDB

[![License: BSL 1.1](https://img.shields.io/badge/License-BSL%201.1-blue.svg)](LICENSE)
[![DuckDB](https://img.shields.io/badge/DuckDB-1.4.2+-green.svg)](https://duckdb.org)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)]()
[![Technical Depth](https://img.shields.io/badge/Technical%20Depth-A%20(93%25)-brightgreen.svg)](#code-quality)
[![Code Health](https://img.shields.io/badge/Code%20Health-A--(%2090%25)-green.svg)](#code-quality)
[![Tests](https://img.shields.io/badge/Tests-138%20passed-brightgreen.svg)]()

<sub>Technical Depth and Code Health scores calculated using [PMAT](https://github.com/paiml/paiml-mcp-agent-toolkit)</sub>


> [!IMPORTANT]
> This extension is in early development, so bugs and breaking changes are expected.
> Please use the [issues page](https://github.com/DataZooDE/anofox-forecast/issues) to report bugs or request features.

A time series forecasting extension for DuckDB with 32 models, data preparation, and analytics — all in pure SQL.


## ✨ Key Features

### 🎯 Forecasting (32 Models)
- **AutoML**: AutoETS, AutoARIMA, AutoMFLES, AutoMSTL, AutoTBATS
- **Statistical**: ETS, ARIMA, Theta, Holt-Winters, Seasonal Naive
- **Advanced**: TBATS, MSTL, MFLES (multiple seasonality)
- **Intermittent Demand**: Croston, ADIDA, IMAPA, TSB
- **Exogenous Variables**: ARIMAX, ThetaX, MFLESX (external regressors support)

### 📊 Complete Workflow
- **EDA & Data Quality**: 5 functions (2 table functions, 3 macros) for exploratory analysis and data quality assessment
- **Data Preparation**: 12 macros for cleaning and transformation
- **Multi-Key Hierarchy**: 4 functions for combining, aggregating, and splitting hierarchical time series (region/store/item)
- **Cross-Validation & Backtesting**: Time series CV with expanding/fixed/sliding windows, gap, embargo, and variable horizon support
- **Conformal Prediction**: Distribution-free prediction intervals with guaranteed coverage probability
- **Evaluation**: 12 metrics including coverage analysis
- **Seasonality Detection**: Automatic period identification, seasonality classification, and peak detection
- **Changepoint Detection**: Regime identification with probabilities

### 🔢 Feature Calculation
- **76+ Statistical Features**: Extract comprehensive time series features for ML pipelines
- **GROUP BY & Window Support**: Native DuckDB parallelization for multi-series feature extraction
- **Flexible Configuration**: Select specific features, customize parameters, or use JSON/CSV configs
- **tsfresh-Compatible**: Compatible feature vectors for seamless integration with existing ML workflows (hctsa will come also)

### ⚡ Performance
- **Parallel**: Native DuckDB parallelization on GROUP BY
- **Scalable**: Handles millions of series
- **Memory Efficient**: Columnar storage, streaming operations
- **Native Rust Core**: High-performance native implementations for data preparation and forecasting

### 🎨 User-Friendly API
- **Zero Setup**: All macros load automatically
- **Consistent**: MAP-based parameters
- **Composable**: Chain operations easily
- **Multi-Language**: Use from Python, R, Julia, C++, Rust, and more!

## 📋 Table of Contents

- [Installation](#installation)
- [Multi-Language Support](#multi-language-support)
- [API Reference](#api-reference)
- [Guides](#guides)
- [Performance](#performance)
- [License](#license)


## Attribution

This extension uses the `anofox-forecast` Rust crate and implements algorithms from several open-source projects.
See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for complete attribution and license information.


## Installation

### Community Extension

```sql
INSTALL anofox_forecast FROM community;
LOAD anofox_forecast;
```

### From Source

```bash
# Clone the repository
git clone https://github.com/DataZooDE/anofox-forecast.git
cd anofox-forecast

# Build the extension (requires Rust toolchain and CMake)
make release

# The extension will be built to:
# build/extension/anofox_forecast/anofox_forecast.duckdb_extension
```

## 🚀 Quick Start on M5 Dataset

The forecast takes ~2 minutes on a Dell XPS 13. (You need DuckDB v1.4.2).

```sql
-- Load extension
LOAD httpfs;
LOAD anofox_forecast;

CREATE OR REPLACE TABLE m5 AS 
SELECT item_id, CAST(timestamp AS TIMESTAMP) AS ds, demand AS y FROM 'https://m5-benchmarks.s3.amazonaws.com/data/train/target.parquet'
ORDER BY item_id, timestamp;

CREATE OR REPLACE TABLE m5_train AS
SELECT * FROM m5 WHERE ds < DATE '2016-04-25';

CREATE OR REPLACE TABLE m5_test AS
SELECT * FROM m5 WHERE ds >= DATE '2016-04-25';

-- Perform baseline forecast and evaluate performance
CREATE OR REPLACE TABLE forecast_results AS (
    SELECT *
    FROM anofox_fcst_ts_forecast_by('m5_train', item_id, ds, y, 'SeasonalNaive', 28, {'seasonal_period': 7})
    UNION ALL
    SELECT *
    FROM anofox_fcst_ts_forecast_by('m5_train', item_id, ds, y, 'Theta', 28, {'seasonal_period': 7})
    UNION ALL
    SELECT *
    FROM anofox_fcst_ts_forecast_by('m5_train', item_id, ds, y, 'AutoARIMA', 28, {'seasonal_period': 7})
);

-- MAE and Bias of Forecasts
CREATE OR REPLACE TABLE evaluation_results AS (
  SELECT 
      item_id,
      model_name,
      anofox_fcst_ts_mae(LIST(y), LIST(point_forecast)) AS mae,
      anofox_fcst_ts_bias(LIST(y), LIST(point_forecast)) AS bias
  FROM (
      -- Join Forecast with Test Data
      SELECT 
          m.item_id,
          m.ds,
          m.y,
          n.model_name,
          n.point_forecast
      FROM forecast_results n
      JOIN m5_test m ON n.item_id = m.item_id AND n.date = m.ds
  )
  GROUP BY item_id, model_name
);

-- Summarise evaluation results by model
SELECT
  model_name,
  AVG(mae) AS avg_mae,
  STDDEV(mae) AS std_mae,
  AVG(bias) AS avg_bias,
  STDDEV(bias) AS std_bias
FROM evaluation_results
GROUP BY model_name
ORDER BY avg_mae;
```

## 🌍 Multi-Language Support

**Write SQL once, use everywhere!** The extension works from any language with DuckDB bindings.

| Language | Status | Guide |
|----------|--------|-------|
| **Python** | ✅ | [Python Usage](guides/81_python_integration.md) |
| **R** | ✅ | [R Usage](guides/82_r_integration.md) |
| **Julia** | ✅ | [Julia Usage](guides/83_julia_integration.md) |
| **C++** | ✅ | Via DuckDB C++ bindings |
| **Rust** | ✅ | Via DuckDB Rust bindings |
| **Node.js** | ✅ | Via DuckDB Node bindings |
| **Go** | ✅ | Via DuckDB Go bindings |
| **Java** | ✅ | Via DuckDB JDBC driver |

**See**: [Multi-Language Overview](guides/80_multi_language_overview.md) for polyglot workflows!

---

## 📚 API Reference

For complete function signatures, parameters, and detailed documentation, see the [API Reference](docs/API_REFERENCE.md).

### Documentation Structure

| Category | Description | Documentation |
|----------|-------------|---------------|
| **Getting Started** | Installation and first forecast | [Getting Started Guide](docs/guides/01-getting-started.md) |
| **Model Selection** | Choose the right model | [Model Selection Guide](docs/guides/02-model-selection.md) |
| **Cross-Validation** | Evaluate forecast accuracy | [Cross-Validation Guide](docs/guides/03-cross-validation.md) |

### API Documentation

| Topic | Description | Reference |
|-------|-------------|-----------|
| **Hierarchical Data** | Multi-key hierarchy functions | [02-hierarchical.md](docs/api/02-hierarchical.md) |
| **Statistics** | 34 statistical metrics, data quality | [03-statistics.md](docs/api/03-statistics.md) |
| **Data Preparation** | Cleaning, imputation, filtering | [04-data-preparation.md](docs/api/04-data-preparation.md) |
| **Period Detection** | Seasonality detection (12 methods) | [05-period-detection.md](docs/api/05-period-detection.md) |
| **Changepoint Detection** | Structural break detection | [06-changepoint-detection.md](docs/api/06-changepoint-detection.md) |
| **Forecasting** | 32 forecasting models | [07-forecasting.md](docs/api/07-forecasting.md) |
| **Cross-Validation** | Backtesting and CV functions | [08-cross-validation.md](docs/api/08-cross-validation.md) |
| **Evaluation Metrics** | 12 accuracy metrics | [09-evaluation-metrics.md](docs/api/09-evaluation-metrics.md) |
| **Conformal Prediction** | Distribution-free prediction intervals | [11-conformal-prediction.md](docs/api/11-conformal-prediction.md) |
| **Feature Extraction** | 117 tsfresh-compatible features | [20-feature-extraction.md](docs/api/20-feature-extraction.md) |

### Model Reference (32 Models)

| Category | Models | Reference |
|----------|--------|-----------|
| **Baseline** | Naive, SMA, SeasonalNaive, RandomWalkDrift | [baseline/](docs/reference/models/baseline/) |
| **Exponential Smoothing** | SES, Holt, HoltWinters, SeasonalES | [exponential-smoothing/](docs/reference/models/exponential-smoothing/) |
| **State Space** | ETS, ARIMA, AutoETS, AutoARIMA | [state-space/](docs/reference/models/state-space/) |
| **Theta** | Theta, OptimizedTheta, DynamicTheta, AutoTheta | [theta/](docs/reference/models/theta/) |
| **Multi-Seasonal** | MFLES, MSTL, TBATS (+ Auto variants) | [multi-seasonal/](docs/reference/models/multi-seasonal/) |
| **Intermittent Demand** | Croston, CrostonSBA, ADIDA, IMAPA, TSB | [intermittent/](docs/reference/models/intermittent/) |


## 📦 Development

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
- Eigen3 (linear algebra library)
- Make or Ninja (build system)

### Build from Source

```bash
# Clone with submodules
git clone --recurse-submodules https://github.com/DataZooDE/anofox-forecast.git
cd anofox-forecast

# Set up Git hooks (recommended)
./scripts/setup-hooks.sh

# Build (choose one)
make -j$(nproc)              # With Make
GEN=ninja make release       # With Ninja (faster)
```

### Code Quality

This project uses Git hooks to ensure code quality before commits:

- **cargo fmt** - Enforces consistent code formatting
- **cargo clippy** - Catches common mistakes and enforces best practices

Setup the hooks after cloning:
```bash
./scripts/setup-hooks.sh
```

To run checks manually:
```bash
cargo fmt --all           # Format code
cargo clippy --workspace  # Run linter
cargo test --workspace    # Run tests
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

## 🤝 Contributing

We welcome contributions! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.


## 📞 Support

- **Documentation**: [guides/](guides/)
- **Issues**: [GitHub Issues](https://github.com/DataZooDE/anofox-forecast/issues)
- **Discussions**: [GitHub Discussions](https://github.com/DataZooDE/anofox-forecast/discussions)
- **Email**: sm@data-zoo.de

## 🎓 Citation

If you use this extension in research, please cite:

```bibtex
@software{anofox_forecast,
  title = {Anofox Forecast: Time Series Forecasting for DuckDB},
  author = {Joachim Rosskopf, Simon Müller, DataZoo GmbH},
  year = {2025},
  url = {https://github.com/DataZooDE/anofox-forecast}
}
```

## 🏆 Acknowledgments

Built on top of:
- [DuckDB](https://duckdb.org) - Amazing analytical database
- [anofox-time](https://github.com/anofox/anofox-time) - Core forecasting library

Special thanks to the DuckDB team for making extensions possible!

---

**Made with ❤️ by the Anofox Team**

⭐ **Star us on GitHub** if you find this useful!

📢 **Follow us** for updates: [@datazoo](https://www.linkedin.com/company/datazoo/)

🚀 **Get started now**: `LOAD 'anofox_forecast';`
