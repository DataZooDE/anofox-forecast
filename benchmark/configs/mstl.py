"""MSTL (Multiple Seasonal-Trend decomposition using Loess) models configuration."""

BENCHMARK_NAME = 'mstl'

# Define models with their parameter functions
# Note: MSTL supports 'seasonal_periods' (plural) for multiple seasonality patterns
MODELS = [
    {
        'name': 'MSTL',
        'params': lambda seasonality: {'seasonal_periods': [seasonality]}
    },
]

