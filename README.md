# Anofox Forecast - Time Series Forecasting for DuckDB

[![License: BSL 1.1](https://img.shields.io/badge/License-BSL%201.1-blue.svg)](LICENSE)
[![DuckDB](https://img.shields.io/badge/DuckDB-1.4.2+-green.svg)](https://duckdb.org)
[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)]()


> [!IMPORTANT]
> This extension is in early development, so bugs and breaking changes are expected.
> Please use the [issues page](https://github.com/DataZooDE/anofox-forecast/issues) to report bugs or request features.

A time series forecasting extension for DuckDB with 31 models, data preparation, and analytics — all in pure SQL.


## ✨ Key Features

### 🎯 Forecasting (31 Models)
- **AutoML**: AutoETS, AutoARIMA, AutoMFLES, AutoMSTL, AutoTBATS
- **Statistical**: ETS, ARIMA, Theta, Holt-Winters, Seasonal Naive
- **Advanced**: TBATS, MSTL, MFLES (multiple seasonality)
- **Intermittent Demand**: Croston, ADIDA, IMAPA, TSB

### 📊 Complete Workflow
- **EDA**: 5 macros for data quality analysis
- **Data Preparation**: 12 macros for cleaning and transformation
- **Evaluation**: 12 metrics including coverage analysis
- **Seasonality Detection**: Automatic period identification
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


## Why a C++ Port?

We implemented time-series forecasting algorithms in C++ with native DuckDB
integration, drawing from multiple open-source implementations including
StatsForecast (Python) and various Rust libraries focused on financial analytics.

- **Zero Python overhead** - No subprocess calls, no serialization, pure native execution
- **Automatic parallelization** - DuckDB handles parallel execution across CPU cores natively
- **In-database forecasting** - Generate forecasts directly in SQL without moving data
- **Production-ready performance** - C++ speed with DuckDB's query optimization
- **Portability**: Run forecasts in DuckDB anywhere—including the browser via WASM (WebAssembly)—and from any language that supports DuckDB integration.

## Attribution

This extension includes C++ ports of algorithms from several open-source projects.
See [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES) for complete attribution and license information.


## Installation

### Community Extension

```sql
INSTALL anofox_forecast FROM community;
LOAD anofox_forecast;
```

### From Source

```bash
# Clone the repository with submodules
git clone --recurse-submodules https://github.com/DataZooDE/anofox-forecast.git
cd anofox-forecast

# Build the extension
make release

# The extension will be built to:
# build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension
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
    FROM TS_FORECAST_BY('m5_train', item_id, ds, y, 'SeasonalNaive', 28, {'seasonal_period': 7})
    UNION ALL
    SELECT *
    FROM TS_FORECAST_BY('m5_train', item_id, ds, y, 'Theta', 28, {'seasonal_period': 7})
    UNION ALL
    SELECT *
    FROM TS_FORECAST_BY('m5_train', item_id, ds, y, 'AutoARIMA', 28, {'seasonal_period': 7})
);

-- MAE and Bias of Forecasts
CREATE OR REPLACE TABLE evaluation_results AS (
  SELECT 
      item_id,
      model_name,
      TS_MAE(LIST(y), LIST(point_forecast)) AS mae,
      TS_BIAS(LIST(y), LIST(point_forecast)) AS bias
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
SELECT 
    product_id,
    TS_DETECT_SEASONALITY(LIST(amount ORDER BY date)) AS detected_periods
FROM sales_prepared
GROUP BY product_id;

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
| **Python** | ✅ | [Python Usage](guides/81_python_integration.md) |
| **R** | ✅ | [R Usage](guides/82_r_integration.md) |
| **Julia** | ✅ | [Julia Usage](guides/83_julia_integration.md) |
| **C++** | ✅ | [C++ Usage](guides/84_cpp_integration.md) |
| **Rust** | ✅ | [Rust Usage](guides/85_rust_integration.md) |
| **Node.js** | ✅ | Via DuckDB Node bindings |
| **Go** | ✅ | Via DuckDB Go bindings |
| **Java** | ✅ | Via DuckDB JDBC driver |

**See**: [Multi-Language Overview](guides/80_multi_language_overview.md) for polyglot workflows!

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
Single time series forecasting with automatic parameter validation.

```sql
TS_FORECAST(
    table_name: VARCHAR,
    date_col: DATE|TIMESTAMP|INTEGER,
    value_col: DOUBLE,
    method: VARCHAR,
    horizon: INTEGER,
    params: MAP
) → TABLE(
    forecast_step INTEGER,
    date_col DATE|TIMESTAMP|INTEGER,
    point_forecast DOUBLE,
    lower DOUBLE,
    upper DOUBLE,
    model_name VARCHAR,
    insample_fitted DOUBLE[],
    confidence_level DOUBLE
)
```

**Behavioral Notes**:
- Timestamp generation based on training data interval (configurable via `generate_timestamps`)
- Prediction intervals computed at specified confidence level (default 0.90)
- Optional in-sample fitted values via `return_insample: true`

#### TS_FORECAST_BY
Multiple time series forecasting with native DuckDB GROUP BY parallelization.

```sql
TS_FORECAST_BY(
    table_name: VARCHAR,
    group_col: ANY,
    date_col: DATE|TIMESTAMP|INTEGER,
    value_col: DOUBLE,
    method: VARCHAR,
    horizon: INTEGER,
    params: MAP
) → TABLE(
    group_col ANY,
    forecast_step INTEGER,
    date_col DATE|TIMESTAMP|INTEGER,
    point_forecast DOUBLE,
    lower DOUBLE,
    upper DOUBLE,
    model_name VARCHAR,
    insample_fitted DOUBLE[],
    confidence_level DOUBLE
)
```

**Behavioral Notes**:
- Automatic parallelization: series distributed across CPU cores
- Group column type preserved in output
- Independent parameter validation per series

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

All metrics accept DOUBLE[] arrays and return DOUBLE. Use with GROUP BY via LIST() aggregation:

```sql
TS_MAE(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE              -- Mean Absolute Error
TS_MSE(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE              -- Mean Squared Error
TS_RMSE(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE             -- Root Mean Squared Error
TS_MAPE(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE             -- Mean Absolute Percentage Error
TS_SMAPE(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE            -- Symmetric MAPE
TS_MASE(actual DOUBLE[], predicted DOUBLE[], baseline DOUBLE[]) → DOUBLE   -- Mean Absolute Scaled Error
TS_R2(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE               -- R-squared
TS_BIAS(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE             -- Forecast Bias
TS_RMAE(actual DOUBLE[], pred1 DOUBLE[], pred2 DOUBLE[]) → DOUBLE -- Relative MAE
TS_QUANTILE_LOSS(actual DOUBLE[], predicted DOUBLE[], q DOUBLE) → DOUBLE -- Quantile Loss
TS_MQLOSS(actual DOUBLE[], quantiles DOUBLE[][], levels DOUBLE[]) → DOUBLE -- Mean Quantile Loss
TS_COVERAGE(actual DOUBLE[], lower DOUBLE[], upper DOUBLE[]) → DOUBLE -- Interval Coverage
```

**GROUP BY Usage Pattern**:
```sql
SELECT 
    product_id,
    TS_MAE(LIST(actual), LIST(predicted)) AS mae,
    TS_RMSE(LIST(actual), LIST(predicted)) AS rmse
FROM results
GROUP BY product_id;
```

### EDA Functions (5 macros)

SQL macros for exploratory data analysis and quality assessment:

```sql
TS_STATS(table, group_col, date_col, value_col)
-- Returns: per-series statistics (length, mean, std, min, max, nulls, zeros, plateau_size, n_zeros_start/end, expected_length)

TS_QUALITY_REPORT(stats_table, min_length)
-- Returns: quality assessment with configurable minimum length threshold

TS_STATS_SUMMARY(stats_table)
-- Returns: aggregate statistics across all series from TS_STATS output
```

### Data Quality Health Card (2 macros)

Comprehensive data quality assessment:

```sql
TS_DATA_QUALITY(table, unique_id_col, date_col, value_col, n_short)
-- Returns: comprehensive health card with metrics and values by dimension
-- Dimensions: Structural, Temporal, Magnitude, Behavioural
-- Parameters: n_short (optional, default 30) - threshold for short series detection

TS_DATA_QUALITY_SUMMARY(table, unique_id_col, date_col, value_col, n_short)
-- Returns: aggregated summary by dimension and metric
```

**Example Output**:
```
| unique_id | dimension    | metric           | value | value_pct |
|-----------|--------------|------------------|-------|-----------|
| Store_A   | Temporal     | timestamp_gaps   | 23    | 0.152     |
| Store_A   | Magnitude    | missing_values   | 13    | 0.085     |
| Store_B   | Temporal     | series_length    | 5     | NULL      |
```

### Data Preparation (12 macros)

SQL macros for data cleaning and transformation. Date type support varies by function.

**Gap Filling** (DATE/TIMESTAMP and INTEGER via function overloading):
```sql
TS_FILL_GAPS(table, group_col, date_col, value_col, frequency)
-- Fills missing timestamps/indices in series
-- Function overloading: Same function name works for both DATE/TIMESTAMP and INTEGER columns
-- For DATE/TIMESTAMP: frequency is VARCHAR ('30m', '1h', '1d', '1w', '1mo', '1q', '1y'), defaults to '1d'
-- For INTEGER: frequency is INTEGER (1, 2, 3, ...), defaults to 1
-- DuckDB automatically selects the correct overload based on frequency parameter type

TS_FILL_FORWARD(table, group_col, date_col, value_col, target_date, frequency)
-- Extends series to target date/index
-- Function overloading: Same function name works for both DATE/TIMESTAMP and INTEGER columns
-- For DATE/TIMESTAMP: frequency is VARCHAR ('30m', '1h', '1d', '1w', '1mo', '1q', '1y'), defaults to '1d'
-- For INTEGER: frequency is INTEGER (1, 2, 3, ...), defaults to 1
-- DuckDB automatically selects the correct overload based on frequency parameter type
```

**Filtering** (all date types):
```sql
TS_DROP_CONSTANT(table, group_col, value_col)
-- Removes series with constant values

TS_DROP_SHORT(table, group_col, date_col, min_length)
-- Removes series below minimum length

TS_DROP_GAPPY(table, group_col, date_col, max_gap_pct)
-- Removes series with excessive gaps (DATE/TIMESTAMP only)
-- INTEGER variant: TS_DROP_GAPPY_INT
```

**Edge Cleaning** (all date types):
```sql
TS_DROP_LEADING_ZEROS(table, group_col, date_col, value_col)
TS_DROP_TRAILING_ZEROS(table, group_col, date_col, value_col)
TS_DROP_EDGE_ZEROS(table, group_col, date_col, value_col)
```

**Imputation** (all date types):
```sql
TS_FILL_NULLS_CONST(table, group_col, date_col, value_col, fill_value)
TS_FILL_NULLS_FORWARD(table, group_col, date_col, value_col)
TS_FILL_NULLS_BACKWARD(table, group_col, date_col, value_col)
TS_FILL_NULLS_MEAN(table, group_col, date_col, value_col)
```

### Seasonality & Changepoints

**Seasonality Detection**:
```sql
TS_DETECT_SEASONALITY(values DOUBLE[]) → INTEGER[]
-- Returns: detected seasonal periods sorted by strength

TS_ANALYZE_SEASONALITY(timestamps ANY[], values DOUBLE[]) → STRUCT
-- Returns: detailed seasonality analysis structure
```

**Changepoint Detection** (Bayesian Online Changepoint Detection):
```sql
TS_DETECT_CHANGEPOINTS(
    table VARCHAR,
    date_col DATE|TIMESTAMP,
    value_col DOUBLE,
    params MAP
) → TABLE(date_col, value_col, is_changepoint BOOLEAN, changepoint_probability DOUBLE)

TS_DETECT_CHANGEPOINTS_BY(
    table VARCHAR,
    group_col ANY,
    date_col DATE|TIMESTAMP,
    value_col DOUBLE,
    params MAP
) → TABLE(group_col, date_col, value_col, is_changepoint BOOLEAN, changepoint_probability DOUBLE)
```

**Parameters**:
- `hazard_lambda` (default: 250.0): Expected run length between changepoints
- `include_probabilities` (default: false): Compute Bayesian probabilities

**Behavioral Notes**:
- BOCPD algorithm detects level shifts, trend changes, variance shifts, regime changes
- Full parallelization on GROUP BY operations
- Normal-Gamma conjugate prior for probabilistic detection

### Scalability Characteristics

- ✅ **Millions of rows**: Columnar storage + streaming operations
- ✅ **Thousands of series**: Native DuckDB parallelization on GROUP BY
- ✅ **Large horizons**: Optimized forecasting algorithms with vectorization
- ✅ **Memory efficient**: ~1GB for 1M rows, 1K series
- ✅ **CPU utilization**: Automatic distribution across cores for multi-series forecasting


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

## 🌟 Features Roadmap

### Coming Soon
- [ ] More EDA & Data Preparation tools
- [ ] External regressors support
- [ ] Probabilistic forecasting (quantile regression)
- [ ] Cross-validation utilities

### Under Consideration
- [ ] Machine Learning
- [ ] Web UI for visualization

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
