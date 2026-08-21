"""Global panel model configurations for anofox-forecast benchmark.

These models fit shared parameters across the entire panel (cross-series learning),
calling ts_forecast_panel_by with the panel variant of the anofox runner.
"""

BENCHMARK_NAME = 'global_ets'

# Tell the anofox runner to use the panel function instead of per-series ts_forecast_by.
# The panel function fits all series jointly via GlobalETS/GlobalTheta/GlobalCroston.
FUNCTION_NAME = 'TS_FORECAST_PANEL_BY'

# Limit to a representative subset for GlobalETS (full M4 Daily = 4,227 series with
# avg 2,357 obs and seasonality=7 takes ~6 min per GlobalETS run with Reduced pool).
# 500 series is sufficient for behavioral/approximate parity evaluation (CONTEXT D-Area4).
# Set to 0 to run on all series.
MAX_SERIES = 500

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
