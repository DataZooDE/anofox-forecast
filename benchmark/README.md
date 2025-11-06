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
| **ETS** | Anofox | AutoETS | **1.148** | 175.79 | 207.48 | 466 |
| | Anofox | HoltWinters | **1.152** | 175.92 | 207.42 | 117 |
| | Anofox | SeasonalESOptimized | 1.203 | 186.67 | 218.23 | 8.0 |
| | Anofox | SeasonalES | 1.355 | 210.88 | 240.48 | 1.1 |
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
1. **RandomWalkWithDrift** (1.147) - Best overall, extremely fast (0.17s)
2. **AutoETS** (1.148) - Best complex model with automatic selection
3. **OptimizedTheta** (1.149) - Both Anofox and Statsforecast variants excel
4. **AutoTheta** (1.149) - Statsforecast automatic Theta selection
5. **Statsforecast AutoARIMA** (1.150) - Best ARIMA accuracy, competitive with top methods
6. **HoltWinters** (1.152) - Fast ETS variant (117s)

**Speed Champions:**
- **Fastest Overall**: SMA (0.16s, MASE 1.343)
- **Fastest High-Accuracy**: RandomWalkWithDrift (0.17s, MASE 1.147)
- **Fast Automatic**: Anofox AutoARIMA (5.2s, MASE 1.212) - **562x faster than Statsforecast AutoARIMA**
- **Fast Theta**: Anofox Theta (20s, MASE 1.226)

**Implementation Comparison:**
- **Baseline Models**: Anofox and Statsforecast produce identical accuracy
- **Theta Methods**: Statsforecast runs ~1.5x faster (693s vs 900-1000s), similar accuracy
- **ARIMA**: Statsforecast more accurate (1.150 vs 1.212), but Anofox **562x faster** (5.2s vs 2,923s)
  - Anofox: Best for speed-critical applications
  - Statsforecast: Best for accuracy-critical batch processing
- **MFLES**: Statsforecast significantly outperforms Anofox (1.184 vs 1.887)
  - Note: This gap requires investigation; likely due to implementation differences

**Practical Recommendations:**
- **Production Default**: RandomWalkWithDrift - Best accuracy + fastest speed (0.17s)
- **Best Automatic Complex**: AutoETS - Excellent accuracy with automatic selection (8 min)
- **Fastest Automatic**: Anofox AutoARIMA - Good accuracy in seconds (5.2s, MASE 1.212)
- **Best ARIMA Accuracy**: Statsforecast AutoARIMA when you can afford 49 minutes (MASE 1.150)
- **Best Accuracy**: OptimizedTheta when you can afford 15-17 minutes (MASE 1.149)
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
- **Models**: 4 exponential smoothing variants
  - AutoETS, HoltWinters, SeasonalESOptimized, SeasonalES
- **Implementations**: Anofox (Statsforecast comparison pending)
- **Status**: ✅ Complete - AutoETS achieves best complex model accuracy (MASE 1.148)
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
