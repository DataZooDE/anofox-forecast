# anofox-time

<div align="center">

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)](https://github.com/yourusername/anofox-time)
[![License](https://img.shields.io/badge/license-BSL--1.0-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![CMake](https://img.shields.io/badge/CMake-3.21+-blue.svg)](https://cmake.org/)

**Lightning-fast time series forecasting with state-of-the-art statistical and econometric models**

</div>

---

## Table of Contents

* [Installation](#installation)
* [Quick Start](#quick-start)
* [Models](#models)
* [Features](#features)
* [Examples](#examples)
* [API Reference](#api-reference)
* [Documentation](#documentation)
* [How to Contribute](#how-to-contribute)
* [License](#license)
* [Citation](#citation)

---

## Installation

### Requirements

- C++17 compiler (GCC 10+, Clang 12+, or MSVC 2019+)
- CMake ≥ 3.21
- vcpkg for dependency management

### Building from source

```bash
# Clone the repository
git clone https://github.com/yourusername/anofox-time.git
cd anofox-time

# Configure with vcpkg
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DBUILD_EXAMPLES=ON \
  -DBUILD_TESTING=ON

# Build
cmake --build build -j

# Run tests
ctest --test-dir build --output-on-failure
```

---

## Quick Start

### Simple forecasting pipeline

```cpp
#include "anofox-time/quick.hpp"
#include <iostream>
#include <vector>

int main() {
    // Your time series data
    std::vector<double> history = {112, 118, 132, 129, 121, 135, 148, 148, 136, 119, 104, 118};
    
    // Configure auto-selection
    anofoxtime::quick::AutoSelectOptions opts;
    opts.horizon = 12;
    opts.sma_windows = {3, 6, 12};
    
    // Automatically select best model
    auto result = anofoxtime::quick::autoSelect(history, opts);
    
    std::cout << "Best model: " << result.model_name << "\n";
    std::cout << "MAE: " << result.best_score << "\n";
    
    return 0;
}
```

### Advanced pipeline with preprocessing

```cpp
#include "anofox-time/quick.hpp"
#include "anofox-time/transform/transformers.hpp"

std::vector<double> history = {/* your data */};
std::vector<double> actual = {/* validation data */};

anofoxtime::quick::AutoSelectOptions opts;
opts.horizon = 12;
opts.actual = actual;
opts.sma_windows = {6, 12};

// Add preprocessing pipeline
opts.pipeline_factory = [] {
    using namespace anofoxtime::transform;
    std::vector<std::unique_ptr<Transformer>> stages;
    stages.emplace_back(std::make_unique<StandardScaler>());
    stages.emplace_back(std::make_unique<YeoJohnson>());
    return std::make_unique<Pipeline>(std::move(stages));
};

auto result = anofoxtime::quick::autoSelect(history, opts);
std::cout << "Best model: " << result.model_name << "\n";
```

---

## Models

anofox-time includes a comprehensive collection of statistical and econometric forecasting models:

### Baseline & Statistical Models
- **Naive, SeasonalNaive**: Simple baseline forecasters
- **SMA** (Simple Moving Average): Smoothing-based forecasting
- **SES** (Simple Exponential Smoothing): Level-only exponential smoothing
- **Holt**: Linear trend exponential smoothing
- **Holt-Winters**: Seasonal exponential smoothing

### State Space Models
- **ETS** (Error, Trend, Seasonality): Automatic exponential smoothing
- **AutoETS**: Automatic model selection for ETS
- **MSTL** (Multiple Seasonal-Trend decomposition using LOESS): Complex seasonality decomposition
- **AutoMSTL**: Automatic MSTL configuration

### ARIMA Family
- **ARIMA**: Auto-Regressive Integrated Moving Average
- **SARIMA**: Seasonal ARIMA
- **AutoARIMA**: Automatic ARIMA model selection

### Theta Methods
- **Theta**: Classic Theta method
- **OptimizedTheta**: Parameter-optimized Theta
- **DynamicTheta**: Dynamic parameter Theta
- **DynamicOptimizedTheta**: Fully optimized dynamic Theta

### Complex Seasonality
- **MFLES** (Multi-Frequency Level Exponential Smoothing): Multiple seasonal patterns
- **AutoMFLES**: Automatic MFLES configuration
- **TBATS** (Trigonometric, Box-Cox transform, ARMA errors, Trend, and Seasonal components)
- **AutoTBATS**: Automatic TBATS configuration

### Intermittent Demand
- **Croston** (Classic, Optimized, SBA): Intermittent demand forecasting
- **TSB** (Teunter-Syntetos-Babai): Advanced intermittent forecasting
- **ADIDA**: Aggregate-Disaggregate Intermittent Demand Approach
- **IMAPA**: Intermittent Multiple Aggregation Prediction Algorithm

### Volatility & Ensembles
- **GARCH**: Generalized AutoRegressive Conditional Heteroskedasticity
- **Ensemble**: Model combination with multiple strategies

---

## Features

### 🚀 Production-Ready
- High-performance C++17 implementation
- Thoroughly tested with 417+ unit and integration tests
- Zero-copy operations for maximum efficiency

### 📊 Comprehensive Diagnostics
- **Anomaly Detection**: MAD-based point anomaly detection
- **Changepoint Detection**: BOCPD (Bayesian Online Changepoint Detection)
- **Outlier Detection**: DBSCAN-based segment screening
- **Validation**: Rolling cross-validation and backtesting

### 🔧 Composable Transformers
- **Scaling**: StandardScaler, MinMaxScaler
- **Power Transforms**: BoxCox, YeoJohnson
- **Imputation**: Linear interpolation for missing values
- **Pipeline**: Chain multiple transformers with automatic inverse transforms

### 🎯 Auto-Selection
- Automatic model selection based on validation metrics
- Support for custom preprocessing pipelines
- Configurable candidate model sets

### 📈 Similarity Search
- **DTW** (Dynamic Time Warping): Time series distance computation
- Distance matrix construction for clustering

---

## Examples

The `examples/` directory contains comprehensive demonstrations:

- **`pipeline_forecasting.cpp`**: End-to-end forecasting pipeline with preprocessing
- **`monitoring_workflow.cpp`**: Anomaly and changepoint detection workflow
- **`auto_arima_example.cpp`**: Automatic ARIMA model selection
- **`theta_example.cpp`**: Theta method variants
- **`mfles_example.cpp`**: Multiple frequency exponential smoothing
- **`mstl_example.cpp`**: Multiple seasonal decomposition
- **`tbats_example.cpp`**: TBATS model with complex seasonality
- **`intermittent_example.cpp`**: Intermittent demand forecasting
- **`ensemble_example.cpp`**: Model ensemble strategies
- **`airpassengers_benchmark.cpp`**: Comprehensive benchmarking suite

### Running examples

```bash
# Build all examples
cmake --build build --target examples

# Run a specific example
./build/examples/pipeline_forecasting

# Run all examples
make -C build run-examples
```

---

## API Reference

### Core Components

#### `anofox-time/core/`
- **`TimeSeries`**: Time series data container with metadata
- **`Forecast`**: Forecast results with prediction intervals
- **`DistanceMatrix`**: Distance computation for similarity analysis

#### `anofox-time/models/`
All forecasting models implementing a common interface:
```cpp
class Forecaster {
public:
    virtual void fit(const TimeSeries& ts) = 0;
    virtual Forecast predict(size_t horizon) = 0;
    virtual std::string getName() const = 0;
};
```

#### `anofox-time/transform/`
- **`Transformer`**: Base class for data transformations
- **`Pipeline`**: Chain multiple transformers
- Stateless design with fit/transform/inverse operations

#### `anofox-time/quick.hpp`
High-level convenience functions:
- `autoSelect()`: Automatic model selection
- `rolling_backtest_arima()`: ARIMA backtesting
- `detect_outliers()`: Outlier detection
- `detect_changepoints()`: Changepoint detection

#### `anofox-time/validation.hpp`
- Accuracy metrics (MAE, RMSE, MAPE, SMAPE, MASE, R²)
- Cross-validation utilities
- Backtesting framework

---

## Documentation

### Building the project

```bash
# Standard build
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build

# With examples and tests
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DBUILD_EXAMPLES=ON \
  -DBUILD_TESTING=ON
cmake --build build -j
```

### Running tests

```bash
# All tests
ctest --test-dir build --output-on-failure

# Specific test
./build/anofox-time-tests "ARIMA"

# Verbose output
./build/anofox-time-tests --success --durations yes
```

### Code coverage

Generate code coverage reports using the Makefile:

```bash
# Build with coverage instrumentation, run tests, and generate HTML report
make coverage

# Clean coverage artifacts
make coverage-clean

# Open coverage report in browser
make coverage-open
```

The coverage report will be generated in `coverage_html/index.html`. The coverage build uses:
- `lcov` for coverage data collection
- `genhtml` for HTML report generation
- Coverage flags: `--coverage` (gcc/clang)

**Requirements:**
- `lcov` package must be installed (e.g., `sudo apt-get install lcov` or `sudo pacman -S lcov`)

### Project Structure

```
anofox-time/
├── include/anofox-time/     # Public headers
│   ├── core/                # Core data structures
│   ├── models/              # Forecasting models (35 models)
│   ├── transform/           # Data transformers
│   ├── detectors/           # Anomaly detection
│   ├── outlier/             # Outlier detection
│   ├── changepoint/         # Changepoint detection
│   ├── seasonality/         # Seasonality analysis
│   ├── clustering/          # Clustering algorithms
│   ├── selectors/           # Model selection
│   ├── utils/               # Utilities
│   ├── quick.hpp            # Convenience layer
│   └── validation.hpp       # Validation utilities
├── src/                     # Implementation files
├── tests/                   # Unit and integration tests
├── examples/                # Example programs
└── CMakeLists.txt           # Build configuration
```

---

## How to Contribute

We welcome contributions! Here's how to get started:

1. **Fork the repository** and clone your fork
2. **Create a feature branch**: `git checkout -b feature/your-feature-name`
3. **Make your changes** and add tests
4. **Build and test**: 
   ```bash
   cmake --build build -j
   ctest --test-dir build --output-on-failure
   ```
5. **Commit your changes**: Use descriptive commit messages
6. **Push to your fork**: `git push origin feature/your-feature-name`
7. **Submit a Pull Request**: Provide a clear description of your changes

### Development Guidelines

- Follow C++17 best practices
- Add unit tests for new functionality
- Update documentation for API changes
- Include benchmark results for performance improvements
- Use `clang-format` for code formatting

---

## License

This project is licensed under the Business Source License 1.1 (BSL-1.1) - see the [LICENSE](LICENSE) file for details.

**Key Points:**
- Free for evaluation, non-production experimentation, academic research, and development
- Will convert to Apache License 2.0 on January 1, 2027
- See LICENSE file for full details and terms

---

## Citation

If you use anofox-time in your research, please cite:

```bibtex
@software{anofox_time,
  author = {Your Name},
  title = {anofox-time: High-Performance Time Series Forecasting in C++},
  year = {2025},
  url = {https://github.com/yourusername/anofox-time},
  version = {1.0.0}
}
```

---

<div align="center">

**[⬆ back to top](#anofox-time)**

Made with ❤️ by the anofox-time contributors

</div>
