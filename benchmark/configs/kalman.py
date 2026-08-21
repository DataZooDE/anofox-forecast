"""Kalman filter model configuration for anofox-forecast benchmark.

KalmanForecaster supports two state-space specifications:
  - 'local_level': random walk + noise (default)
  - 'local_linear_trend': level + trend state-space

Reference: statsmodels UnobservedComponents (local level / local linear trend).
Parity standard: behavioral/approximate — anofox uses fixed variance params
(obs_var=1.0, level_var=0.1) while statsmodels estimates via MLE; exact numeric
match is NOT expected.
"""

BENCHMARK_NAME = 'kalman'

# Uses the standard per-series ts_forecast_by surface
FUNCTION_NAME = 'TS_FORECAST_BY'

# Moderate cap — Kalman is fast but statsmodels MLE reference is slower
MAX_SERIES = 200

MODELS = [
    {
        'name': 'Kalman',
        'params': lambda seasonality: {}  # default: local_level
    },
    {
        'name': 'Kalman',
        'params': lambda seasonality: {'kalman_model': 'local_linear_trend'}
    },
]
