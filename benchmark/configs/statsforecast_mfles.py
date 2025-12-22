"""Statsforecast MFLES model configuration."""

from statsforecast.models import MFLES

BENCHMARK_NAME = 'MFLES'

# Define models with their factories and parameters
def get_models_config(seasonality: int, horizon: int):
    """
    Get MFLES model configuration.

    Parameters
    ----------
    seasonality : int
        Seasonal period
    horizon : int
        Forecast horizon (not used for MFLES)

    Returns
    -------
    list
        List of model configurations
    """
    return [
        {
            'model_factory': MFLES,
            'params': {'season_length': seasonality}
        },
    ]

# MFLES requires prediction intervals to be configured at model initialization
# For now, disable prediction intervals to get base forecasts working
INCLUDE_PREDICTION_INTERVALS = False
