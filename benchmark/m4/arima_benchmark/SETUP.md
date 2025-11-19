# ARIMA Benchmark Setup Guide

## Current Status

✅ **Benchmark infrastructure complete and committed**
⚠️ **Extension build required before running benchmarks**

## What Was Created

A comprehensive ARIMA benchmark suite that replicates the [Nixtla statsforecast ARIMA benchmark](https://github.com/Nixtla/statsforecast/tree/main/experiments/arima) to compare anofox-forecast AutoARIMA with other implementations:

### Files Created

```
benchmark/arima_benchmark/
├── README.md                     # Comprehensive documentation
├── SETUP.md                      # This file
├── run_benchmark.py              # Main benchmark runner (CLI)
└── src/
    ├── __init__.py
    ├── data.py                   # M4 dataset loading
    ├── anofox.py                 # Anofox-forecast benchmark
    ├── statsforecast.py          # Statsforecast benchmark
    ├── pmdarima.py               # pmdarima benchmark
    └── evaluation.py             # Metrics & comparison
```

### Dependencies Installed

```toml
# benchmark/pyproject.toml
[project]
requires-python = ">=3.11,<3.13"
dependencies = [
    "duckdb>=1.4.1",
    "pandas>=2.2.0",
    "numpy>=1.26.0",
    "fire>=0.7.0",
    "tabulate>=0.9.0",
    "datasetsforecast>=0.0.8",  # For M4 dataset
]

[project.optional-dependencies]
comparison = [
    "statsforecast>=2.0.2",     # Nixtla's implementation
    "pmdarima>=2.0.4",          # Python auto_arima
    "prophet>=1.1.6",           # Facebook Prophet
]
```

## Build Issues Encountered

The extension build failed due to compiler/environment conflicts:

1. **Conda C++ Compiler**: Build uses conda's gcc 7.3.0 which lacks access to system OpenSSL headers
2. **OpenSSL Headers**: Available at `/usr/include/openssl/` but not in conda compiler's search path
3. **Solution Needed**: Either:
   - Install OpenSSL in conda environment
   - Configure build to use system compiler
   - Add include path to CMakeLists.txt

## How to Build the Extension

### Option 1: System Compiler (Recommended)

```bash
# Ensure you're in project root
cd /home/simonm/projects/duckdb/anofox-forecast

# Clean any partial builds
rm -rf build

# Use system compiler
export CC=/usr/bin/gcc
export CXX=/usr/bin/g++
make release
```

### Option 2: Install Dependencies in Conda

```bash
# If you want to use conda's compiler
conda install -c conda-forge openssl-devel eigen
make release
```

### Option 3: Direct CMake Configuration

```bash
rm -rf build
mkdir -p build/release
cd build/release

cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER=/usr/bin/gcc \
      -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
      -DEXTENSION_STATIC_BUILD=1 \
      ../../duckdb

cmake --build . --target anofox_forecast_extension
```

### Verify Build

```bash
# Extension should be at:
ls -lh build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension

# Test loading
./build/release/duckdb -c "
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';
SELECT 'Extension loaded! ✅' AS status;
"
```

## Running the Benchmark

Once the extension is built:

### Full Benchmark (All Models)

```bash
cd benchmark

# Run on Daily dataset (4,227 series, ~14 observations per series for forecast)
uv run python arima_benchmark/run_benchmark.py run --group=Daily

# Expected output:
# ================================================================================
# Running anofox on M4 Daily
# ================================================================================
# Loading M4 Daily data...
# Loaded 9,919,251 rows from 4,227 series
# Forecast horizon: 14, Seasonality: 7
# Running AutoARIMA forecasts...
# ✅ Forecast completed in XX.XX seconds
# ...
# (Similar for statsforecast and pmdarima)
# ...
# ================================================================================
# Evaluating ARIMA Benchmark Results - M4 Daily
# ================================================================================
# +---------------+--------+----------+-----------+----------+--------+
# | Model         | MASE   | MAE      | RMSE      | Time     | Series |
# +===============+========+==========+===========+==========+========+
# | anofox        | ?.???  | ????.??  | ?????.??  | ????s    | 4227   |
# | statsforecast | 3.260  | 1234.56  | 12345.67  | 1.41s    | 4227   |
# | pmdarima      | ?.???  | ????.??  | ?????.??  | ~120s    | 4227   |
# +---------------+--------+----------+-----------+----------+--------+
```

### Run Individual Models

```bash
# Test just anofox first
uv run python arima_benchmark/run_benchmark.py model anofox Daily

# Then compare with statsforecast
uv run python arima_benchmark/run_benchmark.py model statsforecast Daily

# And pmdarima
uv run python arima_benchmark/run_benchmark.py model pmdarima Daily
```

### Try Different Datasets

```bash
# Hourly (414 series, 48-hour forecast)
uv run python arima_benchmark/run_benchmark.py run --group=Hourly

# Weekly (359 series, 13-week forecast)
uv run python arima_benchmark/run_benchmark.py run --group=Weekly
```

## Expected Results

Based on the original Nixtla benchmark:

### Daily Dataset (4,227 series)
- **statsforecast**: MASE ~3.26, Time ~1.41s
- **R's auto.arima**: MASE ~4.46, Time ~1.81s
- **pmdarima**: MASE ~?, Time ~120s
- **anofox**: MASE ~?, Time ~? (To be determined!)

### Hourly Dataset (414 series)
- **statsforecast**: Best MASE, Time ~?
- **pmdarima**: Had issues in original benchmark

### Weekly Dataset (359 series)
- **statsforecast**: Competitive with R

## Results Location

After running:

```
benchmark/arima_benchmark/results/
├── anofox-Daily.csv              # Forecast points
├── anofox-Daily-metrics.csv      # Timing metrics
├── statsforecast-Daily.csv
├── statsforecast-Daily-metrics.csv
├── pmdarima-Daily.csv
├── pmdarima-Daily-metrics.csv
└── evaluation-Daily.csv          # Comparison table
```

## Interpreting Results

### MASE (Mean Absolute Scaled Error)
- **Lower is better**
- Scaled relative to naive seasonal forecast
- MASE < 1.0 means better than seasonal naive
- Industry standard for M4 competition

### Time
- Wallclock time to generate all forecasts
- Includes model selection and parameter optimization
- **anofox** should leverage DuckDB's parallelization
- **statsforecast** uses C++ with parallel processing
- **pmdarima** is single-threaded Python (slowest)

## Next Steps

1. **Build Extension**: Use one of the options above
2. **Run Quick Test**: `uv run python arima_benchmark/run_benchmark.py model anofox Daily`
3. **Full Benchmark**: `uv run python arima_benchmark/run_benchmark.py run --group=Daily`
4. **Analyze Results**: Check `results/evaluation-Daily.csv`
5. **Try Other Datasets**: Hourly and Weekly

## Troubleshooting

### "Extension not found"
```bash
# Make sure you built the extension
ls build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension

# If missing, rebuild:
make release
```

### "Module not found: datasetsforecast"
```bash
# Install dependencies
cd benchmark
uv sync --extra comparison
```

### Build fails with OpenSSL errors
```bash
# Check OpenSSL is installed
pacman -Q openssl

# If using conda compiler, install OpenSSL there too
conda install -c conda-forge openssl-devel

# Or use system compiler (see Option 1 above)
```

## Contact

For questions or issues:
- **GitHub Issues**: https://github.com/DataZooDE/anofox-forecast/issues
- **Documentation**: See `benchmark/arima_benchmark/README.md`
