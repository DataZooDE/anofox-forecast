# Benchmark Suite

This directory contains performance benchmarks and validation tests for the anofox-forecast extension.

## Latest Results

**M4 Daily Dataset** (4,227 time series, 14-step forecast horizon):

| Model | MASE | MAE | RMSE | Time (s) | Speedup |
|-------|------|-----|------|----------|---------|
| Statsforecast AutoARIMA | 1.150 | 176.88 | 625.34 | 2,922.8 | 1.0x |
| **Anofox AutoARIMA** | **1.212** | **183.95** | **601.83** | **10.4** | **279.8x** |

**Summary**: Anofox achieves competitive accuracy (5.4% MASE gap) with **280x faster** execution on native DuckDB tables.

## Benchmarks

### ARIMA Benchmark Suite
- **`arima_benchmark/`** - Comprehensive ARIMA comparison benchmark
  - Compares anofox-forecast AutoARIMA with statsforecast, pmdarima, and prophet
  - Uses M4 Competition datasets (Daily, Hourly, Weekly)
  - Measures MASE, MAE, RMSE, and execution time
  - Based on [Nixtla's statsforecast benchmark](https://github.com/Nixtla/statsforecast/tree/main/experiments/arima)
  - **See**: `arima_benchmark/README.md` for details

### M5 Competition Dataset
- **`m5_benchmark.py`** - M5 forecasting competition benchmark
- **`m5_test.sql`** - SQL tests using M5 dataset

### Performance Tests
- **`10k_series_synthetic_test.sql`** - Large-scale performance test with 10,000 time series

## Purpose

These benchmarks are used to:
1. Validate forecasting accuracy against known datasets
2. Measure performance with realistic workloads
3. Compare with other forecasting libraries
4. Identify performance regressions

## Running Benchmarks

### ARIMA Benchmark (NEW!)
```bash
# Full benchmark on Daily data (recommended for first run)
uv run python arima_benchmark/run_benchmark.py run --group=Daily

# Run specific model
uv run python arima_benchmark/run_benchmark.py model anofox Daily

# See arima_benchmark/README.md for more options
```

### M5 Benchmark
```bash
uv run python m5_benchmark.py
```

### SQL Performance Test
```bash
duckdb :memory: < 10k_series_synthetic_test.sql
```

## Environment

The benchmark environment uses `uv` for Python dependency management:
- Python packages defined in `pyproject.toml`
- Locked dependencies in `uv.lock`
- Python version in `.python-version`

## Adding Benchmarks

When adding new benchmarks:
1. Use realistic datasets (M5, real-world data)
2. Test at scale (1K+ series for performance tests)
3. Include validation against known-good results
4. Document expected performance characteristics

## Related

- **Guides**: See `guides/60_performance_optimization.md` for performance tuning
- **Examples**: SQL examples are in `test/sql/docs_examples/`
- **Tests**: Unit tests are in `test/sql/`
