# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

Releases from `2026.09.26` onward use CalVer (`vYYYY.MM.DD`, the date the binary set
was cut), matching the rest of the anofox and erpl extensions. Earlier entries below
use the semver numbering this project started with.

## [Unreleased] — v0.10.0 Statistical Correctness

### Breaking changes

- **Prediction intervals are now crate-native, not fabricated.** `yhat_lower`/`yhat_upper`
  across every forecast surface (`ts_forecast_by`, `ts_forecast_exog_by`, `ts_forecast_agg`,
  the scalar paths, `ts_cv_forecast_by`) are each model's own crate `predict_with_intervals`
  bounds at `confidence_level` — no more fabricated `point ± z·SD(history)·√h` band. Interval
  widths are narrower and model-specific; they will not match pre-v0.10.0 values for any
  model. `GARCH`, `Kalman`, `AutoEnsemble` (via `ts_forecast_by`), and the explicit-member
  `ts_forecast_ensemble_by` now return real bounds (previously `NULL`/heuristic). Models/paths
  with no native interval support (`ts_forecast_panel_by`; `MSTL`/`AutoMSTL` with regressors)
  return `NULL` bounds — use conformal prediction for calibrated coverage there.
- **Fitted values, residuals, and MSE are now crate-native, not an SES(α=0.3) surrogate.**
  `insample_fitted` / residuals / MSE come from each model's own `fitted_values()` /
  `residuals()`. Warmup rows (e.g. ARIMA/SARIMA differencing) and interpolated interior rows
  are `NULL`; MSE is the mean residual² over non-interpolated, non-`NULL` positions only.
- **`'ARIMA'` requires `arima_order=[p,d,q]`.** The old fixed-coefficient AR(1)-on-differences
  stand-in is gone; `'ARIMA'` without `arima_order` is now a hard error naming `AutoARIMA` as
  the alternative. With an order, `ARIMA`/`SARIMA` runs a real crate fit and `model_name`
  reports the fitted order (plus `" with drift"` / `" with non-zero mean"` when applicable).
- **`'ETS'` without a spec now runs the crate's own `AutoETS` selection** (not a hand-picked
  HoltWinters/Holt/SES default) and reports the chosen spec, e.g. `ETS(A,A,M)` — the
  `"ETS(ETS(...))"` double-wrap bug is also fixed. An `['ETS','ETS']` ensemble member is now
  bit-identical to standalone `'ETS'`.
- **Every former silent fallback is now an SQL error naming the offending model/parameter:**
  `AutoETS` crate fit/predict failure or panic (previously silently substituted
  HoltWinters/Holt/SES under the `AutoETS` name); a seasonal-period-requiring model
  (`SeasonalNaive`, `SeasonalWindowAverage`, `HoltWinters`, `SeasonalES`,
  `SeasonalESOptimized`, `MSTL`, `AutoMSTL`, `TBATS`, `AutoTBATS`) with no detectable and no
  explicit period (no more silent default of `12` or `2`); an unrecognized `kalman_model`;
  an unrecognized panel `croston_variant` or `model_pool`; an unparsable `seasonal_periods`
  entry; invalid UTF-8 in any option string; regressors passed to a model that doesn't
  support them.
- **`AutoMFLES`/`AutoMSTL` report `model_name` as `MFLES`/`MSTL`** (the bare family name, not
  the `Auto`-prefixed alias) and `Kalman` reports its spec (`Kalman(local_level)` /
  `Kalman(local_linear_trend)`) — both were previously inconsistent.
- **The exogenous-regressor path now routes through the crate's own exog support, not a
  wrapper-OLS hack.** `ARIMAX`/`ThetaX`/`MFLESX` stand-in model names are removed entirely;
  the exog-capable model list (`Naive`, `ARIMA`, `AutoARIMA`, `Theta`, `OptimizedTheta`,
  `DynamicTheta`, `DynamicOptimizedTheta`, `AutoTheta`, `ETS`, `AutoETS`, `MFLES`, `MSTL`)
  reports its own base name with `" with regressors"` appended once (e.g.
  `AutoARIMA(3,0,1)(0,1,0)[9] with regressors`). Regressors on a non-exog-capable model are
  now a hard error naming the model and the exog-capable list. A regressor constant
  throughout history is dropped (0 effect, no suffix) rather than destabilizing the fit.
  `confidence_level` on the exog surface now defaults to `0.90` (was `0.95`), matching
  `ts_forecast_by`.
- **NaN is treated identically to NULL** across every forecast surface; leading/trailing
  `NULL`/`NaN` rows are trimmed before fitting (the forecast horizon still starts from the
  period after the series' ORIGINAL last row); an all-missing or fewer-than-3-valid-value
  series is a hard, per-group-isolated error; `±Inf` anywhere is a hard error naming the index.
  **Exception:** `ts_forecast_agg` drops `NULL`/`NaN`-valued rows entirely before fitting
  rather than interpolating interior gaps, and trailing `NULL` rows move its forecast origin
  back to the last non-`NULL` row instead of staying anchored to the series' original last
  row — aligning this surface with the rest is tracked for Phase 12 (RUST-08).
- **`ts_forecast_ensemble_by`'s `seasonal_period := 0` now auto-detects** the same way
  `ts_forecast_by` does, instead of requiring every member's period to be given explicitly.
- **`ts_forecast_agg` now validates params with the same closed key set as `ts_forecast_by`**
  (unknown keys, including the panel-only `croston_variant`, are errors — previously this
  surface had zero key validation), honours every `ts_forecast_by` param (`kalman_model`,
  `garch_p`/`garch_q`, `model_pool`, `window`, `seasonal_periods`, `laplace_variant`,
  `laplace_seasonal_batch_init`, `top_k`, `combination_method`, `arima_order`/
  `seasonal_order`, `confidence_level` — previously silently dropped except
  `arima_order`/`seasonal_order`/`seasonal_period`), raises an SQL error for invalid
  model/parameter values matching `ts_forecast_by`'s error (previously only ever reported
  in that group's `error_message` field, never aborting the query), and names its bound
  fields `lower_<pct>`/`upper_<pct>` after the requested `confidence_level` (default
  `lower_90`/`upper_90` unchanged; `confidence_level` must be a constant expression —
  previously hardcoded to `0.90` regardless of the params MAP).

### Added

- `arima_order` / `seasonal_order` parameters on `ts_forecast_by`, `ts_forecast_exog_by`,
  `ts_cv_forecast_by`, and `ts_forecast_agg`.
- `confidence_level` parameter on `ts_forecast_ensemble_by` (default `0.90`).
- `params` (6th argument, `ANY`) on the single-series `ts_forecast_exog` scalar function and
  its underlying `_ts_forecast_exog`, carrying every `ts_forecast_by` option onto the exog
  surface (previously a reduced parameter set).
- The full `ts_forecast_by` params set on `ts_forecast_agg`: `kalman_model`,
  `garch_p`/`garch_q`, `model_pool`, `window`, `seasonal_periods`, `laplace_variant`,
  `laplace_seasonal_batch_init`, `top_k`, `combination_method`, and a working
  `confidence_level` (see Breaking changes above — these were previously accepted
  syntactically but silently ignored on this surface).

### Fixed

- `'ETS'`'s model_name no longer double-wraps the selected spec (`"ETS(ETS(...))"`).
- `ts_forecast_agg` / `ts_cv_forecast_by` no longer dereference a null bounds pointer for a
  model with no native interval support — they emit SQL `NULL` instead of crashing.
- `_ts_forecast`'s list outputs are SQL `NULL` (not uninitialised-memory-backed non-null
  lists) when a model has no bounds, and individual `NaN` fitted/residual elements are SQL
  `NULL` rather than the literal `NaN` value.
- `ts_cv_forecast_by('AutoEnsemble', ...)` no longer crashes — the CV native previously
  zeroed ensemble-specific params (`ensemble_top_k`/`ensemble_method`), which could
  dereference null bounds downstream; verified running end-to-end against the built
  extension.

## [2026.09.26] - 2026-09-26

### Changed

- Every public function now documents itself in `duckdb_functions()`: 285 of 285
  eligible entries carry a description, a runnable example and categories. An agent
  connected to a DuckDB database can only learn what an extension does by querying that
  view — a README is not reachable from a SQL connection.
- The 36 `_`-prefixed natives behind the public `ts_*` surface are declared as
  documentation exemptions rather than given prose. They are implementation detail with
  a documented public counterpart each (`_ts_forecast_native` behind `ts_forecast`), and
  anofox-evolve's prompt-vocabulary builder already skips them for the same reason.
- First release to use CalVer; see the note above.

## [0.4.14] - 2026-06-17

### Changed
- Build and ship against both DuckDB **v1.4.5 LTS** and **v1.5.4** (latest). The distribution
  pipeline now runs two build jobs (`duckdb-lts-build` and `duckdb-latest-build`) instead of a
  single v1.5.x build.
- Updated `extension-ci-tools` to the maintained line branches `v1.4-andium` (LTS) and
  `v1.5-variegata` (latest); `duckdb` submodule moved to v1.5.4.

### Fixed
- Linux build against DuckDB v1.4.5 LTS: pass `-Wl,--allow-multiple-definition` so GNU ld on
  glibc/GCC-14 tolerates DuckDB's byte-identical constexpr static members (`LogicalType::FLOAT`,
  etc.) that the v1.4 line emits into multiple static archives. No-op on the v1.5 line.

## [0.2.4] - 2026-01-03

### Changed
- Reimplemented core algorithms in Rust for improved performance and maintainability
- C++ FFI layer provides seamless DuckDB integration
- Full API compatibility with previous versions

### Added
- SeasonalWindowAverage forecasting model (32 models total)
- RandomWalkWithDrift alias for backward compatibility
- PostHog telemetry integration (opt-out via `DATAZOO_DISABLE_TELEMETRY=1`)
- Native `ts_fill_forward_operator` with improved parallel execution safety
- MSTL `insufficient_data` parameter with 'fail', 'trend', 'none' modes
- Feature parameter validation warnings

### Fixed
- Timestamp boundary alignment in gap filling macros
- Thread safety in table operators via `MaxThreads()` override

## [0.2.3] - Previous C++ Release

See [cpp-legacy branch](https://github.com/DataZooDE/anofox-forecast/tree/cpp-legacy) for previous C++ implementation history.
