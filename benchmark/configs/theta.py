"""Theta models configuration."""

BENCHMARK_NAME = 'theta'

# Define models with their parameter functions
MODELS = [
    {
        'name': 'Theta',
        'params': lambda seasonality: {'seasonal_period': seasonality, 'theta_param': 2.0}
    },
    {
        'name': 'OptimizedTheta',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
    {
        'name': 'DynamicTheta',
        'params': lambda seasonality: {'seasonal_period': seasonality, 'theta_param': 2.0}
    },
    {
        'name': 'DynamicOptimizedTheta',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
]
