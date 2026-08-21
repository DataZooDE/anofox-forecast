"""Statsforecast reference models for the global panel benchmark.

Reference models chosen for behavioral/approximate parity (D-Area4):
- GlobalETS   -> AutoETS: same ETS spec selection (per-series rather than pooled, but
                           comparable accuracy on M4 Daily — behavioral parity standard)
- GlobalTheta -> AutoTheta: standard Theta method, same family as GlobalTheta's pooled alpha
- GlobalCroston -> CrostonOptimized: per-series optimized Croston, closest available reference

Note: statsforecast has no exact GlobalETS/GlobalTheta/GlobalCroston equivalents because
it uses per-series fitting. Parity is behavioral/approximate per project CONTEXT.md D-Area4.
"""

from statsforecast.models import AutoETS, AutoTheta, CrostonOptimized

BENCHMARK_NAME = 'statsforecast-global'

def get_models_config(seasonality: int, horizon: int):
    """
    Get statsforecast reference models for global panel benchmark.

    Parameters
    ----------
    seasonality : int
        Seasonal period
    horizon : int
        Forecast horizon

    Returns
    -------
    list
        List of model configurations
    """
    return [
        {
            'model_factory': AutoETS,
            'params': {'season_length': seasonality, 'alias': 'AutoETS'},
            'display_name': 'AutoETS',
        },
        {
            'model_factory': AutoTheta,
            'params': {'season_length': seasonality, 'alias': 'AutoTheta'},
            'display_name': 'AutoTheta',
        },
        {
            'model_factory': CrostonOptimized,
            'params': {'alias': 'CrostonOptimized'},
            'display_name': 'CrostonOptimized',
        },
    ]

# Disable prediction intervals: CrostonOptimized does not support them
INCLUDE_PREDICTION_INTERVALS = False
