"""GARCH model configuration for anofox-forecast benchmark.

GARCH forecasts conditional volatility (standard deviation = sqrt(forecast_variance)).
Unlike most ts_forecast_by models, GARCH is designed for financial returns
(first differences), NOT raw price levels.

Reference: arch package (Kevin Sheppard) — GARCH(1,1) via arch.arch_model.
Parity standard: behavioral/approximate (not exact numeric match).
"""

BENCHMARK_NAME = 'garch'

# Uses the standard per-series ts_forecast_by surface
FUNCTION_NAME = 'TS_FORECAST_BY'

# Cap series count — GARCH is slower than baseline models (MLE per series)
MAX_SERIES = 100

MODELS = [
    {
        'name': 'GARCH',
        'params': lambda seasonality: {}  # No seasonal params for GARCH
    },
]
