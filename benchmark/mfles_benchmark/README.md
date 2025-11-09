# MFLES Benchmark

Comprehensive benchmarking comparing MFLES (Multiple Forecast Length Exponential Smoothing) implementations from **anofox-forecast** and **statsforecast**.

## Latest Results

**M4 Daily Dataset** (4,227 series, horizon=14, seasonality=7):

| Implementation | Model | MASE | MAE | RMSE | Time (s) | Note |
|----------------|-------|------|-----|------|----------|------|
| Anofox | MFLES | **1.179** | 181.62 | 212.87 | 21 | ⭐ Best accuracy |
| Statsforecast | MFLES | 1.184 | 185.38 | 217.10 | 161 | Very close |

### Key Findings

**Performance:**
- **Anofox MFLES achieves MASE 1.179** - Best MFLES implementation
- **Statsforecast MFLES achieves MASE 1.184** - Very close (0.4% difference)
- Both implementations perform well on M4 Daily dataset

**Speed Comparison:**
- Anofox MFLES: 21s (7.6x faster than Statsforecast)
- Statsforecast MFLES: 161s

**Recommendations:**
- **Anofox MFLES recommended** for production use (best accuracy + faster)
- Both implementations provide excellent results for forecasting
- Anofox provides better speed/accuracy trade-off

## Implementation Details

### Anofox MFLES

**Core Features:**
- **5-Component Decomposition**:
  - Median baseline for robust center
  - Gradient-boosted trend estimation
  - Weighted Fourier seasonality with automatic period detection
  - Multi-alpha exponential smoothing ensemble
  - Moving window medians for adaptive baseline

- **Robust Trend Methods**:
  - OLS (Ordinary Least Squares) - Default, fast
  - Siegel Robust Regression - Outlier-resistant
  - Piecewise Linear - Handles changepoints

- **Configurable Parameters**:
  - `max_rounds`: Number of boosting iterations (default: 10)
  - `seasonal_periods`: List of seasonal periods (e.g., [7, 365])
  - `fourier_order`: Harmonics per seasonal period (default: auto)
  - `lr_trend`, `lr_season`, `lr_rs`: Learning rates for components
  - `trend_method`: OLS, Siegel, or Piecewise
  - `ma_window`: Moving average window for smoothing
  - `es_ensemble_size`: Number of ES forecasts to ensemble (default: 20)

**SQL Example:**
```sql
SELECT * FROM TS_FORECAST_BY(
    'sales', store_id, date, revenue,
    'MFLES', 14,
    {
        'seasonal_periods': [7],
        'max_rounds': 10,
        'lr_trend': 0.3,
        'lr_season': 0.5,
        'lr_rs': 0.8
    }
);
```

### Statsforecast MFLES

**Implementation:**
- Standard MFLES from Nixtla's statsforecast library
- Multi-core parallel processing
- Configurable season length
- Standard exponential smoothing approach

**Key Differences from Anofox:**
- Different decomposition strategy
- Different smoothing algorithms
- Both implementations well-optimized
- Very similar accuracy results (within 0.4%)

## Evaluation Metrics

All benchmarks measure:
- **MASE** (Mean Absolute Scaled Error) - Primary metric, scale-independent
- **MAE** (Mean Absolute Error) - Absolute forecast error
- **RMSE** (Root Mean Squared Error) - Penalizes large errors
- **Time** - Total execution time in seconds

MASE < 1.0 means the model beats a naive seasonal baseline.
