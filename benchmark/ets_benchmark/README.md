# Exponential Smoothing (ETS) Methods Benchmark

Comprehensive benchmark comparing Anofox and Statsforecast implementations of exponential smoothing (ETS) forecasting models on M4 Competition datasets.

## Overview

This benchmark evaluates exponential smoothing methods widely used in forecasting for comparison. These exponential smoothing methods are essential for understanding whether more complex models provide meaningful improvements.

**Baseline Models Tested:**
- **Naive**: Uses the last observed value as the forecast
- **SeasonalNaive**: Uses the observation from the same season in the previous cycle
- **RandomWalkWithDrift**: Naive forecast with linear trend component
- **SMA (Simple Moving Average)**: Average of recent observations
- **SeasonalWindowAverage**: Moving average with seasonal adjustment

**Datasets:**
- M4 Competition: Daily (4,227 series), Hourly (414 series), Weekly (359 series)
- Forecast horizons: Daily=14, Hourly=48, Weekly=13

**Metrics:**
- **MASE (Mean Absolute Scaled Error)**: Primary metric, scale-independent
- **MAE (Mean Absolute Error)**: Average absolute forecast error
- **RMSE (Root Mean Squared Error)**: Penalizes larger errors more heavily

## Directory Structure

```
ets_benchmark/
├── run_benchmark.py              # Main entry point
├── src/
│   ├── __init__.py
│   ├── data.py                   # M4 data loading utilities
│   ├── anofox_ets.py        # Anofox ETS models
│   ├── statsforecast_ets.py # Statsforecast ETS models
│   └── evaluation_ets.py    # Metrics calculation
└── results/                      # Generated benchmark results
    ├── anofox-*.parquet          # Anofox forecasts
    ├── statsforecast-*.parquet   # Statsforecast forecasts
    └── baseline-evaluation-*.parquet  # Evaluation results
```

## Quick Start

### Run Full Benchmark

Run all ETS models and evaluation on M4 Daily data:

```bash
cd /home/simonm/projects/duckdb/anofox-forecast
uv run python benchmark/ets_benchmark/run_benchmark.py run --group=Daily
```

This will:
1. Run 5 Anofox ETS models
2. Run 5 Statsforecast ETS models
3. Evaluate all forecasts against test data
4. Display summary table with MASE/MAE/RMSE

### Run Individual Components

**Anofox ETS models only:**
```bash
uv run python benchmark/ets_benchmark/run_benchmark.py anofox --group=Daily
```

**Specific Anofox model:**
```bash
uv run python benchmark/ets_benchmark/run_benchmark.py anofox --group=Daily --model=SeasonalNaive
```

**Statsforecast ETS models only:**
```bash
uv run python benchmark/ets_benchmark/run_benchmark.py statsforecast --group=Daily
```

**Evaluate existing results:**
```bash
uv run python benchmark/ets_benchmark/run_benchmark.py eval --group=Daily
```

**Clean results:**
```bash
# Clean specific group
uv run python benchmark/ets_benchmark/run_benchmark.py clean --group=Daily

# Clean all results
uv run python benchmark/ets_benchmark/run_benchmark.py clean
```

## Command Reference

### run

Run complete baseline benchmark pipeline.

```bash
uv run python benchmark/ets_benchmark/run_benchmark.py run [--group=GROUP]
```

**Parameters:**
- `--group`: M4 frequency group ('Daily', 'Hourly', or 'Weekly'). Default: 'Daily'

**Example:**
```bash
uv run python benchmark/ets_benchmark/run_benchmark.py run --group=Weekly
```

### anofox

Run Anofox ETS models only.

```bash
uv run python benchmark/ets_benchmark/run_benchmark.py anofox [--group=GROUP] [--model=MODEL]
```

**Parameters:**
- `--group`: M4 frequency group. Default: 'Daily'
- `--model`: Specific model to run. Options: 'Naive', 'SeasonalNaive', 'RandomWalkWithDrift', 'SMA', 'SeasonalWindowAverage'. Default: All models

**Examples:**
```bash
# Run all Anofox ETS models
uv run python benchmark/ets_benchmark/run_benchmark.py anofox --group=Daily

# Run specific model
uv run python benchmark/ets_benchmark/run_benchmark.py anofox --group=Daily --model=Naive
```

### statsforecast

Run Statsforecast ETS models only.

```bash
uv run python benchmark/ets_benchmark/run_benchmark.py statsforecast [--group=GROUP]
```

**Parameters:**
- `--group`: M4 frequency group. Default: 'Daily'

**Example:**
```bash
uv run python benchmark/ets_benchmark/run_benchmark.py statsforecast --group=Hourly
```

### eval

Evaluate existing forecast results.

```bash
uv run python benchmark/ets_benchmark/run_benchmark.py eval [--group=GROUP]
```

**Parameters:**
- `--group`: M4 frequency group. Default: 'Daily'

**Example:**
```bash
uv run python benchmark/ets_benchmark/run_benchmark.py eval --group=Daily
```

### clean

Clean benchmark result files.

```bash
uv run python benchmark/ets_benchmark/run_benchmark.py clean [--group=GROUP]
```

**Parameters:**
- `--group`: M4 frequency group to clean. If omitted, cleans all results.

**Examples:**
```bash
# Clean Daily results only
uv run python benchmark/ets_benchmark/run_benchmark.py clean --group=Daily

# Clean all results
uv run python benchmark/ets_benchmark/run_benchmark.py clean
```

## Model Implementations

### Anofox Baseline Models

Implemented in `src/anofox_ets.py` using the `TS_FORECAST_BY()` SQL function:

```python
BASELINE_MODELS = [
    'Naive',                    # Last value forecast
    'SeasonalNaive',           # Seasonal last value
    'RandomWalkWithDrift',     # Naive + trend
    'SMA',                     # Simple moving average
    'SeasonalWindowAverage',   # Seasonal moving average
]
```

Each model is executed via DuckDB SQL:
```sql
SELECT
    unique_id AS id_cols,
    date_col AS time_col,
    point_forecast AS forecast_col,
    lower,
    upper
FROM TS_FORECAST_BY(
    'train',
    unique_id,
    ds,
    y,
    'ModelName',
    horizon,
    params
)
```

### Statsforecast Baseline Models

Implemented in `src/statsforecast_ets.py` using Statsforecast library:

```python
from statsforecast.models import (
    Naive,
    SeasonalNaive,
    RandomWalkWithDrift,
    WindowAverage,              # SMA equivalent
    SeasonalWindowAverage
)

models = [
    Naive(),
    SeasonalNaive(season_length=seasonality),
    RandomWalkWithDrift(),
    WindowAverage(window_size=window_size),
    SeasonalWindowAverage(season_length=seasonality, window_size=window_size),
]

sf = StatsForecast(models=models, freq=freq, n_jobs=-1)
fcst_df = sf.forecast(df=train_df, h=horizon, level=[95])
```

## Evaluation Methodology

### Metrics Calculation

**MASE (Mean Absolute Scaled Error):**
```python
def mase(y_true, y_pred, y_train, seasonality):
    mae = mean(abs(y_true - y_pred))

    if seasonality > 1 and len(y_train) > seasonality:
        # Seasonal naive baseline
        naive_error = mean(abs(y_train[seasonality:] - y_train[:-seasonality]))
    else:
        # Regular naive baseline
        naive_error = mean(abs(y_train[1:] - y_train[:-1]))

    return mae / naive_error
```

**MAE:**
```python
mae = mean(abs(y_true - y_pred))
```

**RMSE:**
```python
rmse = sqrt(mean((y_true - y_pred)^2))
```

### Per-Series and Aggregate

Metrics are calculated:
1. For each individual time series
2. Averaged across all series for aggregate performance

## Output Files

All results are saved in `ets_benchmark/results/` as Parquet files:

**Forecast files:**
- `anofox-{Model}-{Group}.parquet`: Anofox forecasts for each model
- `statsforecast-Baseline-{Group}.parquet`: All Statsforecast forecasts in one file

**Metrics files:**
- `anofox-{Model}-{Group}-metrics.parquet`: Timing metrics for each Anofox model
- `statsforecast-Baseline-{Group}-metrics.parquet`: Timing metrics for Statsforecast

**Evaluation file:**
- `baseline-evaluation-{Group}.parquet`: Final MASE/MAE/RMSE results

## Expected Performance

ETS models are extremely fast (typically < 1 second for 4,227 series) and provide:
- Simple benchmarks for complex model comparison
- Quick sanity checks for forecasting pipelines
- Baselines for MASE calculation

**Typical accuracy (M4 Daily):**
- Naive: MASE ~ 4.0-5.0
- SeasonalNaive: MASE ~ 1.5-2.5
- RandomWalkWithDrift: MASE ~ 3.0-4.0
- SMA: MASE ~ 3.5-4.5
- SeasonalWindowAverage: MASE ~ 1.8-2.8

## Prerequisites

The extension must be built before running benchmarks:

```bash
cd /home/simonm/projects/duckdb/anofox-forecast
make release
```

Python dependencies are managed via `uv` (see parent `benchmark/` directory).

## Troubleshooting

**Extension not found:**
```bash
# Rebuild the extension
make release

# Check path in src/anofox_ets.py matches build output
ls build/release/extension/anofox_forecast/
```

**Import errors:**
```bash
# Run from project root
cd /home/simonm/projects/duckdb/anofox-forecast

# Use uv to manage dependencies
uv run python benchmark/ets_benchmark/run_benchmark.py run
```

**Missing data:**
The M4 dataset is automatically downloaded by `datasetsforecast` library on first use.

## Related

- **Main Benchmark README**: `../README.md` - Overview of all benchmark suites
- **ARIMA Benchmark**: `../arima_benchmark/` - AutoARIMA comparison
- **Theta Benchmark**: `../theta_benchmark/` - Theta methods comparison
- **Model Parameters Guide**: `../../guides/41_model_parameters.md` - Baseline model documentation
