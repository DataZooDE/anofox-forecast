"""ARIMA models configuration."""

BENCHMARK_NAME = 'arima'

# Define models with their parameter functions
MODELS = [
    {
        'name': 'AutoARIMA',
        'params': lambda seasonality: {'seasonal_period': seasonality, 'confidence_level': 0.95}
    },
]
