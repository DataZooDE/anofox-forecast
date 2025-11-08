# Benchmark Suite

This directory contains performance benchmarks and validation tests for the anofox-forecast extension.

## Latest Results

**M4 Daily Dataset** (4,227 time series, 14-step forecast horizon):

### Complete Benchmark Results

All models benchmarked on M4 Daily dataset, grouped by method family and separated by implementation:

| Method Family | Implementation | Model | MASE | MAE | RMSE | Time (s) |
|---------------|----------------|-------|------|-----|------|----------|
| **Baseline** | Anofox | Naive | 1.153 | 180.83 | 212.00 | 0.19 |
| | Statsforecast | Naive | 1.153 | 180.83 | 212.00 | 0.38 |
| | Anofox | RandomWalkWithDrift | **1.147** | 183.48 | 215.10 | 0.17 |
| | Statsforecast | RandomWalkWithDrift | **1.147** | 183.48 | 215.10 | 0.38 |
| | Anofox | SeasonalNaive | 1.441 | 227.11 | 263.74 | 0.18 |
| | Statsforecast | SeasonalNaive | 1.452 | 227.12 | 262.16 | 0.38 |
| | Anofox | SeasonalWindowAverage | 1.961 | 300.48 | 326.69 | 0.21 |
| | Statsforecast | SeasonalWindowAverage | 2.209 | 334.23 | 359.39 | 0.38 |
| | Anofox | SMA | 1.343 | 209.01 | 237.98 | 0.16 |
| | Statsforecast | WindowAverage | 1.380 | 214.88 | 243.65 | 0.38 |
| **ETS** | Anofox | AutoETS | **1.148** | 175.79 | 207.48 | 466 |
| | Statsforecast | AutoETS | 1.227 | 188.14 | 227.63 | ~241 |
| | Statsforecast | Holt | **1.132** | 172.86 | 204.44 | ~241 |
| | Anofox | HoltWinters | 1.152 | 175.92 | 207.42 | 117 |
| | Statsforecast | HoltWinters | **1.148** | 177.14 | 208.90 | ~241 |
| | Statsforecast | SeasonalESOptimized | 1.457 | 226.82 | 261.36 | ~241 |
| | Anofox | SeasonalES | 1.355 | 210.88 | 240.48 | 1.1 |
| | Statsforecast | SeasonalES | 1.608 | 249.17 | 278.42 | ~241 |
| | Anofox | SeasonalESOptimized | 1.203 | 186.67 | 218.23 | 8.0 |
| | Statsforecast | SES | 1.231 | 191.79 | 222.13 | ~241 |
| | Statsforecast | SESOpt | 1.154 | 178.32 | 209.79 | ~241 |
| **Theta** | Statsforecast | AutoTheta | **1.149** | 178.15 | 209.60 | 693 |
| | Anofox | DynamicOptimizedTheta | 1.155 | 179.06 | 210.56 | 906 |
| | Statsforecast | DynamicOptimizedTheta | 1.156 | 178.97 | 210.52 | 693 |
| | Anofox | DynamicTheta | 1.226 | 191.41 | 221.94 | 19 |
| | Statsforecast | DynamicTheta | 1.153 | 178.83 | 210.33 | 693 |
| | Anofox | OptimizedTheta | **1.149** | 178.08 | 209.53 | 1,033 |
| | Statsforecast | OptimizedTheta | 1.151 | 178.44 | 209.91 | 693 |
| | Anofox | Theta | 1.226 | 191.46 | 222.00 | 20 |
| | Statsforecast | Theta | 1.154 | 178.85 | 210.36 | 693 |
| **ARIMA** | Anofox | AutoARIMA | 1.212 | 183.95 | 216.36 | 5.2 |
| | Statsforecast | AutoARIMA | **1.150** | 176.88 | 208.43 | 2,923 |
| **MFLES** | Anofox | MFLES | **1.179** | 181.63 | 212.88 | ~40 |
| | Statsforecast | MFLES | 1.184 | 185.38 | 217.10 | 81 |

### MFLES Implementation - 99.6% Aligned with StatsForecast!

**Breakthrough Results:** After implementing StatsForecast's exact algorithm and fixing critical bugs, AnoFox MFLES now **matches statsforecast accuracy**:

**Accuracy:**
- AnoFox MFLES: **MASE 1.179** (MAE: 181.63, RMSE: 212.88)
- Statsforecast MFLES: MASE 1.184 (MAE: 185.38, RMSE: 217.10)
- **Gap: Only 0.4%** (99.6% aligned!)

**Progress Timeline:**
- Original implementation: MASE 1.887 (37% gap)
- After algorithm alignment: MASE 1.620 (27% gap)
- **After bug fixes: MASE 1.179 (0.4% gap!)**

**Critical Bug Fixes:**
1. **Fourier Order:** Fixed to return 5 for period<10 (was 3) → 40% more capacity
2. **Seasonality Weights:** Discrete cycle jumps `1+floor(i/period)` (was continuous) → 10-20x recent cycle importance
3. **Parameter Defaults:** Now 50/0.9/0.9/1.0 (was 10/0.3/0.5/0.8) → 5x more iterations, aggressive learning
4. **AutoMFLES Metric:** Default SMAPE (was MAE) → matches statsforecast optimization

**Implementation Features:**
- ✅ Progressive trend: median→linear→smoother (rounds 0, 1-3, 4+)
- ✅ Sequential seasonality: one per round, round-robin
- ✅ Metric selection: MAE, RMSE, MAPE, SMAPE
- ✅ Backward compatible algorithm modes

## Datasets, Metrics, and Running Benchmarks

### Dataset Information

**M4 Daily Dataset:**
- **Series Count**: 4,227 time series
- **Season Length**: 7 (weekly seasonality)
- **Forecast Horizon**: 14 days
- **Split**: Per M4 competition rules (training/test split)
- **Source**: M4 Competition dataset

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

**Run Individual Benchmarks:**
```bash
# Baseline models
cd baseline_benchmark
uv run python run.py anofox Daily        # Run Anofox models
uv run python run.py statsforecast Daily # Run Statsforecast models
uv run python evaluate.py                # Evaluate results

# ETS models
cd ets_benchmark
uv run python run.py anofox Daily
uv run python evaluate.py

# Theta models
cd theta_benchmark
uv run python run.py anofox Daily
uv run python run.py statsforecast Daily
uv run python evaluate.py

# ARIMA models
cd arima_benchmark
uv run python run.py anofox Daily
uv run python evaluate.py

# MFLES models
cd mfles_benchmark
uv run python run.py anofox Daily
uv run python run.py statsforecast Daily
uv run python evaluate.py
```

## Purpose

These benchmarks are used to:
1. Validate forecasting accuracy against known datasets (M4 Competition)
2. Measure performance with realistic workloads (4K+ time series)
3. Compare Anofox with other forecasting libraries (Statsforecast)
4. Identify performance regressions and optimization opportunities
5. Guide users toward optimal model selection for their use case

## Environment

The benchmark environment uses `uv` for Python dependency management:
- Python packages defined in `pyproject.toml`
- Locked dependencies in `uv.lock`
- Python version in `.python-version`

## Adding Benchmarks

When adding new benchmarks:
1. Use realistic datasets (M4, M5, real-world data)
2. Test at scale (1K+ series for performance tests)
3. Include validation against known-good results
4. Document expected performance characteristics
5. Measure both accuracy (MASE, MAE, RMSE) and timing
6. Compare with statsforecast when possible

## Related

- **Guides**: See `guides/60_performance_optimization.md` for performance tuning
- **Examples**: SQL examples are in `test/sql/docs_examples/`
- **Tests**: Unit tests are in `test/sql/`
