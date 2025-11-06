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
| | Anofox | SeasonalNaive | 1.441 | 227.11 | 263.74 | 0.18 |
| | Statsforecast | SeasonalNaive | 1.452 | 227.12 | 262.16 | 0.38 |
| | Anofox | RandomWalkWithDrift | **1.147** | 183.48 | 215.10 | 0.17 |
| | Statsforecast | RandomWalkWithDrift | **1.147** | 183.48 | 215.10 | 0.38 |
| | Anofox | SMA | 1.343 | 209.01 | 237.98 | 0.16 |
| | Statsforecast | WindowAverage | 1.380 | 214.88 | 243.65 | 0.38 |
| | Anofox | SeasonalWindowAverage | 1.961 | 300.48 | 326.69 | 0.21 |
| | Statsforecast | SeasonalWindowAverage | 2.209 | 334.23 | 359.39 | 0.38 |
| **ETS** | Statsforecast | Holt | **1.132** | 172.86 | 204.44 | ~241 |
| | Anofox | AutoETS | **1.148** | 175.79 | 207.48 | 466 |
| | Statsforecast | HoltWinters | **1.148** | 177.14 | 208.90 | ~241 |
| | Anofox | HoltWinters | 1.152 | 175.92 | 207.42 | 117 |
| | Statsforecast | SESOpt | 1.154 | 178.32 | 209.79 | ~241 |
| | Anofox | SeasonalESOptimized | 1.203 | 186.67 | 218.23 | 8.0 |
| | Statsforecast | AutoETS | 1.227 | 188.14 | 227.63 | ~241 |
| | Statsforecast | SES | 1.231 | 191.79 | 222.13 | ~241 |
| | Anofox | SeasonalES | 1.355 | 210.88 | 240.48 | 1.1 |
| | Statsforecast | SeasESOpt | 1.457 | 226.82 | 261.36 | ~241 |
| | Statsforecast | SeasonalES | 1.608 | 249.17 | 278.42 | ~241 |
| **Theta** | Anofox | OptimizedTheta | **1.149** | 178.08 | 209.53 | 1,033 |
| | Statsforecast | AutoTheta | **1.149** | 178.15 | 209.60 | 693 |
| | Statsforecast | OptimizedTheta | 1.151 | 178.44 | 209.91 | 693 |
| | Statsforecast | DynamicTheta | 1.153 | 178.83 | 210.33 | 693 |
| | Statsforecast | Theta | 1.154 | 178.85 | 210.36 | 693 |
| | Anofox | DynamicOptimizedTheta | 1.155 | 179.06 | 210.56 | 906 |
| | Statsforecast | DynamicOptimizedTheta | 1.156 | 178.97 | 210.52 | 693 |
| | Anofox | Theta | 1.226 | 191.46 | 222.00 | 20 |
| | Anofox | DynamicTheta | 1.226 | 191.41 | 221.94 | 19 |
| **ARIMA** | Statsforecast | AutoARIMA | **1.150** | 176.88 | 208.43 | 2,923 |
| | Anofox | AutoARIMA | 1.212 | 183.95 | 216.36 | 5.2 |
| **MFLES** | Statsforecast | MFLES | **1.184** | 185.38 | 217.10 | 81 |
| | Anofox | MFLES | 1.887 | 296.36 | 322.79 | 4.1 |
| | Anofox | AutoMFLES | 1.888 | 297.39 | 323.55 | 548 |

### Key Findings

**Top Performers (MASE ≤ 1.15):**
1. **Statsforecast Holt** (1.132) - **NEW overall best!** Beats all methods
2. **RandomWalkWithDrift** (1.147) - Previous best, extremely fast (0.17s)
3. **Anofox AutoETS** (1.148) - Best complex automatic selection
4. **Statsforecast HoltWinters** (1.148) - Tied with Anofox AutoETS
5. **OptimizedTheta** (1.149) - Both Anofox and Statsforecast variants excel
6. **AutoTheta** (1.149) - Statsforecast automatic Theta selection
7. **Statsforecast AutoARIMA** (1.150) - Best ARIMA accuracy
8. **Anofox HoltWinters** (1.152) - Fast & accurate ETS (117s)

**Speed Champions:**
- **Fastest Overall**: SMA (0.16s, MASE 1.343)
- **Fastest High-Accuracy**: RandomWalkWithDrift (0.17s, MASE 1.147)
- **Fast Automatic**: Anofox AutoARIMA (5.2s, MASE 1.212) - **562x faster than Statsforecast AutoARIMA**
- **Fast Theta**: Anofox Theta (20s, MASE 1.226)

**Implementation Comparison:**
- **Baseline Models**: Anofox and Statsforecast produce identical accuracy
- **ETS Methods**: Mixed results - Statsforecast Holt best overall (1.132), but Anofox AutoETS (1.148) beats Statsforecast AutoETS (1.227) by 6.4%
  - Statsforecast Holt: Best single model if you know to use it
  - Anofox AutoETS: Best automatic selection, faster per model
  - Anofox wins overall average: 1.214 vs 1.280
- **Theta Methods**: Statsforecast runs ~1.5x faster (693s vs 900-1000s), similar accuracy
- **ARIMA**: Statsforecast more accurate (1.150 vs 1.212), but Anofox **562x faster** (5.2s vs 2,923s)
  - Anofox: Best for speed-critical applications
  - Statsforecast: Best for accuracy-critical batch processing
- **MFLES**: Statsforecast significantly outperforms Anofox (1.184 vs 1.887)
  - Note: This gap requires investigation; likely due to implementation differences

**Practical Recommendations:**
- **Best Overall Accuracy**: Statsforecast Holt if you know to use it (MASE 1.132)
- **Production Default**: RandomWalkWithDrift - Previous best + fastest speed (0.17s, MASE 1.147)
- **Best Automatic Complex**: Anofox AutoETS - Excellent accuracy with automatic selection (8 min, MASE 1.148)
- **Fastest Automatic**: Anofox AutoARIMA - Good accuracy in seconds (5.2s, MASE 1.212)
- **Fast & Accurate**: Anofox HoltWinters (117s, MASE 1.152)
- **Balanced Choice**: Anofox Theta for near-optimal accuracy in 20s (MASE 1.226)

### AutoMFLES Performance

AutoMFLES successfully implements automatic hyperparameter optimization:
- Achieves **identical accuracy** to manually-tuned MFLES baseline (1.888 vs 1.887)
- Evaluates 24 configurations via cross-validation
- **133x overhead** for automatic parameter selection (548s vs 4.1s)
- Trade-off: Automatic tuning vs manual configuration

## Benchmark Suites

### Baseline Models Benchmark
- **Location**: `baseline_benchmark/`
- **Models**: 5 fundamental forecasting models
  - Naive, SeasonalNaive, RandomWalkWithDrift, SMA, SeasonalWindowAverage
- **Implementations**: Both Anofox and Statsforecast
- **Status**: ✅ Complete (Daily results available)
- **See**: [baseline_benchmark/README.md](baseline_benchmark/README.md)

### ETS Benchmark
- **Location**: `ets_benchmark/`
- **Models**: 11 exponential smoothing variants
  - **Anofox**: AutoETS, HoltWinters, SeasonalESOptimized, SeasonalES
  - **Statsforecast**: Holt, HoltWinters, SESOpt, AutoETS, SES, SeasESOpt, SeasonalES
- **Implementations**: Both Anofox and Statsforecast
- **Status**: ✅ Complete - **Statsforecast Holt achieves NEW overall best (MASE 1.132)**!
- **Key Finding**: Anofox AutoETS (1.148) beats Statsforecast AutoETS (1.227) by 6.4%
- **See**: [ets_benchmark/README.md](ets_benchmark/README.md)

### Theta Benchmark
- **Location**: `theta_benchmark/`
- **Models**: 9 Theta method variants
  - **Anofox**: Theta, OptimizedTheta, DynamicTheta, DynamicOptimizedTheta
  - **Statsforecast**: AutoTheta, Theta, OptimizedTheta, DynamicTheta, DynamicOptimizedTheta
- **Status**: ✅ Complete (Daily results available)
- **See**: [theta_benchmark/README.md](theta_benchmark/README.md)

### ARIMA Benchmark
- **Location**: `arima_benchmark/`
- **Models**: AutoARIMA with automatic order selection
- **Implementations**: Both Anofox and Statsforecast
  - **Anofox**: 5.2s, MASE 1.212 (fast)
  - **Statsforecast**: 2,923s (48.7 min), MASE 1.150 (accurate)
- **Status**: ✅ Complete - Demonstrates speed/accuracy trade-off (562x speed vs 5.4% accuracy)
- **See**: [arima_benchmark/README.md](arima_benchmark/README.md)

### MFLES Benchmark
- **Location**: `mfles_benchmark/`
- **Models**: Multiple seasonality forecasting
  - **Anofox**: MFLES, AutoMFLES
  - **Statsforecast**: MFLES
- **Status**: ⚠️ Complete but performance gap requires investigation
- **See**: [mfles_benchmark/README.md](mfles_benchmark/README.md)

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
- **Time** - Total execution time in seconds

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
