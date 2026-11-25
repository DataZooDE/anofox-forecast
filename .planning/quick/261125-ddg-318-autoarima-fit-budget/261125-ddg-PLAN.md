---
phase: quick-261125-ddg
plan: 01
type: execute
requirements: []
---

# 261125-ddg: #318 AutoARIMA fit-cost limits in SQL (crate 0.21.0 time budget / max_models)

Executed inline. User decision: defaults stay unbounded (no budget, max_models = 94 as in R),
so no existing result changes; the limits are opt-in.

1. Core: `AutoArimaLimits { max_models, time_budget_ms }` on `ForecastOptions` /
   `ForecastOptionsExog`; `new_auto_arima` applies it on every AutoARIMA construction
   (forecast, exog, inspect, CV factory). Setting either on a model other than AutoARIMA is
   an error (same rule as arima_order). Zero / negative rejected.
2. FFI: `max_models` / `time_budget_ms` (int64, 0 = default) at the end of both option
   structs; header regenerated.
3. C++: new keys `max_models`, `time_budget_ms` in every params parser
   (ts_forecast_params.hpp, ts_forecast_native, ts_cv_forecast_native, exog ts_forecast).
4. Tests: Rust unit tests (budget honoured, validation); sqllogic test through the
   `ts_forecast_by` / `ts_forecast_agg` / `ts_cv_forecast_by` / inspect macros; the W118
   case from the issue finishes within the budget.
5. Docs + example: docs/api/07-forecasting.md params table, example SQL, CHANGELOG.
6. Also: confirm #321 (permutation_entropy determinism) on the current build.
