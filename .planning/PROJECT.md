# anofox-forecast — DuckDB time-series forecasting extension

## What This Is

`anofox-forecast` is a DuckDB extension that exposes SQL-native time-series forecasting, backed by the `anofox-forecast` Rust crate (v0.15.3) via an FFI boundary. It surfaces 36+ forecasting models, 117 features, cross-validation, conformal prediction intervals, seasonality/period/changepoint/peak detection, statistical diagnostics, and — as of v0.8.0 — **model ensembling** (automatic top-K and user-named members, six combination methods, ensemble conformal intervals, member/weight introspection), all as SQL functions and `ts_*_by` macros.

It is a brownfield capability-exposure project, not a rewrite — the established delivery pattern is: Rust FFI export → C++ table/scalar function → `ts_*_by` SQL macro → runnable verified example → docs.

## Core Value

SQL users can produce, validate, combine, and interval-bound time-series forecasts — including multi-model ensembles with weight introspection — entirely within DuckDB, without leaving SQL.

## Current State

- **Shipped v0.8.0 — Ensemble Forecasting (2026-08-31):** AutoEnsemble (`ts_forecast_by('AutoEnsemble')`), explicit-member ensembles (`ts_forecast_ensemble_by`), six combination methods, ensemble conformal intervals (existing path), and member/weight introspection (`ts_ensemble_inspect_by` / `ts_auto_ensemble_inspect_by`). See `.planning/milestones/v0.8.0-*`.
- **Shipped v0.7.0 — Diagnostics + Model Coverage (2026-08-22):** stationarity + residual diagnostics, global/panel models, GARCH/Kalman/VAR.

## Current Milestone: v0.9.0 WASM Runtime Verification

**Goal:** Prove the built `anofox_forecast` `.wasm` actually loads and runs in DuckDB-Wasm — not just that it compiles and links — and gate it in CI so WASM regressions fail the build.

**Target features:**
- Node harness under `test/wasm/` that boots DuckDB-Wasm, serves + `LOAD`s the locally-built `.wasm`, and runs the full `test/sql/**/*.test` suite via a minimal sqllogictest-subset runner
- Gating CI job (`needs:` the wasm build) that fails on any WASM load/runtime error
- Dedicated WASM workflow + README badge reflecting WASM status specifically
- `@duckdb/duckdb-wasm` pinned to the engine version matching the built DuckDB version, documented
- OpenSSL made a `!wasm32` dependency in `vcpkg.json` (unused on WASM; telemetry is off there)

This is a CI/infrastructure hardening milestone — no new SQL surface, no crate bump. Reference implementation to port: anofox-statistics PR #131 (`test/wasm/run.mjs`, `sqllogic.mjs`, `WasmTest.yml`). Tracks GH issue #255.

## Requirements

### Validated

<!-- Inferred from existing codebase (brownfield); relied upon and shipped. -->

- ✓ `ts_forecast_by` + 36 model strings (baselines, ETS/Holt-Winters, Theta, ARIMA, MFLES, MSTL, TBATS, intermittent, Laplace) — existing
- ✓ 117-feature extraction (`ts_features*`), tsfresh-compatible — existing
- ✓ Cross-validation + backtest (`ts_cv_folds_by`, `ts_cv_forecast_by`, leakage check) — existing
- ✓ Conformal prediction intervals (split/adaptive/asymmetric/per-step, learn+apply) + bootstrap — existing
- ✓ Period/seasonality detection (~15 methods), MSTL decomposition, changepoints (PELT + BOCPD), peaks — existing
- ✓ 12 accuracy metrics, data-quality scoring, gap/null/differencing data prep — existing
- ✓ FFI + native-table-function + SQL-macro exposure pattern; DuckDB GROUP BY parallelism (no custom threading) — existing
- ✓ Stationarity tests: `ts_adf(_by)`, `ts_kpss(_by)`, combined `ts_stationarity(_by)` four-way verdict — v0.7.0 (statsmodels-cross-checked)
- ✓ Residual diagnostics: `ts_ljung_box_by`, `ts_durbin_watson_by`, `ts_jarque_bera_by`, combined `ts_residual_diagnostics_by` — v0.7.0
- ✓ Global/panel models: `ts_forecast_panel_by` (GlobalETS/GlobalTheta/GlobalCroston, cross-series learning) — v0.7.0 (statsforecast M4 parity)
- ✓ Classical models: `ts_forecast_by` methods `'GARCH'` (conditional volatility) and `'Kalman'` (state-space) — v0.7.0
- ✓ Multivariate: `ts_forecast_var_by` (VAR, N value columns → per-variable long-format forecasts) — v0.7.0 (statsmodels VAR parity)
- ✓ Milestone DoD upheld for every new function: runnable verified `examples/*.sql`, committed benchmark parity, `docs/api/` + `docs/reference/models/`, statsmodels/arch/R cross-checks
- ✓ AutoEnsemble SQL surface: `ts_forecast_by(..., 'AutoEnsemble', ..., {top_k, combination_method, seasonal_period})` (top-K across ARIMA/ETS/Theta) — v0.8.0 (Mean cross-check exact) (ENS-01)
- ✓ Six combination methods exposed (Mean, Median, WeightedMSE, InverseAIC, Stacking, HorizonAdaptive) across AutoEnsemble + explicit-member surfaces — v0.8.0 (COMB-01..04)
- ✓ Explicit-member ensemble: `ts_forecast_ensemble_by('table', grp, ds, y, members VARCHAR[], ...)` + `build_forecaster` factory (26-member allowlist, 10 blocked with clear errors) — v0.8.0 (ENS-02)
- ✓ Ensemble prediction intervals via the existing conformal path (`ts_cv_folds_by` + `ts_conformal_calibrate`/apply; per-fold loop for the scalar ensemble surfaces) — v0.8.0 (EPI-01)
- ✓ Ensemble introspection: `ts_ensemble_inspect_by` (members + weights) / `ts_auto_ensemble_inspect_by` (selected members + MSE score + rank) — v0.8.0 (INSP-01)

### Active

<!-- v0.9.0 WASM Runtime Verification — see REQUIREMENTS.md for REQ-IDs. -->

- [ ] Node harness loads the built `.wasm` in DuckDB-Wasm and runs the full `test/sql` suite
- [ ] Gating CI job fails the build on any WASM load/runtime error
- [ ] WASM status badge in README, backed by a dedicated WASM workflow
- [ ] `@duckdb/duckdb-wasm` pinned to the engine version matching the built DuckDB version, documented
- [ ] `openssl` made a `!wasm32` dependency in `vcpkg.json`

Deferred from v0.8.0 (candidates for a future milestone):
- **CV ensemble params + AutoEnsemble CV segfault:** `ts_cv_forecast_by('AutoEnsemble')` segfaults (CV native `ts_cv_forecast_native.cpp:380-388` never parses `ensemble_top_k`/`ensemble_method`). Wire ensemble-param parsing into the CV native so AutoEnsemble backtests natively (and honor top_k/combination_method instead of defaulting to top_k=3/Mean).
- **AutoEnsemble inner combination weights (upstream):** crate 0.15.3 exposes no accessor for the inner ensemble's non-Mean weights; `ts_auto_ensemble_inspect_by` returns NULL weight for WeightedMSE/InverseAIC/Stacking/HorizonAdaptive. Upstream crate enhancement.
- **ENS-F1** custom hand-supplied combination weights; **ENS-F2** panel/global + multivariate (VAR) ensembling; per-member ensemble parameters — all explicitly deferred at v0.8.0 start.
- `build_forecaster` SeasonalWindowAverage `n_seasons=2` hardcoded (TODO ENS-03) — Forecaster trait has no pre-fit series-length hook.

Deferred from v0.7.0 (candidates for a future milestone):
- Intermittent-demand classification (ADI/CV² taxonomy) — INTER-01 descoped; user has a more advanced approach TBD
- Prediction intervals for the new global/panel + GARCH/Kalman/VAR surfaces (route through the existing conformal path)
- VAR automatic lag-order selection (AIC/BIC); per-panel VAR; GARCH advanced coefficient overrides beyond p/q

### Out of Scope

- Anomaly detection (Mahalanobis/Parade/ZBank) — deferred to a later milestone despite `anomaly` feature being compiled in; not selected for v1
- Hierarchical reconciliation (MinTrace/BottomUp/TopDown/MiddleOut) — large standalone capability, own milestone
- Forecastability / triage (AMI, GCMI, transfer entropy, Lyapunov, STI, `run_triage`) — requires enabling the `forecastability` crate feature; deferred
- Multicollinearity / VIF — deferred with the exogenous-regression track
- Power transforms (Box-Cox / Yeo-Johnson) and scaling/rolling/EWM transforms — deferred (pairs with global-regression-fe work later)
- Extra conformal methods (IDR, QRA, CQR, EnbPI, binned) and extra changepoint algorithms (Binseg/BottomUp/Dynp/Window/KernelCpd) — existing coverage sufficient for now
- Outlier detection, model persistence (save/load), feature selection — deferred

## Context

- **Delivery pattern (established):** new capability = Rust FFI `#[no_mangle] pub extern "C"` export in `crates/anofox-fcst-ffi` → C++ table/scalar/aggregate function in `src/` → registration in `src/anofox_forecast_extension.cpp` → user-facing `ts_*_by` macro in `src/macros/ts_macros.cpp` → `examples/*.sql` → `docs/`.
- **Crate features currently enabled:** `anomaly`, `serde`, and default `postprocess` (→ `distributional`). NOT enabled: `forecastability`, `seasonal-detection`, `parallel`. The diagnostics and models in this milestone live under already-enabled features (`crate::validation`, `crate::models::*`), so no new feature flags are required for v1 scope.
- **Global models** are panel/batch forecasters (`GlobalETS`/`GlobalTheta`/`GlobalCroston`, `crate::batch`) — they cross-learn across series, so the SQL surface must accept a grouped panel, not a single series. This differs from the per-series `ts_forecast_by` dispatch and needs design attention.
- **VAR** is multivariate — output/interface shape differs from univariate models; may warrant its own function rather than a `method` string on `ts_forecast_by`.
- **Diagnostics** operate on residuals or a raw series and return scalar/struct verdicts — natural fit for scalar functions + `_by` macros, mirroring the metrics functions.
- Verified reference: docs SQL examples must be run through the built extension, not eyeballed (established rule from PR #230).
- **Shipped v0.7.0** (2026-08-22): +17.9k LOC across 57 commits / 111 files. Extension now surfaces 36 forecasting models (incl. GARCH, Kalman, panel Global*, multivariate VAR via `ts_forecast_var_by`) plus 7 statistical-diagnostic functions. `arch` added to `benchmark/.venv` (comparison group) for GARCH parity. New non-globbed C++ sources (`diagnostics.cpp`, `ts_forecast_panel_native.cpp`, `ts_forecast_var_native.cpp`) are explicitly listed in CMakeLists.
- **Panel/table-in macro convention (v0.7.0 lesson):** table-in macros must wrap `query_table(...)` in a subselect `(SELECT ... FROM query_table(...))`; a bare TABLE arg silently fails to register.
- **Shipped v0.8.0** (2026-08-31): +13.9k LOC across 57 files / 3 phases / 6 plans. Extension now surfaces AutoEnsemble (`ts_forecast_by('AutoEnsemble')`) + explicit-member ensembles (`ts_forecast_ensemble_by`) with six combination methods, ensemble conformal intervals (existing path), and member/weight introspection (`ts_ensemble_inspect_by` / `ts_auto_ensemble_inspect_by`). Two new non-globbed C++ sources (`ts_forecast_ensemble_native.cpp`, `ts_ensemble_inspect_native.cpp`) explicitly listed in CMakeLists. No external reference library for ensembles — verification is internal-consistency cross-checks (combined == manual weighted combination of members).
- **`ts_cv_forecast_by('AutoEnsemble')` segfaults** (crate/CV-native bug: `ts_cv_forecast_native.cpp:380-388` never parses the ensemble params) — ensemble conformal CV uses a manual per-fold `_ts_forecast_scalar` loop instead. Known tech debt.

## Constraints

- **Tech stack**: DuckDB v1.4.3+ extension; Rust 1.86+ core via FFI; C++17. No new languages.
- **Architecture**: Parallelism stays at the DuckDB GROUP BY / scalar-function layer — no custom threading or table-in/table-out (established project rule).
- **Dependencies**: Stay on `anofox-forecast` 0.15.3 unless a required capability is missing; global-model steady-state ARIMA optimization tracked separately (awaiting 0.5.4-class improvements).
- **Compatibility**: Must build and load across Linux/macOS/Windows and WASM; OpenSSL stays statically linked; verify clean-machine load (not just green CI).
- **Verification**: Every new SQL function must be exercised by a runnable example against the built extension before it counts as done.

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Scope milestone to diagnostics + model coverage (defer anomaly, reconciliation, triage) | Both chosen themes reuse already-enabled crate features and the existing exposure pattern; lower risk than new-feature-flag work | ✓ Good — v0.7.0 shipped all diagnostics + 6 new models via the existing pattern with no new crate feature flags |
| Expose diagnostics as scalar functions + `ts_*_by` macros | Mirrors existing metrics surface; returns scalar/struct verdicts per series | ✓ Good — 7 diagnostic functions shipped, statsmodels-cross-checked |
| Global/panel models need a panel-aware SQL surface | GlobalETS/Theta/Croston cross-learn across series; per-series `ts_forecast_by` dispatch is insufficient | ✓ Good — `ts_forecast_panel_by` fit-once-emit-many native table function delivered |
| VAR is a dedicated multivariate function (`ts_forecast_var_by`), not a `method` string | Multivariate I/O shape (N cols → N forecasts) differs from univariate `ts_forecast_by` | ✓ Good — long-format `{variable, forecast_date, forecast_value}` surface delivered |
| Definition of done = example + benchmark parity + docs + reference cross-check | User requires all four validation signals for every item | ✓ Good — upheld for all 13 requirements |
| ForecastOptions FFI ABI extended additively for GARCH/Kalman params | Backward-compatible with existing univariate methods; avoids a parallel options struct | ✓ Good — integration-checker confirmed no ABI breakage across pre-milestone methods |
| Autonomous code-review + fix loop after each phase | Happy-path verifiers miss edge cases (spurious intervals, WASM free UB, overflow) | ⚠️ Revisit — caught real bugs, but each phase needed 2–3 fix iterations; consider tightening executor guidance to prevent recurrence |
| AutoEnsemble & explicit-member ensembles share `parse_combination_method` + `build_forecaster` back-ends (v0.8.0) | One combination-method parser + one name→forecaster factory keep all six methods consistent across three SQL surfaces | ✓ Good — integration-checker confirmed identical method behavior across `ts_forecast_by('AutoEnsemble')`, `ts_forecast_ensemble_by`, `ts_ensemble_inspect_by` |
| Default combination method = Mean (override crate's WeightedMSE default) (v0.8.0) | Makes the internal-consistency cross-check (combined == arithmetic mean of members) the default, most-verifiable path | ✓ Good — Mean cross-check diff=0.0 across all ensemble surfaces |
| Explicit-member ensemble as a dedicated `ts_forecast_ensemble_by` macro, not a method-string (v0.8.0) | A `VARCHAR[]` member list + method doesn't fit the single method-string of `ts_forecast_by`; ScalarFunction dispatch mirrors `_ts_forecast_scalar` | ✓ Good — new native ScalarFunction + CMakeLists entry; per-series long-format output |
| EPI-01 reuses the existing conformal path (no new interval machinery) (v0.8.0) | Conformal is model-agnostic on residual columns; DoD says reuse, don't rebuild | ✓ Good — bounds produced for both surfaces; surfaced a pre-existing `ts_cv_forecast_by('AutoEnsemble')` segfault (worked around + documented as tech debt) |
| INSP-01 scoped to what crate 0.15.3 exposes: full weights for explicit-member, NULL weights for AutoEnsemble non-Mean (v0.8.0) | Crate has no accessor for AutoEnsemble's inner combination weights; re-implementing selection would be fragile/drift-prone | ✓ Good — honest surface; limitation documented; upstream enhancement filed as tech debt |
| Verify executor/agent reports against git+disk, re-run every example against the built extension (v0.8.0) | Two executors fabricated commit hashes/SUMMARY files; three long-running agents died on transient API errors mid-run | ✓ Good — caught + reconciled all fabrications; per-task commits protected work across agent deaths |

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd-transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd-complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

---
*Last updated: 2026-09-01 — started v0.9.0 WASM Runtime Verification milestone*
