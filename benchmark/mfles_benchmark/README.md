# MFLES Benchmark Suite

Comprehensive benchmarking comparing MFLES (Multiple Forecast Length Exponential Smoothing) implementations from **anofox-forecast** and **statsforecast**.

## Overview

This benchmark suite compares MFLES implementations on the M4 competition dataset:

- **anofox-forecast MFLES**: Enhanced implementation with gradient boosted decomposition, robust estimation, and configuration presets
- **statsforecast MFLES**: Standard MFLES implementation from Nixtla's statsforecast library

### Anofox MFLES Features

- **5-Component Decomposition**: Median baseline, robust trend, weighted seasonality, ES ensemble, exogenous factors
- **Robust Trend Methods**: OLS, Siegel Robust Regression, Piecewise Linear
- **Weighted Fourier Seasonality**: Automatic period detection with configurable orders
- **Multi-Alpha ES Ensemble**: Averages multiple exponential smoothing forecasts
- **Moving Window Medians**: Adaptive baseline using recent data
- **Configuration Presets**: Fast, Balanced, Accurate, Robust
- **AutoMFLES**: Automatic hyperparameter selection via cross-validation

## Requirements

1. **Build anofox-forecast extension**:
   ```bash
   cd /path/to/anofox-forecast
   make release
   ```

2. **Install Python dependencies** (handled by uv):
   - datasetsforecast
   - pandas
   - fire
   - statsforecast (for statsforecast MFLES comparison)

## Usage

All commands should be run from the `benchmark/` directory:

```bash
cd benchmark
```

### Run Full Benchmark

Runs all anofox MFLES variants and statsforecast MFLES, then evaluates:

```bash
export PATH="$HOME/.local/bin:$PATH"
uv run python mfles_benchmark/run_benchmark.py run --group=Daily
```

### Run Specific Models

```bash
# Run only anofox MFLES with Fast preset
uv run python mfles_benchmark/run_benchmark.py anofox --group=Daily --model=MFLES-Fast

# Run only anofox AutoMFLES
uv run python mfles_benchmark/run_benchmark.py anofox --group=Daily --model=AutoMFLES

# Run only statsforecast MFLES
uv run python mfles_benchmark/run_benchmark.py statsforecast --group=Daily
```

### Evaluate Existing Results

```bash
uv run python mfles_benchmark/run_benchmark.py eval --group=Daily
```

### Clean Results

```bash
uv run python mfles_benchmark/run_benchmark.py clean
```

## Available Models

### Anofox MFLES Variants

1. **anofox-MFLES-Fast**: Quick forecasting with minimal computation
   - 3 boosting rounds
   - Fourier order 3
   - 10 ES ensemble steps

2. **anofox-MFLES-Balanced** (Recommended default):
   - 5 boosting rounds
   - Fourier order 5
   - 20 ES ensemble steps

3. **anofox-MFLES-Accurate**: High accuracy with more computation
   - 10 boosting rounds
   - Fourier order 7
   - Siegel robust trend
   - 30 ES ensemble steps

4. **anofox-MFLES-Robust**: Maximum resistance to outliers
   - 7 boosting rounds
   - Fourier order 5
   - Siegel robust trend
   - Outlier capping enabled

5. **anofox-AutoMFLES**: Automatic hyperparameter optimization
   - Grid search over trend methods, Fourier orders, max rounds
   - Cross-validation based selection
   - Configurable CV strategy (rolling/expanding)

### Statsforecast MFLES

6. **statsforecast-MFLES**: Standard MFLES implementation from Nixtla
   - Configurable season length
   - Multi-core parallel processing
   - Standard exponential smoothing approach

## Dataset Groups

- **Daily**: 14-day horizon, seasonality=7 (weekly pattern)
- **Hourly**: 48-hour horizon, seasonality=24 (daily pattern)
- **Weekly**: 13-week horizon, seasonality=52 (yearly pattern)

## Evaluation Metrics

- **MASE** (Mean Absolute Scaled Error): Scaled against naive seasonal baseline
- **MAE** (Mean Absolute Error): Average forecast error
- **RMSE** (Root Mean Squared Error): Penalizes large errors

## Output Files

Results are saved to `mfles_benchmark/results/`:

- `anofox-{model}-{group}.parquet`: Anofox forecast values
- `anofox-{model}-{group}-metrics.parquet`: Anofox timing information
- `statsforecast-MFLES-{group}.parquet`: Statsforecast forecast values
- `statsforecast-MFLES-{group}-metrics.parquet`: Statsforecast timing information
- `evaluation-MFLES-{group}.parquet`: Aggregated performance metrics comparing all models

## Comparison with Other Methods

This benchmark suite focuses on comparing MFLES implementations. For comparisons with other forecasting methods, see:
- `arima_benchmark/` - ARIMA models
- `theta_benchmark/` - Theta methods
- `ets_benchmark/` - ETS models
- `baseline_benchmark/` - Naive methods

## Example Output

```
MFLES BENCHMARK RESULTS
================================================================================
                model  group      MASE      MAE     RMSE  time_seconds  series_count
statsforecast-MFLES  Daily     2.156   126.45   191.12         52.34           414
  anofox-MFLES-Fast  Daily     2.145   125.34   189.23         45.21           414
anofox-MFLES-Balanced  Daily     2.087   119.87   182.56         98.45           414
anofox-MFLES-Robust  Daily     2.098   121.45   185.34        156.89           414
anofox-MFLES-Accurate  Daily     2.054   117.32   179.12        245.67           414
anofox-AutoMFLES  Daily     2.041   116.78   178.45        312.34           414

🏆 Best Model: anofox-AutoMFLES
   MASE: 2.041
   Time: 312.34s
```

## Architecture

```
mfles_benchmark/
├── src/
│   ├── __init__.py
│   ├── data.py               # M4 data loading
│   ├── anofox_mfles.py       # Anofox MFLES benchmarks
│   ├── statsforecast_mfles.py # Statsforecast MFLES benchmarks
│   └── evaluation_mfles.py   # Metrics calculation and comparison
├── results/                   # Output directory (created automatically)
├── run_benchmark.py          # Main entry point
└── README.md                 # This file
```
