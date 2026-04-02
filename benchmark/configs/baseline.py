"""Baseline models configuration."""

BENCHMARK_NAME = 'baseline'

# Define models with their parameter functions
# Note: Naive, RandomWalkDrift, SMA don't accept seasonal_period
MODELS = [
    {
        'name': 'Naive',
        'params': lambda seasonality: {}
    },
    {
        'name': 'SeasonalNaive',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
    {
        'name': 'RandomWalkDrift',
        'params': lambda seasonality: {}
    },
    {
        'name': 'SMA',
        'params': lambda seasonality: {}
    },
    {
        'name': 'SeasonalWindowAverage',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
]
