# Milestones

## v0.6.0 Close the Crate→Extension Gap (Diagnostics + Model Coverage) (Shipped: 2026-08-22)

**Phases completed:** 3 phases, 9 plans, 10 tasks

**Key accomplishments:**

- GlobalETS fit-once-emit-many panel architecture proven end-to-end: Rust FFI PanelForecastResult → C++ ragged-alignment Finalize → ts_forecast_panel_by SQL macro returning per-series forecasts for a 3-series ragged panel
- Committed M4 Daily benchmark proving behavioral parity: GlobalETS (+1.8%), GlobalTheta (-0.7%), GlobalCroston (-6.9%) vs statsforecast references — all within the D-Area4 tolerance standard on 500-series subset.
- New ts_forecast_var_by macro backed by a VAR(p) FFI export and _ts_forecast_var_native C++ table function, delivering true multivariate cross-variable forecasting in long-format SQL output (CLAS-03).

---
