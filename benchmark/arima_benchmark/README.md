# ARIMA Benchmark Suite

Comprehensive benchmark comparing **anofox-forecast AutoARIMA** (DuckDB extension) with other popular ARIMA implementations:
- **statsforecast** (Nixtla's optimized implementation)
- **pmdarima** (Python's popular auto_arima)
- **prophet** (Facebook's forecasting tool) [optional]

Based on the [Nixtla statsforecast ARIMA benchmark](https://github.com/Nixtla/statsforecast/tree/main/experiments/arima).

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
   cd /home/simonm/projects/duckdb/anofox-forecast
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
uv run python arima_benchmark/run_benchmark.py run --group=Daily
```

#### Run specific model:
```bash
# Anofox-forecast AutoARIMA
uv run python arima_benchmark/run_benchmark.py model anofox Daily

# Statsforecast
uv run python arima_benchmark/run_benchmark.py model statsforecast Daily

# pmdarima
uv run python arima_benchmark/run_benchmark.py model pmdarima Daily
```

#### Run on different datasets:
```bash
# Hourly data
uv run python arima_benchmark/run_benchmark.py run --group=Hourly

# Weekly data
uv run python arima_benchmark/run_benchmark.py run --group=Weekly
```

#### Evaluate existing results:
```bash
uv run python arima_benchmark/run_benchmark.py eval Daily
```

#### Clean results:
```bash
uv run python arima_benchmark/run_benchmark.py clean
```

## Directory Structure

```
arima_benchmark/
├── README.md                 # This file
├── run_benchmark.py          # Main benchmark runner
├── src/                      # Source code
│   ├── __init__.py
│   ├── data.py              # Data loading utilities
│   ├── anofox.py            # Anofox-forecast benchmark
│   ├── statsforecast.py     # Statsforecast benchmark
│   ├── pmdarima.py          # pmdarima benchmark
│   └── evaluation.py        # Evaluation metrics
├── data/                     # Downloaded datasets (auto-created)
└── results/                  # Benchmark results (auto-created)
    ├── anofox-Daily.csv
    ├── anofox-Daily-metrics.csv
    ├── statsforecast-Daily.csv
    ├── statsforecast-Daily-metrics.csv
    ├── pmdarima-Daily.csv
    ├── pmdarima-Daily-metrics.csv
    └── evaluation-Daily.csv
```

## Expected Results

Based on the original Nixtla benchmark and our implementation:

### Daily Dataset (4,227 series)
| Model | MASE | Time |
|-------|------|------|
| anofox | ? | ? |
| statsforecast | 3.26 | 1.41s |
| pmdarima | ? | ~120s |

### Performance Goals

- **Accuracy**: Achieve MASE comparable to statsforecast (state-of-the-art)
- **Speed**: Leverage DuckDB's parallelization for competitive performance
- **Scalability**: Handle thousands of series efficiently

## Implementation Details

### Anofox-forecast (DuckDB Extension)

Uses the `TS_FORECAST_BY` function with AutoARIMA model:

```python
forecast_query = f"""
    SELECT
        unique_id,
        date_col AS ds,
        point_forecast AS AutoARIMA,
        lower,
        upper,
        forecast_step
    FROM TS_FORECAST_BY(
        'train',
        'unique_id',
        'ds',
        'y',
        'AutoARIMA',
        {horizon},
        {{'seasonal_period': {seasonality}, 'confidence_level': 0.95}}
    )
    ORDER BY unique_id, forecast_step
"""
```

### Statsforecast

Uses Nixtla's optimized C++ implementation:

```python
from statsforecast import StatsForecast
from statsforecast.models import AutoARIMA

sf = StatsForecast(
    df=train_df,
    models=[AutoARIMA(season_length=seasonality)],
    freq=freq,
    n_jobs=-1,
)
fcst_df = sf.forecast(h=horizon, level=[95])
```

### pmdarima

Uses the popular Python implementation:

```python
from pmdarima import auto_arima

model = auto_arima(
    series_data,
    seasonal=True,
    m=seasonality,
    stepwise=True,
)
forecast, conf_int = model.predict(n_periods=horizon, return_conf_int=True)
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

## Contributing

To add a new model:

1. Create `src/newmodel.py` following the existing pattern
2. Implement data loading, forecasting, and result saving
3. Add to the models list in `run_benchmark.py`
4. Update this README

## References

- [Original Nixtla ARIMA Benchmark](https://github.com/Nixtla/statsforecast/tree/main/experiments/arima)
- [M4 Competition](https://www.sciencedirect.com/science/article/pii/S0169207019301128)
- [MASE Metric](https://otexts.com/fpp3/accuracy.html)
- [Anofox-forecast Documentation](../../guides/)

## License

Same as parent project: Business Source License 1.1 (BSL 1.1)
