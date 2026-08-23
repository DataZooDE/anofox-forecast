"""VAR multivariate model configuration for anofox-forecast benchmark.

VAR (Vector Autoregression) forecasts multiple time series simultaneously
via ts_forecast_var_by. The benchmark uses SYNTHETIC data (not M4) because
no multivariate M4/M5 dataset exists in the harness.

Reference: statsmodels.tsa.api.VAR on the same synthetic VAR(1) data.
Parity standard: behavioral/approximate — coefficient recovery within 5%
of ground-truth VAR(1) parameters; forecast MAE close to statsmodels reference.

Synthetic data: VAR(1) with k=2 variables, N=200 observations, seed=42.
  c=[0.5, 0.3], A=[[0.6, 0.1], [0.05, 0.7]]
"""

BENCHMARK_NAME = 'var'

# VAR uses its own dedicated function (not ts_forecast_by)
FUNCTION_NAME = 'TS_FORECAST_VAR_BY'

# Synthetic dataset — series count is not applicable for VAR
MAX_SERIES = 0
