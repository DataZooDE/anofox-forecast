"""Baseline models configuration."""

BENCHMARK_NAME = 'baseline'

# Define models with their parameter functions
MODELS = [
    {
        'name': 'Naive',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
    {
        'name': 'SeasonalNaive',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
    {
        'name': 'RandomWalkWithDrift',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
    {
        'name': 'SMA',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
    {
        'name': 'SeasonalWindowAverage',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
]
