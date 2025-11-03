"""ETS (Exponential Smoothing) models configuration."""

BENCHMARK_NAME = 'ets'

# Define models with their parameter functions
MODELS = [
    {
        'name': 'SES',
        'params': lambda seasonality: {}
    },
    {
        'name': 'SESOptimized',
        'params': lambda seasonality: {}
    },
    {
        'name': 'SeasonalES',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
    {
        'name': 'SeasonalESOptimized',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
    {
        'name': 'Holt',
        'params': lambda seasonality: {}
    },
    {
        'name': 'HoltWinters',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
    {
        'name': 'ETS',
        'params': lambda seasonality: {}
    },
    {
        'name': 'AutoETS',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
]
