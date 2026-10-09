# Phase 10: Forecast Wrapper Delegates to Crate Models - Research

**Researched:** 2026-10-09
**Domain:** Rust FFI forecasting wrapper (anofox-fcst-core/forecast.rs + anofox-fcst-ffi/lib.rs) delegating to anofox-forecast 0.15.10 crate models; C++ DuckDB table/scalar/agg param validation
**Confidence:** HIGH (every claim below is grounded in `Read`/`grep` of the actual crate source at `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.10` and the repo's own `forecast.rs`/`lib.rs`/`ts_forecast_native.cpp` this session — no training-knowledge guesses about model internals)

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

- D-01: Use the crate model's `predict_with_intervals(h, level)` for every model; pass crate intervals through unchanged, including models whose crate intervals are known to be miscalibrated until Phase 11/17 (SES flat, seasonal ETS flat, Theta ~2×, TBATS too narrow, GARCH) — document each known issue per model in the reference docs.
- D-02: Models with no native interval implementation (crate default `predict_with_intervals` returns point only) → NULL lower/upper bounds; docs point users to the conformal functions.
- D-03: Delete `calculate_confidence_intervals` entirely — no opt-in legacy heuristic. Laplace distributional intervals pass through unchanged. Any z that remains anywhere uses exact Φ⁻¹(1−α/2) (statrs), never a step table.
- D-04: Every former silent fallback becomes an SQL error with a clear message; `model_name` in the output always identifies the model (and selected spec/order) that actually ran.
- D-05: `'ETS'` without `ets_model` → run the crate AutoETS selection and report the chosen spec in model_name (e.g. `ETS(M,A,M)`); the ensemble builder uses the same rule (FCST-07: standalone == ensemble member).
- D-06: `AutoMFLES` / `AutoMSTL` remain accepted as documented, deprecated aliases; model_name reports `MFLES` / `MSTL`.
- D-07: MSTL / TBATS / HoltWinters / SeasonalES without a seasonal period → auto-detect the period (existing detection path); if none is detected → SQL error. No default 12 or 2. Unknown `kalman_model`, misspelled panel Croston variant, invalid UTF-8 model name, unparsable seasonal-period entries → errors. AutoETS crate failure → error (not HW/Holt/SES fallback).
- D-08: New ts_forecast_by parameter keys `arima_order` [p,d,q] and `seasonal_order` [P,D,Q] (seasonal period from `seasonal_period`); added to ValidateParamKeys and the FFI ForecastOptions additively (ABI-compatible, mirroring the GARCH/Kalman extension pattern).
- D-09: `'ARIMA'` without `arima_order` → SQL error suggesting `AutoARIMA`. With an order → real crate ARIMA fit (forecasts match R `Arima()` at that order within 1%).
- D-10: Exogenous path uses the crate's exog-aware models (`supports_exog` / `predict_with_exog_intervals`): AutoARIMA runs its order search with regressors (selected order in model_name); Optimized/Dynamic Theta stay themselves; ETS/GARCH/Kalman options honoured; a regressor constant in history gets a 0 effect and the forecast stays finite (skip non-finite OLS coefficients if a wrapper OLS path remains).
- D-11: Interior NULLs keep linear interpolation for fitting, but interpolated points are excluded from MSE and their residuals are NULL. Fitted/residuals/MSE come from the crate model (`fitted_values()` / `residuals()`); delete `calculate_fitted_values`.
- D-12: Leading/trailing NULLs are trimmed before fitting (forecast dates unchanged). All-NULL series → error. A valid NaN is treated as missing. Regressor NULLs handled via validity (FCST-08).
- D-13: Changed numerical outputs (narrower/NULL intervals, new errors) are listed in a CHANGELOG "Breaking changes" section; no legacy compatibility flag.

### Claude's Discretion

- Exact error message wording, internal refactoring of forecast.rs dispatch, test organisation (fixture-based value assertions per Phase 9 harness are mandatory).

### Deferred Ideas (OUT OF SCOPE)

- Calibration of crate-native intervals (SES/ETS/Theta/TBATS/MSTL/GARCH) → Phase 11 (upstream) + Phase 17 (R parity).
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| FCST-01 | Intervals from `predict_with_intervals`; delete `calculate_confidence_intervals`; no-native-interval models → NULL | §1 (per-model capability table), §2 (exact discard sites: forecast.rs:796-799, 1001-1004) |
| FCST-02 | Exact normal inverse CDF wherever a z-value is needed | §2 (crate already uses `quantile_normal`/statrs internally for every model listed in §1 — confirms only the deleted wrapper-level step table violates this) |
| FCST-03 | Fitted/residuals/MSE from `fitted_values()`/`residuals()`; delete `calculate_fitted_values`; MSE excludes interpolated points | §2 (exact discard sites: forecast.rs:805-826, 1006-1023), §6 (NULL/interpolation tracking) |
| FCST-04 | Real crate ARIMA fit (user order or default), never fixed-coefficient AR(1) | §3 (ARIMASpec/SARIMASpec, ARIMA::new/SARIMA::new), §9 (R `Arima(order=c(0,1,1), seasonal=c(0,1,1))` fixture already exists) |
| FCST-05 | Exog routes through crate exog support; AutoARIMA order search runs; Theta variants stay themselves; constant regressor → 0 effect, finite forecast | §3 (AutoARIMA exog trait methods), §10 (exog gap: MSTL now supports exog in 0.15.10 but wrapper doesn't route it) |
| FCST-06 | Silent fallbacks → errors; model_name always identifies what ran | §2 (A7 AutoETS catch_unwind fallback, A8 ETS hand-picked HW/Holt/SES, A9 default periods, A10 Kalman/panel-Croston silent fallback, A11 UTF-8 fallback, A12 unparsable periods) — all exact line refs given |
| FCST-07 | Ensemble members (`build_forecaster`) use same defaults as standalone; "ETS" == same model everywhere | §2 (forecast.rs:2609-2804 `build_forecaster`, `ModelType::ETS` arm hard-codes AAA/default() vs the standalone hand-picked HW/Holt/SES — neither matches D-05's AutoETS rule yet) |
| FCST-08 | All-NULL → error; valid NaN → missing; regressor NULLs via validity | §6 (`build_series`, `fill_nulls_interpolate` exact gaps) |
| FCST-09 (Phase 17, not this phase) | R-parity fixtures | §9 (fixtures already exist, ready for Phase 17 consumption) |
</phase_requirements>

## Summary

The bug is **not** "models lack native intervals/fitted/residuals" — in anofox-forecast 0.15.10, **every one of the 36 `ModelType` targets' underlying crate struct already overrides `predict_with_intervals`, `fitted_values`, and `residuals`** (§1). The bug is that `forecast.rs`'s outer wrapper **throws this away**: `extract_forecast()` (the shared helper most models already route through) calls `model.predict(horizon)` instead of `model.predict_with_intervals(horizon, level)`, and the top-level `forecast()`/`forecast_with_exog()` functions **unconditionally recompute** lower/upper via the hand-rolled `calculate_confidence_intervals` (step-table z, historical-SD-based) and fitted/residuals via the hand-rolled `calculate_fitted_values` (SES α=0.3 stand-in) — discarding whatever the per-model dispatch function (including `extract_forecast`) already produced. This is visible even for Laplace, which *already* calls `predict_with_intervals` correctly inside `forecast_laplace` (forecast.rs:1829-1831) — its real intervals are computed, then thrown away one call frame up.

The fix is therefore smaller in surface area than the audit's severity rating suggests, but touches every call site: (1) make `extract_forecast` and the handful of functions that bypass it (AutoARIMA, AutoETS, Laplace already do it right internally but still get overridden) return real `lower`/`upper`/`fitted`/`residuals` through to the final `ForecastOutput`; (2) delete the two "calculate_*" functions and the code that calls them at forecast.rs:796-799/805-826 and :1001-1004/1006-1023; (3) replace four genuinely-fabricated model paths (`Naive`/`SeasonalNaive`/`SMA`/`RandomWalkDrift` are currently 100% hand-rolled arithmetic with no crate model at all, and `ARIMA` is a fixed-coefficient AR(1) stand-in) with real crate model instantiation + `extract_forecast`; (4) fix the five silent-fallback sites (A7-A12) to error instead of silently substituting a different model; (5) add `arima_order`/`seasonal_order` to the FFI struct and C++ param validators, additively; (6) route the exog path through the crate's native `supports_exog`/`predict_with_exog_intervals` instead of the OLS+fake-ARIMA/Theta-on-residuals hack.

**Primary recommendation:** Do the "stop discarding real data" fix first (it fixes ~30 of the 36 models for free, including Laplace, GARCH, Kalman, AutoEnsemble, AutoARIMA, AutoETS — all of which already call the crate correctly and only lose their results to the wrapper's override), then do the four "no crate model at all yet" replacements (Naive family + ARIMA), then the five silent-fallback-to-error conversions, then the ARIMA order / exog plumbing.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Prediction intervals, fitted values, residuals, MSE | Rust core (`anofox-fcst-core::forecast`) | Rust crate (`anofox-forecast::models::*::Forecaster`) | Crate models already compute these correctly (§1); core's job is to *not discard them*, never to recompute them |
| z-value / quantile for a confidence level | Rust crate (`statrs` via `quantile_normal`, already used inside every model's `predict_with_intervals`) | — | No z-table may exist anywhere in `forecast.rs` after this phase; the crate's own `quantile_normal` is the only legitimate source |
| Model dispatch (string → `ModelType` → crate struct) | Rust core (`forecast.rs` match + `build_forecaster`) | FFI (`ModelType::from_str`) | Core owns which crate struct backs which `ModelType`; must be the *same* struct/config for standalone and ensemble-member construction (FCST-07) |
| Parameter validation (`arima_order`, `seasonal_order`, `kalman_model`, panel Croston variant, etc.) | C++ (`ValidateParamKeys` + per-function parsers) | Rust FFI (defensive re-validation at the boundary) | C++ rejects unknown keys before they reach Rust; Rust FFI must independently reject malformed *values* (unparsable orders, invalid UTF-8) rather than silently defaulting |
| NULL/NaN handling, leading/trailing trim | Rust FFI (`build_series`) + Rust core (`fill_nulls_interpolate`) | — | DuckDB validity bitmap → `Option<f64>` conversion and all missing-value policy must live in Rust, not C++ (established project rule) |
| Exogenous regressor plumbing | Rust core (`forecast_with_exog`) delegating to crate `TimeSeries::with_regressor` + `Forecaster::supports_exog`/`predict_with_exog_intervals` | FFI (`ExogenousData`/`ExogenousRegressor` marshalling, already exists) | The crate's own exog machinery (OLS pre-regression + residual model fit) replaces the wrapper's duplicate hand-rolled OLS+fake-model hack |

## Standard Stack

No new external dependencies are introduced by this phase. The phase consumes the already-bumped (Phase 9) `anofox-forecast = "0.15.10"` and `anofox-regression = "=0.5.15"` (`Cargo.toml` — `[VERIFIED: Cargo.toml:18]`, quote: `anofox-forecast = { version = "0.15.10", features = ["anomaly", "serde"] }`). No `npm view`/`pip`/`cargo search` verification is needed because no new package is added; the registry source path confirming 0.15.10 is actually installed is `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.10/` (present and read this session).

## Package Legitimacy Audit

**Not applicable** — this phase adds zero new external packages (Rust, Python, or npm). It only changes internal call patterns against the crate version Phase 9 already vetted and bumped. No `package-legitimacy check` run was needed; none is required for the planner to gate.

## §1. Per-Model Capability Table (crate 0.15.10)

Confirmed via `grep -rn "impl Forecaster for"` and `grep -rln "fn predict_with_intervals\|fn fitted_values\|fn residuals\|fn supports_exog\|fn predict_with_exog_intervals"` across every file in `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.10/src/` this session — i.e., confirmed that the named method is **overridden** (not using the trait default) inside the file that contains that model's `impl Forecaster for X` block. `[VERIFIED: anofox-forecast-0.15.10 source, file-level grep this session]` for every row. Deep line-by-line read (not just grep) was additionally done for: `traits.rs` (trait defaults), `baseline/naive.rs`, `baseline/sma.rs`, `baseline/random_walk.rs`, `baseline/seasonal_naive.rs`, `arima/model.rs` (ARIMA+SARIMA), `arima/auto_arima.rs`, `exponential/auto_ets.rs` (via forecast.rs call site), `garch.rs`, `kalman_forecaster.rs`, `ensemble/auto.rs`, `ensemble/model.rs`.

| ModelType (enum) | Crate struct instantiated today | native intervals? | native fitted/residuals? | `supports_exog`? | Current wrapper bug |
|---|---|---|---|---|---|
| Naive | **hand-rolled** (no crate model) — crate `Naive` exists and has all 3 | ✓ (crate, unused) | ✓ (crate, unused) | ✓ (crate, unused) | Doesn't call the crate model at all (forecast.rs:1173) |
| SeasonalNaive | **hand-rolled** — crate `SeasonalNaive` exists and has all 3 | ✓ (unused) | ✓ (unused) | — | Doesn't call the crate model at all (forecast.rs:1188) |
| SMA | **hand-rolled** — crate `SimpleMovingAverage` is the semantic match (`WindowAverage` used inconsistently in `build_forecaster`) | ✓ (unused) | ✓ (unused) | — | Doesn't call the crate model; `build_forecaster` uses a *different* crate type (`WindowAverage`) than the standalone semantics imply |
| RandomWalkDrift | **hand-rolled** — crate `RandomWalkWithDrift` exists and has all 3 | ✓ (unused) | ✓ (unused) | — | Doesn't call the crate model at all (forecast.rs:1228) |
| SES / SESOptimized | `SimpleExponentialSmoothing::new(0.3)` / `::auto()` via `extract_forecast` | ✓ | ✓ | — | `extract_forecast` calls `.predict()` not `.predict_with_intervals()` (forecast.rs:2390) |
| Holt | `HoltLinearTrend::auto()` via `extract_forecast` | ✓ | ✓ | — | same `extract_forecast` bug |
| HoltWinters | `HoltWintersModel::auto(p, Additive)` via `extract_forecast` | ✓ | ✓ | — | same; also default period-12 fallback (A9) |
| SeasonalES / SeasonalESOptimized | `SeasonalESModel::new/optimized(p)` via `extract_forecast` | ✓ | ✓ | — | same `extract_forecast` bug |
| SeasonalWindowAverage | `SeasonalWindowAverage::new(p, n_seasons)` via `extract_forecast` | ✓ | ✓ | — | same |
| ETS (no spec) | **hand-picked HW/Holt/SES** relabelled "ETS" (A8) | n/a (wrong model) | n/a | ✓ (real `ETS` struct, unused) | Not the crate `ETS`/`AutoETS` type at all |
| ETS (with spec) | `ETSModel::new(spec, p)` via `forecast_with_ets_spec` | ✓ (unused — calls `.predict()`) | ✓ (used, but residuals hard-coded `None`) | ✓ (unused) | forecast.rs:1512 calls `.predict()` not `.predict_with_intervals()`; residuals never read (:1530 `residuals: None`) |
| AutoETS | `AutoETS::with_config` | ✓ (unused — calls `.predict()`) | ✓ | — (not in supports_exog list) | forecast.rs:1736 `.predict()` not `.predict_with_intervals()`; crate panic/error → silent HW/Holt/SES fallback relabelled "AutoETS" (A7, forecast.rs:1778-1787) |
| Theta | `Theta::new()/seasonal(p)` via `extract_forecast` | ✓ | ✓ | ✓ | `extract_forecast` bug |
| OptimizedTheta | `OptimizedTheta::new()/seasonal(p)` via `extract_forecast` | ✓ | ✓ | ✓ | same |
| DynamicTheta / DynamicOptimizedTheta | `DynamicTheta::new/seasonal/optimized/seasonal_optimized` via `extract_forecast` | ✓ | ✓ | ✓ | same |
| AutoTheta | `AutoTheta::new()/seasonal(p)` | ✓ | ✓ | ✓ | routed via `forecast_with_model`, same overwrite pattern |
| ARIMA | **hand-rolled fixed-coefficient AR(1)** (A4, forecast.rs:1538-1578) | n/a | n/a (`None`) | n/a | Not the crate `ARIMA` type at all; `n<5` silently returns Naive still labelled "ARIMA" |
| AutoARIMA | `AutoARIMA::with_config` | ✓ (unused — calls `.predict()`) | ✓ | ✓ (true) | forecast.rs:1611 `.predict()` not `.predict_with_intervals()` |
| MFLES / AutoMFLES | `MFLES` via `forecast_mfles` | ✓ | ✓ | ✓ | overwrite pattern (not individually read this session — grep-confirmed) |
| MSTL / AutoMSTL | `MSTLForecaster` via `forecast_mstl_lib` | ✓ | ✓ | ✓ (new in 0.15.10 — not routed by wrapper's exog dispatch, see §10) | overwrite pattern; exog support unused |
| TBATS / AutoTBATS | `TBATS` / `AutoTBATS` | ✓ | ✓ | — | overwrite pattern; default period-12 fallback (A9) |
| CrostonClassic/Optimized/SBA | `Croston::new()/optimized()/sba()` via `extract_forecast` | ✓ | ✓ | — | `extract_forecast` bug |
| ADIDA | `ADIDA::new()` via `extract_forecast` | ✓ | ✓ | — | same |
| IMAPA | `IMAPA::new()` via `extract_forecast` | ✓ | ✓ | — | same |
| TSB | `TSB::new()` via `extract_forecast` | ✓ | ✓ | — | same |
| Laplace | `LaplaceForecaster::new().auto()/.auto_aid()/.skaters()` | ✓ (**already called correctly**, forecast.rs:1829-1831) | ✓ (crate has it, but wrapper hard-codes `fitted: None, residuals: None` at :1855-1856) | — | Real interval computed then discarded one frame up (A1); fitted/residuals never read even though available |
| GARCH | `GARCH::default()`-equivalent via `forecast_garch` | ✓ (real: conditional-variance-based, via `quantile_normal`) | ✓ | — | Wrapper force-empties lower/upper for GARCH (forecast.rs:797) **contradicting D-01**, which says GARCH's (miscalibrated-but-real) intervals should pass through |
| Kalman | `KalmanForecaster::local_level()/local_linear_trend()` via `extract_forecast` | ✓ (real, residual-SD based) | ✓ | — | Wrapper force-empties lower/upper for Kalman (forecast.rs:797) — stale; crate now has real intervals |
| AutoEnsemble | `AutoEnsemble::with_config` via `extract_forecast` | ✓ (real — delegates to inner `Ensemble::predict_with_intervals`, which pools member intervals) | ✓ | — | Wrapper force-empties lower/upper for AutoEnsemble (forecast.rs:797) — stale per-model doc comment says "deferred to Phase 6 EPI-01"; crate now implements it |

**Key finding for the planner:** D-02's "NULL for models with no native interval implementation" branch has **no currently-known applicable model** among the 36 — every crate struct backing a `ModelType` overrides `predict_with_intervals`. D-02 should still be implemented defensively (a future crate regression or an as-yet-unverified model should still degrade to NULL, not panic), but the plan should not expect many/any models to actually hit that branch today. Three models (GARCH, Kalman, AutoEnsemble) are *currently hard-coded* to NULL intervals in the wrapper (forecast.rs:797) despite having real crate-native intervals — per D-01 these three hard-codes must be **removed**, not kept.

## §2. Exact Discard / Fabrication Sites in `crates/anofox-fcst-core/src/forecast.rs` (minimal-change map)

All line numbers `[VERIFIED: crates/anofox-fcst-core/src/forecast.rs, read this session]`.

1. **`extract_forecast` (forecast.rs:2385-2417)** — the shared helper used by SES/SESOptimized/Holt/HoltWinters/SeasonalES(Optimized)/SeasonalWindowAverage/Theta family/Croston family/ADIDA/IMAPA/TSB/Kalman/AutoEnsemble. Line 2390: `let forecast = model.predict(horizon)` — **must become** `model.predict_with_intervals(horizon, level)`, which requires adding a `level: f64` parameter threaded from each caller's `options.confidence_level`. `fitted`/`residuals` at :2410-2411 are already correct (`model.fitted_values()`/`model.residuals()`) and just need to survive to the final struct (see next point).

2. **No-exog `forecast()` (forecast.rs:788-855)**:
   - :796-799 — `let (lower, upper) = match options.model { GARCH|Kalman|AutoEnsemble => (vec![],vec![]), _ => calculate_confidence_intervals(...) }` — delete the match; use `result.lower`/`result.upper` (the lower/upper already computed by whichever `forecast_*` function ran, once step 1 is fixed) for every model.
   - :805-826 — recomputes `fitted`/`residuals` via `calculate_fitted_values` (SES α=0.3 stand-in, except 3 hard-coded special cases for Naive/SeasonalNaive/SeasonalWindowAverage) — delete; use `result.fitted`/`result.residuals`.
   - :838-854 — the final `ForecastOutput` construction takes `result.point` but **not** `result.lower`/`result.upper`/`result.fitted`/`result.residuals` — this is the actual override; once steps above are deleted this becomes a straight passthrough.

3. **Exog `forecast_with_exog` (forecast.rs:890-1054)** — same pattern duplicated: :1001-1004 (interval override), :1006-1023 (fitted/residual override), :1043-1053 (final struct discards `result.lower/upper/fitted/residuals`). Same fix.

4. **`forecast_ets` (forecast.rs:1425-1484)** — no-spec branch (:1474-1483) hand-picks HW/Holt/SES by heuristic and relabels "ETS" (A8). Per D-05, replace with a call into the same AutoETS path used by `forecast_auto_ets`, relabeling model_name `ETS(<spec>)` instead of `AutoETS(<spec>)`.

5. **`forecast_with_ets_spec` (forecast.rs:1487-1536)** — :1512 `forecaster.predict(horizon)` → must become `predict_with_intervals`; :1530 `residuals: None` → must become `forecaster.residuals().map(|v| v.to_vec())`.

6. **`forecast_arima` (forecast.rs:1538-1578)** — entirely fake (A4): fixed `ar_coef = 0.5`, `n<5` → delegates to `forecast_naive` but keeps empty `model_name` (falls back to the string "ARIMA" at the outer level, masking that it's actually Naive). Must become real `ARIMA::new(p,d,q)` (or `SARIMA::new(...)` when `seasonal_order` given) fit via `extract_forecast`-equivalent, sourced from the new `arima_order`/`seasonal_order` options (§3). No-order case is an **error** (D-09), not a fallback to AR(1) or Naive.

7. **`forecast_auto_arima` (forecast.rs:1582-1668)** — :1611 `model.predict(horizon)` → `predict_with_intervals`; model-name construction (:1616-1640, using `model.selected_full_order()`/`selected_order()`/`model_scores()`) is already correct and should be preserved as-is.

8. **`forecast_auto_ets` (forecast.rs:1690-1788)** — :1736 `model.predict(horizon)` → `predict_with_intervals`; :1778-1787 `catch_unwind` fallback to `forecast_ets(...)` relabelled "AutoETS" on **any** library error or panic (A7) → per D-07 must become a propagated `ForecastError`, not a fallback. (Keeping `catch_unwind` itself is fine/necessary for panic safety; only the *silent-success-via-different-model* behavior must go.)

9. **`forecast_laplace` (forecast.rs:1794-1886)** — internally **already correct** (calls `predict_with_intervals(horizon, level.clamp(0.5,0.999))` at :1829-1831); only bug is :1855-1856 hard-coding `fitted: None, residuals: None` instead of reading `model.fitted_values()`/`model.residuals()`, and the one-level-up override described in point 2.

10. **`forecast_arima_with_exog` / `forecast_theta_with_exog` (forecast.rs:3259-3330)** — both are hand-rolled `fit_ols_regression` + fake-model-on-residuals + `apply_regression` (A5/A15), labelled "ARIMAX"/"ThetaX". Per D-10, replace with: build a `TimeSeries` carrying the regressors via `.with_regressor(name, values)` per column, `fit()` the real `AutoARIMA`/`OptimizedTheta`/`DynamicTheta` (which auto-detect regressors via `series.has_regressors()`), then call `predict_with_exog_intervals(horizon, future_regressors_map, level)`. Model name for AutoARIMA should report the selected order (same formatting as point 7).

11. **`build_forecaster` (forecast.rs:2609-2804)** — used only by `forecast_explicit_ensemble` (explicit-member ensembles). The `ModelType::ETS` arm (:2714-2722) builds `ETSModel::new(ETSSpec::aaa(), p)` or `ETSModel::default()` — **does not match** either the current standalone hand-picked behavior (A8) or the D-05 fixed behavior (AutoETS selection) — this is the FCST-07 gap: ensemble "ETS" member and standalone "ETS" model must resolve identically once D-05 lands (i.e., this arm likely needs to become `Box::new(AutoETS::with_config(...))`, matching whatever the fixed standalone `forecast_ets` does).

12. **`calculate_confidence_intervals` (forecast.rs:3414-3447)** and **`calculate_fitted_values` (forecast.rs:3449-3505)** — delete entirely per D-03/D-11. The z-table at :3426-3432 (`0.99→2.576, 0.95→1.96, 0.90→1.645, 0.80→1.28, _→1.0`) is the literal violation of FCST-02; every crate model's own `predict_with_intervals` already uses `crate::utils::stats::quantile_normal` (confirmed in `naive.rs:296`, `garch.rs:713`, `kalman_forecaster.rs:121` — exact statrs-backed inverse normal CDF), so FCST-02 is automatically satisfied once step 2/3 above route through the crate instead of this function.

## §3. ARIMA / AutoARIMA crate API (0.15.10)

`[VERIFIED: anofox-forecast-0.15.10/src/models/arima/model.rs:55-234, 2099-2179, read this session]`

```rust
// Non-seasonal
pub struct ARIMASpec { pub p: usize, pub d: usize, pub q: usize }
impl ARIMASpec { pub fn new(p: usize, d: usize, q: usize) -> Self }
pub struct ARIMA { /* ... */ }
impl ARIMA {
    pub fn new(p: usize, d: usize, q: usize) -> Self          // model.rs:217
    pub fn arima_111() -> Self                                 // ARIMA(1,1,1)
    pub fn ar(p: usize) -> Self                                 // ARIMA(p,0,0)
    pub fn ma(q: usize) -> Self                                 // ARIMA(0,0,q)
}
// impl Forecaster for ARIMA at model.rs:1810 — has predict_with_intervals,
// fitted_values, residuals, supports_exog, predict_with_exog, predict_with_exog_intervals

// Seasonal
pub struct SARIMASpec { pub p, d, q, cap_p, cap_d, cap_q, s: usize }
pub struct SARIMA { /* ... */ }
impl SARIMA {
    pub fn new(p: usize, d: usize, q: usize,
               cap_p: usize, cap_d: usize, cap_q: usize, s: usize) -> Self   // model.rs:2151
}
// impl Forecaster for SARIMA at model.rs:2997 — same trait surface as ARIMA
```

**Mapping for D-08/D-09:** `arima_order=[p,d,q]` + no `seasonal_order` (or `seasonal_order=[0,0,0]`) → `ARIMA::new(p,d,q)`. `arima_order=[p,d,q]` + `seasonal_order=[P,D,Q]` with `seasonal_period=s>1` → `SARIMA::new(p,d,q,P,D,Q,s)`. Both implement `Forecaster` and can be `Box<dyn Forecaster>`, so the C++/FFI/core plumbing can treat them uniformly via `extract_forecast`.

**R reference for FCST-04/D-09 ("matches R `Arima()` at that order within 1%"):** the Phase 9 fixture `validation/reference/fixtures/r_forecast/air_passengers.json` already contains model key `arima_airline` with `r_call: "forecast(Arima(air, order = c(0,1,1), seasonal = c(0,1,1)), h = 12, level = c(80, 95))"` and point/interval arrays `[VERIFIED: validation/reference/fixtures/r_forecast/air_passengers.json, read this session]`. This is the exact (p,d,q)(P,D,Q)[s] = (0,1,1)(0,1,1)[12] "airline model" fixture to assert against for a seasonal `SARIMA::new(0,1,1,0,1,1,12)` test — no new R generation is needed for this specific case, though further (p,d,q) combinations the plan wants covered would need new fixture entries.

**AutoARIMA exog API** `[VERIFIED: anofox-forecast-0.15.10/src/models/arima/auto_arima.rs:1038-1095, read this session]`:
```rust
fn supports_exog(&self) -> bool { true }   // auto_arima.rs:1038
fn predict_with_exog(&self, horizon, future_regressors: &HashMap<String, Vec<f64>>) -> Result<Forecast>
fn predict_with_exog_intervals(&self, horizon, future_regressors, level: f64) -> Result<Forecast>
```
Both delegate to whichever concrete model (`ARIMA` or `SARIMA`) the order search selected (`self.selected_model`). `fit()` on `AutoARIMA` will automatically run the order search *with* regressors once the `TimeSeries` passed to `fit()` has regressors attached (`series.has_regressors()` — confirmed in `core/time_series.rs:179,187,595,608`), matching D-10's "AutoARIMA runs its order search with regressors."

## §4. AutoETS spec reporting + ETS-without-spec fix (D-05)

`forecast_auto_ets` (forecast.rs:1741-1748) already extracts the selected spec via `model.selected_spec()` and formats it as `format!("AutoETS({:?},{:?},{:?})", spec.error, spec.trend, spec.seasonal)`. For D-05 ("ETS" without spec → run AutoETS, report `ETS(M,A,M)` not `AutoETS(...)`) the minimal change is: route `forecast_ets`'s no-spec branch into the same `AutoETS::with_config` + `.fit()` + `.predict_with_intervals()` call as `forecast_auto_ets`, but format the model name with the `ETS(...)` prefix instead of `AutoETS(...)`. The crate failure path (D-07: AutoETS crate failure → error) means this call must propagate `Err` rather than using the `catch_unwind`-and-fallback pattern currently in `forecast_auto_ets` — i.e., both the "ETS" and "AutoETS" ModelType arms need the fallback branch at forecast.rs:1778-1787 removed/changed to `Err(...)`.

## §5. FFI `ForecastOptions` additive extension pattern + C++ param validation

**Pattern precedent** `[VERIFIED: crates/anofox-fcst-ffi/src/types.rs:372-422, read this session]`: the repr(C) `ForecastOptions` struct has had fields appended at the *end* across versions (`garch_p`, `garch_q`, `kalman_model`, `ensemble_top_k`, `ensemble_method` were all Phase 3/4 additions, appended after the pre-existing fields, never inserted in the middle). cbindgen regenerates `src/include/anofox_fcst_ffi.h` from this struct on build (confirmed struct mirrored verbatim at `anofox_fcst_ffi.h:1036-1076` — same field order). For D-08, `arima_order` and `seasonal_order` must be appended **after** `ensemble_method` (the current last field) in **both** `ForecastOptions` (types.rs:373) and `ForecastOptionsExog` (types.rs:547) to stay ABI-additive. Given p/d/q are small non-negative integers, not strings, two plausible encodings:
- **Fixed-size int arrays** (`arima_order: [c_int; 3]`, `seasonal_order: [c_int; 3]`, sentineled by all-zero = "not set") — simplest, avoids a new string-parsing path.
- **String-encoded** (mirroring the existing `seasonal_periods_str: [c_char; 64]` pattern already used for the variable-length seasonal-periods list) — consistent with precedent but requires a new parse function and loses the "all zero = unset" simplicity since `(0,0,0)` is itself a valid ARIMA(0,0,0).

Either is additive and ABI-safe; the plan should pick one explicitly (not resolved by this research — flagged in Open Questions). Default (unset) must be distinguishable from `[0,0,0]` for D-09's "no `arima_order` → error" to work — a fixed-size array needs a separate `bool has_arima_order` flag or an out-of-range sentinel (e.g. all `-1`), while the string encoding naturally uses `""` for "not set" the same way `seasonal_periods_str` already does.

**C++ param validation sites that must accept the new keys** `[VERIFIED: file:line, read this session]`:
- `src/table_functions/ts_forecast_native.cpp:278` `ValidateParamKeys` — `valid_keys` unordered_set currently: `model, seasonal_period, seasonal_periods, confidence_level, window, model_pool, laplace_variant, laplace_seasonal_batch_init, garch_p, garch_q, kalman_model, top_k, combination_method`. Must add `arima_order, seasonal_order`.
- `src/table_functions/ts_cv_forecast_native.cpp:254` — separate `valid_keys` set, same pattern, same addition needed if CV forecasting exposes ARIMA.
- `src/table_functions/ts_forecast_panel_native.cpp:162` — separate `std::set` (note: `std::set` not `unordered_set` here), panel path — likely N/A for `arima_order` (panel models are Global* only) but should be checked for consistency.
- `src/scalar_functions/ts_forecast_scalar.cpp:133` — separate `valid_keys` set, same pattern, same addition needed (the scalar single-point-forecast wrapper).
- `ts_forecast_agg.cpp` (aggregate path) — not found with a `valid_keys = {` literal by this grep; needs the plan to locate its param-acceptance mechanism directly (may delegate to the table-function bind data rather than having its own literal set — verify at plan time).

**Existing int-list → string parsing precedent** `[VERIFIED: forecast.rs's seasonal_periods_str handling + ffi/lib.rs:3370-3381, read this session]`: `ParseStringFromParams(params, key, default)` in C++ calls `.ToString()` on a DuckDB `LIST` value, which stringifies as `"[24, 168]"`-style text; `parse_seasonal_periods_str` in `anofox-fcst-ffi/src/lib.rs:3370` then does `.trim_start_matches('[').trim_end_matches(']').split(',').filter_map(|p| p.trim().parse::<usize>().ok())`. **This exact pattern is the string-encoding option above** — reusing it for `arima_order`/`seasonal_order` is the lower-novelty path, but note the current implementation's `.filter_map(...ok())` **silently drops unparsable entries** (A12) — the new parser for `arima_order`/`seasonal_order` must not repeat this; it should return `Result<[usize;3], String>` and error on any non-numeric or wrong-length entry, and the existing `parse_seasonal_periods_str` itself should also be fixed to error per D-07.

## §6. NULL/NaN handling gaps (FCST-08, D-11, D-12)

`[VERIFIED: crates/anofox-fcst-ffi/src/lib.rs:104-129, crates/anofox-fcst-core/src/imputation.rs:62-110, read this session]`

- **`build_series` (ffi/lib.rs:104-129)**: converts the DuckDB validity bitmap into `Vec<Option<f64>>`. It does **not** special-case `f64::NAN` — a *valid* NaN (validity bit set, but value is NaN) passes through as `Some(f64::NAN)`, not `None`. D-12 ("a valid NaN is treated as missing") requires an added `.map(|v| v.filter(|x| !x.is_nan()))`-style pass, either inside `build_series` or immediately after, before any model fit.
- **`fill_nulls_interpolate` (core/imputation.rs:62-110)**: on an all-`None` input, `first_idx`/`last_idx` are both `None` and the function returns a vector of all `f64::NAN` **silently** (imputation.rs:73-75) — no error. D-12 ("all-NULL series → error") requires the caller (`forecast`/`forecast_with_exog` in forecast.rs, right after calling `fill_nulls_interpolate`) to check for this condition and return `Err` before any model sees the data; it does NOT belong inside `fill_nulls_interpolate` itself (a general-purpose utility also used elsewhere, e.g. in `ts_fill_gaps_native`/`ts_fill_nulls_mean_by`, where all-NULL may be legitimate).
- **Leading/trailing NULL behavior**: `fill_nulls_interpolate` currently *extends* (flat-fills) leading/trailing NULLs with the first/last known value (imputation.rs:80-92) rather than *trimming* them. D-12 ("leading/trailing NULLs are trimmed before fitting; forecast dates unchanged") is a **behavior change**, not a bug fix to this function — the wrapper needs to compute `first_idx`/`last_idx` itself (same logic `fill_nulls_interpolate` already computes internally) and slice `clean_values` (and the corresponding date/validity arrays, at the C++/FFI layer where dates are still available) to `[first_idx..=last_idx]` *before* calling `fill_nulls_interpolate` on the interior-only slice, then only interior NULLs get interpolated. The forecast horizon's dates are computed from the *original* series' date column, so trimming leading/trailing history must not shift the forecast-start date — this needs the date/horizon computation (currently in the C++ table function, which already tracks `dates: vector<int64_t>` per `ForecastGroupData`) to stay anchored to the original last-observed date, not the trimmed series' last index, unless they're the same (which they are if only leading NULLs were trimmed — trailing-NULL trimming is the case to watch: if the *last* N rows are NULL, "forecast dates unchanged" implies the horizon still starts right after the original last row's date, even though the model fits on data ending earlier).
- **Interpolated-point tracking for FCST-03/D-11** ("interpolated points excluded from MSE, their residuals NULL"): neither `build_series` nor `fill_nulls_interpolate` currently returns *which* indices were originally `None` vs `Some` — this mask must be computed once (e.g. `let was_null: Vec<bool> = values.iter().map(|v| v.is_none()).collect()` before calling `fill_nulls_interpolate`) and threaded alongside `clean_values` so that, after fetching `model.residuals()` (now real, from crate `fitted_values()`/length-matched `residuals()`), the wrapper can zero out (`None`/NaN) any residual at a `was_null[i] == true` position before computing MSE over the remainder. This mask does not exist anywhere in the current code and is new plumbing the plan must add.
- **Regressor NULLs (FCST-08's "handled via validity")**: the `ExogenousRegressor` FFI struct (`types.rs:514-524`) carries `values: *const c_double` + `n_values` but **no validity pointer** — regressor NULLs are not currently representable at the FFI boundary at all. Adding this is itself an additive struct change (new field) with the same ABI-append discipline as §5; the plan should treat this as part of the same `arima_order`/`seasonal_order` additive-struct-change task, not a separate ABI break.

## §7. Existing test churn — size and locations (sizing input for the plan, not a line-item task list)

27 `test/sql/*.test` files reference `ts_forecast` (`[VERIFIED: grep -rln "ts_forecast" test/sql/*.test, this session]`): `ts_cv_forecast.test, ts_forecast_agg.test, ts_forecast_auto.test, ts_forecast_basic_models.test, ts_forecast_by.test, ts_forecast_error_isolation.test, ts_forecast_ets_model.test, ts_forecast_exog.test, ts_forecast_exp_smoothing.test, ts_forecast_inspect_explain.test, ts_forecast_intermittent.test, ts_forecast_laplace.test, ts_forecast_mfles_stability.test, ts_forecast_multi_seasonal.test, ts_forecast_param_grid.test, ts_forecast_params.test, ts_forecast_statespace.test, ts_forecast.test, ts_forecast_theta.test, ts_conformal_coverage.test, extension_comparison.test, ts_native_model_names.test, ts_native_param_validation.test, ts_parallel_correctness.test, ts_table_macro_aliases.test, ts_varchar_edge_cases.test, ts_integer_frequency.test, ts_model_distinctness.test`. 12 of these additionally assert `lower_90`/`upper_90`/`mse` values directly, which are **guaranteed to change** once the fabricated-interval/fitted-value code paths are removed (per the audit's own evidence: "MSE 2090.28 identical across SES/Holt/AutoARIMA/AutoETS/Theta/HW/MSTL/Croston" — any test asserting that exact MSE value, or the ±234 interval half-width, is pinned to the bug and will need its expected values regenerated against the fixed behavior). The plan should budget a dedicated pass to regenerate these 12 files' expected values against the corrected extension build (per the project's "verify every doc/test example against the built extension" rule), not assume they can be fixed mechanically.

The Rust-side `forecast_snapshot.rs` fixture (Phase 9, `validation/reference/fixtures/forecast_snapshot/point_forecasts.json`) only pins **point forecasts**, not intervals/fitted/residuals — it is not directly affected by this phase's changes (point forecasts for most models are unchanged by this fix; only ARIMA's point forecast will change, since it goes from fake-AR(1) to real ARIMA, which is an expected/documented change this phase must add to that fixture's `allowed_to_change` list per the Phase 9-established pattern, or the snapshot test will correctly fail and need updating).

## §8. Seasonal period auto-detection and the D-07 "error if none detected" requirement

`[VERIFIED: crates/anofox-fcst-core/src/seasonality.rs:323-335, forecast.rs:602-612, read this session]`

`detect_seasonality(values, max_period) -> Result<Vec<i32>>` returns `Err(InsufficientData)` if `values.len() < 4`, and `Ok(vec![])` (empty, not an error) if `max_lag < 2` or no periods are found. The call site in `forecast()` (forecast.rs:602-607):
```rust
let period = if options.auto_detect_seasonality && options.seasonal_period == 0 {
    detect_seasonality(&clean_values, None).ok()
        .and_then(|p| p.first().cloned())
        .unwrap_or(1) as usize
} else { ... };
```
collapses **both** the `Err` case and the "detected nothing" (`Ok(vec![])`) case to `period = 1` via `.ok()` + `.unwrap_or(1)`. Then, individually, the `MFLES|AutoMFLES`, `MSTL|AutoMSTL`, `TBATS`, `AutoTBATS` match arms (forecast.rs:689-735) each have their own `else { vec![12] }`/`else { &vec![12] }` fallback when `period <= 1`, and `HoltWinters`/`SeasonalES(Optimized)` use `.max(2)` (forecast.rs:1282, 1359, 1373) to force a minimum period of 2. Per D-07, for these four specific model families *only* (not every model — `Theta`/`ETS` etc. legitimately treat `period<=1` as "non-seasonal" and that's correct), the plan needs a distinct code path: when `auto_detect_seasonality` is true and `seasonal_period==0` and detection yields nothing (empty or error) **and** the requested model is MSTL/AutoMSTL/TBATS/AutoTBATS/HoltWinters/SeasonalES/SeasonalESOptimized, return `Err(InvalidInput("no seasonal period could be auto-detected for model X; specify seasonal_period explicitly"))` instead of silently defaulting to 12 or 2.

## §9. R reference fixtures available today (Phase 9 harness)

`[VERIFIED: validation/reference/fixtures/r_forecast/air_passengers.json, read + parsed this session]`

The fixture's `tolerance` block states explicitly: `model_point_rel: 0.01` and `interval_rel: 0.05` are "parity targets for later phases (Phase 10 FCST-01, Phase 17 FCST-09), **not asserted in Phase 9**" — i.e., Phase 9 deliberately deferred the actual assertion to this phase. Models with ready-made R reference values (`r_call`, `point`, `lower_80/upper_80/lower_95/upper_95`): `naive, snaive, rwf_drift, ses, holt, hw_additive, hw_multiplicative, ets_auto, ets_MAM, ets_AAA, auto_arima, arima_airline, thetaf`. `arima_airline`'s `r_call` is `forecast(Arima(air, order = c(0,1,1), seasonal = c(0,1,1)), h = 12, level = c(80, 95))` — the exact (p,d,q)(P,D,Q)[s] combination to use for a D-09 ARIMA-with-explicit-order acceptance test (point forecasts: `[447.05, 421.88, 453.53, ...]`). `ets_MAM`/`ets_AAA` give fixed-spec ETS references (useful for the `forecast_with_ets_spec` fix in §2 item 5). The companion `simulated_series.json` exists for non-AirPassengers coverage but was not opened this session — the plan should read it directly if it needs non-seasonal/short-series cases.

**Gap:** no R reference currently exists for a non-seasonal `arima_order` test (e.g. plain `ARIMA(1,1,1)` without a seasonal component) — only the seasonal airline-model case is in the fixture. If the plan wants a non-seasonal ARIMA acceptance test, a new R-generated fixture entry is needed (the existing `validation/reference/R/` generator scripts — not read this session, see Phase 9 summaries — are the established pattern to extend, mirroring how `arima_airline` was added).

## §10. Exog dispatch gap found this session (new finding, not in the original audit)

The crate's `MSTLForecaster` overrides `supports_exog` (confirmed via grep this session: `src/models/mstl_forecaster.rs` appears in the `supports_exog` override file list) — this is new/changed behavior versus what the original STAT-AUDIT-2026-10-08.md's A5/A6 findings described (which only named AutoARIMA/ARIMA/OptimizedTheta/DynamicTheta/MFLES as exog-capable). The wrapper's `supports_exog` match in `forecast_with_exog` (forecast.rs:926-934) does **not** include `ModelType::MSTL`/`AutoMSTL`, so even after the D-10 fix routes ARIMA/Theta/MFLES through the crate's native exog path, an MSTL-with-exog request will still silently fall through to the plain (no-exog) branch at forecast.rs:962-992 unless the plan explicitly adds it. Flagging this as a scope question for the planner/discuss-phase: D-10's decision text only enumerates "AutoARIMA ... Theta ... ETS/GARCH/Kalman" — MSTL exog is not mentioned and may be deliberately out of scope for this phase, but it is a real crate capability being left on the table if so.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|--------------|-----|
| Prediction interval half-width for a confidence level | A z-value lookup table, a `point ± z·SD(history)·√h` formula | `model.predict_with_intervals(horizon, level)` — every model in §1 already implements this correctly, model-appropriately (e.g. GARCH uses conditional variance, not historical SD) | The hand-rolled version is provably wrong (audit A1/A2) and a lookup table can never match model-specific interval theory (ETS analytic variance, ARIMA forecast-error variance, Laplace mixture quantiles, etc.) |
| In-sample fitted values / residuals for any model | Any generic formula (even a correct-looking EWMA) applied uniformly across all models | `model.fitted_values()` / `model.residuals()` | Every model's fitted/residual definition differs by construction (AR warmup NaNs, seasonal decomposition residuals, Theta's SES-on-transformed-series residuals, etc.) — a shared formula cannot be right for more than one model family at a time, which is exactly what `calculate_fitted_values`'s SES(α=0.3) catch-all proves |
| ARIMA/SARIMA fitting of any kind | A hand-rolled AR(1)-on-differenced-series approximation | `anofox_forecast::models::arima::{ARIMA, SARIMA}` via `ARIMASpec`/`SARIMASpec` | Real ARIMA estimation (CSS/MLE, AIC/BIC, differencing, MA component) is already implemented and crate-tested; a "simplified" stand-in cannot be a drop-in replacement under any model name |
| Exogenous-regressor forecasting ("OLS on y, forecast residuals with model, add back exog effect") | A second, wrapper-level OLS + residual-model combination | `TimeSeries::with_regressor` + model's own `supports_exog`/`predict_with_exog_intervals` | The crate's exog machinery already does OLS pre-regression correctly (per the Phase 9 DEP-01 fix) and integrates it with each model's own uncertainty propagation; a parallel wrapper-level implementation duplicates and diverges from it (exactly what A5/A15 found) |

**Key insight:** almost nothing in this domain needs new statistical code — the fix is overwhelmingly "stop recomputing what the crate already computed correctly" rather than "implement new math." The two genuine new-implementation needs are (1) building `ARIMA`/`SARIMA` from user-supplied orders (crate API already exists, just unused) and (2) the FFI/C++ plumbing for the two new parameter keys.

## Common Pitfalls

### Pitfall 1: Fixing `extract_forecast` alone is not sufient — the override happens one frame up
**What goes wrong:** Changing `extract_forecast` to call `predict_with_intervals` looks like the fix, but `forecast()`/`forecast_with_exog()` still discard `result.lower`/`result.upper`/`result.fitted`/`result.residuals` at the final `ForecastOutput` construction and recompute via `calculate_confidence_intervals`/`calculate_fitted_values`.
**Why it happens:** The override logic lives in the *caller* of every per-model forecast function, not inside any one of them — it's easy to fix the data source and miss that the data never reaches the output struct.
**How to avoid:** Verify end-to-end with a single model (e.g. run `ts_forecast_by` with `method='Naive'` after the fix and confirm the interval width matches R's `naive()` ±66-ish, not the old ±234) before trusting any individual function-level diff.
**Warning signs:** `cargo test` passes (because unit tests on individual `forecast_*` functions may not go through the full `forecast()` wrapper) but SQL-level interval widths are unchanged.

### Pitfall 2: Trimming leading/trailing NULLs can shift the forecast start date if not anchored correctly
**What goes wrong:** D-12 requires trimming leading/trailing NULLs for *fitting* while keeping forecast dates unchanged. If the date-stepping logic (in the C++ table function, which currently computes forecast dates from the *last row's* date) is naively recomputed from the trimmed series' last index instead of the original series' last date, trailing-NULL series will forecast from the wrong anchor date.
**Why it happens:** Date and value arrays are currently processed together; trimming one without explicitly preserving the other's original anchor is an easy one-line mistake.
**How to avoid:** Keep the original last-observed date separate from the trimmed-series index used for model fitting; only use the trimmed series for the `Vec<f64>` passed into `TimeSeries`/`ARIMA`/etc., never for date arithmetic.

### Pitfall 3: `ModelType::ETS`'s ensemble-builder arm and the fixed standalone path must be changed together
**What goes wrong:** Fixing `forecast_ets`'s no-spec branch (D-05, item 4 in §2) without also fixing `build_forecaster`'s `ModelType::ETS` arm (item 11 in §2) leaves FCST-07 violated — ensemble "ETS" members would still use `ETSModel::new(ETSSpec::aaa(), p)`/`::default()` while standalone "ETS" now runs AutoETS selection.
**Why it happens:** The two code paths are ~1200 lines apart in the same file and don't call each other.
**How to avoid:** Treat D-05 and FCST-07 as one task touching both `forecast_ets` and `build_forecaster`, verified together (e.g. a test asserting `ts_forecast_by(method='ETS')` and an ensemble containing "ETS" pick the same AICc-selected spec on the same series).

### Pitfall 4: Deleting `parse_seasonal_periods_str`'s silent-drop behavior changes semantics for already-passing inputs
**What goes wrong:** Making `parse_seasonal_periods_str` error on any unparsable entry (A12 fix) could newly reject SQL that previously "worked" (silently using a subset of the periods). Any existing test that happens to pass a malformed `seasonal_periods` string and currently gets away with it will start failing loudly.
**Why it happens:** Error-on-malformed-input is a behavior change by definition — some currently-passing-but-wrong queries will now correctly error.
**How to avoid:** Grep `test/sql/*.test` and `examples/` for `seasonal_periods` usage before making the change, to catch any test fixture that was unintentionally relying on the silent-drop behavior.

## Code Examples

### Minimal shape of the `extract_forecast` fix (illustrative — not verbatim crate/repo code; function signature and body are this session's proposed diff, not quoted from any source file)
```rust
// BEFORE (forecast.rs:2385-2417, current — quoted verbatim from read this session)
fn extract_forecast(
    model: &dyn Forecaster,
    horizon: usize,
    name_override: &str,
) -> Result<ForecastOutput> {
    let forecast = model.predict(horizon).map_err(|e| { /* ... */ })?;
    // ... uses forecast.lower()/forecast.upper(), which are always empty
    // because predict() (not predict_with_intervals()) never populates them
}

// AFTER (proposed — not yet in the repo; add a `level: f64` parameter and call
// predict_with_intervals; every call site of extract_forecast must be updated
// to pass options.confidence_level through)
fn extract_forecast(
    model: &dyn Forecaster,
    horizon: usize,
    level: f64,
    name_override: &str,
) -> Result<ForecastOutput> {
    let forecast = model.predict_with_intervals(horizon, level).map_err(|e| { /* ... */ })?;
    // forecast.lower()/forecast.upper() are now populated by the model's own
    // interval logic (crate-internal, model-appropriate — see §1)
    // fitted/residuals lines (2410-2411) are already correct, unchanged
}
```

### Crate-native interval call already correct — Laplace (verbatim, quoted for provenance)
```rust
// Source: crates/anofox-fcst-core/src/forecast.rs:1826-1831 (read this session)
// predict_with_intervals uses a symmetric two-sided level; clamp to
// [0.5, 0.999] to stay within the LaplaceForecaster's supported range.
let level = confidence_level.clamp(0.5, 0.999);
let forecast = model
    .predict_with_intervals(horizon, level)
    .map_err(|e| ForecastError::ComputationError(format!("Laplace predict failed: {e}")))?;
```
This proves the pattern already works correctly for at least one model — the fix for the other 35 is making the wrapper stop discarding this kind of result, not inventing a new calling convention.

## State of the Art

| Old Approach (current forecast.rs) | Crate-native Approach (0.15.10, what D-01..D-13 move to) | When Changed | Impact |
|---|---|---|---|
| `calculate_confidence_intervals`: point ± z(step-table)·SD(raw series)·√h | `model.predict_with_intervals(h, level)` per model, using each model's own uncertainty theory | This phase (Phase 10) | Interval widths change for every model that currently goes through the fabricated path — e.g. Naive 95% h=1 half-width goes from ~234 to ~66 on AirPassengers (R parity target) |
| `calculate_fitted_values`: SES(α=0.3) for every model except 3 hard-coded exceptions | `model.fitted_values()`/`model.residuals()` per model | This phase | MSE values that were identical across SES/Holt/AutoARIMA/AutoETS/Theta/HW/MSTL/Croston (2090.28) will diverge per model, correctly |
| `forecast_arima`: fixed-coefficient AR(1) on differenced series | `ARIMA::new(p,d,q)` / `SARIMA::new(...)` from user-supplied `arima_order`/`seasonal_order` | This phase | "ARIMA" in SQL output will, for the first time, be an actual ARIMA fit instead of a different, simpler model under a misleading name |
| `forecast_arima_with_exog`/`forecast_theta_with_exog`: hand-rolled OLS + fake-model-on-residuals | Crate `TimeSeries::with_regressor` + model's native `supports_exog`/`predict_with_exog_intervals` | This phase | AutoARIMA's order search will actually run with regressors present (currently it never does under exog) |

**Deprecated/outdated:** `calculate_confidence_intervals` and `calculate_fitted_values` (forecast.rs:3414-3505) are deleted entirely per D-03/D-11 — no replacement function, no opt-in legacy flag (D-13).

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `arima_order`/`seasonal_order` should be encoded as either fixed-size int arrays or a string-list, mirroring `seasonal_periods_str` — the specific encoding choice is left to the plan | §5 | Low — both are additive-ABI-safe; only affects how much new C-struct/parsing code is written, not correctness |
| A2 | `ModelType::SMA` should map to crate `SimpleMovingAverage` (not `WindowAverage`, which `build_forecaster` currently uses) for consistency with the hand-rolled semantics ("mean of last window observations") | §1 (SMA row) | Medium — if the plan picks `WindowAverage` instead, ensemble-member "SMA" and standalone "SMA" will still diverge (FCST-07), just with a different crate type than before; the plan should verify `WindowAverage` vs `SimpleMovingAverage` semantics match before deciding, not assume this research's pick is final |
| A3 | `build_forecaster`'s `ModelType::ETS` arm needs to change to AutoETS-selection once `forecast_ets`'s standalone path does (FCST-07) | §2 item 11, Pitfall 3 | Medium — if the plan fixes only the standalone path, FCST-07 (ensemble member == standalone) remains violated for ETS specifically; this was not explicitly called out in CONTEXT.md's decisions beyond "the ensemble builder uses the same rule" (D-05), so the plan must treat it as in-scope |
| A4 | MSTL/AutoMSTL exog support (crate-native, confirmed via grep this session) is out of scope for this phase since D-10's decision text doesn't mention it | §10 | Low — if in scope, this is a small addition (one more match arm in `forecast_with_exog`'s `supports_exog` check); if deliberately deferred, no risk, just a missed capability for a future phase |

**If this table is empty:** N/A — see above.

## Open Questions

1. **How should `arima_order`/`seasonal_order` "not set" be distinguished from `[0,0,0]`/`[0,0,0]` at the FFI boundary?**
   - What we know: Fixed-size int arrays are the simplest encoding but need an explicit "unset" sentinel since `ARIMA(0,0,0)` is a (degenerate but real) valid order; the string-encoding precedent (`seasonal_periods_str`) naturally uses `""` for unset.
   - What's unclear: Whether the plan wants the simplicity of int arrays + a `bool has_arima_order` flag, or consistency with the existing string-list pattern.
   - Recommendation: Plan should decide this explicitly as a task-0 design decision before writing the FFI struct change, since it determines both the C++ parsing code and the Rust-side parsing code shape.

2. **Does GARCH's `predict_with_intervals` (conditional-variance-based bounds around a conditional-std-dev point forecast) produce a *semantically meaningful* interval for the extension's documented GARCH output, or just a mechanically-valid one?**
   - What we know: `GARCH::predict_with_intervals` is a real override (garch.rs:703+, uses `quantile_normal` and `forecast_variance`) and D-01 explicitly lists GARCH as a model whose (possibly miscalibrated) intervals should pass through.
   - What's unclear: Since the GARCH model's point forecast is documented as "the conditional standard deviation, NOT a level forecast" (forecast.rs:150-152 doc comment), an interval *around that std-dev estimate* is a different quantity than "a prediction interval for GARCH-implied volatility" — whether this is useful/correct to surface to SQL users or needs its own doc caveat is a product question, not purely technical.
   - Recommendation: Surface the real interval per D-01 (no special-casing), but the docs update (FCST-06/DOCS-01 territory, likely still owned by this phase per the CONTEXT.md phase boundary) should explain what the GARCH interval actually bounds.

3. **Which specific test files among the 27 identified in §7 assert values that will change vs. values that happen to already be correct (and thus need no update)?**
   - What we know: 12 files assert `lower_90`/`upper_90`/`mse` directly; the rest reference `ts_forecast` without necessarily asserting changed quantities (e.g. may only assert row counts, model names, or point forecasts for already-correct models).
   - What's unclear: Exact per-file diff without running the fixed extension against each file.
   - Recommendation: Plan should budget a full `make test_release` pass after implementation and treat every red test as expected-and-to-be-updated rather than a regression, per the project's established "verify against the built extension" rule — not attempt to pre-enumerate which of the 27 will fail from static reading alone.

## Validation Architecture

> Included per explicit phase-researcher instruction, despite `.planning/config.json`'s `workflow.nyquist_validation: false` — the orchestrator's additional_context for this phase explicitly requested this section.

### Test Framework

| Property | Value |
|----------|-------|
| Framework (Rust) | `cargo test` (workspace), `#[cfg(test)]` inline modules + `crates/anofox-fcst-core/tests/*.rs` integration tests |
| Framework (SQL) | DuckDB sqllogictest (`test/sql/*.test`), run via `make test_release` |
| Config file | `Cargo.toml` (workspace), no separate test-framework config; sqllogictest files are self-contained |
| Quick run command | `cargo test -p anofox-fcst-core --lib forecast::` (scoped to forecast.rs's inline tests) |
| Full suite command | `cargo test --workspace --all-features && make test_release` |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| FCST-01 | Naive 95% h=1 interval half-width ≈ R `naive()` ±66 on AirPassengers (not ±234) | integration | `cargo test -p anofox-fcst-core --test <new>` against `validation/reference/fixtures/r_forecast/air_passengers.json` model `naive` | ❌ Wave 0 — new test needed |
| FCST-01 | Laplace intervals equal what `forecast_laplace`'s internal `predict_with_intervals` call already produces (regression-proof that the one-level-up override is gone) | unit | `cargo test -p anofox-fcst-core --lib forecast::tests::laplace_intervals_survive_wrapper` | ❌ Wave 0 |
| FCST-02 | No z-value step table remains anywhere in forecast.rs (grep-based negative-space test) | other | `! grep -n "0.85\|1.645\|2\.576" crates/anofox-fcst-core/src/forecast.rs` (or equivalent grep-based CI check) | ❌ Wave 0 |
| FCST-03 | MSE differs across SES/Holt/AutoARIMA/AutoETS/Theta/HW/MSTL/Croston on the same series (was identical 2090.28) | integration | new test asserting pairwise MSE inequality | ❌ Wave 0 |
| FCST-04 | ARIMA(0,1,1)(0,1,1)[12] point forecasts within 1% of R `Arima()` on AirPassengers | integration | against `validation/reference/fixtures/r_forecast/air_passengers.json` model `arima_airline` | ❌ Wave 0 — fixture exists, test doesn't |
| FCST-05 | AutoARIMA-with-exog actually varies order search result vs. no-exog on the same series+regressor | integration | new test against `exog_typical` fixture (`validation/reference/fixtures/multiscale_ols/`) | ❌ Wave 0 |
| FCST-06 | `ETS` without spec, `AutoETS` on a constant series, misspelled `kalman_model`/panel Croston variant, invalid-UTF8 model name, unparsable `seasonal_periods` → SQL error (not silent fallback) | integration (SQL) | `test/sql/correctness/forecast_silent_fallbacks.test` (new) | ❌ Wave 0 |
| FCST-07 | Standalone `ETS` and an ensemble member named `"ETS"` on the same series select the same AutoETS spec | unit | new test comparing `forecast_ets` output's model_name to a `build_forecaster(ModelType::ETS, ...)`-based ensemble member's selected spec | ❌ Wave 0 |
| FCST-08 | All-NULL series → error; valid NaN → treated as missing; leading/trailing NULL trimmed, forecast date anchored to original last date | unit + integration | new tests in `crates/anofox-fcst-ffi` (build_series) and `anofox-fcst-core` (fill_nulls_interpolate caller) | ❌ Wave 0 |

### Sampling Rate

- **Per task commit:** `cargo test -p anofox-fcst-core --lib forecast::` (fast, scoped)
- **Per wave merge:** `cargo test --workspace --all-features`
- **Phase gate:** `make test_release` full sqllogictest suite green, plus the 12 interval/MSE-asserting `test/sql/*.test` files explicitly regenerated and reviewed (not just "passing" — their expected values are *supposed* to change) before `/gsd-verify-work`.

### Wave 0 Gaps

- [ ] A new `crates/anofox-fcst-core/tests/forecast_intervals.rs` (or similar) asserting FCST-01/02/03 against the Phase 9 R fixture — does not exist yet.
- [ ] A new `test/sql/correctness/forecast_silent_fallbacks.test` for FCST-06 — does not exist yet (the `test/sql/correctness/` directory itself may not exist yet either; TEST-03 in REQUIREMENTS.md assigns this directory's creation to Phase 18, but this phase's own FCST-06 error-message tests can live in an existing `test/sql/*.test` file if the plan prefers not to pre-empt Phase 18's directory structure — flag as a sequencing question for the plan, not resolved here).
- [ ] A new R-generated fixture entry for a non-seasonal `ARIMA(p,d,q)` case (only the seasonal airline model exists today — see §9 Gap) if the plan wants non-seasonal ARIMA order coverage.
- [ ] `forecast_snapshot.rs`'s `allowed_to_change` list needs the ARIMA model entries added before this phase's bump, mirroring the Phase 9 MFLES/AutoMFLES/Laplace pattern (`crates/anofox-fcst-core/tests/forecast_snapshot.rs`, `validation/reference/fixtures/forecast_snapshot/point_forecasts.json` — exists, needs updating, not creating).

## Security Domain

> Included per `security_enforcement: true` (ASVS level 1) in `.planning/config.json`.

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | No | No auth boundary in this phase — internal numerical library code, DuckDB extension has no user-auth concept |
| V3 Session Management | No | N/A |
| V4 Access Control | No | N/A |
| V5 Input Validation | Yes | `ValidateParamKeys` (C++, rejects unknown SQL param keys) + per-field parsers must reject malformed values (e.g. `arima_order=[1,-1,0]` — negative orders, `confidence_level` out of `(0,1)` — already validated at `ts_forecast_native.cpp` for `confidence_level`) rather than silently defaulting or panicking; the FFI boundary (`catch_unwind` already used throughout `anofox-fcst-ffi`) must keep converting any Rust panic to a C++ exception, never a crash, for malformed `arima_order`/`seasonal_order` input |
| V6 Cryptography | No | N/A — no cryptographic material in this phase |

### Known Threat Patterns for this stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Malformed `arima_order`/`seasonal_order` string/array causing a Rust panic that propagates as an uncaught exception across the FFI boundary (crashing the DuckDB process rather than returning a SQL error) | Denial of Service | Keep the existing `catch_unwind(AssertUnwindSafe(...))` pattern (already used throughout `anofox-fcst-ffi/src/lib.rs`, e.g. around `forecast_auto_ets`) wrapping the new parsing/model-construction code; convert any panic to an `AnofoxError`/SQL exception, never let it unwind across the `extern "C"` boundary (already-established project pattern, not new for this phase) |
| Integer overflow/huge `arima_order`/`seasonal_order` values (e.g. `p=999999999`) causing unbounded memory allocation or hang in the ARIMA optimizer | Denial of Service | Apply a sane upper bound (e.g. p,d,q ≤ some small constant like 10-20, matching what R's `Arima()` practically supports) at the C++ or Rust parsing layer, erroring on anything larger, rather than passing arbitrary user-supplied integers straight into `ARIMA::new`/`SARIMA::new` |

## Sources

### Primary (HIGH confidence — direct `Read`/`grep` of actual source this session)
- `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.10/src/models/traits.rs` — `Forecaster` trait definition and defaults
- `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.10/src/models/{baseline/*,arima/*,exponential/*,theta/*,intermittent/*,tbats/*,ensemble/*,garch.rs,kalman_forecaster.rs,mfles.rs,mstl_forecaster.rs,laplace/*}.rs` — per-model `impl Forecaster` overrides (§1)
- `/home/simonm/projects/duckdb/anofox-forecast/crates/anofox-fcst-core/src/forecast.rs` (4935 lines, read in full-coverage sections) — dispatch, extract_forecast, exog, fallback sites, build_forecaster
- `/home/simonm/projects/duckdb/anofox-forecast/crates/anofox-fcst-ffi/src/lib.rs` and `types.rs` — FFI struct, build_series, parse_seasonal_periods_str, panel Croston
- `/home/simonm/projects/duckdb/anofox-forecast/crates/anofox-fcst-core/src/imputation.rs`, `seasonality.rs`
- `/home/simonm/projects/duckdb/anofox-forecast/src/table_functions/ts_forecast_native.cpp`, `src/include/anofox_fcst_ffi.h`
- `/home/simonm/projects/duckdb/anofox-forecast/validation/reference/fixtures/r_forecast/air_passengers.json`
- `/home/simonm/projects/duckdb/anofox-forecast/.planning/research/STAT-AUDIT-2026-10-08.md` section A
- `/home/simonm/projects/duckdb/anofox-forecast/.planning/phases/09-reference-fixtures-safe-dependency-bumps/09-02-SUMMARY.md`, `09-03-SUMMARY.md`

### Secondary (MEDIUM confidence)
- None used — no web search was needed; this phase is entirely about the already-vendored crate's existing behavior.

### Tertiary (LOW confidence)
- None.

## Metadata

**Confidence breakdown:**
- Per-model capability table (§1): HIGH — file-level grep across all 36 model backing types in the actual installed crate source, cross-verified with deep reads for ~12 representative files
- forecast.rs bug locations (§2): HIGH — every line number quoted was read directly this session
- FFI/ABI extension pattern (§5): HIGH for the precedent (read verbatim), MEDIUM for the specific encoding recommendation (left as an open question, not asserted as fact)
- NULL handling gaps (§6): HIGH for what the code currently does, MEDIUM for the exact plumbing needed to fix the leading/trailing-trim date-anchoring (not yet attempted, reasoned from the code's structure)

**Research date:** 2026-10-09
**Valid until:** Until anofox-forecast is bumped past 0.15.10 (any model-override changes would need re-verification) or ~30 days, whichever is sooner
