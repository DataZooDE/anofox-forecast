---
phase: quick-261125-vo0
plan: 01
status: complete
---

# 261125-vo0 SUMMARY: #317 forecasting with given model parameters

- Upstream anofox-forecast 0.23.0 (sipemu/anofox-forecast#253): `ETS::with_fixed_params`
  (values kept exactly, ML initial states), `Inspectable for ETS`. WASM size gate
  re-baselined (+1.16%, CI measurement).
- Extension: params keys alpha/beta/gamma/phi (ETS + model), alpha_d/alpha_p (TSB),
  ar/ma/seasonal_ar/seasonal_ma_coefficients + intercept (ARIMA + arima_order) on every
  forecast surface; fixed-spec 'ETS' inspectable. Strict parsing in Rust
  (fixed_params.rs), one packed FFI field.
- Verified on crates.io 0.23.0: test/sql/ts_forecast* 1650 assertions (new
  ts_forecast_fixed_params.test 37), ts_cv* 831, check_doc_snippets 373/0, example PASS;
  Rust round trips (ARIMA, SARIMA intercept conversion); fmt, clippy -D warnings.
- Not done: time-varying parameters (#317 item 2).
