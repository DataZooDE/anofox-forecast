"""Statsforecast baseline models configuration."""

from statsforecast.models import Naive, SeasonalNaive, RandomWalkWithDrift, WindowAverage, SeasonalWindowAverage

BENCHMARK_NAME = 'Baseline'

# Define models with their factories and parameters
def get_models_config(seasonality: int, horizon: int):
    """
    Get baseline models configuration.

    Parameters
    ----------
    seasonality : int
        Seasonal period
    horizon : int
        Forecast horizon (used for window size calculation)

    Returns
    -------
    list
        List of model configurations
    """
    window_size = min(7, horizon)  # Use a reasonable window size

    return [
        {
            'model_factory': Naive,
            'params': {}
        },
        {
            'model_factory': SeasonalNaive,
            'params': {'season_length': seasonality}
        },
        {
            'model_factory': RandomWalkWithDrift,
            'params': {'model_name': 'RandomWalkWithDrift'}
        },
        {
            'model_factory': WindowAverage,
            'params': {'window_size': window_size, 'model_name': 'SMA'}
        },
        {
            'model_factory': SeasonalWindowAverage,
            'params': {'season_length': seasonality, 'window_size': window_size, 'model_name': 'SeasonalWindowAverage'}
        },
    ]

# Baseline models don't use prediction intervals in original implementation
INCLUDE_PREDICTION_INTERVALS = False
