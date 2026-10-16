# Milestones

## v0.10.0 Statistical Correctness (Shipped: 2026-10-16)

**Phases completed:** 10 phases (9-18), 57 plans, 157 tasks · 65/65 requirements · branch `fix/statistical-correctness`

**Key accomplishments:**

- **Reference-fixture harness:** checked-in R (`forecast`, `tseries`, `urca`) and uv-pinned Python (scipy, statsmodels, tsfresh, numpy) generators, with every statistic asserted against fixture or closed-form values (Phase 9, enforced milestone-wide in Phase 18).
- **The forecast wrapper delegates to the crate:** prediction intervals, fitted values, residuals and model identity come from the crate models. That brought real ARIMA/SARIMA, crate exogenous support, and errors instead of silent fallbacks (Phase 10).
- **Upstream anofox-forecast correctness, released as 0.16.0, 0.16.1 and 0.16.2 and consumed from crates.io:** augmented ADF with MacKinnon p-values, bounded KPSS, AutoARIMA reproducing R `auto.arima`, the ETS likelihood and Hyndman intervals, Theta STM, and ARIMA intervals matching R `KalmanForecast`. The 36-check R `forecast` parity suite runs with zero AutoARIMA exceptions (Phases 11, 17, 18).
- **Statistics moved out of C++/SQL into Rust:** a validity-carrying NULL policy, a ToUnifiedFormat list marshaller, scipy-exact moments, STL strengths, Hyndman-Koehler MASE, all 117 features at tsfresh parity, scipy-semantics peaks, correct BOCPD and PELT, and STL-based MSTL on fdars 0.47 (Phases 12-15).
- **Conformal and bootstrap rebuilt:** horizon-ordered, per-group and computed in Rust, with corrected split, asymmetric, adaptive and jackknife+ variants and the correct bootstrap sign (Phase 16).
- **Regression-proofed:** json is statically linked, so 50 previously skipped test files run. A `test/sql/correctness/*` inventory gate, a length-only Rust-test sweep, a `make audit` no-stats-in-C++/SQL guard with a CI workflow, a permanent `make check-docs` snippet checker, and the v0.10.0 breaking-changes CHANGELOG and release notes complete it. Final suite: 5593 SQL assertions in 94 test cases (Phase 18).

**Closeout:** override_closeout. Known verification overrides: 10 phases flagged "stale" by gsd's staleness heuristic (later commits touched covered files). All VERIFICATION.md files are status: passed, and the integration check re-ran green at HEAD. 19 newly acknowledged, 0 carried forward from a prior close (see STATE.md Deferred Items).

**Known limitations / tech debt (non-blocking):** TBATS interval width vs statsforecast (optimiser admissibility gap); the ETS(M,A,M) optimum differs from R; integer `frequency` is not cast in `ts_forecast_panel_by` / `ts_forecast_var_by` / `ts_stats_by`; missing `window` / `seasonal_periods` misuse checks in the shared params header; unparsable MAP numeric values fall back silently; the examples/ metric `_by` bare-table form; the `ts_features_config_*` "features" filter is ignored; a dead `ConformalIntervalFFI`. Full list: milestones/v0.10.0-MILESTONE-AUDIT.md.

**Upstream releases shipped during the milestone:** anofox-forecast v0.16.0 (2026-10-10), v0.16.1 (2026-10-14), v0.16.2 (2026-10-16).

## v0.9.0 WASM Runtime Verification (Shipped: 2026-09-10; closed on main 2026-10-16)

**Phases completed:** 2 phases (7-8), 2 plans, 3 tasks · 8/8 requirements

**Key accomplishments:**

- DuckDB-Wasm Node harness (`test/wasm/run.mjs` + `sqllogic.mjs`) loads the built `wasm_eh` extension and runs a curated 8-file / 396-assertion sqllogictest subset (Phase 7).
- `wasm-runtime-test` gating job in MainDistributionPipeline.yml, a dedicated `WasmTest.yml` workflow (via `workflow_run`), and a README WASM badge (Phase 8).

**Closeout note:** the 2026-09-10 close was committed on a side branch (`milestone/v0.8.0-ensemble-forecasting`, 9976346) and never merged, so v0.9.0 read "parked" on main. It was restored on 2026-10-16, once the outstanding live-CI negative control was observed: the gate went RED on run 38905747917 (only the deliberate control failed) and back to GREEN after the revert on run 38914644475 (draft PR #275, closed unmerged).

**Deferred:** WASM-F1 browser E2E harness; WASM-F2 shared-memory `wasm_threads` (blocked upstream).

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
