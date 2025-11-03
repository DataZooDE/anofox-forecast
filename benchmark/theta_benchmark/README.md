# Theta Benchmark Suite

Comprehensive benchmark comparing **Theta method variants** between Anofox-forecast (DuckDB extension) and Statsforecast.

## Status

**Infrastructure**: ✅ Complete and ready to use
**Results**: ⏳ Pending (benchmarks need to be run manually due to long execution time)

The benchmark suite is fully functional. Run the commands below to generate results on the M4 Competition datasets.

## Theta Variants Tested

### Anofox-forecast (4 variants)
- **Theta** - Standard Theta method with theta parameter = 2.0
- **OptimizedTheta** - Auto-optimized theta parameter selection
- **DynamicTheta** - Dynamic Theta with theta parameter = 2.0
- **DynamicOptimizedTheta** - Dynamic with auto-optimized theta

### Statsforecast (5 variants)
- **AutoTheta** - Automatic Theta model selection
- **Theta** - Standard Theta method
- **OptimizedTheta** - Optimized theta parameter
- **DynamicTheta** - Dynamic Theta
- **DynamicOptimizedTheta** - Dynamic with optimization

## Dataset

Uses the **M4 Competition** datasets:
- **Daily**: 4,227 series, mean length 2,371 observations
- **Hourly**: 414 series, mean length 901 observations
- **Weekly**: 359 series, mean length 1,035 observations

## Metrics

- **MASE** (Mean Absolute Scaled Error) - Primary metric
- **MAE** (Mean Absolute Error)
- **RMSE** (Root Mean Squared Error)
- **Time** (seconds to generate forecasts)

## Quick Start

### Prerequisites

1. **Build the extension first**:
   ```bash
   cd /path/to/anofox-forecast
   make release
   ```

2. **Install Python dependencies** (already done if using uv):
   ```bash
   cd benchmark
   uv sync --extra comparison
   ```

### Running Benchmarks

#### Run full benchmark (all models on Daily data):
```bash
cd benchmark
uv run python theta_benchmark/run_benchmark.py run --group=Daily
```

#### Run specific Anofox Theta variant:
```bash
# Specific variant
uv run python theta_benchmark/run_benchmark.py anofox --group=Daily --model=OptimizedTheta

# All Anofox variants
uv run python theta_benchmark/run_benchmark.py anofox --group=Daily
```

#### Run Statsforecast Theta variants:
```bash
uv run python theta_benchmark/run_benchmark.py statsforecast --group=Daily
```

#### Run on different datasets:
```bash
# Hourly data
uv run python theta_benchmark/run_benchmark.py run --group=Hourly

# Weekly data
uv run python theta_benchmark/run_benchmark.py run --group=Weekly
```

#### Evaluate existing results:
```bash
uv run python theta_benchmark/run_benchmark.py eval --group=Daily
```

#### Clean results:
```bash
uv run python theta_benchmark/run_benchmark.py clean
```

## Directory Structure

```
theta_benchmark/
├── README.md                 # This file
├── run_benchmark.py          # Main benchmark runner
├── src/                      # Source code
│   ├── __init__.py
│   ├── data.py              # Data loading utilities (shared with ARIMA)
│   ├── anofox_theta.py      # Anofox Theta benchmarks
│   ├── statsforecast_theta.py  # Statsforecast Theta benchmarks
│   └── evaluation_theta.py  # Evaluation metrics
├── data/                     # Downloaded datasets (shared, auto-created)
└── results/                  # Benchmark results (auto-created)
    ├── anofox-Theta-Daily.parquet
    ├── anofox-OptimizedTheta-Daily.parquet
    ├── anofox-DynamicTheta-Daily.parquet
    ├── anofox-DynamicOptimizedTheta-Daily.parquet
    ├── statsforecast-Theta-Daily.parquet
    └── evaluation-Theta-Daily.parquet
```

## Implementation Details

### Anofox-forecast (DuckDB Extension)

Uses the `TS_FORECAST_BY` function with Theta variants:

```python
# Example: OptimizedTheta
forecast_query = f"""
    SELECT
        unique_id,
        date_col AS ds,
        point_forecast AS forecast,
        lower,
        upper
    FROM TS_FORECAST_BY(
        'train',
        'unique_id',
        'ds',
        'y',
        'OptimizedTheta',
        {horizon},
        {{'seasonal_period': {seasonality}}}
    )
    ORDER BY unique_id, ds
"""
```

### Statsforecast

Uses Nixtla's optimized implementation:

```python
from statsforecast import StatsForecast
from statsforecast.models import AutoTheta, Theta, OptimizedTheta, DynamicTheta, DynamicOptimizedTheta

sf = StatsForecast(
    df=train_df,
    models=[
        AutoTheta(season_length=seasonality),
        Theta(season_length=seasonality),
        OptimizedTheta(season_length=seasonality),
        DynamicTheta(season_length=seasonality),
        DynamicOptimizedTheta(season_length=seasonality),
    ],
    freq=freq,
    n_jobs=-1,
)
fcst_df = sf.forecast(h=horizon)
```

## Troubleshooting

### Extension not found
```
ERROR: Extension not found at build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension
```
**Solution**: Build the extension first with `make release`

### Python version issues
The benchmark requires Python 3.11 or 3.12 (not 3.13 due to package compatibility).

**Solution**: Use uv's Python management:
```bash
uv python pin 3.12
uv sync
```

### Missing datasets
Datasets are automatically downloaded on first run using the `datasetsforecast` package.
The data directory is shared with the ARIMA benchmark.

## References

- [Theta Method Paper](https://doi.org/10.1016/S0169-2070(00)00066-2)
- [M4 Competition](https://www.sciencedirect.com/science/article/pii/S0169207019301128)
- [Nixtla Statsforecast](https://github.com/Nixtla/statsforecast)
- [MASE Metric](https://otexts.com/fpp3/accuracy.html)

## License

Same as parent project: Business Source License 1.1 (BSL 1.1)
