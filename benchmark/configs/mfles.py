"""MFLES (Multiple Fourier and Linear Exponential Smoothing) models configuration."""

BENCHMARK_NAME = 'mfles'

# Define models with their parameter functions
# Note: MFLES supports 'seasonal_periods' (plural) for multiple seasonality patterns
MODELS = [
    {
        'name': 'MFLES',
        'params': lambda seasonality: {'seasonal_periods': [seasonality]}
    },
    {
        'name': 'AutoMFLES',
        'params': lambda seasonality: {'seasonal_periods': [seasonality]}
    },
]
