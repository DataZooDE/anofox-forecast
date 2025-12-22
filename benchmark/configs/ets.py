"""ETS (Exponential Smoothing) models configuration."""

BENCHMARK_NAME = 'ets'

# Define models with their parameter functions
# Note: For models without seasonality, we pass None instead of {} to avoid SQL syntax errors
MODELS = [
    {
        'name': 'SES',
        'params': lambda seasonality: {'alpha': 0.5, 'model_name': 'SES'}
    },
    {
        'name': 'SESOptimized',
        'params': lambda seasonality: {'model_name': 'SESOptimized'}
    },
    {
        'name': 'SeasonalES',
        'params': lambda seasonality: {'seasonal_period': seasonality, 'alpha': 0.5, 'model_name': 'SeasonalES'}
    },
    {
        'name': 'SeasonalESOptimized',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
    {
        'name': 'Holt',
        'params': lambda seasonality: {'alpha': 0.5, 'model_name': 'Holt'}
    },
    {
        'name': 'HoltWinters',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
    {
        'name': 'AutoETS',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
]
