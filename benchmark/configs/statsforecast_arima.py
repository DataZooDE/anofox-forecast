"""Statsforecast ARIMA model configuration."""

from statsforecast.models import AutoARIMA

BENCHMARK_NAME = 'statsforecast'  # Original script uses this name for output files

# Define models with their factories and parameters
def get_models_config(seasonality: int, horizon: int):
    """
    Get AutoARIMA model configuration.

    Parameters
    ----------
    seasonality : int
        Seasonal period
    horizon : int
        Forecast horizon (not used for AutoARIMA)

    Returns
    -------
    list
        List of model configurations
    """
    return [
        {
            'model_factory': AutoARIMA,
            'params': {'season_length': seasonality}
        },
    ]

# AutoARIMA uses prediction intervals
INCLUDE_PREDICTION_INTERVALS = True

# Original ARIMA script renames columns to standardized format
COLUMN_MAPPING = {
    'unique_id': 'id_cols',
    'ds': 'date_col',
    'AutoARIMA': 'forecast_col',
    'AutoARIMA-lo-95': 'lower',
    'AutoARIMA-hi-95': 'upper',
}
