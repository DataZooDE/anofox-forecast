"""MFLES (Multi-Frequency Level-based Exponential Smoothing) models configuration."""

BENCHMARK_NAME = 'mfles'

# Define models with their parameter functions
MODELS = [
    {
        'name': 'MFLES-Fast',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
    {
        'name': 'MFLES-Balanced',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
    {
        'name': 'MFLES-Accurate',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
    {
        'name': 'MFLES-Robust',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
    {
        'name': 'AutoMFLES',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
]
