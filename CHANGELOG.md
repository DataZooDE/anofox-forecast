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
- **`ts_forecast_agg` uses the shared forecasting NULL policy and calendar-correct dates
  (D-11, D-12, Phase 12/RUST-08).** It no longer drops `NULL`-valued rows before fitting;
  interior `NULL`s are interpolated and trailing `NULL` rows keep the forecast origin
  anchored to the series' ORIGINAL last row, exactly like `ts_forecast_by` — only a `NULL`
  **timestamp** drops a row now.
- **Monthly/quarterly/yearly forecast dates follow the month-end rule on every forecast
  surface (`ts_forecast_by`, `ts_forecast_agg`, `ts_forecast_exog_by`,
  `ts_forecast_ensemble_by`, `ts_forecast_panel_by`, `ts_forecast_var_by`).** Forecast dates
  for `'1mo'`/`'3mo'`/`'1q'`/`'1y'` frequencies are computed in Rust via a calendar-aware
  rule instead of fixed-microsecond (or, on the exog surface, epoch-second) arithmetic: a
  series whose every observed date is a month-end forecasts month-ends (no drift into a
  short month, e.g. a series ending `2023-11-30` now forecasts `2023-12-31` /
  `2024-01-31` / `2024-02-29`, not `2023-12-30` / ...); otherwise the day-of-month is kept
  and clamped to each target month's length. All six surfaces now agree on forecast dates,
  not just values, for the same series.
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
- **Paired metrics use pairwise-complete observations, computed in Rust, not spliced in
  C++ (RUST-01/RUST-02, Phase 12).** `ts_mae`, `ts_mse`, `ts_rmse`, `ts_mape`, `ts_smape`,
  `ts_r2`, `ts_bias`, `ts_rmae`, `ts_mase`, `ts_quantile_loss`, `ts_mqloss`, `ts_coverage`
  previously extracted each list argument's NULLs independently in C++ before calling the
  FFI, silently misaligning pairs — `ts_mae([1,NULL,3,4],[1,2,NULL,4])` returned `0.333`
  (treating the arrays as `[1,3,4]` vs `[1,2,4]`). It now returns `0.0`: a pair (or row, for
  3+ input metrics) is dropped the moment ANY input is NULL at that position, via a
  validity bitmask passed through the FFI into a new Rust missing-data policy module
  (`anofox-fcst-core::missing`). No complete pair/row remaining returns SQL `NULL`, not an
  error; mismatched lengths still return `NULL`. `ts_mqloss`'s `levels` parameter is NOT
  data — a NULL level is now a hard SQL error naming its position. The `ts_*_by` metric
  macros (`_ts_metrics_native`) are now identical to the scalars on the same data.
- **Single-series analyses reject interior NULLs instead of silently splicing them out
  (RUST-01/RUST-02, Phase 12).** `ts_adf`, `ts_kpss`, `ts_stationarity`, `ts_ljung_box`,
  `ts_durbin_watson`, `ts_jarque_bera`, `ts_residual_diagnostics`; every period estimator
  (`_ts_detect_periods`, `ts_estimate_period_fft`, `ts_estimate_period_acf`,
  `ts_detect_multiple_periods`, `ts_autoperiod`, `ts_cfd_autoperiod`, `ts_lomb_scargle`,
  `ts_aic_period`, `ts_ssa_period`, `ts_stl_period`, `ts_matrix_profile_period`,
  `ts_sazed_period`); `ts_detect_seasonality` / `ts_analyze_seasonality` /
  `ts_classify_seasonality`; and — as of plan 12-03 — `ts_detrend`, `ts_decompose_seasonal`,
  `ts_seasonal_strength`, `ts_seasonal_strength_windowed`, `ts_detect_seasonality_changes`,
  `ts_instantaneous_period`, `ts_detect_amplitude_modulation`, `ts_detect_peaks`,
  `ts_analyze_peak_timing`, `_ts_mstl_decomposition`, and `_ts_detect_changepoints_bocpd`
  (and, transitively, the single-series `ts_detect_changepoints` macro) previously dropped
  NULL list elements before calling the FFI, silently splicing the remaining values
  together and shifting the implied time spacing. They now trim leading/trailing
  NULLs/NaN (no behaviour change there), but an interior NULL that survives trimming is a
  hard SQL error naming its 1-based position in the original list (e.g. `ts_adf: Invalid
  input: interior NULL at position 6`) — wrap in `try(...)` to get `NULL` instead. A list
  with no non-NULL value still returns `NULL`, not an error. Per-observation array outputs
  of the plan-12-03 functions (trend, detrended, seasonal, peak indices, changepoint flags,
  ...) now describe the trimmed series — position 1 is the first non-NULL input value, not
  the original row 0. The single-series gate (`anofox_ts_series_gate`) lives in Rust
  (`anofox-fcst-core::missing::trim_edges_reject_interior`); C++ only marshals values and
  validity.
- **`ts_features_agg` / `ts_features` no longer drop NULL-valued rows (D-11, plan 12-03).**
  Both previously `continue`d past any row whose value was NULL before it reached the
  aggregate state — a splice, not edge-trimming. They now carry a validity bitmask through
  to the same Rust single-series gate used by the functions above: leading/trailing NULLs
  are trimmed, an interior NULL is a hard SQL error naming its 1-based position (e.g.
  `ts_features_agg: interior NULL at position 11`), and an all-NULL series returns a NULL
  features struct instead of computing over a silently-shortened array. `ts_features_by`
  (the table surface, `_ts_features_native`) now applies the identical policy and agrees
  with `ts_features_agg`/`ts_features` on the same series — previously it wrote `NaN` for a
  NULL input value and never errored on an interior NULL.
- **Per-step conformal errors when the residual count is not a multiple of the horizon
  (previously truncated silently), plan 12-04.** `ts_conformal_predict_per_step` used to
  compute `n_folds = fold_forecasts.size() / horizon` in C++ — any residual count that
  wasn't an exact multiple of `horizon` silently truncated to fewer folds, dropping the
  trailing partial fold with no warning. Rust now owns this validation: a non-multiple
  residual count is a hard SQL error naming both counts (e.g. `residual count 7 is not a
  multiple of horizon 3`); `horizon <= 0` is likewise an error, not a silent `NULL` row.
- **Conformal/bootstrap NULL handling is position-preserving and pairwise-complete, not a
  splice-shift (plan 12-04, D-02).** Every conformal/bootstrap scalar
  (`ts_conformal_quantile`, `ts_conformal_intervals`, `ts_conformal_predict(_asymmetric)`,
  `ts_conformal_learn`/`ts_conformal_apply`, `ts_conformal_coverage`/`ts_conformal_evaluate`,
  `ts_mean_interval_width`, `ts_conformal_predict_per_step`, `ts_bootstrap_intervals`,
  `ts_bootstrap_quantiles`) now reads its `LIST(DOUBLE)` arguments through the shared
  `ListDoubleReader`/`ScalarArgReader` (D-01) instead of a private `ExtractListAsDouble`
  copy that dropped NULL elements before the FFI call (the old splice-shift behaviour).
  Residual pools drop `NULL`/`NaN` entries (`missing::drop_missing`); paired evaluation
  inputs use pairwise-complete observations (`missing::complete_cases`); a `NULL` forecast
  position yields `NULL` at that position only in every output array
  (`missing::fill_missing` + `missing::propagate_missing`) — never shifting later
  positions into earlier ones; parameter lists (`alphas`, calibration-profile arrays,
  `quantile_levels`) reject a `NULL` entry as a malformed argument naming the parameter.
- **`ts_diff_by`'s `diff_order` is now d-th order differencing, not lag-k** (RUST-03, D-05/D-06,
  plan 12-07). `diff_order=2` now computes numpy's `diff(n=2)` (the true second difference
  `y[t] - 2*y[t-1] + y[t-2]`), not the old `y[t] - y[t-2]` lag-2 reading. A new named
  `seasonal_lag` parameter (default `1`) recovers the old lag-k behaviour: pass the same value
  as `seasonal_lag` with `diff_order := 1` for the pre-v0.10.0 reading. Output is still one row
  per input row (NULL for the first `diff_order * seasonal_lag` rows); any difference touching
  a NULL/NaN input is NULL, never an error.
- **`ts_drop_constant_by` now uses the Rust `is_constant` (RUST-09, D-09, plan 12-07):** groups
  with fewer than two non-NULL values — **including an all-NULL group** — now classify as
  constant and are dropped. The old SQL `MIN(value)=MAX(value)` rule kept all-NULL groups
  (`MIN`/`MAX` both `NULL`, so the `OR MIN(value_col) IS NULL` branch matched and excluded them
  from the constant check); this is a breaking change for callers relying on all-NULL groups
  surviving `ts_drop_constant_by`.
- **`ts_fill_nulls_mean_by` now treats `NaN` as missing** (RUST-05, D-09, plan 12-07), matching
  the Phase 10 NaN = NULL convention: a `NaN` value is excluded from the group mean and
  replaced by it, same as a `NULL`. Previously a `NaN` poisoned the SQL `AVG(value_col)`
  (any `NaN` in a group made `_mean_val` `NaN` for the whole group).
- **Hierarchy aggregation uses SQL `SUM` NULL semantics** (RUST-07, D-08, plan 12-08).
  `ts_aggregate_hierarchy` previously treated a `NULL` leaf value as `0` when summing —
  it never propagated to `NULL` and never got excluded, silently contributing zero to every
  level's total. It now follows exact SQL `SUM` behaviour: a group whose leaves are all
  `NULL` for a given date aggregates to `NULL`; a partially-NULL group aggregates to the sum
  of only its present (non-`NULL`) values. Breaking change for any caller relying on the old
  `NULL -> 0` imputation.
- **`ts_stats`/`ts_stats_by`/`ts_stats_agg` moments, entropy, and gap semantics are now
  textbook-correct (STAT-01/STAT-03).** `skewness`/`kurtosis` are now
  scipy `bias=False` G1/excess-G2 (population 2nd/3rd/4th central moments with the proper
  bias-correction factors, not a population-moment/sample-std-dev mix); `bimodality_coef`
  uses the SAS small-sample formula on those G1/G2 values (audit vector bimodality 0.466,
  previously 0.600). `entropy` is now Pincus (1991) approximate entropy (`m=2`, `r=0.2 *
  sample std dev`, Chebyshev distance, self-match included) instead of a 10-bin Shannon
  histogram entropy; it is `NULL` when fewer than 3 present values remain after trimming
  leading/trailing missing values, or when an interior value is missing/NaN (previously a
  non-`NULL` float in both cases). `n_gaps` is now the number of **missing periods**
  (expected timestamps absent under the declared or detected frequency) instead of the
  number of gap *occurrences* — a single 10-period gap now reports `n_gaps = 10`, not `1`.
  The pre-v0.10.0 `n_gaps` meaning (gap occurrences) is preserved under a new `n_gap_runs`
  column on all three surfaces. The `ts_stats` macro's monthly/quarterly/yearly gap
  counting is now calendar-aware (matching `ts_stats_by`), where it previously used
  approximate fixed-microsecond durations. `ts_stats_agg` now auto-detects the series'
  frequency from its own timestamps (previously always reported `NULL` date metrics), and
  keeps `NULL`-valued rows (counted in `n_nulls`, position preserved) instead of silently
  dropping them (Phase 12 deferred item (a)) — only a `NULL` **timestamp** is skipped.
- **`ts_stats`/`ts_stats_by`/`ts_stats_agg` `trend_strength`/`seasonality_strength` are now
  Wang/Hyndman STL decomposition strengths (STAT-02, D-02)**, computed
  from an R-faithful port of `stats::stl` (Cleveland et al. 1990) and `stats::supsmu`
  (Friedman's super smoother) — the crate's own STL (`anofox_forecast::seasonality::stl::STL`,
  0.15.3) uses a degree-0 kernel smoother with truncated windows and no cycle-subseries
  extension, and does not reproduce `stats::stl`: `F_T = max(0, 1 - Var(R)/Var(T+R))`,
  `F_S = max(0, 1 - Var(R)/Var(S+R))`, replacing the old ad-hoc `|r|`-of-a-linear-fit trend
  strength and `max(|ACF|)`-at-lags-2/4/7/12 seasonality strength. A new `seasonal_period`
  parameter (`ts_stats`/`ts_stats_by`, default `0`) selects `0` = auto-detect, `1` =
  non-seasonal, `>= 2` = explicit period; `ts_stats_agg` has no period argument and always
  auto-detects. Several periods use the `forecast::mstl` iteration, reporting the **maximum**
  per-period `F_S_i`. Both strengths are now `NULL`-able: `seasonality_strength` is `NULL`
  when no period is usable (none detected, `seasonal_period := 1`, or `n <= 2 * period`) or
  the series has zero seasonal variance; `trend_strength` is `NULL` only for an
  interior-missing series. On the audit's 150-point random walk, `seasonality_strength` is
  now about 0.09 (previously 0.96 under the old lag-peak formula).
- **`ts_data_quality`/`ts_data_quality_by`/`ts_data_quality_summary`/`ts_data_quality_agg`
  now use real dates, frequency, and `n_short` (D-05, WRAP-02)** —
  previously `temporal_score` was always `1.0` and `n_gaps` was always `0` because the
  underlying `compute_data_quality` call never received the date array; `n_short` and
  `frequency` were silently ignored. `n_gaps` is now the number of **missing periods**
  (same meaning change as `ts_stats`'s `n_gaps`, D-04), with the pre-v0.10.0 meaning
  (gap occurrences) preserved under a new `n_gap_runs` column; a new `is_short` column
  reports whether the present-value count is below `n_short`. `structural_score`'s
  length factor is now `min(1, n_present / n_short)` (was a fixed `/30`). Quality mode/
  tie logic is deterministic (shares `gaps::detect_step`'s smaller-step tie rule; the
  old `HashMap`-based `find_mode` is removed). `behavioral_score` (D-06, STAT-04) drops
  the `|ACF1| > 0.95` penalty entirely and scores constancy (0.0), robust
  Iglewicz-Hoaglin-modified-z-score outliers, and long zero-runs only — a predictable,
  easy-to-forecast series is no longer penalised. `ts_data_quality_agg` (Phase 12
  deferred item (a)) now keeps `NULL`-valued rows (counted in `n_missing`, position
  preserved) instead of silently dropping them, and uses a fixed `n_short = 30` with
  auto-detected frequency (it takes no `n_short`/`frequency` argument).
- **Monthly/quarterly/yearly gap fill and fill-forward preserve calendar alignment
  (D-09, G24)** — `ts_fill_gaps_by`/`ts_fill_forward_by` previously
  always snapped inserted dates to the start of the period (the 1st); they now step
  from the previous observed date (`ts_fill_gaps_by`) or the last observed date
  (`ts_fill_forward_by`) using the same month-end-preserving rule `ts_forecast_by`'s
  future dates use (D-12): a month-end series stays month-end
  (`2024-01-31` → `2024-02-29` → `2024-03-31`), a 1st-of-month series is unchanged, and
  any other day-of-month keeps its own day (clamped to the target month's length)
  instead of snapping to the 1st. `detect_frequency`'s microsecond return unit (Phase
  12 deferred item (e)) is now documented at the Rust doc-comment, FFI, and
  [SQL-doc](docs/api/22-supported-frequencies.md#frequency-auto-detection) level
  (WRAP-03).
- **`ts_mase` is now Hyndman-Koehler MASE with a required `seasonal_period` (D-08,
  STAT-06).** New signature: `ts_mase(actual, forecast, insample, seasonal_period) →
  DOUBLE` — the out-of-sample forecast MAE, scaled by the IN-SAMPLE seasonal-naive MAE
  at lag `seasonal_period`. `seasonal_period` is a required 4th positional argument
  (DuckDB scalar functions don't accept `:=` named arguments). In-sample lag pairs
  touching a missing value are skipped, never spliced out and re-paired; a
  (numerically) zero in-sample scale returns `NULL` (R's `forecast::accuracy()` returns
  `Inf` here). The pre-v0.10.0 3-argument `ts_mase(actual, forecast, baseline)` form —
  which computed `MAE(actual, forecast) / MAE(actual, baseline)`, a relative MAE and
  not MASE at all — is REMOVED: calling it now raises an error naming `ts_rmae`, which
  still provides that computation unchanged. `ts_mase_by`'s signature is now
  `ts_mase_by(source, date_col, actual_col, forecast_col, seasonal_period := 1)`;
  `source` must include each group's HISTORY rows (dated before the first forecast,
  `forecast` NULL) to supply the in-sample scale. The old 5th-argument baseline-column
  form is also removed, detected by type (a VARCHAR column name) and erroring naming
  `ts_rmae_by`.
- **`ts_mape`/`ts_mape_by` return `NULL` when any pairwise-complete `actual` is exactly
  `0.0` (D-07, STAT-05)**, instead of silently excluding that pair from the sum and
  divisor count (the pre-v0.10.0 behavior) — MAPE is undefined with a zero actual, not
  a special case to drop. Use `ts_smape` or `ts_mase` for intermittent-demand series
  where exact zeros are expected.
- **`ts_features` / `ts_features_by` / `ts_features_agg` values now match tsfresh 0.21.2
  (Phase 14).** Every one of the 117 features is checked against a tsfresh-generated
  fixture (`validation/reference/fixtures/tsfresh_features/`, D-01); a feature counts as
  fixed once its mapping row's `status` is `"match"`. Plan 14-01 fixes partial
  autocorrelation and autocorrelation; plans 14-02 through 14-04 fix the remaining
  mismatching features (moments, locations, derivatives, time reversal, `agg_linear_trend`
  rvalue, FFT/spectral, entropy/complexity/Benford) and append their own before/after rows
  to the table below.

  | Feature | Before | After (= tsfresh) | What changed |
  |---------|--------|--------------------|--------------|
  | `partial_autocorrelation_lag1` | 0.02541518343629891 | 0.026752824669788355 | Durbin-Levinson recursion on the adjusted `(n-k)`-divided autocovariance (statsmodels `pacf(method="ld")`), replacing an ad-hoc Yule-Walker shortcut |
  | `partial_autocorrelation_lag2` | -0.06739432948956775 | -0.07488582800969257 | same (lag 2 is no longer a different, hard-coded estimator) |
  | `partial_autocorrelation_lag3` | -0.06739432948956775 (reused the lag-2 value) | -0.025999690982590442 | same (lags 3-5 previously all reused the lag-2 formula verbatim — the headline bug this plan fixes) |
  | `partial_autocorrelation_lag4` | -0.06739432948956775 (reused the lag-2 value) | -0.39110953857042163 | same |
  | `partial_autocorrelation_lag5` | -0.06739432948956775 (reused the lag-2 value) | 0.1107535522386214 | same |
  | `autocorrelation_lag1` | 0.02541518343629891 | 0.02675282466978834 | Denominator changed from the whole-series sum of squared deviations (`n*var(x)`) to tsfresh's `(n-lag)*var(x)` — a mismatch beyond the original audit's F1-F11 list, found by running the live tsfresh calculator this session |
  | `autocorrelation_lag2` | -0.06670486581681923 | -0.0741165175742436 | same |
  | `autocorrelation_lag10` | -0.17966429233157769 (never reached any SQL surface) | -0.35932858466315537 | same, PLUS the value now actually reaches `ts_features_by`/`ts_features`/`ts_features_agg`/`ts_features_table` (see next bullet) |

  (All rows above are on the audit `x20`/`short_n20` vector, `validation/reference/fixtures/moments/scipy_moments.json` vectors.x20.)
- **Every `ts_features` surface now returns all 117 features, including `autocorrelation_lag10`.**
  `ts_features_by`, `ts_features`, `ts_features_agg`, and `ts_features_table` previously
  derived their output schema from calling the Rust core on a 10-point dummy series —
  `autocorrelation_lag10` requires `n > 10` to be computed at all under the old
  length-gated insertion logic, so it silently never appeared in any SQL result (116
  columns/struct keys instead of 117). `extract_features()` now inserts every one of the
  117 `list_features()` names unconditionally for any input of length >= 1 (features
  undefined for that input length — e.g. `mean_change` at `n=1`, `fft_coefficient_9_*` at
  `n=8` — are `NULL`/NaN rather than a missing column).

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
- **Constant scalar arguments (e.g. a literal period) are read correctly for every row**
  (RUST-01, Phase 12). The period estimators' and seasonality functions' optional
  parameters (`max_period`, `min_confidence`, `tolerance`, `max_lag`, `min_strength`,
  `acf_threshold`, `min_period`/`max_period`, `n_frequencies`/`n_candidates`,
  `window_size`/`n_components`, `subsequence_length`/`n_best`, `zero_pad_factor`,
  `period`, `strength_threshold`, `timing_threshold`) previously read
  `FlatVector::GetData(...)[0]` once per chunk — correct only for row 0; every other row
  of a chunk got row 0's value regardless of its own argument. They now read through a
  `ScalarArgReader<T>` (`ToUnifiedFormat`-based) per row, matching the list-argument fix
  from plan 12-01.
- `ts_cv_forecast_by('AutoEnsemble', ...)` no longer crashes — the CV native previously
  zeroed ensemble-specific params (`ensemble_top_k`/`ensemble_method`), which could
  dereference null bounds downstream; verified running end-to-end against the built
  extension.
- List arguments are read correctly for every row (Phase 12, D-01). Metric scalar functions
  (and `ts_data_quality`) now read `LIST(DOUBLE)` arguments via `ToUnifiedFormat` on both the
  list vector and its child vector (the shared `src/include/ts_list_marshal.hpp` reader);
  previously a literal/constant list argument (e.g. `ts_mae(a, [0,0,0])` broadcast across a
  table) was only read correctly on the first row of a chunk and returned `NULL`/garbage on
  every subsequent row.
- **Constant scalar/list arguments on detrend/decomposition/peak/changepoint functions are
  now read correctly for every row** (RUST-01, plan 12-03). `ts_detrend`,
  `ts_decompose_seasonal`, `ts_seasonal_strength`, `ts_seasonal_strength_windowed`,
  `ts_detect_seasonality_changes`, `ts_instantaneous_period`,
  `ts_detect_amplitude_modulation`, `ts_detect_peaks`, `ts_analyze_peak_timing`,
  `_ts_mstl_decomposition`, and `_ts_detect_changepoints_bocpd` previously read their method
  string and threshold/window/lambda parameters via row-0-only `FlatVector::GetData(...)[0]`
  (every other row of a chunk silently reused row 0's value); they now read every argument
  per row via `ScalarArgReader<T>`. Method strings also go through `GetString()` instead of
  treating `string_t::GetData()` as a NUL-terminated C string.
- Dead, never-registered scalar function bodies removed with no SQL surface change (plan
  12-03): the `ts_features` scalar body in `ts_features.cpp` and the two
  `ts_detect_changepoints` scalar bodies in `ts_changepoints.cpp` were unreachable from SQL
  (their `Register*` functions were no-ops) and are deleted along with their private
  `ExtractListAsDouble`/feature-name helpers.
- **`detect_frequency`'s mode is now deterministic on tied consecutive-difference counts**
  (D-12, plan 12-05): ties are broken by the SMALLER interval, independent of hash-map
  iteration order (previously `HashMap::into_iter().max_by_key(count)` could return either
  tied value depending on iteration order — rare but nondeterministic input-dependent
  behavior for the frequency-detection paths that rely on this mode).
- **`ts_forecast_exog_by` future dates no longer drift (30-day-month epoch bound replaced
  by calendar stepping), RUST-09/D-12, plan 12-06.** The macro previously built future dates
  with `generate_series(last_date + interval, last_date + (horizon * EXTRACT(EPOCH FROM
  interval) || ' seconds')::INTERVAL, interval)` — a fixed 30-day-month / 365.25-day-year
  epoch upper bound checked against a calendar step, which could return a different number
  of rows than `len(point)` and misalign the zipped `UNNEST`s. It now computes future dates
  through the same Rust `_ts_future_dates` calendar rule every other forecast surface uses,
  guaranteeing exactly `horizon` dates aligned 1:1 with `forecast_step`.
- **Zero C++ month/quarter/year arithmetic remains in the forecast date path**, plan 12-06.
  `ts_forecast_ensemble_native.cpp`, `ts_forecast_panel_native.cpp`,
  `ts_forecast_var_native.cpp`, and `ts_forecast_native.cpp` previously each reimplemented
  the month-end-preserving / day-of-month-clamp stepping rule with their own
  `Date::MonthDays` arithmetic; all four now call the same Rust `ComputeFutureDates` helper
  `ts_forecast_by`/`ts_forecast_agg` use (plan 12-05), so every public forecast surface
  shares one calendar implementation.
- Dead, never-reachable-from-SQL scalar sources removed with no SQL surface change (D-09,
  plan 12-07): `src/table_functions/ts_filter.cpp` and `src/table_functions/ts_imputation.cpp`
  (every `Register*` body in them a no-op — the live SQL surface was always the `_by` table
  macros in `ts_macros.cpp`) are deleted, along with the no-op `RegisterTsFeaturesFunction`
  (`ts_features.cpp`) and `RegisterTsDetectChangepointsFunction` (`ts_changepoints.cpp`)
  registrations.
- **`ts_conformal_predict_per_step` no longer reads past the end of its `lower`/`upper`
  heap allocations when `point_forecasts` has more elements than `horizon`** (CR-01,
  phase 12 review fix). `point`/`lower`/`upper` are now required to share one length:
  `point_forecasts.len()` must equal `horizon`, or the call is a hard SQL error naming
  both counts (e.g. `point_forecasts has 5 elements but horizon is 3`) instead of
  producing a `point` array longer than `lower`/`upper` and reading `n_point_forecasts -
  horizon` doubles past a `horizon`-sized allocation.
- **`_ts_is_constant([]::DOUBLE[])` (empty-but-non-NULL list) now returns `true`** (WR-01,
  phase 12 review fix), matching its own documented contract ("fewer than two non-missing
  values, including an all-missing group, classifies as constant"). The FFI's null-pointer
  guard previously ran before the `length == 0` short-circuit, so the C++ caller's
  `n == 0 ? nullptr : values.data()` call (passing a genuine NULL for an empty list) always
  hit the null-pointer error path instead. Not reachable via `ts_drop_constant_by` (every
  `GROUP BY` partition has at least one row), but `_ts_is_constant` is directly callable.
- `ts_forecast_agg` / `ts_features_agg` now break ties deterministically for groups with
  duplicate timestamps (WR-02, phase 12 review fix). `Finalize`'s sort previously ordered
  rows by timestamp only, so the relative order — and hence the `(value, valid)` pairing —
  of same-timestamp rows was unspecified and could vary with the order partial aggregate
  states were concatenated in `Combine()` (itself dependent on thread scheduling); the sort
  now tie-breaks by `(valid, value)` — valid before `NULL`, then ascending value with `NaN`
  last — the same comparator `ts_stats.cpp`'s `StatsValueLess` already uses, so results
  depend only on the data, not on arrival order or thread interleaving, for any group
  containing a duplicate-date row.

### Removed

- **`anofox_fcst_core::metrics::mqloss`** (WR-03, phase 12 review fix): dead production
  code superseded by `anofox_ts_mqloss`'s own per-level `quantile_loss` + `complete_cases`
  loop at the FFI boundary (D-02). Only its own unit test called it, so it could silently
  drift out of sync with the real path; the test is deleted along with the function
  (`quantile_loss` already has its own dedicated coverage).
- **`anofox_fcst_core::conformal::conformal_predict_per_step`** is no longer part of the
  compiled library (WR-03, phase 12 review fix): the FFI boundary calls
  `conformal_predict_per_step_validated` instead (D-02/D-10). Unlike `mqloss`, it is kept
  as a `#[cfg(test)]`-only cross-check oracle — `complete_data_matches_crate_backed_function`
  still asserts the two implementations agree on complete data — rather than deleted
  outright, since that regression guard has ongoing value (see CR-01, caught by this same
  pair of implementations silently diverging).
- **`_ts_backtest_native`** (RUST-06, D-07, plan 12-08): the internal, macro-less table
  function left orphaned when `ts_backtest_auto_by` was removed. Its own forecast-minus-actual
  error sign disagreed with the SQL metric scalars' actual-minus-forecast convention (D-16);
  that inconsistency is resolved by removal rather than a fix, since nothing called it. Use
  the `ts_cv_folds_by` + `ts_cv_forecast_by` two-step CV workflow; `ts_estimate_backtest_memory`
  (reworded to describe that workflow) is unaffected and still live.

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
