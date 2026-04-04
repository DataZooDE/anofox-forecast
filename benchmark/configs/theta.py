"""Theta models configuration."""

BENCHMARK_NAME = 'theta'

# Define models with their parameter functions
# Note: theta_param is not a valid MAP parameter — theta value is set internally
MODELS = [
    {
        'name': 'Theta',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
    {
        'name': 'OptimizedTheta',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
    {
        'name': 'DynamicTheta',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
    {
        'name': 'DynamicOptimizedTheta',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
]
