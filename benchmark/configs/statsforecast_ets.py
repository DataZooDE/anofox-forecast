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
            'params': {'alpha': 0.5, 'model_name': 'SES'}
        },
        {
            'model_factory': SimpleExponentialSmoothingOptimized,
            'params': {'model_name': 'SESOptimized'}
        },
        {
            'model_factory': SeasonalExponentialSmoothing,
            'params': {'season_length': seasonality, 'alpha': 0.5, 'model_name': 'SeasonalES'}
        },
        {
            'model_factory': SeasonalExponentialSmoothingOptimized,
            'params': {'season_length': seasonality, 'model_name': 'SeasonalESOptimized'},
        },
        {
            'model_factory': Holt,
            'params': {'model_name': 'Holt'}
        },
        {
            'model_factory': HoltWinters,
            'params': {'season_length': seasonality, 'model_name': 'HoltWinters'}
        },
        {
            'model_factory': AutoETS,
            'params': {'season_length': seasonality, 'model_name': 'AutoETS'}
        },
    ]

# ETS models: disable prediction intervals to avoid errors with SimpleExponentialSmoothing
# Some ETS models don't support prediction intervals via level parameter
INCLUDE_PREDICTION_INTERVALS = False
