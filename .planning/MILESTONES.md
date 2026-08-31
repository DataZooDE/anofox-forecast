# Milestones

## v0.8.0 Ensemble Forecasting (Shipped: 2026-08-31)

**Phases completed:** 3 phases, 6 plans, 11 tasks

**Key accomplishments:**

- Rust `build_forecaster` factory (exhaustive 36-variant match) + `forecast_explicit_ensemble` + `anofox_ts_forecast_ensemble` FFI (null-delimited member buffer) + `_ts_forecast_ensemble_native` ScalarFunction + `ts_forecast_ensemble_by` macro, end-to-end verified: Mean cross-check mismatch_count=0 on ['AutoARIMA','AutoETS','Naive'] with NULL intervals
- Full ENS-02 DoD delivered: canonical ['AutoARIMA','AutoETS','Theta'] Mean cross-check passes (mismatch_count=0, diff=0.0), all six combination methods smoke-tested, 26-member allowlist sampled, three error paths verified, reference doc + API entry written with all snippets run through built extension
- Two introspection Rust functions + EnsembleInspectResult FFI struct + 2 FFI exports + companion free fn + 2 C++ ScalarFunctions + CMakeLists entry + registration + 2 SQL macros, end-to-end verified: explicit Mean weights==1/k (sum 1), AutoEnsemble WeightedMSE weight IS NULL + score>0 + rank
- EPI-01 conformal example (2 sections, both bad_rows=0) + INSP-01 full DoD example (4 sections, 8 assertions all 0) + 3 docs files — all SQL verified against the built extension; both CV-native limitations documented honestly

---

## v0.7.0 Close the Crate→Extension Gap (Diagnostics + Model Coverage) (Shipped: 2026-08-22)

**Phases completed:** 3 phases, 9 plans, 10 tasks

**Key accomplishments:**

- GlobalETS fit-once-emit-many panel architecture proven end-to-end: Rust FFI PanelForecastResult → C++ ragged-alignment Finalize → ts_forecast_panel_by SQL macro returning per-series forecasts for a 3-series ragged panel
- Committed M4 Daily benchmark proving behavioral parity: GlobalETS (+1.8%), GlobalTheta (-0.7%), GlobalCroston (-6.9%) vs statsforecast references — all within the D-Area4 tolerance standard on 500-series subset.
- New ts_forecast_var_by macro backed by a VAR(p) FFI export and _ts_forecast_var_native C++ table function, delivering true multivariate cross-variable forecasting in long-format SQL output (CLAS-03).

---
