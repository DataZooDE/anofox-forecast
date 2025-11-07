"""ETS (Exponential Smoothing) models configuration."""

BENCHMARK_NAME = 'ets'

# Define models with their parameter functions
# Note: For models without seasonality, we pass None instead of {} to avoid SQL syntax errors
MODELS = [
    {
        'name': 'SESOptimized',
        'params': lambda seasonality: None  # No parameters needed
    },
    {
        'name': 'SeasonalESOptimized',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
    {
        'name': 'HoltWinters',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
    {
        'name': 'AutoETS',
        'params': lambda seasonality: {'season_length': seasonality}
    },
]
