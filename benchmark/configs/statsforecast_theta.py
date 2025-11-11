"""Statsforecast Theta models configuration."""

from statsforecast.models import AutoTheta, Theta, OptimizedTheta, DynamicTheta, DynamicOptimizedTheta

BENCHMARK_NAME = 'Theta'

# Define models with their factories and parameters
def get_models_config(seasonality: int, horizon: int):
    """
    Get Theta models configuration.

    Parameters
    ----------
    seasonality : int
        Seasonal period
    horizon : int
        Forecast horizon (not used for Theta models)

    Returns
    -------
    list
        List of model configurations
    """
    return [
        {
            'model_factory': Theta,
            'params': {'season_length': seasonality}
        },
        {
            'model_factory': OptimizedTheta,
            'params': {'season_length': seasonality}
        },
        {
            'model_factory': DynamicTheta,
            'params': {'season_length': seasonality}
        },
        {
            'model_factory': DynamicOptimizedTheta,
            'params': {'season_length': seasonality}
        },
    ]

# Theta models use prediction intervals
INCLUDE_PREDICTION_INTERVALS = True
