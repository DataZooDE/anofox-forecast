"""Global panel model configurations for anofox-forecast benchmark.

These models fit shared parameters across the entire panel (cross-series learning),
calling ts_forecast_panel_by with the panel variant of the anofox runner.
"""

BENCHMARK_NAME = 'global_ets'

# Tell the anofox runner to use the panel function instead of per-series ts_forecast_by.
# The panel function fits all series jointly via GlobalETS/GlobalTheta/GlobalCroston.
FUNCTION_NAME = 'TS_FORECAST_PANEL_BY'

MODELS = [
    {
        'name': 'GlobalETS',
        'params': lambda seasonality: {'seasonal_period': seasonality}
    },
    {
        'name': 'GlobalTheta',
        'params': lambda seasonality: {}
    },
    {
        'name': 'GlobalCroston',
        'params': lambda seasonality: {}
    },
]
