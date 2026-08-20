# anofox-forecast — Milestone: Close the Crate→Extension Gap (Diagnostics + Model Coverage)

## What This Is

`anofox-forecast` is a DuckDB extension that exposes SQL-native time-series forecasting, backed by the `anofox-forecast` Rust crate (v0.15.3) via an FFI boundary. It already surfaces 36 forecasting models, 117 features, cross-validation, conformal prediction intervals, seasonality/period/changepoint/peak detection, and data-prep utilities as SQL functions and `ts_*_by` macros.

This milestone extends that SQL surface to reach crate capabilities that are currently unreachable from SQL: statistical **diagnostics & validation**, and additional **forecasting models** (global/panel and classical). It is a brownfield capability-exposure milestone, not a rewrite — the delivery pattern is the established one: Rust FFI export → C++ table/scalar/aggregate function → `ts_*_by` SQL macro → runnable example → docs.

## Core Value

SQL users can validate whether a series/model is statistically sound (stationarity, residual adequacy, demand regime) and can reach the crate's higher-coverage models (global + classical) — all without leaving DuckDB.

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

### Active

<!-- This milestone. Each is a crate capability to expose through the full FFI→C++→macro→example→docs pattern. -->

**Diagnostics & validation**
- [ ] Stationarity tests: ADF, KPSS, and a combined stationarity verdict
- [ ] Residual diagnostics: Ljung-Box, Durbin-Watson, Jarque-Bera on forecast residuals
- [ ] Intermittent-demand classification: ADI/CV² taxonomy (smooth / erratic / lumpy / intermittent)

**New forecasting models**
- [ ] Global/panel models: GlobalETS, GlobalTheta, GlobalCroston (cross-series learning)
- [ ] Classical extras: GARCH (volatility), Kalman, VAR (multivariate)

**Validation of the milestone itself (definition of done, applies to every item above)**
- [ ] Each new function has a runnable `examples/*.sql` snippet, verified end-to-end against the built extension
- [ ] New models checked for benchmark parity (M4/M5 or statsforecast reference) in `benchmark/`
- [ ] Every new function documented in `docs/api/` and, for models, `docs/reference/models/`
- [ ] Diagnostics numerically cross-checked against statsmodels/R reference outputs

### Out of Scope

- Anomaly detection (Mahalanobis/Parade/ZBank) — deferred to a later milestone despite `anomaly` feature being compiled in; not selected for v1
- Hierarchical reconciliation (MinTrace/BottomUp/TopDown/MiddleOut) — large standalone capability, own milestone
- Forecastability / triage (AMI, GCMI, transfer entropy, Lyapunov, STI, `run_triage`) — requires enabling the `forecastability` crate feature; deferred
- Multicollinearity / VIF — deferred with the exogenous-regression track
- Power transforms (Box-Cox / Yeo-Johnson) and scaling/rolling/EWM transforms — deferred (pairs with global-regression-fe work later)
- Ensemble / AutoEnsemble — not selected for v1
- Extra conformal methods (IDR, QRA, CQR, EnbPI, binned) and extra changepoint algorithms (Binseg/BottomUp/Dynp/Window/KernelCpd) — existing coverage sufficient for now
- Outlier detection, model persistence (save/load), feature selection — deferred

## Context

- **Delivery pattern (established):** new capability = Rust FFI `#[no_mangle] pub extern "C"` export in `crates/anofox-fcst-ffi` → C++ table/scalar/aggregate function in `src/` → registration in `src/anofox_forecast_extension.cpp` → user-facing `ts_*_by` macro in `src/macros/ts_macros.cpp` → `examples/*.sql` → `docs/`.
- **Crate features currently enabled:** `anomaly`, `serde`, and default `postprocess` (→ `distributional`). NOT enabled: `forecastability`, `seasonal-detection`, `parallel`. The diagnostics and models in this milestone live under already-enabled features (`crate::validation`, `crate::models::*`), so no new feature flags are required for v1 scope.
- **Global models** are panel/batch forecasters (`GlobalETS`/`GlobalTheta`/`GlobalCroston`, `crate::batch`) — they cross-learn across series, so the SQL surface must accept a grouped panel, not a single series. This differs from the per-series `ts_forecast_by` dispatch and needs design attention.
- **VAR** is multivariate — output/interface shape differs from univariate models; may warrant its own function rather than a `method` string on `ts_forecast_by`.
- **Diagnostics** operate on residuals or a raw series and return scalar/struct verdicts — natural fit for scalar functions + `_by` macros, mirroring the metrics functions.
- Verified reference: docs SQL examples must be run through the built extension, not eyeballed (established rule from PR #230).

## Constraints

- **Tech stack**: DuckDB v1.4.3+ extension; Rust 1.86+ core via FFI; C++17. No new languages.
- **Architecture**: Parallelism stays at the DuckDB GROUP BY / scalar-function layer — no custom threading or table-in/table-out (established project rule).
- **Dependencies**: Stay on `anofox-forecast` 0.15.3 unless a required capability is missing; global-model steady-state ARIMA optimization tracked separately (awaiting 0.5.4-class improvements).
- **Compatibility**: Must build and load across Linux/macOS/Windows and WASM; OpenSSL stays statically linked; verify clean-machine load (not just green CI).
- **Verification**: Every new SQL function must be exercised by a runnable example against the built extension before it counts as done.

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Scope milestone to diagnostics + model coverage (defer anomaly, reconciliation, triage) | Both chosen themes reuse already-enabled crate features and the existing exposure pattern; lower risk than new-feature-flag work | — Pending |
| Expose diagnostics as scalar functions + `ts_*_by` macros | Mirrors existing metrics surface; returns scalar/struct verdicts per series | — Pending |
| Global/panel models need a panel-aware SQL surface | GlobalETS/Theta/Croston cross-learn across series; per-series `ts_forecast_by` dispatch is insufficient | — Pending |
| VAR likely a dedicated multivariate function, not a `method` string | Multivariate I/O shape differs from univariate `ts_forecast_by` | — Pending |
| Definition of done = example + benchmark parity + docs + reference cross-check | User requires all four validation signals for every item | — Pending |

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
*Last updated: 2026-08-21 after initialization*
