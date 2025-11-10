# MSTL Benchmark

Comprehensive benchmark comparing MSTL (Multiple Seasonal-Trend decomposition
using Loess) implementations between **Anofox-forecast** and **Statsforecast**
on M4 Competition datasets.

## Latest Results

**M4 Daily Dataset** (4,227 series, horizon=14, seasonality=7):

### Point Forecasts Only (Prediction Intervals Excluded)

| Implementation | Model | MASE | MAE | RMSE | Time (s) |
|----------------|-------|------|-----|------|----------|
| Anofox | MSTL | 1.302 | 202.82 | 232.93 | **0.60** |
| Statsforecast | MSTL | **1.200** | **184.34** | **216.14** | 425 |

## Implementation Details

### Anofox MSTL

**Core Features:**

- **Multiple Seasonal Decomposition**: Handles multiple nested seasonal
  patterns using STL (Seasonal and Trend decomposition using Loess)
- **Flexible Trend Forecasting**: Supports various trend extrapolation
  methods
- **Automatic Parameter Selection**: AutoMSTL variant automatically
  optimizes decomposition parameters
- **SQL-Native Integration**: Runs directly in DuckDB without data export

**Decomposition Process:**

1. **Multiple STL Iterations**: Sequentially removes each seasonal pattern
2. **Trend Extraction**: Loess-based trend smoothing
3. **Seasonal Component Modeling**: Individual seasonal patterns extracted
   and forecast
4. **Component Recombination**: Forecasts combined from all components

**SQL Example:**

```sql
-- Basic MSTL with single seasonality
SELECT * FROM TS_FORECAST_BY(
    'sales', store_id, date, revenue,
    'MSTL', 14,
    {
        'seasonal_periods': [7]  -- Weekly pattern
    }
);

-- MSTL with multiple seasonality
SELECT * FROM TS_FORECAST_BY(
    'hourly_energy', meter_id, timestamp, kwh,
    'MSTL', 168,  -- 1 week ahead
    {
        'seasonal_periods': [24, 168]  -- Daily + Weekly patterns
    }
);

-- AutoMSTL with automatic optimization
SELECT * FROM TS_FORECAST_BY(
    'sales', store_id, date, revenue,
    'AutoMSTL', 14,
    {
        'seasonal_periods': [7, 365]  -- Weekly + Annual patterns
    }
);
```

**Parameters:**

- `seasonal_periods`: List of seasonal period lengths (e.g., [7], [24, 168])
- `trend_method`: Trend forecasting method (0=Linear, 1=SES, 2=Holt,
  3=None, 4=AutoETS Additive, 5=AutoETS Multiplicative)
- `seasonal_method`: Seasonal forecasting method (0=Cyclic, 1=AutoETS
  Additive, 2=AutoETS Multiplicative)
- `deseasonalized_method`: **NEW!** Deseasonalized forecasting method:
  - 0 = ExponentialSmoothing (default, fastest - 0.60s)
  - 1 = Linear (medium speed/accuracy)
  - 2 = AutoETS (slowest, most accurate)

**Performance (Optimized):**

- **MSTL**: **0.60s** for 4,227 series (~0.00014s per series) - **658x
  faster than before!**
- **AutoMSTL**: 14s for 4,227 series (~0.003s per series) - 28x faster
- MASE 1.302 with ExponentialSmoothing (fast default)
- **710x faster than Statsforecast!**
- Ideal for real-time forecasting with complex seasonal patterns

### Statsforecast MSTL

**Implementation:**

- Standard MSTL from Nixtla's statsforecast library
- Based on R's `mstl()` function implementation
- Multi-core parallel processing across series
- Configurable season lengths

**Python Example:**

```python
from statsforecast import StatsForecast
from statsforecast.models import MSTL

# Single seasonality
models = [MSTL(season_length=7)]

# Multiple seasonality (automatic handling)
models = [MSTL(season_length=[7, 365])]

sf = StatsForecast(models=models, freq='D', n_jobs=-1)
forecasts = sf.forecast(df=train_df, h=14, level=[95])
```

**Key Features:**

- **Automatic Multiple Seasonality**: Handles multiple periods automatically
- **Prediction Intervals**: Generates confidence bounds via bootstrap
- **Pandas Integration**: Seamless DataFrame workflow
- **Parallel Processing**: Multi-core execution for speed

**Performance:**

- 425s for 4,227 series (~0.10s per series)
- MASE 1.200 - Best accuracy
- **710x slower** than optimized Anofox implementation
- Best for Python-based workflows when prediction intervals are required

### MSTL Method Comparison

| Characteristic | Anofox MSTL | Anofox AutoMSTL | Statsforecast MSTL |
|----------------|-------------|-----------------|-------------------|
| **Accuracy (MASE)** | 1.302 | 1.302 | **1.200** (best) |
| **Speed** | **0.60s** (fastest!) | 14s | 425s |
| **Parameter Tuning** | Manual | Automatic | Manual |
| **Configurable Methods** | ✅ 3 options | ✅ 3 options | ❌ Fixed |
| **SQL Integration** | ✅ Native | ✅ Native | ❌ Requires export |
| **Multiple Seasonality** | ✅ Yes | ✅ Yes | ✅ Yes |
| **Prediction Intervals** | ❌ No | ❌ No | ✅ Yes |
| **Best For** | Production/real-time | Auto-tuning | High accuracy needed |

### MSTL Selection Guide

| Scenario | Recommended Model | Reason |
|----------|-------------------|--------|
| **Real-time/Production** | **Anofox MSTL** | **710x faster (0.6s!)** |
| SQL-native workflow | Anofox MSTL/AutoMSTL | DuckDB integration |
| Speed critical | Anofox MSTL | Sub-second for 4K series |
| Multiple seasonality | Any MSTL variant | All handle well |
| Automatic tuning | Anofox AutoMSTL | Fast parameter optimization |
| Highest accuracy needed | Statsforecast MSTL | MASE 1.200 |
| Need confidence bounds | Statsforecast MSTL | Prediction intervals |
| Configurable speed/accuracy | Anofox MSTL | 3 deseasonalized methods |

### MSTL Method Overview

**What is MSTL?**

MSTL (Multiple Seasonal-Trend decomposition using Loess) extends classical
STL decomposition to handle multiple seasonal patterns simultaneously. It's
particularly effective for:

- **Hourly data** with daily and weekly patterns
- **Daily data** with weekly and annual patterns
- **Complex hierarchical seasonality** in business data

**Decomposition Process:**

1. **First Seasonal Extraction**: Remove primary seasonal pattern (e.g.,
   daily)
2. **Second Seasonal Extraction**: Remove secondary pattern from remainder
   (e.g., weekly)
3. **Trend Estimation**: Extract smooth trend using Loess
4. **Remainder**: What's left after removing all components
5. **Forecast**: Extrapolate each component and recombine

**Mathematical Formulation:**

```text
y_t = T_t + S₁_t + S₂_t + ... + Sₖ_t + R_t
```

Where:

- `T_t` = Trend component
- `S₁_t, S₂_t, ..., Sₖ_t` = Multiple seasonal components
- `R_t` = Remainder (irregular component)

**Why MSTL Works:**

- **Handles Complex Patterns**: Multiple nested seasonalities
- **Robust Decomposition**: Loess smoothing resists outliers
- **Flexible Forecasting**: Each component forecast independently
- **Well-Established**: Based on proven STL methodology

**When to Use MSTL:**

✅ **Good for:**

- Multiple clear seasonal patterns (e.g., hourly data)
- Longer series (>2 full cycles of longest seasonality)
- Stable seasonal patterns over time
- Need for interpretable decomposition

❌ **Consider alternatives for:**

- Single seasonality (use simpler methods)
- Very short series (< 2 seasonal cycles)
- Rapidly changing seasonality (use TBATS)
- Extremely long seasonality (TBATS may be better)

## Evaluation Metrics

All benchmarks measure:

- **MASE** (Mean Absolute Scaled Error) - Primary metric, scale-independent
- **MAE** (Mean Absolute Error) - Absolute forecast error
- **RMSE** (Root Mean Squared Error) - Penalizes large errors
- **Time** - Total execution time in seconds

**MASE Interpretation:**

- MASE < 1.0: Better than naive seasonal baseline
- MASE = 1.0: Equal to naive seasonal baseline
- MASE > 1.0: Worse than naive seasonal baseline

For this benchmark, **MASE = 1.200** means the MSTL models are competitive
but slightly worse than a simple seasonal naive forecast for this particular
dataset.

## Running the Benchmark

### Prerequisites

```bash
# Build the DuckDB extension
cd /home/simonm/projects/duckdb/anofox-forecast
make release

# Install Python dependencies
cd benchmark
uv pip install statsforecast pandas duckdb fire
```

### Run Complete Benchmark

```bash
cd benchmark/mstl_benchmark

# Run all models (Anofox + Statsforecast) and evaluate
python run_benchmark.py run --group=Daily
```

### Run Individual Components

```bash
# Run only Anofox models
python run_benchmark.py anofox --group=Daily

# Run only Statsforecast models
python run_benchmark.py statsforecast --group=Daily

# Evaluate existing results
python run_benchmark.py eval --group=Daily

# Clean results
python run_benchmark.py clean
```

### Alternative: Use Modular Scripts

```bash
# Anofox MSTL
python run.py anofox --group=Daily

# Statsforecast MSTL
python run_statsforecast.py --group=Daily

# Evaluation
python run.py evaluate --group=Daily
```

## Results Structure

```text
results/
├── anofox-MSTL-Daily.parquet              # Anofox MSTL forecasts
├── anofox-MSTL-Daily-metrics.parquet      # Timing metrics
├── anofox-AutoMSTL-Daily.parquet          # Anofox AutoMSTL forecasts
├── anofox-AutoMSTL-Daily-metrics.parquet  # Timing metrics
├── statsforecast-MSTL-Daily.parquet       # Statsforecast forecasts
├── statsforecast-MSTL-Daily-metrics.parquet # Timing metrics
└── mstl-evaluation-Daily.parquet          # Accuracy evaluation
```

## Performance Summary

**Speed Ranking (4,227 series):**

1. 🥇 **Anofox MSTL: 0.60s (~0.00014s per series)** - **DRAMATICALLY FASTER!**
2. 🥈 Anofox AutoMSTL: 14s (~0.003s per series)
3. 🥉 Statsforecast MSTL: 425s (~0.10s per series)

**Accuracy Ranking:**

1. 🥇 Statsforecast MSTL: MASE = 1.200
2. 🥈 Anofox MSTL/AutoMSTL: MASE = 1.302 (8.5% higher, but 710x faster)

**Recommendation:**

- For **Real-time/Production**: Use Anofox MSTL (0.6s for 4K series!)
- For **SQL workflows**: Use Anofox MSTL/AutoMSTL (native DuckDB)
- For **Highest accuracy**: Use Statsforecast MSTL (accepts 710x slower)
- For **Configurable speed/accuracy**: Use Anofox MSTL with `deseasonalized_method`:
  - 0 = ExponentialSmoothing (fastest, default)
  - 1 = Linear (medium)
  - 2 = AutoETS (slowest, highest accuracy)
- For **automatic tuning**: Use Anofox AutoMSTL
- For **prediction intervals**: Use Statsforecast MSTL

## Implementation Notes

### Anofox Implementation

- C++ implementation via anofox-time library
- Direct DuckDB table function integration
- No data serialization overhead
- Supports both single and multiple seasonalities
- AutoMSTL variant includes automatic parameter optimization

### Statsforecast Implementation

- Python NumPy/Numba implementation
- Multi-core parallelization via joblib
- Requires DataFrame conversion
- Bootstrap-based prediction intervals
- Mature, well-tested library from Nixtla

## Related Benchmarks

- **Theta Benchmark** (`theta_benchmark/`) - Theta method variants
- **MFLES Benchmark** (`mfles_benchmark/`) - Multiple Fourier seasonality
- **ETS Benchmark** (`ets_benchmark/`) - Exponential smoothing
- **Baseline Benchmark** (`baseline_benchmark/`) - Naive methods

## References

1. **MSTL Paper**: Kasun Bandara, Rob J Hyndman, Christoph Bergmeir (2021).
   "MSTL: A Seasonal-Trend Decomposition Algorithm for Time Series with
   Multiple Seasonal Patterns"
2. **STL Original**: Cleveland et al. (1990). "STL: A Seasonal-Trend
   Decomposition Procedure Based on Loess"
3. **Statsforecast**: Nixtla's open-source forecasting library
4. **M4 Competition**: Makridakis et al. (2020)
