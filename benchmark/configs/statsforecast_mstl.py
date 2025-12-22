"""Statsforecast MSTL model configuration."""

from statsforecast.models import MSTL

BENCHMARK_NAME = 'MSTL'

# Define models with their factories and parameters
def get_models_config(seasonality: int, horizon: int):
    """
    Get MSTL model configuration.

    Parameters
    ----------
    seasonality : int
        Seasonal period
    horizon : int
        Forecast horizon (not used for MSTL)

    Returns
    -------
    list
        List of model configurations
    """
    return [
        {
            'model_factory': MSTL,
            'params': {'season_length': seasonality}
        },
    ]

# MSTL supports prediction intervals
INCLUDE_PREDICTION_INTERVALS = True

