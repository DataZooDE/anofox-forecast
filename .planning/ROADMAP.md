# Roadmap: anofox-forecast — Diagnostics + Model Coverage Milestone

## Overview

This milestone exposes two categories of unreachable crate capabilities through the established
FFI→C++→macro→example→docs delivery pattern: statistical diagnostics & validation (stationarity
tests, residual diagnostics, demand classification) and additional forecasting models (global/panel
cross-series learners and classical extras). Lower-risk, pattern-matching diagnostics phases come
first; higher-design-risk global/panel and multivariate model phases follow. Every item ships with
a runnable example, docs, and a numerical reference cross-check before it counts as done.

## Phases

- [x] **Phase 1: Statistical Diagnostics** - Expose stationarity tests (ADF, KPSS, combined verdict) and residual diagnostics (Ljung-Box, Durbin-Watson, Jarque-Bera, combined adequacy report) as scalar functions + `ts_*_by` macros. (Demand classification / INTER-01 deferred — user has a more advanced approach to be specified separately.)
- [ ] **Phase 2: Global / Panel Models** - Expose GlobalETS, GlobalTheta, and GlobalCroston via a panel-aware SQL surface that cross-learns across series
- [ ] **Phase 3: Classical & Multivariate Models** - Expose GARCH and Kalman as new `ts_forecast_by` methods and VAR as a dedicated multivariate function

## Phase Details

### Phase 1: Statistical Diagnostics

**Goal**: SQL users can validate a series' statistical properties (stationarity, residual adequacy) without leaving DuckDB
**Mode:** mvp
**Depends on**: Nothing (first phase)
**Requirements**: STAT-01, STAT-02, STAT-03, RESID-01, RESID-02, RESID-03, RESID-04
**Success Criteria** (what must be TRUE):

  1. User can call `ts_adf_by` and `ts_kpss_by` on a grouped table and receive test statistic, p-value, and (for ADF) lag per series
  2. User can call `ts_stationarity_by` and receive a four-way verdict (stationary / trend-stationary / difference-stationary / non-stationary) combining ADF and KPSS results
  3. User can call `ts_ljung_box_by`, `ts_durbin_watson_by`, and `ts_jarque_bera_by` on residuals and receive the relevant statistic and p-value per series
  4. User can call `ts_residual_diagnostics_by` and receive all three residual tests plus a combined pass/fail adequacy verdict in one query
  5. Every function is verified against statsmodels/R reference outputs and documented in `docs/api/`

**Deferred from this phase**: INTER-01 (intermittent-demand classification) — user has a more advanced approach than standard ADI/CV²; to be specified and scheduled separately.
**Plans**: 0/3 plans executed

- [x] 01-1-PLAN.md — ADF tracer: ts_adf / ts_adf_by end-to-end through all five layers + scaffolding (STAT-01)
- [x] 01-2-PLAN.md — Stationarity completion: ts_kpss + ts_stationarity four-way verdict (STAT-02, STAT-03)
- [x] 01-3-PLAN.md — Residual diagnostics: ts_ljung_box, ts_durbin_watson, ts_jarque_bera, ts_residual_diagnostics (RESID-01..04)

### Phase 2: Global / Panel Models

**Goal**: SQL users can forecast a grouped panel using cross-series global learners (GlobalETS, GlobalTheta, GlobalCroston) via a panel-aware SQL surface
**Mode:** mvp
**Depends on**: Phase 1
**Requirements**: GLOB-01, GLOB-02, GLOB-03
**Success Criteria** (what must be TRUE):

  1. User can call a panel forecast function with a grouped table and receive per-series forecasts produced by GlobalETS, which cross-learns across all series in the panel
  2. User can call the same panel surface with `method = 'GlobalTheta'` and `method = 'GlobalCroston'` and receive correct per-series forecasts
  3. Benchmark results for each global model are committed to `benchmark/` and show parity with a statsforecast or M4/M5 reference baseline
  4. Each model is documented in `docs/api/` and `docs/reference/models/` with a runnable `examples/*.sql` snippet verified against the built extension

**Risk / Design consideration**: GlobalETS, GlobalTheta, and GlobalCroston (`crate::batch`) cross-learn across all series simultaneously — the existing per-series `ts_forecast_by` dispatch is insufficient. The SQL surface must accept a full panel (all series at once), fit the global model once, then emit per-series forecasts. This requires a new table-function signature distinct from `ts_forecast_by`; design must be settled in the plan for this phase before implementation begins.
**Plans**: TBD

### Phase 3: Classical & Multivariate Models

**Goal**: SQL users can forecast conditional volatility with GARCH, apply Kalman-filter smoothing/forecasting, and produce multivariate VAR forecasts — all from SQL
**Mode:** mvp
**Depends on**: Phase 2
**Requirements**: CLAS-01, CLAS-02, CLAS-03
**Success Criteria** (what must be TRUE):

  1. User can call `ts_forecast_by` with `method = 'GARCH'` and receive conditional volatility forecasts; a runnable example in `examples/` is verified against the built extension
  2. User can call `ts_forecast_by` with `method = 'Kalman'` and receive smoothed/forecasted values; documented and verified end-to-end
  3. User can call `ts_forecast_var_by` (or equivalent multivariate surface) with multiple value columns and receive per-variable forecasts from a VAR model; benchmark parity is confirmed
  4. All three models are documented in `docs/api/` and `docs/reference/models/` and cross-checked against a statsforecast or R reference baseline in `benchmark/`

**Risk / Design consideration**: VAR is multivariate — it accepts N value columns and returns N forecast columns, a different I/O shape from all existing univariate `ts_forecast_by` methods. A dedicated function (`ts_forecast_var` / `ts_forecast_var_by`) is the anticipated design, but the exact multivariate column-mapping API must be settled in the plan before implementation.
**Plans**: TBD

## Progress

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Statistical Diagnostics | 0/3 | Planned    |  |
| 2. Global / Panel Models | 0/TBD | Not started | - |
| 3. Classical & Multivariate Models | 0/TBD | Not started | - |

---
*Roadmap created: 2026-08-21*
*Milestone: Close the Crate→Extension Gap (Diagnostics + Model Coverage)*
