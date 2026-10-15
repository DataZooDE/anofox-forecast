# Deferred Items

Out-of-scope discoveries logged during plan execution (per executor deviation rules'
scope boundary). Not fixed here.

## Plan 18-01

- **Bare `frequency` parameter passed to a VARCHAR-typed native function in
  `ts_forecast_panel_by`, `ts_forecast_var_by`, and `ts_stats`/`ts_stats_by`.**
  `src/macros/ts_macros.cpp` passes the macro's `frequency` positional parameter
  straight through to `_ts_forecast_panel_native` / `_ts_forecast_var_native` /
  `_ts_stats_by_native` with no `::VARCHAR` cast. This is the same class of bug fixed
  in this plan for `ts_fill_gaps_by`, `ts_fill_forward_by`, and `ts_forecast_by`
  (18-01, commit 52e82ad): when `frequency` is called with an INTEGER literal
  (e.g. `ts_forecast_panel_by(..., 12, 1)`) instead of a VARCHAR literal (`'1d'`), the
  call fails to bind against the VARCHAR-only native-function overload. None of this
  plan's 50 json-gated test files exercise integer-frequency on these three functions
  (`test/sql/ts_integer_frequency.test`, the only file testing integer-frequency
  support broadly, only covers `ts_fill_gaps_by`/`ts_fill_forward_by`/`ts_forecast_by`),
  so it is out of scope here. Fix: add `frequency::VARCHAR` at each of the three call
  sites (mirrors the 18-01 fix), plus a regression test exercising integer frequency
  for each function.

- **`ts_forecast_params.hpp::ApplyForecastParamSet` is still missing the 'window'
  (SMA-only) and 'seasonal_periods' (multi-seasonal-only) parameter-misuse
  validations.** This plan fixed the 'model' (ETS-only) validation gap in this shared
  header (test/sql/ts_forecast_ets_model.test, issue #177) because a test explicitly
  exercises it. `src/table_functions/ts_forecast_native.cpp` has two sibling checks
  this header still lacks: "Parameter 'window' is only valid when method='SMA'" and
  "Parameter 'seasonal_periods' is only valid for multi-seasonal models (MFLES,
  AutoMFLES, MSTL, AutoMSTL, TBATS, AutoTBATS)". Neither is exercised by any of this
  plan's 50 json-gated test files, so adding them is out of scope here. Fix: port both
  checks into `ApplyForecastParamSet` (mirroring the 'model' fix's placement/wording),
  plus regression tests for `ts_forecast_by`/`ts_forecast_agg` analogous to
  ts_forecast_ets_model.test's existing coverage.
