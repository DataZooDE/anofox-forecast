# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

Releases from `2026.09.26` onward use CalVer (`vYYYY.MM.DD`, the date the binary set
was cut), matching the rest of the anofox and erpl extensions. Earlier entries below
use the semver numbering this project started with.

## [Unreleased]

No signature changes. `ts_fill_forward_operator` changes implementation and now
returns the source table's column names and types (previously always
`group_col`/`date_col`/`value_col`, `TIMESTAMP`/`DOUBLE`).

### Deprecated

- **`ts_fill_forward_operator` and its `anofox_fcst_ts_fill_forward_operator`
  alias are deprecated and will be removed in the next release.** Use
  `ts_fill_forward_by` with column references instead (same signature:
  source table name, group, date, value, target date, frequency).

### Changed

- **DuckDB v2.0.0 is now the stable build line.** The `duckdb` submodule is pinned to
  v2.0.0 and `extension-ci-tools` to v2.0-cyanoptera, so a local `make release` builds
  against 2.0.0. CI now builds, smoke-tests and deploys three lines: v2.0.0 (stable,
  ci-tools v2.0-cyanoptera), v1.5.6 (v1.5-variegata, the registry's `variegata` track)
  and v1.4.5 LTS (v1.4-andium). The former non-blocking 2.0 build-only job is folded
  into the stable line. This follows community-extensions' "Update path: After DuckDB
  release".
- **WASM runtime test stays on the v1.5.6 artifact.** No `@duckdb/duckdb-wasm` npm
  build bundles a DuckDB v2.0.0 engine yet (the newest, 1.33.1-dev65.0, bundles
  v1.5.6). The Node harness moves to the stable line when one ships.
- **`wasm_mvp` keeps shipping (#272).** `wasm_mvp` was measured against the v1.5.6
  engine: it LOADs and the full 94-file suite shows zero `wasm_mvp`-only failures
  versus `wasm_eh`. A `ReferenceError: _setThrew is not defined` console error is
  a non-fatal, upstream `@duckdb/duckdb-wasm` defect (its own JS glue never binds
  `_setThrew` to the main module's `setThrew` export, on either bundle); it never
  failed a query result in any measurement. The post-upload deploy check now
  LOADs `wasm_mvp` too (not just `wasm_eh`), so a real future regression still
  blocks the deploy. See `docs/distribution/get-erpl-io.md` for the full
  measurement, root-cause, and decision record, and for the bucket owner's
  handover (CORS rule, stale paths, redeploy steps).

### Fixed

- **Post-build smoke test on DuckDB v2.0.0.** v2.0.0 renamed its CLI release assets
  from `duckdb_cli-<os>-<arch>.zip` to `duckdb-cli-<os>-<arch>.tar.gz`.
  `scripts/smoke_test.py` now tries the new tarball first and falls back to the legacy
  zip, so one script serves all three lines.
- **WASM artifacts on get.erpl.io carried two `duckdb_signature` sections and failed
  to LOAD (#272).** `extension-upload.sh` used to unconditionally append a second
  `duckdb_signature` custom section to every WASM artifact instead of overwriting
  the placeholder inside the existing one, like the native path already did.
  `extension-upload.sh` now replaces the signature in place for WASM too, guarded
  by pre- and post-upload section-count checks (`scripts/wasm_signature_sections.py`)
  that abort rather than ever produce a double-signed artifact. The deploy job
  (`_extension_deploy.yml`) now also verifies every artifact it just uploaded:
  section count for every WASM arch, and a real DuckDB-Wasm LOAD via the Node
  harness for `v1.5.6` `wasm_eh` and `wasm_mvp` (`scripts/verify-wasm-artifact.sh`,
  `scripts/extension-verify-upload.sh`). Any failure fails the deploy job.
- **`ts_fill_forward_operator` hung natively and trapped DuckDB-Wasm (#289).** The
  old native C++ table function ran a nested query from its `InitGlobal`, which hung
  natively and trapped the DuckDB-Wasm instance; it also interpolated identifiers
  unquoted via `StringUtil::Format` (breaking on reserved words or names with
  spaces) and ignored the `frequency` argument (always forward-filled at one day).
  It is now a table macro delegating to `ts_fill_forward_by`, which fixes all three:
  the macro binder resolves column names via `COLUMNS([param])` (no SQL text is
  built from user input), and `frequency` is honoured.
- **`ts_features_list()` / `ts_features_config_template()` trapped DuckDB-Wasm
  with "memory access out of bounds" (#289).** Two independent defects, both
  specific to these two zero-argument catalog functions: (1) the FFI
  feature-name array is allocated with this crate's own allocator shim (a
  size-prefixed `std::alloc` shim on WASM, required because WASM has no
  libc `free()` that carries its own size metadata), but C++ released it with
  raw libc `free()`, corrupting the Emscripten heap on wasm32; (2) wrapping
  both catalog functions' bind/execute pairs in the `DATAZOO_GUARD` error-hint
  macro at the same time made the wasm_eh build miscompile, apparently a WASM
  toolchain code-folding defect merging `GuardedFunction<...>::Call`
  instantiations that share a signature but differ only in the baked-in
  function pointer. Fixed by releasing the array through a new, paired
  `anofox_free_ts_features_list` FFI function, and by no longer wrapping
  these two functions in `DATAZOO_GUARD` (neither throws). Also: re-executing
  a prepared statement over either function returned 117 rows then 0 on the
  second `EXECUTE` (the output cursor lived in bind data, which is immutable
  per-execution); state now lives in `init_global`, fixing this on every
  target including native.

## [2026.10.22] - 2026-10-22 — macOS build on DuckDB v2.0.0

No SQL signature or numeric output changes.

### Fixed

- **macOS build against DuckDB v2.0.0.** DuckDB 2.0's bundled HTTP client uses the
  macOS keychain, so on macOS the extension must link the `Security` framework (next to
  `CoreFoundation`). Without it the `osx_arm64` build on v2.0.0 failed to link with
  undefined `_Sec*` symbols. The community-extensions registry builds macOS only after
  merge, and one failed platform there skips the deploy for every platform. The 2.0
  build now passes on `osx_arm64`; on DuckDB 1.x the extra framework is harmless.

## [2026.10.21] - 2026-10-21 — DuckDB v2.0.0

No SQL signature or numeric output changes. This release builds against the final
DuckDB v2.0.0 release, which the community-extensions registry now targets by default.

### Fixed

- **Build against DuckDB v2.0.0.** v2.0.0 defines `EXTENSION_NAME` as the bare
  extension name before it reads the extension's `CMakeLists.txt`. Our banner link lines
  used that variable without setting it, so configure failed with
  `Cannot specify link libraries for target "anofox_forecast" which is not built by
  this project`. The targets are now named explicitly. On DuckDB 1.x those lines had
  silently linked nothing, which did no harm because the banner is header-only.
- **Builds without OpenSSL** no longer fail to link. The banner's telemetry hook is
  now disabled whenever PostHog telemetry is not built.

### Changed

- The DuckDB 2.0 CI job now builds and tests against the `v2.0.0` tag instead of a
  2.0-dev commit. The full SQL suite (5591 assertions / 94 files) passes on v2.0.0.

## [2026.10.17] - 2026-10-17 — DuckDB 2.0 Compatibility

No SQL signature or numeric output changes: on DuckDB v1.4.5 LTS and v1.5.6 every
function returns exactly what it returned in v2026.10.16. This release makes the
extension build and run on DuckDB 2.0-dev as well (PR #277).

### Added

- **DuckDB 2.0-dev support.** The extension now builds and passes the full SQL suite
  (5591 assertions / 94 files) on DuckDB 2.0-dev, v1.5.6 and v1.4.5 LTS from one
  source tree. All DuckDB API calls go through a new `src/include/duckdb_compat.hpp`,
  which detects the API by shape (never by version macro).
- **Non-blocking DuckDB 2.0-dev CI job** (`linux_amd64` + `wasm_eh`) alongside the
  v1.4.5 LTS and v1.5.6 build matrices.

### Changed

- Every registered scalar function is marked fallible. No effect on 1.x; on 2.0 user
  errors raised by a scalar stay user errors instead of surfacing as internal errors.
- WASM builds pin Rust 1.86.0 through Corrosion (`ANOFOX_WASM_RUST_TOOLCHAIN`, set it
  to `""` to disable). Rust >= 1.87 emits wasm target features that the binaryen
  bundled with DuckDB's emsdk 3.1.71 rejects. Native builds keep the 1.89 floor.
- Tests, examples and docs use the keyword lambda syntax (`lambda x: ...`) instead of
  the deprecated arrow form.
- On DuckDB 2.0 the extension is statically linked into DuckDB's own test binaries,
  so the 2.0 sqllogictest run actually exercises it (it skipped all 94 files before).

### Fixed

- `ts_estimate_backtest_memory` could read past the end of a constant input vector;
  it now reads its inputs through DuckDB's unified vector format.
- `ts_features_config_from_json` read its input in a way that fails on DuckDB 2.0;
  it now reads through the unified vector format too.

## [2026.10.16] - 2026-10-16 — Statistical Correctness

### Breaking changes in v2026.10.16

Every row below changes a function's NUMERIC OUTPUT or its SQL usage (required
arguments, signature, error behavior) somewhere on the `ts_*` surface. This is the
consolidated, at-a-glance index; the full technical record — exact before/after values,
root causes, and verification methodology — is the detailed per-phase bullets and
before/after tables in "Breaking changes — details by phase" immediately below.

#### Numerical output changes by area

| Area | Function(s) | Before | After | Phase |
|---|---|---|---|---|
| stats | `ts_stats`/`ts_stats_by`/`ts_stats_agg` moments (`skewness`/`kurtosis`/`entropy`) | population-moment mix; 10-bin Shannon histogram entropy | scipy `bias=False` G1/excess-G2; Pincus (1991) approximate entropy | Phase 13 |
| stats | `ts_stats*` `n_gaps` | gap-*occurrence* count | missing-*period* count (old meaning preserved as new `n_gap_runs`) | Phase 13 |
| stats | `trend_strength`/`seasonality_strength` | ad-hoc `\|r\|`/max-ACF-lag formula | Wang/Hyndman STL `F_T`/`F_S` from an R-faithful STL (`stats::stl`/`forecast::mstl` port) | Phase 13 |
| metrics | `ts_mae`/`ts_mse`/`ts_rmse`/`ts_mape`/`ts_smape`/`ts_r2`/`ts_bias`/`ts_rmae`/`ts_quantile_loss`/`ts_mqloss`/`ts_coverage` | each array's NULLs spliced out independently in C++ before the FFI call | pairwise-complete observations via a Rust validity bitmask (`anofox-fcst-core::missing`) | Phase 12 |
| metrics | `ts_mase` | 3-arg relative-MAE (`MAE(actual,forecast)/MAE(actual,baseline)`) | Hyndman-Koehler MASE: out-of-sample MAE scaled by in-sample seasonal-naive MAE | Phase 13 |
| metrics | `ts_mape`/`ts_mape_by` | silently excluded zero-actual pairs from both sum and divisor | `NULL` for the whole call when any pairwise-complete `actual` is exactly `0.0` | Phase 13 |
| data quality | `ts_data_quality*` `temporal_score`/`n_gaps`/`n_short`/`frequency` | `temporal_score` always `1.0`; `n_gaps` always `0`; `n_short`/`frequency` silently ignored | real date-driven scoring; missing-period `n_gaps`; both arguments honored | Phase 13 |
| data quality | `ts_data_quality_summary` | all-NULL-dated groups silently undercounted (excluded from every bucket, inflating `n_total`'s implied denominator) | new `n_unassessed` column; `n_good+n_fair+n_poor+n_unassessed=n_total` always holds | Phase 18 (WR-05/D-10) |
| features | all 117 tsfresh-inspired features | partial/incorrect match vs tsfresh 0.21.2 (PACF/ACF, moments, FFT/spectral, entropy/complexity families — see the detailed table below) | 117/117 match tsfresh 0.21.2 (or a documented, intentional deviation) | Phase 14 |
| features | `autocorrelation_lag10` | never reached any SQL surface (output schema derived from a 10-point dummy series, which can't compute a lag-10 value) | present on every `ts_features*` surface unconditionally | Phase 14 |
| detection | `ts_detect_peaks(_by)` | fdars derivative zero-crossing detector — missed peaks, reported local minima, every plateau `prominence=0`, 0-1-normalised prominence scale | direct `scipy.signal.find_peaks` 1.18.1 port; real-data-scale prominence | Phase 15 |
| detection | BOCPD (`ts_detect_changepoints*`, `_ts_detect_changepoints_bocpd`) | unnormalised predictive density (no normalising constant); posterior-MEAN update; no run-length truncation | normalised Student-t predictive (log domain); posterior sample-mean update; truncation at 500; standardised input (scale/shift invariant) | Phase 15 |
| detection | PELT (Rust/FFI changepoint detector) | not scale-invariant (unscaled series vs. `*100` gave wildly different changepoints); `L1` cost used the mean | sigma-normalised cost (scale-invariant); `L1` uses the segment median; real exact-safe pruning | Phase 15 |
| detection | `ts_mstl_decomposition_by`/`_ts_mstl_decomposition` | `seasonal_periods`/`periods` argument silently ignored (always `nullptr`/`0` to the FFI); classical-MA approximation, periods sorted descending | real R `forecast::mstl` port; requested periods honored and returned ascending | Phase 15 |
| detection | period estimators (`ts_estimate_period_fft`/`_acf`, `_ts_detect_periods(..,'regression')`, `ts_lomb_scargle`) | undetected trend bias (FFT reported `period=n`); unreachable short lags (ACF); coarse 50-point regression grid; unsafe Lomb-Scargle ranges (div-by-zero / inverted grid) | linearly detrended first (FFT); lag-2-reachable with a significance bound (ACF); every-integer grid (regression); explicit range-validation errors (Lomb-Scargle) | Phase 15 |
| detection | `ts_seasonal_strength(_windowed)` | measured on the raw (possibly trend-contaminated) series; `'wavelet'` method always returned `1.0` regardless of input | detrends LINEARLY first, then measures strength on the residual; `'wavelet'` removed | Phase 15 |
| conformal | `ts_conformal_by`/`ts_conformal_apply_by`/`ts_interval_width_by` | value-sorted per-column lists, paired independently (silent mispairing across lower/upper/actual) | row-per-`(group,date)`, paired by a required `date_col` | Phase 16 |
| conformal | `ts_conformal_predict_asymmetric` | positive/negative residual-SUBSET quantiles (over-covered nominal by 5-6pp) | full signed-residual-array quantiles (hits nominal coverage within ~0.2pp) | Phase 16 |
| conformal | `ts_bootstrap_intervals` | residuals/forecasts arguments swapped internally — a right-skewed residual's long tail landed on the wrong (lower) side | correct tail placement; `n_paths`/`coverage` validated, never silently clamped | Phase 16 |
| diagnostics | `ts_adf`/`ts_adf_by` | fixed-lag, non-augmented regression; fixed asymptotic critical-value table | AIC-selected lags (statsmodels `adfuller(autolag="AIC")`-exact); MacKinnon (1994/2010) finite-sample p-values and critical values | Phase 17 |
| diagnostics | `ts_kpss` under `regression := 'ct'` | not available (constant-only) | new trend-KPSS implemented in this extension's Rust core, matching R `tseries::kpss.test(null="Trend")` | Phase 17 |
| diagnostics | `ts_stationarity`/`ts_stationarity_by` verdict | 3 of 4 four-way labels were swapped | standard ADF+KPSS mapping; explicit `'constant'` verdict; SQL `NULL` (not a mislabeled string) for a degenerate series | Phase 17 |
| diagnostics | `ts_ljung_box`/`ts_ljung_box_by` | `df` always `= lags`, ignoring any fitted model; Wilson-Hilferty approximation for `df > 30` | `fitted_params` parameter, `df = lags - fitted_params`, exact chi-square survival function matching R `Box.test(fitdf=...)` | Phase 17 |
| forecasts | prediction intervals (`yhat_lower`/`yhat_upper`) across every forecast surface | fabricated `point ± z·SD(history)·√h` band | each model's own crate `predict_with_intervals` bounds; `NULL` where a model has no native interval support | Phase 10 |
| forecasts | fitted values / residuals / MSE | SES(α=0.3) surrogate, independent of the actual fitted model | crate-native `fitted_values()`/`residuals()` per model | Phase 10 |
| forecasts | `MFLES`/`AutoMFLES`/`Laplace` point forecasts | `anofox-forecast` 0.15.3 (issue-219 runaway: MFLES/AutoMFLES could diverge unboundedly) | `anofox-forecast` 0.15.10 (issue-219 bounded; `max_abs_diff=12279.87` on the repro case; 15 other ALLOWED-CHANGE lines are sub-`1e-9`-to-`0.108` floating-point drift across MFLES/AutoMFLES/Laplace); every other model byte-for-byte unchanged across a 125-entry snapshot | Phase 9 (DEP-02) |
| forecasts | `ETS`/`Theta`/`AutoARIMA`/`Holt`/`HoltWinters`/`TBATS`/`MSTL`/`GARCH` fits, intervals, and point forecasts | `anofox-forecast` 0.15.10 formulas and search | `anofox-forecast` 0.16.2 — upstream fixes developed independently in Phase 11 (ADF/KPSS augmentation + MacKinnon p-values, bounded KPSS p-values, `auto.arima`-parity AutoARIMA order selection, corrected ETS likelihood/intervals, Theta alpha optimisation; PRs #222-#225) and consumed in Phase 17 (0.16.1); AutoARIMA/SARIMA 95% prediction intervals now follow R `stats::arima`'s `KalmanForecast` state-space variance (PR #230/#229) consumed in Phase 18 plan 18-05 (0.16.2) | Phase 17 (consumes Phase 11), Phase 18 plan 18-05 |

#### SQL signature and semantics changes

| Function(s) | Before | After | Migration |
|---|---|---|---|
| `ts_conformal_by`/`ts_conformal_apply_by`/`ts_interval_width_by` | no `date_col` argument; lower/upper/actual each sorted independently by value, one row per group | a required `date_col` orders and pairs every row; one row per `(group, date)` | add the date column as the new argument; downstream consumers expecting one-row-per-group output must re-aggregate |
| `ts_conformal_apply_by` | took a literal conformity-score argument | takes a calibration TABLE — `ts_conformal_calibrate`'s own output, matched per group (or pooled fallback) | run `ts_conformal_calibrate` first and pass its output table name instead of a literal score |
| `ts_mase` | 3-arg `(actual, forecast, baseline)` — computed a relative MAE, not MASE | 4-arg `(actual, forecast, insample, seasonal_period)` — Hyndman-Koehler MASE; `seasonal_period` is a required positional argument (no `:=` defaulting on scalar functions) | the pre-v2026.10.16 3-arg call now raises an error naming `ts_rmae`; use `ts_rmae(actual, forecast, baseline)` for the old relative-MAE result |
| `ts_mase_by` | `(source, date_col, actual_col, forecast_col, baseline_col)` | `(source, date_col, actual_col, forecast_col, seasonal_period := 1)`; `source` must include each group's HISTORY rows (dated before the first forecast, `forecast` NULL) | drop the baseline column; ensure `source` carries pre-forecast history rows for the in-sample scale |
| `'jackknife+'` conformal strategy (and legacy aliases `JackknifePlus`/`jackknifeplus`/`jackknife_plus`/`jk+`) | silently computed the identical quantile as `'split'` (no real leave-one-out refits) | REMOVED — every spelling now raises a hard SQL error naming the replacement | use `'split'` or `'crossval'` |
| `'wavelet'` seasonal-strength method (`ts_seasonal_strength(_windowed)`) | always returned `1.0`, regardless of input (could not discriminate signal from noise) | REMOVED — raises a hard SQL error naming the remaining methods | use `'variance'` (default) or `'spectral'` |
| `ts_adf(_by)`/`ts_kpss(_by)`/`ts_stationarity(_by)` | no `regression` parameter (constant-only test) | `regression := 'c'` (default) or `'ct'` (constant + linear trend) | pass `regression := 'ct'` for a trend-stationarity test; re-check any branching logic keyed on the four-way verdict strings (3 of 4 swapped meaning) |
| `ts_data_quality_summary` | 5 output columns: `n_total`, `n_good`, `n_fair`, `n_poor`, `avg_score` | adds a 6th column, `n_unassessed` (groups with no usable dates); `avg_score` now covers assessed groups only | a positional consumer expecting exactly 5 columns must account for the new one |
| `ts_stats*`/`ts_data_quality*` `n_gaps` | counted gap *occurrences* | counts missing *periods*; the old occurrence-count meaning is preserved under a new `n_gap_runs` column | read `n_gap_runs` for pre-v2026.10.16 `n_gaps` semantics |
| deprecated metric `_by` family (`ts_mae_by`, `ts_mse_by`, `ts_rmse_by`, `ts_mape_by`, `ts_smape_by`, `ts_r2_by`, `ts_bias_by`, `ts_mase_by`, `ts_rmae_by`, `ts_coverage_by`, `ts_quantile_loss_by`) | some docs/examples called these with a bare table name and/or an extra group-column argument | `source` MUST be a subquery, e.g. `(SELECT group_col, date_col, actual, forecast FROM table)` — a bare table name fails to bind (the native function's first parameter is `TABLE`-typed); there is no separate positional `group` argument | wrap the source in a subquery selecting exactly the columns the macro needs, in the documented order |
| `ts_diff_by` `diff_order` | lag-k difference, `y[t] - y[t-k]` | true d-th order differencing (numpy `diff(n=diff_order)`) | pass the old `k` as the new named `seasonal_lag` parameter with `diff_order := 1` |
| `ts_backtest_auto_by` / `_ts_backtest_native` | one-step backtest table macro | REMOVED (orphaned internal function removed alongside it) | use the two-step `ts_cv_folds_by` + `ts_cv_forecast_by` workflow |
| `'ARIMA'`/`'SARIMA'` without `arima_order` | ran a fixed-coefficient AR(1)-on-differences stand-in | hard error naming `AutoARIMA` as the alternative | pass `arima_order := [p,d,q]` (and `seasonal_order` for SARIMA), or switch to `'AutoARIMA'` |

### Breaking changes — details by phase

- **Prediction intervals are now crate-native, not fabricated.** `yhat_lower`/`yhat_upper`
  across every forecast surface (`ts_forecast_by`, `ts_forecast_exog_by`, `ts_forecast_agg`,
  the scalar paths, `ts_cv_forecast_by`) are each model's own crate `predict_with_intervals`
  bounds at `confidence_level` — no more fabricated `point ± z·SD(history)·√h` band. Interval
  widths are narrower and model-specific; they will not match pre-v2026.10.16 values for any
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
  as `seasonal_lag` with `diff_order := 1` for the pre-v2026.10.16 reading. Output is still one row
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
  The pre-v2026.10.16 `n_gaps` meaning (gap occurrences) is preserved under a new `n_gap_runs`
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
  (same meaning change as `ts_stats`'s `n_gaps`, D-04), with the pre-v2026.10.16 meaning
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
  `Inf` here). The pre-v2026.10.16 3-argument `ts_mase(actual, forecast, baseline)` form —
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
  divisor count (the pre-v2026.10.16 behavior) — MAPE is undefined with a zero actual, not
  a special case to drop. Use `ts_smape` or `ts_mase` for intermittent-demand series
  where exact zeros are expected.
- **`ts_features` / `ts_features_by` / `ts_features_agg` values now match tsfresh 0.21.2
  (Phase 14) -- all 117/117 features (FEAT-05).** Every one of the 117 features is checked
  against a tsfresh-generated fixture (`validation/reference/fixtures/tsfresh_features/`,
  D-01); as of plan 14-04, every one of the 117 `mapping.json` rows is `status: "match"` --
  no `"pending:<owner>"` row remains. Plan 14-01 fixes partial autocorrelation and
  autocorrelation; plan 14-02 fixes moments/locations/derivatives/time
  reversal/`agg_linear_trend` rvalue; plan 14-03 fixes FFT/spectral; plan 14-04 fixes the
  remaining entropy/complexity/Benford family and closes the 117/117 gate. Each plan appends
  its own before/after rows to the table below.

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
  | `last_location_of_maximum` | 0.85 | 0.9 | `(last_idx+1)/n`, matching tsfresh's `1 - argmax(x[::-1])/n`, instead of the raw 0-based `last_idx/n` |
  | `last_location_of_minimum` | 0.45 | 0.5 | same |
  | `mean_second_derivative_central` | 0.06172222222222214 | 0.030861111111111117 | added the missing `1/2` factor in the denominator: `(x[-1]-x[-2]-x[1]+x[0]) / (2*(n-2))` |
  | `time_reversal_asymmetry_stat_1` | 0.7533418356111107 | -1.0168216098333345 | fixed the second term's transposed operands (`x*x_lag^2`, not `x_lag*x^2`) |
  | `time_reversal_asymmetry_stat_2` | 2.5632908058124997 | 5.0857501354375 | same |
  | `time_reversal_asymmetry_stat_3` | 5.146344542642859 | 5.978017533928571 | same |
  | `skewness` | 0.27971855030568227 | 0.30292879825084285 | now Fisher's bias-corrected G1 (`scipy.stats.skew(x, bias=False)`), shared with `ts_stats`' own moment helpers (D-05), instead of the biased population moment ratio |
  | `kurtosis` | -0.07363967840233254 | 0.27652865463225274 | same (bias-corrected excess G2) |
  | `variation_coefficient` (random_walk_n200) | 0.7073077067644646 | -0.7073077067644646 | now signed (`std(x)/mean(x)`), instead of always dividing by `\|mean(x)\|` |
  | `agg_linear_trend_rvalue` (random_walk_n200) | 0.7617386359686308 | -0.7617386359686308 | now a signed scipy-`linregress`-semantics Pearson r (`ss_xy/sqrt(ss_xx*ss_yy)`, clipped to `[-1,1]`), instead of the unsigned `sqrt(r^2)` |
  | `agg_linear_trend_stderr` (constant_n30) | 0.0 | NaN | degenerate (zero-variance chunk means) now reports scipy's `NaN` instead of a hard-coded `0.0` |
  | `linear_trend_r_squared` (constant_n30) | 0.0 | NaN | same (scipy's degenerate rvalue is NaN, squared is still NaN) |
  | `fft_coefficient_0_real` | 2.7768499999999996 | 55.537 | now the unnormalised `np.fft.rfft(x)[0]` (`n*mean(x)`), instead of the old O(n^2) DFT's `.../n` normalisation (which made bin 0 just the series mean) |
  | `fft_coefficient_1_real` | 0.3702399431540496 | 7.404798863080996 | same normalisation fix, via the new `rustfft`-backed one-sided spectrum |
  | `fft_coefficient_3_abs` | 0.4827471728054351 | 9.654943456108715 | same |
  | `spectral_centroid` | 2.016904841106497 | 2.740689544749791 | now tsfresh `fft_aggregated`'s first raw moment of the ONE-sided `\|rfft(x)\|` magnitude spectrum, instead of the first raw moment of the full two-sided power spectrum |
  | `spectral_variance` | 22.834066212475133 | 11.264706553070855 | same (second central moment of the one-sided magnitude spectrum, not the two-sided power spectrum) |
  | `sample_entropy` | 0.9808292530117262 | 1.0986122886681098 (= ln 3, the ROADMAP anchor) | ported tsfresh's algorithm exactly: `n-m+1`/`n-m` windows (previously `n-m`, an off-by-one), full symmetric match-count matrix minus self-matches (previously a triangular sum with a combinatorial divisor tsfresh never applies), raw `-ln(A/B)` |
  | `binned_entropy` | 1.9206765920171578 | 1.7820471559051687 | ported `np.histogram`'s exact floor-based 10-bin algorithm (edge corrections, `min==max` widened by `±0.5`), instead of a rounding-based binning scheme with different bin boundaries |
  | `benford_correlation` | 0.42967594716771035 | 0.3816973836882482 | now includes every nonzero value's true leading significant digit via shortest scientific notation (incl. `\|v\|<1`, previously excluded entirely) and divides by `n` (previously divided by the digit-producing count only) |
  | `permutation_entropy` | 0.933798406475988 | 1.6731421371534196 | removed the `ln(order!)` normalisation tsfresh's own calculator never applies -- the pattern-key generation itself was already correct |
  | `lempel_ziv_complexity` | 1.512674833210577 | 0.4 | full algorithm rewrite: tsfresh's `bins=2` range-midpoint (`linspace`) quantisation and sub-word dictionary-growth loop, replacing a mean-threshold binarisation with a different dictionary-growth loop and a `log2(n)/n` normalisation tsfresh does not use |
  | `approximate_entropy` (constant_n30, all_zero_n50) | NaN | 0.0 | `n<=m+1` degenerate branch and the zero-variance (`r=0`) full-match case both now collapse to tsfresh's own `0.0`, not `NaN`; `short_n20` is unchanged (`0.08780500866889396`) since it was already correct |
  | `count_unique` / `has_duplicate` (e.g. `[0.0, -0.0, 1.0]`) | `count_unique=3`, `-0.0` and `+0.0` counted as distinct values | `count_unique=2`, treated as one value | canonicalises `-0.0` to `+0.0` before bit-hashing in `count_unique`/`has_duplicate`/`reoccurring_stats`, matching `np.unique`'s `-0.0 == 0.0` equality |

  `number_peaks` (the 3-point local-max filter, `n=1` support) was confirmed numerically
  equal to tsfresh's `number_peaks(x, n=1)` on all 9 fixture series -- no code change was
  needed (plan 14-04, Task 2 item 6). See
  [`validation/reference/README.md`](validation/reference/README.md)'s "Intentional
  deviations and tolerance overrides" section and
  [`docs/api/21-feature-reference.md`](docs/api/21-feature-reference.md)'s Deviation column
  for the complete, authoritative deviation list (`skewness`/`kurtosis` on a zero-variance
  series are the only two rows that still diverge from tsfresh/pandas, intentionally).

  `skewness`/`kurtosis` on a genuinely zero-variance series (`constant_n30`, `all_zero_n50`) are a documented intentional deviation from tsfresh/pandas, which return `0` by convention for an undefined moment (0/0) — see `validation/reference/fixtures/tsfresh_features/mapping.json`'s `deviation` entries for those two rows (D-03). `ts_stats` and `ts_features` now compute skewness/kurtosis via the same shared `sample_skewness_g1`/`sample_excess_kurtosis_g2` helpers in `stats.rs` (D-05) — the two surfaces can never again drift apart on this family. `fft_coefficient_*` are now unnormalised numpy-`rfft` values (previously divided by `n`), coefficients beyond `n/2` are `NaN`, the transform is `O(n log n)` via `rustfft` (D-07) instead of an `O(n^2)` naive DFT, and `spectral_centroid`/`spectral_variance` use tsfresh's one-sided magnitude-spectrum definition instead of the old two-sided power spectrum.

  (All rows above are on the audit `x20`/`short_n20` vector, `validation/reference/fixtures/moments/scipy_moments.json` vectors.x20, except where noted.)
- **Detection & decomposition run on fdars-core 0.47 and match numpy / scipy / R (Phase 15).**
  fdars-core 0.3 → 0.47 introduces a dimension-tracked `FdMatrix` type for most
  detrend/decompose/seasonality functions; the project's own wrapper code was
  passing `(values, n, 1, ...)` to the 0.3 raw-argument API for a single
  series of length `n` — backwards (should be `(values, 1, n, ...)`). fdars
  0.3's own invalid-dimension guard (`m < 4`) then silently returned a no-op
  / degenerate result instead of erroring, so `ts_detrend`, `ts_detrend_by`,
  `ts_decompose_seasonal`, `ts_classify_seasonality(_by)`, and
  `ts_detect_amplitude_modulation` were effectively dead code (B1/B2). Plan
  15-01 bumps to fdars-core 0.47 (`["parallel"]` native, no `"linalg"` —
  removes the duplicate `anofox-regression 0.4.0` / `faer` pulled in
  transitively by 0.3's `linalg` feature) and routes every call through one
  reviewed `single_series_matrix()` helper. Unknown `ts_detrend`/
  `ts_decompose_seasonal` method names now raise an error naming every valid
  choice instead of silently falling back to `'auto'`/`'additive'` (D-02).

  | Function (input) | Before | After | What changed |
  |---|---|---|---|
  | `ts_detrend(cubic40, 'cubic')` `.trend[1]` | `0.0` | `3.154511161980401` | B1 fix: matches `numpy.polyfit(t, y, 3)` to 1e-8 |
  | `ts_detrend(cubic40, 'cubic')` `.method` | `'auto(none)'` (unrecognised name silently became `auto`, which then hit the same dimension guard) | `'polynomial(3)'` | B1 + D-02: `'cubic'` is now a real degree-3 fit, not a silent `auto` fallback |
  | `ts_detrend(cubic40, 'quadratic')` `.method` | `'auto(none)'` (same silent-`auto` path as `'cubic'` above) | `'polynomial(2)'` | D-02: `'quadratic'` now fits degree 2 (previously not a recognised method name at all) |
  | `ts_detrend(.., 'quartic')` / `ts_detrend(.., '7')` | silently became `'auto(none)'`, no error | SQL error: `expected one of: linear, quadratic (alias polynomial, poly), cubic, diff (alias diff1, difference), diff2, loess (alias lowess), auto` | D-02/B3: unknown method names are now a caller error |
  | `ts_detrend_by(.., 'cubic')` `.trend` | all `0.0` (same B1 bug) | identical to the `ts_detrend` scalar result | B1 fix, grouped macro parity |
  | `ts_classify_seasonality(strong_sine_n120, 12)` | `is_seasonal=false`, `timing_classification='non_seasonal'`, `seasonal_strength=NaN` | `is_seasonal=true`, `timing_classification='stable_seasonal'`, `seasonal_strength≈0.9999999999999993` | B2 fix |
  | `ts_detect_amplitude_modulation(growing_amplitude_sine_n240, 12)` `.modulation_type` | `'non_seasonal'` (`has_modulation=false`, dimension guard short-circuited before any analysis) | `'emerging'` (`has_modulation=true`) | B2 fix |
  | `ts_decompose_seasonal(constant_1.0_n48, 12)` `.trend[1]` | `0.0` | `1.0` | B1 fix |
  | `ts_decompose_seasonal(.., 0.0)` (non-positive period) | returned a `NULL` row | SQL error: `Period must be positive` | the FFI's existing `period <= 0` pre-check already reported `INVALID_INPUT`; the C++ layer now raises on it instead of swallowing it into a `NULL` row |
  | `ts_decompose_seasonal(.., 'classicalish')` (unknown method) | silently became `'additive'`, no error | SQL error: `expected one of: additive (alias add), multiplicative (alias mult, mul)` | D-02, applied to decompose |

  "Before" values for `ts_classify_seasonality`/`ts_detect_amplitude_modulation`/
  `ts_decompose_seasonal` are reproduced from fdars-core 0.3.0's own source
  (its `m < 4` dimension guard on the swapped single-series call, read
  directly from the cached crate at
  `~/.cargo/registry/src/*/fdars-core-0.3.0/src/{detrend,seasonal}.rs`), not
  a live re-run of the pre-bump binary — the binary was already rebuilt with
  the fix by the time this table was written; the guard's early-return
  values are deterministic and match the audit's and this plan's own
  pre-bump probes exactly (`ts_detrend`'s `'linear'`/`'cubic'` before-values
  were captured live from the pre-bump build per plan 15-01 Task 2 step 1,
  before any rebuild). `fdars-core 0.47`'s own `detrend_loess`/`auto_detrend`
  are `O(n^2)`: `ts_detrend(.., 'auto')` on a 20000-point series took ~2.5s
  wall and `ts_detrend(.., 'loess')` alone ~0.27s wall on this machine
  (previously instantaneous, since the swapped-argument bug returned a
  no-op before fdars did any real work) — prefer `'linear'`/`'quadratic'`/
  `'cubic'` over `'loess'`/`'auto'` on long series.

  **Plan 15-02 (DET-01): `ts_detect_peaks`/`ts_detect_peaks_by` are now a
  direct port of `scipy.signal.find_peaks` 1.18.1**, replacing fdars-core's
  own derivative zero-crossing peak detector entirely (peak detection no
  longer calls fdars; `ts_analyze_peak_timing(_by)` is unaffected and still
  does). The audit found the old detector missed peaks, reported local
  minima, and gave every plateau prominence `0` (51 found vs scipy's 66 on
  an audited random walk, only 31 matching). `prominence` is now scipy's
  own definition (same scale as the input data, `wlen=None`) for every
  returned peak, not a 0-1-normalised value. `min_distance`/`min_prominence`
  keep their existing SQL parameter names with scipy's own semantics:
  `min_distance` is the minimum *index* distance between kept peaks
  (`ceil`'d; values `<= 1.0`, including the macro default of `1.0`, apply
  no distance filter); `min_prominence` is an inclusive minimum on the real
  prominence scale, not 0-1.

  | Function (input) | Before | After | What changed |
  |---|---|---|---|
  | `ts_detect_peaks(rw150)` `.n_peaks` | `25` | `38` | matches `scipy.signal.find_peaks`'s count on the audited random walk exactly |
  | `ts_detect_peaks(rw150)` peak indices matching scipy | `12/38` (31% of scipy's set) | `38/38` | every scipy peak found at the exact scipy index |
  | `ts_detect_peaks(rw150)` indices that were local minima | `5` (`5, 47, 61, 103, 116`) | `0` | the scipy port's local-maxima scan structurally cannot report a minimum |
  | `ts_detect_peaks([0,1,1,1,0])` peak | index `1`, `prominence=0.0` | index `2` (plateau midpoint), `prominence=1.0` | plateau midpoint reporting + real (non-normalised) prominence |
  | `ts_detect_peaks(sine240, 8.0)` (distance filter) | not comparable to scipy (old detector had no scipy-equivalent distance semantics) | exact match to `scipy.signal.find_peaks(sine240, distance=8)` | D-05 |
  | `ts_detect_peaks(sine240, 0.0, 2.0)` (prominence filter, real scale) | prominence was 0-1-normalised, so `2.0` filtered out every peak | exact match to `scipy.signal.find_peaks(sine240, prominence=2.0)` | prominence is no longer normalised |

  "Before" values for `ts_detect_peaks(rw150)`/`ts_detect_peaks([0,1,1,1,0])`
  were captured live from the pre-port build (plan 15-02 Task 1 step 1,
  before any rebuild) — see `.planning/phases/15-detection-decomposition-on-fdars-0-47/15-02-SUMMARY.md`
  for the exact commands.

  **Plan 15-03 (DET-02, DET-03): BOCPD is now Adams & MacKay (2007) done
  properly, and PELT (Rust/FFI only) is scale-invariant with a working
  prune.** `_ts_detect_changepoints_bocpd`/`ts_detect_changepoints(_by)`/
  `ts_detect_changepoints_agg` gained a normalised Student-t predictive (the
  full `Gamma((nu+1)/2)/(Gamma(nu/2)*sqrt(nu*pi)*scale)` kernel, computed in
  the log domain — the old code had no normalising constant at all), a
  Normal-Gamma posterior update using each run's SAMPLE mean (not the
  posterior mean), and run-length truncation at 500 that merges the tail
  probability mass into the last kept run length instead of dropping it.
  The series is standardised first (`z = (x - median(x)) / sigma_hat`,
  `sigma_hat` = a MAD-of-first-differences robust scale), making the result
  scale/shift invariant. A changepoint is now flagged on a MAP run-length
  *reset* (the start of the new, shorter run), not `P(r=1) > 0.5` directly;
  `changepoint_probability` is `P(r_t = 1)` — the probability `x_t` itself
  opened a new run — under the corrected predictive.

  | Function (input, hazard 250) | Before | After | What changed |
  |---|---|---|---|
  | `_ts_detect_changepoints_bocpd(step3_n200, 250.0, true)` `.changepoint_indices` | `[]` (missed the 3-sigma step) | `[100]` | normalised Student-t + sample-mean update (G3/G4) |
  | `_ts_detect_changepoints_bocpd(noise_n200, 250.0, true)` `.changepoint_indices` | `[]` | `[]` | unchanged — stays quiet on stationary noise |
  | `_ts_detect_changepoints_bocpd([100]*12+[10]*12, 250.0, true)` `.changepoint_indices` | `[12]` | `[12]` | same flag, but now via the MAP-reset rule, not the old unnormalised `P(r=1)>0.5` heuristic |
  | same, `.changepoint_probability[12]` | `0.9935515031241058` | `0.8798758070736283` | the normalised Student-t predictive changes every probability value (G3) |
  | `_ts_detect_changepoints_bocpd(step3_n200 * 100 + 7, 250.0, true)` `.changepoint_indices` | not scale-invariant (unnormalised raw-data prior) | `[100]` (identical to the unscaled series) | standardisation on `z` makes BOCPD scale/shift invariant |

  Measured false-alarm rate (100 independent `N(0,1)` series of length 200,
  seeds `16000..16099`, run against the fixed implementation): `hazard_lambda
  250` flags 20 points in total across all 100 series, `500` flags 11, `1000`
  flags 5 — hazard is a real false-alarm/sensitivity knob, not a cosmetic
  default. On 100 independent 3-sigma-step series (seeds `17000..17099`), the
  step is flagged within 3 samples of the true changepoint in 94/100 series
  at every one of those three hazard values — detection delay is essentially
  hazard-independent over this range, only the false-alarm rate changes.
  "Before" values above were captured live from the pre-fix build
  (`_ts_detect_changepoints_bocpd(step3_n200, 250.0, true)`,
  `_ts_detect_changepoints_bocpd(noise_n200, 250.0, true)`, and the
  issue-71 step, all at `hazard_lambda=250.0`) before any rebuild.

  **PELT** (`anofox_fcst_core::changepoint::detect_changepoints` —
  Rust/FFI-only: its SQL wrapper was removed as dead code in Phase 12, and
  `ts_detect_changepoints` stays BOCPD; adding a PELT SQL surface is
  deferred) now normalises every cost by the same `sigma_hat` used by
  BOCPD's standardisation, so a series and that series `* 100` (or `+ 1e6`)
  give identical changepoints. `L1`'s cost now uses the segment median, not
  the mean (previously `mean3_n150 * 100` found 25 spurious changepoints
  vs. 2 real ones on the unscaled series — the `* 100` audit repro).
  Pruning is a real exact-safe prune (Killick et al. 2012), not dead code —
  on a 3000-point, 10-segment series it evaluates well under 20% of the
  `n(n+1)/2` segment costs an unpruned exact DP needs, and reproduces that
  unpruned DP's result exactly (`ChangepointResult.n_cost_evaluations`
  measures this directly).

  | Function (input) | Before | After | What changed |
  |---|---|---|---|
  | `detect_changepoints(mean3_n150, 5, None, L2)` | `[50, 100]` (2 changepoints) | `[50, 100]` | unchanged — correct even pre-fix on the unscaled series |
  | `detect_changepoints(mean3_n150 * 100, 5, None, L2)` | `[8, 13, 20, 26, 31, 38, 43, 50, 55, 61, 66, 71, 77, 82, 87, 93, 100, 105, 110, 116, 121, 126, 131, 136, 142]` (25 spurious changepoints) | `[50, 100]` (identical to the unscaled series) | sigma-normalised cost (D-07) |

  "Before" values above were captured live from the pre-fix build (`mean3_n150` =
  `[0]*50 ++ [2.5]*50 ++ [-1.0]*50 + N(0,1)` noise, `rng(15501)`) before any rebuild.

  **Plan 15-04 (DET-04): MSTL is now a real R `forecast::mstl` port, and the periods you
  request are honoured end to end.** Before this plan, `_ts_mstl_decomposition` /
  `ts_mstl_decomposition_by` ALWAYS passed `nullptr`/`0` for the periods argument to the
  FFI, regardless of what SQL requested — `seasonal_periods`/`periods` had no effect
  whatsoever, and MSTL itself was a one-pass classical moving average that sorted
  requested periods DESCENDING (longest first) with a broken even-window trend formula
  outside the seasonal path (the "no usable period" fallback summed `2*half_window+1`
  values but divided by `window`, off by one element). `decomposition::mstl_fit` is now
  the single shared implementation of R `forecast::mstl` (ascending usable-period filter
  `p >= 2 && n > 2p`, de-duplicated, two passes with `s.window = 7 + 4*position`, trend
  from the last STL fit) consumed by BOTH `mstl_decompose` and `ts_stats`'
  `trend_strength`/`seasonality_strength` (D-09) — `ts_stl_period`'s candidate scoring
  changes accordingly, since it now scores against real STL instead of the old
  classical-MA approximation.

  | Function (input) | Before | After | What changed |
  |---|---|---|---|
  | `_ts_mstl_decomposition(ones_n100, 'trend', [])` (no periods, non-seasonal fallback) `.trend[50]` | `1.05` | `1.0` | the old fallback trend summed 21 values but divided by a window of 20 (off-by-one); the new `supsmu` fallback gives the exact constant (G11) |
  | `_ts_mstl_decomposition(det84, 'fail', [7])` `.periods` | `[]` (periods argument was always ignored) | `[7]` | periods now flow from SQL to the FFI; `.trend`/`.seasonal` match R `forecast::mstl` to 1e-8 |
  | `_ts_mstl_decomposition(msts_7_28, 'fail', [28, 7])` `.periods` | `[]` (ignored) | `[7, 28]` | periods are honoured AND returned ascending, regardless of request order (previously the classical-MA code sorted descending) |
  | `ts_mstl_decomposition_by(.., MAP{'seasonal_periods': '[7, 365]'})` `.periods` | `[]` (param had no effect at all) | `[7, 365]` (or whichever survive the `n > 2p` usable filter) | the macro now `CAST`s `params.seasonal_periods`/`periods` into the native table function's new third argument |

  "Before" values above (`.trend[50]=1.05`, periods always `[]`) are reproduced from the
  pre-plan source directly (`crates/anofox-fcst-core/src/decomposition.rs`'s old
  `(n/5).max(3).min(n)`-windowed fallback and the C++ layer's hardcoded `nullptr, 0`
  periods argument, both read and removed by this plan) rather than a live pre-rebuild
  probe, since both are simple closed-form/structural facts, not something that could
  drift between a probe and the documented formula.

  **`ts_decompose_seasonal` gains classical decomposition (D-10): `'classical_additive'`
  (alias `'classical'`) / `'classical_multiplicative'`, an R `stats::decompose` port**,
  next to the existing fdars LOESS+Fourier `'additive'`/`'multiplicative'` methods from
  plan 15-01. A centred `2 x period` filter (even period) or plain `period`-term mean (odd
  period) trend, `NaN` at the `floor(period/2)` edges exactly where R returns `NA`, and a
  centred per-phase seasonal figure. `classical_multiplicative` rejects any value `<= 0`
  with a SQL error — a deliberate, documented deviation from R, which would silently
  divide by it and return `Inf`/`NaN`/negative garbage (see
  `validation/reference/README.md`).

  | Function (input) | Before | After | What changed |
  |---|---|---|---|
  | `ts_decompose_seasonal(ones48, 12.0, 'classical_additive')` `.trend[1]`/`.trend[7]` | n/a (method did not exist) | `NaN` / exactly `1.0` | new method; matches R `stats::decompose` NA edges and constant-series exactness (G10/G11) |
  | `ts_decompose_seasonal(.., 12.5, 'classical')` | n/a | SQL error: `classical decomposition requires an integer period >= 2` | classical methods require an integer period |

  **Plan 15-05 (DET-05, DET-06, DET-07): period detection is now scipy/statsmodels-faithful,
  seasonal strength detrends first, wavelet strength is removed, and `is_constant` is
  scale-relative everywhere.** fdars-core 0.47's own FFT/ACF/regression period estimators and
  `seasonal_strength_wavelet` remained broken after the 15-01 bump (D-04); they are replaced
  or fixed in this project's own wrapper code (D-11, D-12), with no upstream fdars-core change.

  | Function (input) | Before | After | What changed |
  |---|---|---|---|
  | `ts_estimate_period_fft(0.5*t + 2*sin(2*pi*t/12), n=121)` `.period` | `121` (= `n`) | `12.1` | the undetrended periodogram's strongest bin was the trend's own near-zero frequency; now linearly detrended first, candidates restricted to `3 <= k <= floor(n/2)` (G18) |
  | `ts_estimate_period_fft(.., n=121)` `.confidence` | n/a (period was wrong) | `58.246...` | peak / mean(non-DC power) on the detrended periodogram |
  | `ts_estimate_period_acf([1,-1]*20)` `.period` | not reachable (search started at lag 3, no significance test) | `2.0` | smallest significant local-max lag `>= 2`, bound `1.96/sqrt(n)` (G17) |
  | `_ts_detect_periods(shape12_n144, 'regression')` `.primary_period` | could miss an exact period like `12` (50-point linear grid) | `12.0` | default grid is now every integer `2..floor(n/2)` (G20) |
  | `ts_lomb_scargle(.., 0.0, 0.0, 1)` (n_frequencies=1) | division by zero (`n_freq - 1 == 0`), garbage power | SQL error: `n_frequencies must be at least 2` | G21 |
  | `ts_lomb_scargle(.., 24.0, 6.0, 0)` (min > max) | an inverted frequency grid, garbage power | SQL error: `min_period must be less than max_period` | G21 |
  | `ts_seasonal_strength(0.5*t + 2*sin(2*pi*t/12), 12.0)` (`'variance'`) | `0.002963899897805402` | `>= 0.95` | detrends LINEARLY first, then measures strength on the residual (G19) |
  | `ts_seasonal_strength(.., 12.0, 'spectral')` | `0.0142` | `>= 0.9` | same detrend-first fix |
  | `ts_seasonal_strength(white_noise, .., 'wavelet')` | `1.0000` (always maximal, regardless of input) | SQL error: `removed: ... use 'variance' (default) or 'spectral'` | `'wavelet'` is removed entirely, not fixed — it was structurally unable to discriminate signal from noise |
  | `ts_seasonal_strength(.., 'foo')` (unknown method) | silently became `'variance'`, no error | SQL error: `expected one of: variance (alias var), spectral (alias spec, fft)` | strict parsing, no silent fallback |
  | `ts_seasonal_strength(.., 0.0)` / `ts_seasonal_strength_windowed(.., 0.0)` (non-positive period) | returned a `NULL` row | SQL error: `Period must be positive` | the FFI's existing `period <= 0` pre-check already reported `INVALID_INPUT`; the C++ layer now raises on it instead of swallowing it into a `NULL` row |
  | `_ts_is_constant([1e12, 1e12 + 0.001])` / `ts_stats`/`ts_data_quality`'s `is_constant` on the same values | `false` (absolute `f64::EPSILON` tolerance: `0.001 > f64::EPSILON`) | `true` | SCALE-RELATIVE tolerance `max - min <= 1e-10 * max(1, max\|x\|)`, now shared by `filter::is_constant`, `ts_stats`, `ts_data_quality`, and `ts_drop_constant_by` (G25); at the `1e12` scale a `0.001` wiggle is immaterial |
  | `_ts_is_constant([1.0, 1.0 + 1e-9])` | `false` | `false` (unchanged) | NOT a behaviour change — shown for contrast with the row above: at the much smaller `1.0` scale, the same relative rule still correctly reports "not constant" for a `1e-9` difference |

  **Edge-NULL interpolation behaviour is unchanged (D-14, revised after research) and now
  documented and pinned in SQL:** `ts_fill_nulls_forward_by`/`ts_fill_nulls_backward_by` fill
  one direction only (a leading NULL stays NULL under forward fill, a trailing NULL stays
  NULL under backward fill); internal exog/panel-grid interpolation fills interior gaps
  linearly and leading/trailing gaps with the nearest observed value (constant edge
  extension, never extrapolated, G26). See `docs/api/04-data-preparation.md`'s new "Edge
  NULLs" subsection.

  "Before" values above are reproduced from this plan's own planning-time probes (the
  planner's `0.5*t + 2*sin(2*pi*t/12)` / wavelet-on-white-noise measurements against the
  pre-15-05 build, recorded in `15-05-PLAN.md`'s `<interfaces>` block and `15-RESEARCH.md`)
  and from closed-form facts read directly from the old `periods.rs`/`seasonality.rs`/
  `filter.rs`/`quality.rs`/`stats.rs` source this plan replaced (the division-by-zero/
  inverted-grid/absolute-epsilon behaviours), not a fresh live re-run of the pre-15-05
  binary — the binary had already been rebuilt with this plan's fix by the time this table
  was written.
- **Every `ts_features` surface now returns all 117 features, including `autocorrelation_lag10`.**
  `ts_features_by`, `ts_features`, `ts_features_agg`, and `ts_features_table` previously
  derived their output schema from calling the Rust core on a 10-point dummy series —
  `autocorrelation_lag10` requires `n > 10` to be computed at all under the old
  length-gated insertion logic, so it silently never appeared in any SQL result (116
  columns/struct keys instead of 117). `extract_features()` now inserts every one of the
  117 `list_features()` names unconditionally for any input of length >= 1 (features
  undefined for that input length — e.g. `mean_change` at `n=1`, `fft_coefficient_9_*` at
  `n=8` — are `NULL`/NaN rather than a missing column).
- **Conformal and bootstrap intervals are horizon-ordered, per-group and keep their
  nominal coverage (Phase 16).** `ts_conformal_by`/`ts_conformal_apply_by`/
  `ts_interval_width_by` pair lower/upper/actual by a required date/step column instead
  of independently value-sorting each side (plan 16-01, D-01/D-04/D-05 audit); residuals
  and empirical coverage are computed in Rust, never subtracted in SQL; `ts_conformal_
  calibrate` calibrates per group by default (pooled on explicit request); asymmetric
  conformal derives both margins from the full signed-residual array instead of a
  positive/negative subset (plan 16-02, CONF-03); adaptive conformal is reachable from
  SQL with explicit per-row difficulty at both calibration and prediction, with no
  batch-mean normalisation (CONF-04); insufficient calibration data is `NULL` +
  `calibration_insufficient = true`, never silently clamped to the maximum residual
  (CONF-02); bootstrap prediction intervals put a right-skewed residual's long tail on
  the correct (upper) side, and `n_paths`/coverage are validated instead of silently
  adjusted (plan 16-03, CONF-01, D-10); the `'jackknife+'` strategy — which computed the
  identical quantile to `'split'` with no leave-one-out refits — is removed, and every
  strategy/method name is parsed strictly, naming the valid choices on a typo (CONF-05,
  D-09). No compatibility flag restores the old signatures or semantics.

  | Function (input) | Before | After | What changed |
  |---|---|---|---|
  | `ts_conformal_by(two_group, ..., alpha=0.1)` output shape | one row per group, `lower`/`upper`/`point` as value-sorted `LIST`s | one row per `(group, date)`, scalar `lower`/`upper`/`point` columns | D-01/D-13: a required `date_col` orders and pairs every row; no more independent per-column sorting |
  | same, group A's `point` order | `[10.0, 20.0, 30.0]` (value-sorted — destroyed the input order) | `[30.0, 10.0, 20.0]` (the input order, paired by `date_col`) | the single most severe pre-Phase-16 bug: silent mispairing, not just misordering |
  | same, `.coverage` | `0.9` (the nominal `1 - alpha` literal) | `0.95` (EMPIRICAL fraction of calibration residuals within bound); new `.nominal_level = 0.9` | D-02: `coverage` now reports what was actually achieved, not the request |
  | `ts_conformal_calibrate(two_group, ..., alpha=0.1)` | 1 pooled row, `conformity_score = 17.075000000000003`, `n_residuals = 40` | per group by default: A `conformity_score = 1.9050000000000056`, B `= 19.05` (each `n_calibration = 20`); pooled only via `params := {'pooled': true}` | D-03: groups with different residual scales get different quantiles; `n_residuals` renamed `n_calibration` |
  | `ts_conformal_apply_by(forecasts, group_col, forecast_col, 15.0)` | took a literal conformity score; one row per group | takes a calibration TABLE (`ts_conformal_calibrate`'s output), matched per group or pooled fallback; one row per `(group, date)` | D-14: a literal score is applied by wrapping it in a one-row table with `group_col = NULL` |
  | `ts_interval_width_by(results, group_col, lower_col, upper_col)` | 4 positional args, lower/upper each independently sorted by value | 5 args (adds a required `date_col`); lower/upper read from ONE date-ordered `LIST` of per-row structs | D-01/D-05: pairs by date, never two independently-sorted lists |
  | `ts_conformal_quantile([1,2,3,4,5]::DOUBLE[], 0.1)` | `5.0` (clamped to the max residual) | `NULL` + would-be `calibration_insufficient = true` on the calibrate/by surfaces | D-05/D-06 (CONF-02): `ceil((n+1)(1-alpha)) > n` (`n=5 < 9`, the alpha=0.1 minimum) is now reported explicitly, never silently clamped |
  | `ts_conformal_quantile([1,2,3,4,5]::DOUBLE[], 0.0)` / `(..., 1.5)` | `5.0` / `NULL` | SQL error naming the value (`"alpha must be in (0, 1), got 0"` / `"got 1.5"`) on every conformal surface | D-06 |
  | `ts_conformal_predict_asymmetric(asym_n20, [100.0], 0.2)` margins | lower `1.0`, upper `3.75` (positive/negative RESIDUAL-SUBSET quantiles) | lower `0.81`, upper `3.5125` (alpha/2 and 1-alpha/2 quantiles of the FULL signed-residual array and its negation) | D-07 (CONF-03): the old construction over-covered by 5-6pp and was 1.2x-1.7x wider on a seeded Monte Carlo simulation (normal and log-normal residuals, alpha 0.1/0.2); the new construction hits nominal coverage within ~0.2pp |
  | adaptive conformal (`method := 'adaptive'`) from SQL | unreachable — no difficulty parameter existed on any macro/scalar | `ts_conformal_by`/`ts_conformal_calibrate`/`ts_conformal_apply_by` take a `difficulty_col`; `ts_conformal_learn`/`ts_conformal_apply`/`ts_conformal_intervals` gain difficulty overloads; a forecast's interval is independent of its batch (exact-equality tested) | D-08 (CONF-04): no batch-mean normalisation (previously would have scaled by the mean difficulty of whichever batch a forecast happened to be computed alongside) |
  | `ts_conformal_predict_per_step` (2 folds x horizon 3, alpha 0.1) `.half_widths` | `[1.0, 3.0, 1.0]` (clamped to the per-step maximum) | `[NULL, NULL, NULL]` (`ceil(3*0.9)=3 > 2` is insufficient at every step) | D-05 applied per-step |
  | `ts_bootstrap_intervals([0]*9+[10.0], [100.0], 2000, 0.9, 42)` | lower `90.0`, upper `100.0` (the long tail mirrored onto the LOWER side) | lower `100.0`, upper `110.0` (the long tail correctly on the UPPER side) | D-10 (CONF-01, G2): `BootstrapPredictor.fit` was called with residuals and zeros swapped |
  | `ts_bootstrap_intervals(residuals, forecasts, n_paths, coverage, seed)` with `coverage=1.5`, `n_paths=5`, or `n_paths * horizon > 10,000,000` | silently clamped (`n_paths` floored to 10) or panicked (bad `coverage`), or allocated unboundedly | SQL error naming the offending value | T-16-17 |
  | `ts_conformal_learn(residuals, [0.1], 'symmetric', 'jackknife+')` (and `'JackknifePlus'`/`'jackknifeplus'`/`'jackknife_plus'`/`'jk+'`) | silently mapped to the `JackknifePlus` strategy (which computed the identical quantile to `'split'`) | SQL error: `"conformal strategy '...' was removed: it computed the same quantile as 'split' (no leave-one-out refits). Use 'split' or 'crossval'."` | D-09 (CONF-05, G15) |
  | `ts_conformal_learn(residuals, [0.1], 'bogus', 'typo')` | silently defaulted to method `'symmetric'`, strategy `'split'` | SQL error listing the valid method names / `"unknown conformal strategy '...'; valid: split, crossval (alias cross_val, cv)"` | D-09: strategy/method parsing is strict in Rust everywhere, not just for the removed jackknife+ aliases |
  | conformal macro `params` argument | read via `json_extract_string` (required `LOAD json`) | read json-free via `params::MAP(VARCHAR, VARCHAR)` with `CAST` (a `{'key': value}` STRUCT literal casts implicitly); malformed values error instead of silently defaulting | plan 16-01 |

  The method strings returned by the from-pairs/calibrate surfaces are the canonical
  `'symmetric'`/`'asymmetric'`/`'adaptive'` names (never the legacy `'split_conformal'`/
  `'asymmetric_conformal'` the older `ts_conformal_predict`/`ts_conformal_predict_
  asymmetric` scalars still return — those two scalars are untouched by Phase 16).

- **Runs on anofox-forecast 0.16.1 — ADF/KPSS match statsmodels / R tseries,
  `ts_stationarity` uses the standard ADF/KPSS four-way verdict, and AutoARIMA selects
  models the way R `auto.arima` does (Phase 17).** `anofox-forecast` bumps `0.15.10` →
  `0.16.1` (D-01/D-14; no `[patch]`/git/path override). `ts_adf`/`ts_adf_by`/`ts_kpss`/
  `ts_kpss_by`/`ts_stationarity`/`ts_stationarity_by` gain a `regression := 'c' | 'ct'`
  parameter (default `'c'`, statsmodels/urca naming) applied to both tests; `ts_adf`
  returns statsmodels `adfuller(autolag='AIC')`-exact statistics, AIC-selected lags, and
  MacKinnon (1994/2010) p-values and finite-sample critical values (no longer a fixed
  asymptotic constant table); `ts_kpss` under `regression := 'ct'` runs a new trend-KPSS
  implemented in this extension's Rust core (D-05a — not available in the crate),
  matching R `tseries::kpss.test(null = "Trend")`. `ts_stationarity`'s four-way verdict
  is now the standard ADF+KPSS mapping (D-04) — three of the four labels were previously
  swapped — plus an explicit `'constant'` verdict for a constant series and SQL `NULL`
  (not a mislabeled string) for a degenerate series (too short, or a collinear
  regression). `ETS` model specs `'MAA'`/`'MAdA'` now fit (the crate's own
  `ETSSpec::is_valid()` always returns `true` since 0.16.0; this wrapper's own redundant
  gate is removed). `AutoARIMA` now reproduces R `forecast::auto.arima` (PR #223, UPST-01)
  — selected orders and point forecasts change wherever the old search differed from R.
  `ETS`/`SES`/`Holt`/`HoltWinters`/`Theta`/`TBATS`/`MSTL`/`GARCH` prediction intervals and
  `Theta`'s point forecasts also shift (0.16.0's corrected formulas and alpha
  optimisation, PRs #222/#224/#225); plan 17-03 documents the measured R parity.

  | Function (input) | Before | After | What changed |
  |---|---|---|---|
  | `ts_adf(air_passengers)` `.statistic` / `.p_value` / `.lags` | `-1.7304178767068465` / `0.3` / `5` | `-0.986479461317597` / `0.7581636095609505` / `4` | 0.16.1's `adfuller`-equivalent augmented regression + AIC lag search replaces the old fixed-lag, non-augmented implementation |
  | `ts_adf` critical values (any series, regression `'c'`) | fixed asymptotic constants `-3.43` / `-2.86` / `-2.57` for every `n` | MacKinnon (2010) finite-sample surface, depends on `nobs`; e.g. `diag_series` group B: `-3.626651907578875` / `-2.9459512825788754` / `-2.6116707716049383` | D-05: `test/sql/ts_diagnostics.test`'s 3 hardcoded-constant assertions re-pinned to the fixture |
  | `ts_adf_by`/`ts_kpss_by`/`ts_stationarity_by` `regression` parameter | did not exist (constant-only) | `regression := 'c'` (default) or `'ct'`, applied to both tests | D-05 |
  | `ts_kpss(trend_stationary_series, regression := 'ct')` | n/a (no trend KPSS) | `.statistic = 0.11714250016361194`, matches R `tseries::kpss.test(null="Trend")` to 1e-10 | D-05a, new trend KPSS implemented in this extension |
  | `ts_kpss` p-value range | approximate, level table only | `[0.01, 0.10]` for both `'c'` and `'ct'` (interpolated tseries tables) | D-05a |
  | `ts_stationarity(air_passengers)` `.verdict`, regression `'c'` / `'ct'` | `'difference_stationary'` / `'difference_stationary'` (3 of 4 D-04 labels were swapped) | `'non_stationary'` / `'stationary'` | D-04: ADF rejects+KPSS doesn't → `stationary`; ADF doesn't+KPSS rejects → `non_stationary`; neither rejects → `trend_stationary`; both reject → `difference_stationary` |
  | `ts_stationarity` on a 30-point constant series | `'non_stationary'` | `'constant'` | D-04 explicit constant verdict |
  | `ts_stationarity` on `[1.0, 2.0, 3.0]` (too short) / critical values on any NaN-statistic input | `'difference_stationary'` (a real-looking but meaningless label) / `0.0` | `.verdict IS NULL`, `.adf_statistic`/critical values all `NaN` | D-04/T-17-04: a NaN statistic now propagates to every dependent field, never a misleading label or a `0.0` |
  | `ts_adf`/`ts_kpss`/`ts_stationarity` with `regression := 'n'` (or any value other than `'c'`/`'ct'`) | n/a (parameter did not exist) | SQL error: `"expected one of: c, ct"` | D-05 |
  | `ts_ljung_box`/`ts_ljung_box_by` `fitted_params` parameter | did not exist; `df` was always `= lags` regardless of any fitted model | `fitted_params := p + q` (default `0`), `df = lags - fitted_params`, p-value matches R `Box.test(fitdf = p + q)`; e.g. ARMA(1,1) residuals, lag 10: `df = 8`, `.p_value = 0.601916478272279` | D-06, DIAG-04 |
  | `ts_ljung_box(x, 0)` on a 30-point series | `.statistic = 0.0` / `.p_value = 1.0` / `.df = 1` (silently floored by the crate) | SQL error naming `fitted_params`, the lags and "degrees of freedom" | T-17-07: `df <= 0` at the effective lag count is now a caller error, not a silently floored result |
  | `ts_ljung_box`/`ts_ljung_box_by` with `fitted_params >=` the effective lags (explicit or default) | n/a (parameter did not exist) | SQL error, same as above | T-17-07 |
  | `ts_ljung_box` p-value for `df > 30` (e.g. lag 40 on `diff(rw)`) | Wilson-Hilferty approximation, `.p_value ≈ 0.5296114971103353` at the pre-`fitted_params` `df = 40` | exact chi-square survival function (`statrs::distribution::ChiSquared`), `.p_value = 0.438648802479122` at `df = 38` (`fitted_params := 2`) | D-06/T-17-09: matches R `pchisq`/`Box.test` exactly instead of deviating by up to ~2e-4 |
  | `ts_forecast_by('ETS', .., MAP{'model': 'MAA'})` / `'MAdA'` | SQL error: `"ETS(MAA) is an unstable model combination per FPP3 taxonomy"` | fits: `model_name = 'ETS(M,A,A)'` / `'ETS(M,Ad,A)'` | 0.16.0 CHANGELOG "Changed — ETS (UPST-03)": `ETSSpec::is_valid()` always returns `true` |
  | `ts_forecast_by('AutoETS', air_passengers, seasonal_period=12)` `.model_name` / `.yhat[1]` | `'AutoETS(A,N,M)'` / `446.24696013683115` | `'AutoETS(M,A,M)'` / `447.0613404930957` | 0.16.0 PR #224 (ETS) changes the AIC-selected candidate and its fit |
  | `ts_forecast_by('Theta', air_passengers, seasonal_period=12)` `.yhat[1]` | `433.1175018887427` | `440.07675284965757` | 0.16.0 PR #222: Theta's alpha is now optimised per-series instead of fixed at `0.1` |
  | `ts_forecast_by('AutoARIMA', air_passengers, seasonal_period=12)` `.model_name` / `.yhat[1]` | `'AutoARIMA(1,1,3)(0,1,1)[12]'` / `444.4025123655398` | `'AutoARIMA(2,1,1)(0,1,0)[12]'` / `445.5968786698059` | 0.16.1 PR #223 (UPST-01): AutoARIMA ports R `forecast::auto.arima`'s stepwise search and estimation — `(2,1,1)(0,1,0)[12]` is the order R itself selects on this series |
  | `ts_forecast_by('Holt', air_passengers)` `.yhat[1]` | `437.9429629258883` | `434.233746360747` | D-12/D-13 (plan 17-03 Task 1): SQL `'Holt'` remapped from `HoltLinearTrend::auto()` to the crate's `ETS(A,A,N)` — R `holt()`'s actual definition. Cuts the AirPassengers R-parity point deviation 6.86x (rw 13.41x); root cause `RC-01` (`validation/reference/fixtures/r_forecast/air_passengers.json` `tolerance.root_causes`, rendered in `validation/reference/README.md` by plan 17-03 Task 3). `model_name` stays `'Holt'`; `build_forecaster`'s CV/ensemble construction changed identically |
  | `ETS`/`SES`/`Holt`/`HoltWinters`/`Theta`/`TBATS`/`MSTL`/`GARCH` prediction intervals | pre-0.16.0 crate interval formulas | 0.16.0's corrected formulas | summary row; plan 17-03 documents the measured R parity per model |
- **`ts_data_quality_summary` gains `n_unassessed` (Phase 18, WR-05/D-10).** A group whose
  `date_col` values are all `NULL` previously contributed `0` to `n_good`/`n_fair`/`n_poor`
  (SQL three-valued logic: `NULL >= 0.8` is `NULL`, not `TRUE`, so `SUM(CASE WHEN ... THEN 1
  ELSE 0 END)` took the `ELSE 0` branch for every bucket) while still counting toward
  `n_total` and being skipped by `AVG()` — silently undercounting the classified total. A
  new final column, `n_unassessed`, counts these groups explicitly: the invariant
  `n_good + n_fair + n_poor + n_unassessed = n_total` now always holds, and `avg_score`
  is documented as covering assessed groups only.

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
- **R `forecast` parity suite (FCST-09, Phase 17 plan 17-03).** 36 point / 95%-interval
  checks against R `forecast` 9.0.2 across AirPassengers (15), a seeded random walk (9), an
  intermittent-demand series (3) and a new seeded random-walk-with-drift series (9), covering
  all ten FCST-09 models (`Naive`, `SeasonalNaive`, `RandomWalkDrift`, `SESOptimized`/`ETS
  'ANN'`, `Holt`/`ETS 'AAN'`, `HoltWinters`/`ETS 'AAA'`/`ETS 'MAM'`, `AutoETS`, `AutoARIMA`,
  `Theta`, `CrostonClassic`). At the time of this suite's original authorship (Phase 17 plan
  17-03, anofox-forecast 0.16.1), 24 of 36 checks were `parity`; the 12 `known_gap` checks each
  cited an identified root cause (D-12) — 1 extension cause already fixed (`RC-01`, the
  `'Holt'` remap, see Breaking changes), 1 upstream `AutoETS` optimiser gap (`RC-03`), 1
  upstream `SESOptimized` optimiser gap on the intermittent series (`RC-04`), 1 upstream
  `AutoARIMA` interval-variance gap on AirPassengers only (`RC-05`, D-16), and 9 checks where
  R's own optimiser reaches a looser, in-bounds fit than the crate's (`RC-02`, not a defect).
  **RC-05 is now RESOLVED** (Phase 18 plan 18-05, anofox-forecast 0.16.2, upstream PR
  #230/#229): the suite is currently 25 of 36 `parity` / 11 `known_gap` — see the Phase 18
  plan 18-05 entry below. **AutoARIMA's R `auto.arima` parity (D-15/D-16):** since
  anofox-forecast 0.16.1 (PR #223), AutoARIMA's selected order and point forecast are hard
  parity on every series (air `AutoARIMA(2,1,1)(0,1,0)[12]`, rw/trend `AutoARIMA(0,1,0)`); the
  AirPassengers 95% interval was the one allowed exception under D-16, now resolved (see
  below). A seeded selection-frequency simulation (UPST-01, 30 random walks + 30 AR(0.7)
  series) measured 0 share deviation and 60/60 exact `(p,d,q)` agreement with R `auto.arima`,
  closing UPST-01. **Monte Carlo interval coverage (D-10):** a seeded 2,000-replication
  local-level simulation measured `ETS(A,N,N)`/`SESOptimized` 95% coverage within [0.935,
  0.957] per horizon and 80% coverage within [0.787, 0.809] per horizon — both within ±3pp of
  nominal. See `validation/reference/README.md`'s "r_forecast parity tolerances" and
  "R-parity gap root causes" sections for the full tables; upstream drafts for `RC-03`/`RC-04`
  (RC-05 resolved, see below) are under
  `.planning/phases/17-upstream-consumption-diagnostics-parity/diagnosis/upstream-issues/`
  (not filed — left for the user).
- **RC-05 resolved: AutoARIMA's AirPassengers 95% interval is now hard R parity (Phase 18
  plan 18-05, anofox-forecast 0.16.2).** `anofox-forecast` bumps `0.16.1` → `0.16.2` (no
  `[patch]`/git/path override), shipping the SARIMA/ARIMA/AutoARIMA `KalmanForecast`
  prediction-interval fix (upstream PR #230, shipping PR #229). D-16's one allowed AutoARIMA
  known gap (`air/auto_arima/AutoARIMA`'s 95% interval, `RC-05`) is retired: the measured
  interval deviation drops from `1.846662e-1` (0.16.1) to `4.592775e-3` (0.16.2), inside the
  parity tier (`interval_tol=7e-3`). `rw`/`trend`'s already-parity AutoARIMA intervals
  re-measure tighter too (`rw`: `3.276619e-8` → `8.062277e-10`; `trend`: `3.361326e-3` →
  `8.064310e-10`, tolerance tightened `6e-3` → `1e-6`) — never loosened. Every other R-parity
  check is unaffected (point/interval deviations unchanged). The FCST-09 suite is now 25 of 36
  `parity` / 11 `known_gap`. `repro_rc_05` (`r_parity_diagnosis.rs`) is inverted (asserts the
  fix holds, worst-horizon ratio `>= 0.95`, measured `0.9951`) rather than deleted, as a
  standing regression guard, and is no longer `#[ignore]`d.

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
- **`ts_detect_periods` / `ts_detect_periods_by` default call (no `expected_periods`) no
  longer returns an all-NULL row** (`DET-01`, Phase 18 plan 18-06, milestone-audit gap
  closure). Both table macros always call the 6-arg `_ts_detect_periods` overload, passing
  an explicit SQL `NULL` for `expected_periods` when `params` has no `expected_periods` key;
  without `FunctionNullHandling::SPECIAL_HANDLING`, DuckDB's default null handling
  short-circuited the whole row to `NULL` before the function body — which already treats a
  NULL `expected_periods` as "not provided" — ever ran. Same bug class as the Phase 17 WR-01
  `ts_ljung_box`/`ts_residual_diagnostics` fix. Fixed by setting `SPECIAL_HANDLING` on the
  5-arg and 6-arg `_ts_detect_periods` overloads. A second, independent macro bug found and
  fixed alongside it: `expected_periods` was extracted via `json_extract()` (returns a quoted
  JSON scalar that cannot `CAST` to `DOUBLE[]`) instead of `json_extract_string()`, so the
  `expected_periods` path was never reachable through the documented `MAP` params form at
  all — `ts_detect_periods_by(..., MAP{'expected_periods': '[12.0]'})` raised a Conversion
  Error before this fix. Known since Phase 10 (10-06/10-08 SUMMARY), worked around in docs
  and the `anofox-forecast-detection` skill via `ts_detect_periods_agg` — those workaround
  notes are now retired; the aggregate form remains documented as a valid alternative.

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
