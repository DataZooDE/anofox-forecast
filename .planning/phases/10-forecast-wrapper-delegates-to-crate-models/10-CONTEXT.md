# Phase 10: Forecast Wrapper Delegates to Crate Models - Context

**Gathered:** 2026-10-09
**Status:** Ready for planning

<domain>
## Phase Boundary

`ts_forecast_by`, `ts_forecast_exog_by`, the scalar/agg forecast paths and the ensemble surfaces return the prediction intervals, fitted values, residuals and model identity the crate model actually produced (anofox-forecast 0.15.10) — no fabricated point ± z·sd(series)·√h intervals, no SES(α=0.3) stand-in residuals, no fixed-coefficient "ARIMA", no silent model/parameter fallbacks. Requirements FCST-01..08. Upstream crate correctness (ETS likelihood, Theta, AutoARIMA order selection, interval formulas) is Phase 11; R-parity of ETS/Theta/AutoARIMA intervals (FCST-09) is Phase 17. Docs for every changed behaviour are updated in this phase (docs/api/07-forecasting.md, docs/reference/models/**, models skill), replacing the "(being corrected in v0.10.0, Phase 10)" notes added by quick task 261009-d02.

</domain>

<decisions>
## Implementation Decisions

### Prediction intervals
- **D-01:** Use the crate model's `predict_with_intervals(h, level)` for every model; pass crate intervals through unchanged, including models whose crate intervals are known to be miscalibrated until Phase 11/17 (SES flat, seasonal ETS flat, Theta ~2×, TBATS too narrow, GARCH) — document each known issue per model in the reference docs.
- **D-02:** Models with no native interval implementation (crate default `predict_with_intervals` returns point only) → NULL lower/upper bounds; docs point users to the conformal functions.
- **D-03:** Delete `calculate_confidence_intervals` entirely — no opt-in legacy heuristic. Laplace distributional intervals pass through unchanged. Any z that remains anywhere uses exact Φ⁻¹(1−α/2) (statrs), never a step table.

### Silent fallbacks
- **D-04:** Every former silent fallback becomes an SQL error with a clear message; `model_name` in the output always identifies the model (and selected spec/order) that actually ran.
- **D-05:** `'ETS'` without `ets_model` → run the crate AutoETS selection and report the chosen spec in model_name (e.g. `ETS(M,A,M)`); the ensemble builder uses the same rule (FCST-07: standalone == ensemble member).
- **D-06:** `AutoMFLES` / `AutoMSTL` remain accepted as documented, deprecated aliases; model_name reports `MFLES` / `MSTL`.
- **D-07:** MSTL / TBATS / HoltWinters / SeasonalES without a seasonal period → auto-detect the period (existing detection path); if none is detected → SQL error. No default 12 or 2. Unknown `kalman_model`, misspelled panel Croston variant, invalid UTF-8 model name, unparsable seasonal-period entries → errors. AutoETS crate failure → error (not HW/Holt/SES fallback).

### ARIMA
- **D-08:** New ts_forecast_by parameter keys `arima_order` [p,d,q] and `seasonal_order` [P,D,Q] (seasonal period from `seasonal_period`); added to ValidateParamKeys and the FFI ForecastOptions additively (ABI-compatible, mirroring the GARCH/Kalman extension pattern).
- **D-09:** `'ARIMA'` without `arima_order` → SQL error suggesting `AutoARIMA`. With an order → real crate ARIMA fit (forecasts match R `Arima()` at that order within 1%).
- **D-10:** Exogenous path uses the crate's exog-aware models (`supports_exog` / `predict_with_exog_intervals`): AutoARIMA runs its order search with regressors (selected order in model_name); Optimized/Dynamic Theta stay themselves; ETS/GARCH/Kalman options honoured; a regressor constant in history gets a 0 effect and the forecast stays finite (skip non-finite OLS coefficients if a wrapper OLS path remains).

### Fitted values, NULLs, compatibility
- **D-11:** Interior NULLs keep linear interpolation for fitting, but interpolated points are excluded from MSE and their residuals are NULL. Fitted/residuals/MSE come from the crate model (`fitted_values()` / `residuals()`); delete `calculate_fitted_values`.
- **D-12:** Leading/trailing NULLs are trimmed before fitting (forecast dates unchanged). All-NULL series → error. A valid NaN is treated as missing. Regressor NULLs handled via validity (FCST-08).
- **D-13:** Changed numerical outputs (narrower/NULL intervals, new errors) are listed in a CHANGELOG "Breaking changes" section; no legacy compatibility flag.

### Claude's Discretion
- Exact error message wording, internal refactoring of forecast.rs dispatch, test organisation (fixture-based value assertions per Phase 9 harness are mandatory).

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- Phase 9 harness: `validation/reference/` (R forecast reference incl. AirPassengers points + 80/95 intervals for naive/snaive/rwf/ses/holt/ets/auto.arima/thetaf/croston), `crates/anofox-fcst-core/tests/common/mod.rs` loader, `tests/forecast_snapshot.rs` (point-forecast snapshot), `multiscale_ols` + exog fixtures, `validation/reference/sql/check_dep01_exog.sql`.
- Crate trait `anofox_forecast::models::traits::Forecaster` (0.15.10, traits.rs:50-280): `predict_with_intervals`, `fitted_values`, `residuals`, `supports_exog`, `predict_with_exog`, `predict_with_exog_intervals`; `ARIMA::new(p,d,q)` + `ARIMASpec` (models/arima/model.rs).

### Established Patterns
- forecast.rs dispatch :657-728; interval override :796-799, :1003; fake ARIMA :1538; exog :1058-1170, :3207-3310; build_forecaster :2616-2780; ForecastOptions additive ABI extension (v0.7.0 GARCH/Kalman).
- C++ param validation `src/table_functions/ts_forecast_native.cpp:277-313` (ValidateParamKeys), defaults :44,:357; scalar `ts_forecast_scalar.cpp`; agg `ts_forecast_agg.cpp`.
- DoD: cargo fmt --check, clippy -D warnings, cargo test --workspace, extension rebuilt and SQL examples/docs snippets run against build/release/duckdb -unsigned; `make test_release`.

### Integration Points
- FFI `crates/anofox-fcst-ffi/src/lib.rs` (ForecastOptions, model-name parsing :255, :3370 seasonal periods, :3412/:3667/:4141/:2248 UTF-8 fallback, panel Croston :7016/:7045); cbindgen header `src/include/anofox_fcst_ffi.h` regenerates on build.
- test/sql/*.test sqllogictests that assert old interval/fitted values will need updating (expected).

</code_context>

<specifics>
## Specific Ideas

- Success evidence (ROADMAP): AirPassengers 95% h=1 Naive half-width ≈ R naive() ±66 (not 234); no model reproduces the old width; level 0.80 → z 1.2816; MSE no longer identical 2090.28 across SES/Holt/AutoARIMA/AutoETS/Theta/HW/MSTL/Croston.
- Audit reference: .planning/research/STAT-AUDIT-2026-10-08.md section A (A1–A15).

</specifics>

<deferred>
## Deferred Ideas

- Calibration of crate-native intervals (SES/ETS/Theta/TBATS/MSTL/GARCH) → Phase 11 (upstream) + Phase 17 (R parity).

</deferred>
