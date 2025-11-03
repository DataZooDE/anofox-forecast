"""Statsforecast ETS models configuration."""

from statsforecast.models import (
    SimpleExponentialSmoothing,
    SimpleExponentialSmoothingOptimized,
    SeasonalExponentialSmoothing,
    SeasonalExponentialSmoothingOptimized,
    Holt,
    HoltWinters,
    AutoETS,
)

BENCHMARK_NAME = 'ETS'

# Define models with their factories and parameters
def get_models_config(seasonality: int, horizon: int):
    """
    Get ETS models configuration.

    Parameters
    ----------
    seasonality : int
        Seasonal period
    horizon : int
        Forecast horizon (not used for ETS models)

    Returns
    -------
    list
        List of model configurations
    """
    return [
        {
            'model_factory': SimpleExponentialSmoothing,
            'params': {'alpha': 0.3}
        },
        {
            'model_factory': SimpleExponentialSmoothingOptimized,
            'params': {}
        },
        {
            'model_factory': SeasonalExponentialSmoothing,
            'params': {'season_length': seasonality, 'alpha': 0.2, 'gamma': 0.1}
        },
        {
            'model_factory': SeasonalExponentialSmoothingOptimized,
            'params': {'season_length': seasonality}
        },
        {
            'model_factory': Holt,
            'params': {}
        },
        {
            'model_factory': HoltWinters,
            'params': {'season_length': seasonality}
        },
        {
            'model_factory': AutoETS,
            'params': {'season_length': seasonality}
        },
    ]

# ETS models use prediction intervals
INCLUDE_PREDICTION_INTERVALS = True
