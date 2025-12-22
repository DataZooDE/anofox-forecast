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
            'params': {'alpha': 0.5, 'alias': 'SES'}
        },
        {
            'model_factory': SimpleExponentialSmoothingOptimized,
            'params': {'alias': 'SESOptimized'}
        },
        {
            'model_factory': SeasonalExponentialSmoothing,
            'params': {'season_length': seasonality, 'alpha': 0.5, 'alias': 'SeasonalES'}
        },
        {
            'model_factory': SeasonalExponentialSmoothingOptimized,
            'params': {'season_length': seasonality, 'alias': 'SeasonalESOptimized'},
        },
        {
            'model_factory': Holt,
            'params': {'alias': 'Holt'}
        },
        {
            'model_factory': HoltWinters,
            'params': {'season_length': seasonality, 'alias': 'HoltWinters'}
        },
        {
            'model_factory': AutoETS,
            'params': {'season_length': seasonality, 'alias': 'AutoETS'}
        },
    ]

# ETS models: disable prediction intervals to avoid errors with SimpleExponentialSmoothing
# Some ETS models don't support prediction intervals via level parameter
INCLUDE_PREDICTION_INTERVALS = False
