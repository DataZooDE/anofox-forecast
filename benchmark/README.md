# Benchmark Suite

This directory contains performance benchmarks and validation tests for the anofox-forecast extension.

## Latest Results

**M4 Daily Dataset** (4,227 time series, 14-step forecast horizon):

### Complete Benchmark Results

All models benchmarked on M4 Daily dataset, grouped by method family and separated by implementation:

| Method Family | Implementation | Model | MASE | MAE | RMSE | Time (s) |
|---------------|----------------|-------|------|-----|------|----------|
| **Baseline** | Anofox | Naive | 1.153 | 180.83 | 212.00 | 0.24 |
| | Statsforecast | Naive | 1.153 | 180.83 | 212.00 | 3.17 |
| | Anofox | RandomWalkWithDrift | **1.147** | 183.48 | 215.10 | 0.25 |
| | Statsforecast | RandomWalkWithDrift | **1.147** | 183.48 | 215.10 | 4.78 |
| | Anofox | SeasonalNaive | 1.441 | 227.11 | 263.74 | 0.22 |
| | Statsforecast | SeasonalNaive | 1.452 | 227.12 | 262.16 | 3.51 |
| | Anofox | SeasonalWindowAverage | 1.961 | 300.48 | 326.69 | 0.26 |
| | Statsforecast | SeasonalWindowAverage | 2.209 | 334.23 | 359.39 | 6.20 |
| | Anofox | SMA | 1.343 | 209.01 | 237.98 | 0.22 |
| | Statsforecast | WindowAverage | 1.380 | 214.88 | 243.65 | 4.56 |
| **ETS** | Anofox | AutoETS | **1.148** | 175.79 | 207.48 | 556 |
| | Statsforecast | AutoETS | 1.227 | 188.14 | 227.63 | 3,179 |
| | Statsforecast | Holt | **1.132** | 172.86 | 204.44 | 154 |
| | Anofox | HoltWinters | 1.152 | 175.92 | 207.42 | 176 |
| | Statsforecast | HoltWinters | **1.148** | 177.14 | 208.90 | 1,094 |
| | Statsforecast | SeasonalESOptimized | 1.457 | 226.82 | 261.36 | 10 |
| | Anofox | SeasonalESOptimized | 1.203 | 186.67 | 218.23 | 9 |
| | Statsforecast | SeasonalES | 1.608 | 249.17 | 278.42 | 6 |
| | Statsforecast | SES | 1.231 | 191.79 | 222.13 | 3 |
| | Statsforecast | SESOpt | 1.154 | 178.32 | 209.79 | 6 |
| **Theta** | Statsforecast | AutoTheta | **1.149** | 178.15 | 209.60 | 2,327 |
| | Anofox | DynamicOptimizedTheta | 1.155 | 179.06 | 210.56 | 773 |
| | Statsforecast | DynamicOptimizedTheta | 1.156 | 178.97 | 210.52 | 612 |
| | Anofox | DynamicTheta | 1.226 | 191.41 | 221.94 | 14 |
| | Statsforecast | DynamicTheta | 1.153 | 178.83 | 210.33 | 472 |
| | Anofox | OptimizedTheta | **1.149** | 178.08 | 209.53 | 1,418 |
| | Statsforecast | OptimizedTheta | 1.151 | 178.44 | 209.91 | 751 |
| | Anofox | Theta | 1.226 | 191.46 | 222.00 | 19 |
| | Statsforecast | Theta | 1.154 | 178.85 | 210.36 | 512 |
| **ARIMA** | Anofox | AutoARIMA | 1.212 | 183.95 | 216.36 | 6.2 |
| | Statsforecast | AutoARIMA | **1.150** | 176.82 | 208.63 | 7,299 |
| **MFLES** | Anofox | MFLES | **1.179** | 181.62 | 212.87 | 21 |
| | Statsforecast | MFLES | 1.184 | 185.38 | 217.10 | 161 |
| **MSTL** | Anofox | MSTL | 1.302 | 202.82 | 232.93 | **0.60** |
| | Statsforecast | MSTL | **1.200** | 184.34 | 216.14 | 425 |

## Datasets, Metrics, and Running Benchmarks

### Dataset Information

**M4 Daily Dataset:**

- **Series Count**: 4,227 time series
- **Season Length**: 7 (weekly seasonality)
- **Forecast Horizon**: 14 days
- **Split**: Per M4 competition rules (training/test split)
- **Source**: M4 Competition dataset

**M5 Daily Dataset:**

- **Series Count**: 42,840 time series
- **Season Length**: 7 (weekly seasonality)
- **Forecast Horizon**: 28 days
- **Split**: Date-based split (training before 2016-04-25, test from 2016-04-25 onwards)
- **Source**: M5 Competition dataset (via datasetsforecast library)

### Metrics

All benchmarks measure:

- **MASE** (Mean Absolute Scaled Error) - Primary metric, scale-independent
- **MAE** (Mean Absolute Error) - Absolute forecast error in original units
- **RMSE** (Root Mean Squared Error) - Penalizes large errors
- **Time** - Execution time in seconds per model (each model is timed individually for fair comparison)

### Running Benchmarks

**Environment Setup:**

```bash
cd benchmark
uv sync  # Install dependencies
```

**Run All Benchmarks:**

To run all benchmarks (anofox + statsforecast) for a specific dataset and group:

```bash
# Run all benchmarks for M4 Daily
cd benchmark
uv run python run_all_benchmarks.py --dataset m4 --group Daily

# Run all benchmarks for M5 Daily
uv run python run_all_benchmarks.py --dataset m5 --group Daily

# Run specific benchmarks only
uv run python run_all_benchmarks.py --dataset m4 --group Daily --benchmarks baseline,ets,theta
```

**Run Individual Benchmarks:**

All benchmark commands accept a `--dataset` argument (default `m4`).
Add new datasets by creating `benchmark/<dataset>` and passing that dataset name via CLI.

**M4 Benchmarks:**

```bash
# Baseline models
cd m4/baseline_benchmark
uv run python run.py anofox Daily --dataset m4        # Run Anofox models
uv run python run.py statsforecast Daily --dataset m4 # Run Statsforecast models
uv run python run.py evaluate Daily --dataset m4      # Evaluate results

# ETS models
cd m4/ets_benchmark
uv run python run.py anofox Daily --dataset m4
uv run python run.py evaluate Daily --dataset m4

# Theta models
cd m4/theta_benchmark
uv run python run.py anofox Daily --dataset m4
uv run python run.py statsforecast Daily --dataset m4
uv run python run.py evaluate Daily --dataset m4

# ARIMA models
cd m4/arima_benchmark
uv run python run.py anofox Daily --dataset m4
uv run python run.py evaluate Daily --dataset m4

# MFLES models
cd m4/mfles_benchmark
uv run python run.py anofox Daily --dataset m4
uv run python run.py statsforecast Daily --dataset m4
uv run python run.py evaluate Daily --dataset m4

# MSTL models
cd m4/mstl_benchmark
uv run python run.py anofox Daily --dataset m4
uv run python run.py statsforecast Daily --dataset m4
uv run python run.py evaluate Daily --dataset m4
```

**M5 Benchmarks:**

```bash
# Baseline models
cd m5/baseline_benchmark
uv run python run.py anofox Daily --dataset m5        # Run Anofox models
uv run python run.py statsforecast Daily --dataset m5 # Run Statsforecast models
uv run python run.py evaluate Daily --dataset m5      # Evaluate results

# ETS models
cd m5/ets_benchmark
uv run python run.py anofox Daily --dataset m5
uv run python run.py evaluate Daily --dataset m5

# Theta models
cd m5/theta_benchmark
uv run python run.py anofox Daily --dataset m5
uv run python run.py statsforecast Daily --dataset m5
uv run python run.py evaluate Daily --dataset m5

# ARIMA models
cd m5/arima_benchmark
uv run python run.py anofox Daily --dataset m5
uv run python run.py evaluate Daily --dataset m5

# MFLES models
cd m5/mfles_benchmark
uv run python run.py anofox Daily --dataset m5
uv run python run.py statsforecast Daily --dataset m5
uv run python run.py evaluate Daily --dataset m5

# MSTL models
cd m5/mstl_benchmark
uv run python run.py anofox Daily --dataset m5
uv run python run.py statsforecast Daily --dataset m5
uv run python run.py evaluate Daily --dataset m5
```

## Model Configuration

The model configuration can be adjusted in the Python file in the config folder.

## Purpose

These benchmarks are used to:

1. Validate forecasting accuracy against known datasets (M4 and M5 Competitions)
2. Measure performance on larger workloads (4K+ time series for M4, 42K+ for M5)
3. Validate Anofox with other forecasting libraries (Statsforecast)
4. Identify performance regressions and optimization opportunities

## Environment

The benchmark environment uses `uv` for Python dependency management:

- Python packages defined in `pyproject.toml`
- Locked dependencies in `uv.lock`
- Python version in `.python-version`

## Planned Benchmarks

1. Artificial dataset with 100k+ series for performance tests in realistic scenarios.
