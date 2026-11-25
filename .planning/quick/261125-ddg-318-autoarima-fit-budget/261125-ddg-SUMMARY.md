---
phase: quick-261125-ddg
plan: 01
status: complete
---

# 261125-ddg SUMMARY: #318 AutoARIMA fit-cost limits

- New params keys `max_models` / `time_budget_ms` (AutoARIMA only, default unbounded =
  results unchanged) on ts_forecast_by, ts_forecast_agg, ts_forecast_exog_by,
  ts_cv_forecast_by, ts_forecast_inspect_by. Forwarded to crate 0.21.0
  `AutoARIMAConfig::with_max_models` / `with_time_budget`. Other models and negative values
  error.
- W118 (issue body, threads=1): unbounded 211 s; `{time_budget_ms: 2000}` 2.4 s;
  `{max_models: 10}` 0.8 s.
- Verified: cargo fmt/clippy -D warnings clean; cargo test --workspace --release all pass;
  test/sql/ts_forecast* + ts_cv* + ts_native_param_validation (2,543 assertions) pass
  including new ts_forecast_auto_arima_limits.test (19); examples/forecasting/
  auto_arima_limits.sql PASS; check_doc_snippets 371 PASS / 0 FAIL.
- #321 re-checked on this build: permutation_entropy is bit-identical across 50 calls
  (fixed by #299, shipped in v2026.10.31; the registry still serves 2026.10.22 until
  community-extensions#3673 merges).
- Open from #318: auto-detection picks period 30 on weekly W118 (the root of the slow
  fit); not changed here.
