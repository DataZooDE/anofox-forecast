# Requirements: anofox-forecast — Diagnostics + Model Coverage Milestone

**Defined:** 2026-08-21
**Core Value:** SQL users can validate whether a series/model is statistically sound (stationarity, residual adequacy, demand regime) and can reach the crate's higher-coverage models (global + classical) — all without leaving DuckDB.

## Definition of Done (applies to every v1 requirement)

Each requirement is "Complete" only when ALL of the following hold:

1. **Example** — a runnable `examples/*.sql` snippet exists and is verified end-to-end against the built extension.
2. **Docs** — the function is documented in `docs/api/` (and, for models, `docs/reference/models/`) with its full parameter surface.
3. **Reference cross-check** — diagnostics numerically cross-checked against statsmodels/R; models checked for benchmark parity (M4/M5 or statsforecast reference) in `benchmark/`.
4. Delivered through the established pattern: Rust FFI export → C++ table/scalar/aggregate function → `ts_*_by` SQL macro → registration.

## v1 Requirements

### Stationarity Tests

- [ ] **STAT-01**: User can test a series for stationarity with the Augmented Dickey-Fuller test (`ts_adf` / `ts_adf_by`), returning statistic, p-value, and lag.
- [ ] **STAT-02**: User can test a series for stationarity with the KPSS test (`ts_kpss` / `ts_kpss_by`), returning statistic and p-value.
- [ ] **STAT-03**: User can get a combined ADF+KPSS stationarity verdict (`ts_stationarity` / `ts_stationarity_by`) classifying the series as stationary / trend-stationary / difference-stationary / non-stationary.

### Residual Diagnostics

- [ ] **RESID-01**: User can run a Ljung-Box white-noise test on residuals (`ts_ljung_box` / `ts_ljung_box_by`) at a chosen lag.
- [ ] **RESID-02**: User can compute the Durbin-Watson statistic on residuals (`ts_durbin_watson` / `ts_durbin_watson_by`).
- [ ] **RESID-03**: User can run a Jarque-Bera normality test on residuals (`ts_jarque_bera` / `ts_jarque_bera_by`).
- [ ] **RESID-04**: User can get a combined residual-diagnostics report (`ts_residual_diagnostics_by`) returning all three tests plus a pass/fail adequacy verdict.

### Intermittent-Demand Classification

- [ ] **INTER-01**: User can classify a series' demand pattern via ADI/CV² taxonomy (`ts_classify_demand` / `ts_classify_demand_by`) into smooth / erratic / lumpy / intermittent, returning ADI, CV², and the recommended intermittent model family.

### Global / Panel Models

- [ ] **GLOB-01**: User can forecast a grouped panel with GlobalETS (cross-series learning) via the panel-aware forecast surface.
- [ ] **GLOB-02**: User can forecast a grouped panel with GlobalTheta.
- [ ] **GLOB-03**: User can forecast a grouped panel with GlobalCroston (intermittent panel).

### Classical Models

- [ ] **CLAS-01**: User can forecast conditional volatility with GARCH (`ts_forecast_by` method `'GARCH'`).
- [ ] **CLAS-02**: User can forecast with a Kalman-filter model (`ts_forecast_by` method `'Kalman'`).
- [ ] **CLAS-03**: User can produce multivariate forecasts with VAR via a dedicated multivariate function (`ts_forecast_var` / `_by`), accepting multiple value columns and returning per-variable forecasts.

## v2 Requirements

Deferred to a future milestone. Tracked, not in this roadmap.

### Anomaly Detection
- **ANOM-01**: Streaming anomaly detection (Mahalanobis / Parade / ZBank) — `anomaly` feature already compiled in.

### Hierarchical Reconciliation
- **HIER-01**: Coherent reconciliation (BottomUp / TopDown / MiddleOut / MinTrace variants).

### Forecastability / Triage
- **FCST-01**: Forecastability scoring + triage (AMI, GCMI, transfer entropy, Lyapunov, STI, `run_triage`) — requires enabling the `forecastability` crate feature.

### Exogenous-Regression Track
- **REGR-01**: Regression forecasters (Linear/Ridge/Auto/Polynomial) with multicollinearity/VIF diagnostics.
- **TRAN-01**: Power transforms (Box-Cox / Yeo-Johnson + inverse) and scaling/rolling/EWM windows.

### Ensemble
- **ENSB-01**: AutoEnsemble / weighted model combination as a forecast method.

## Out of Scope

| Feature | Reason |
|---------|--------|
| Extra conformal methods (IDR, QRA, CQR, EnbPI, binned) | Existing conformal coverage sufficient for now |
| Extra changepoint algorithms (Binseg/BottomUp/Dynp/Window/KernelCpd) | PELT + BOCPD sufficient for now |
| Outlier detection (`detect_outliers`) | Not requested for this milestone |
| Model persistence (save/load fitted models) | Deferred; conformal learn/apply covers the immediate reuse need |
| Feature selection (`features::selection`) | Deferred with the regression track |
| Enabling `forecastability` / `seasonal-detection` crate features | Not needed for v1 scope; adds build-surface risk |

## Traceability

Populated during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| STAT-01 | TBD | Pending |
| STAT-02 | TBD | Pending |
| STAT-03 | TBD | Pending |
| RESID-01 | TBD | Pending |
| RESID-02 | TBD | Pending |
| RESID-03 | TBD | Pending |
| RESID-04 | TBD | Pending |
| INTER-01 | TBD | Pending |
| GLOB-01 | TBD | Pending |
| GLOB-02 | TBD | Pending |
| GLOB-03 | TBD | Pending |
| CLAS-01 | TBD | Pending |
| CLAS-02 | TBD | Pending |
| CLAS-03 | TBD | Pending |

**Coverage:**
- v1 requirements: 14 total
- Mapped to phases: 0 (pending roadmap)
- Unmapped: 14 ⚠️

---
*Requirements defined: 2026-08-21*
*Last updated: 2026-08-21 after initial definition*
