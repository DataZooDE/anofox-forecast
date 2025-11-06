# MFLES Benchmark

Comprehensive benchmarking comparing MFLES (Multiple Forecast Length Exponential Smoothing) implementations from **anofox-forecast** and **statsforecast**.

## Latest Results

**M4 Daily Dataset** (4,227 series, horizon=14, seasonality=7):

| Implementation | Model | MASE | MAE | RMSE | Time (s) | Note |
|----------------|-------|------|-----|------|----------|------|
| Statsforecast | MFLES | **1.184** | 185.38 | 217.10 | 81.4 | ⚠️ Significantly better |
| Anofox | MFLES | 1.887 | 296.36 | 322.79 | 4.1 | Baseline |
| Anofox | AutoMFLES | 1.888 | 297.39 | 323.55 | 547.5 | Auto-tuned |

### Key Findings

**Performance Gap:**
- Statsforecast MFLES achieves **MASE 1.184** vs Anofox **1.887** (37% better)
- This significant gap requires investigation
- Likely due to implementation differences in decomposition or smoothing

**AutoMFLES Performance:**
- Successfully matches manually-tuned baseline (1.888 vs 1.887)
- Evaluates 24 configurations via cross-validation
- **133x overhead** for automatic parameter selection (548s vs 4.1s)
- Trade-off: Automatic tuning convenience vs execution time

**Speed Comparison:**
- Anofox MFLES is fastest: 4.1s (manual configuration required)
- Statsforecast MFLES: 81.4s (~20x slower, but much better accuracy)
- Anofox AutoMFLES: 547.5s (automatic selection, matches baseline accuracy)

**Recommendations:**
- Currently, **Statsforecast MFLES is recommended** for production use (best accuracy)
- Anofox MFLES requires performance investigation
- AutoMFLES useful for automatic parameter selection when accuracy matches baseline

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

### Anofox AutoMFLES

**Automatic Hyperparameter Optimization:**
- Grid search over 24 configurations (statsforecast grid)
- Parameters optimized via cross-validation:
  - `seasonality_weights`: true/false
  - `smoother`: true/false (ES ensemble vs MA smoothing)
  - `ma_window`: -1 (period), -2 (period/2), -3 (none)
  - `seasonal_period`: true/false (use seasonality)

- **User-Configurable Options**:
  - `max_rounds`: Boosting iterations (default: 10, tuned)
  - `lr_trend`, `lr_season`, `lr_rs`: Learning rates (defaults: 0.3, 0.5, 0.8)
  - `cv_horizon`: Cross-validation forecast horizon (default: auto = season_length)
  - `cv_n_windows`: Number of CV windows (default: 2)

**SQL Example:**
```sql
SELECT * FROM TS_FORECAST_BY(
    'sales', store_id, date, revenue,
    'AutoMFLES', 14,
    {
        'seasonal_periods': [7],
        'max_rounds': 20,
        'lr_trend': 0.5,
        'cv_n_windows': 3
    }
);
```

**Cross-Validation Strategy:**
- Rolling window CV with 2 windows by default
- Each configuration evaluated on held-out test sets
- Best configuration selected by lowest MAE
- Final model trained on full dataset with best parameters

### Statsforecast MFLES

**Implementation:**
- Standard MFLES from Nixtla's statsforecast library
- Multi-core parallel processing
- Configurable season length
- Standard exponential smoothing approach

**Key Differences from Anofox:**
- Different decomposition strategy
- Different smoothing algorithms
- Optimized for speed and accuracy
- Production-tested on large datasets

### Performance Investigation Needed

The 37% accuracy gap between implementations suggests:
1. Possible issues in Anofox decomposition logic
2. Different hyperparameter defaults
3. Smoothing algorithm differences
4. Ensemble weighting differences

**Next Steps:**
- Compare decomposition outputs component-by-component
- Verify trend estimation methods
- Check seasonal component extraction
- Review ES ensemble implementation
- Test with different parameter configurations

## Evaluation Metrics

All benchmarks measure:
- **MASE** (Mean Absolute Scaled Error) - Primary metric, scale-independent
- **MAE** (Mean Absolute Error) - Absolute forecast error
- **RMSE** (Root Mean Squared Error) - Penalizes large errors
- **Time** - Total execution time in seconds

MASE < 1.0 means the model beats a naive seasonal baseline.
