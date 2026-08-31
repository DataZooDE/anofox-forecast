# Roadmap: anofox-forecast

## Milestones

- ✅ **v0.7.0 — Close the Crate→Extension Gap (Diagnostics + Model Coverage)** — Phases 1-3 (shipped 2026-08-22)
- ✅ **v0.8.0 — Ensemble Forecasting** — Phases 4-6 (shipped 2026-08-31)

## Phases

<details>
<summary>✅ v0.7.0 — Diagnostics + Model Coverage (Phases 1-3) — SHIPPED 2026-08-22</summary>

Full detail: [milestones/v0.7.0-ROADMAP.md](milestones/v0.7.0-ROADMAP.md) · Requirements: [milestones/v0.7.0-REQUIREMENTS.md](milestones/v0.7.0-REQUIREMENTS.md) · Audit: [milestones/v0.7.0-MILESTONE-AUDIT.md](milestones/v0.7.0-MILESTONE-AUDIT.md)

- [x] Phase 1: Statistical Diagnostics (3/3 plans) — completed 2026-08-21
      `ts_adf(_by)`, `ts_kpss(_by)`, `ts_stationarity(_by)`, `ts_ljung_box_by`, `ts_durbin_watson_by`, `ts_jarque_bera_by`, `ts_residual_diagnostics_by` (STAT-01..03, RESID-01..04)

- [x] Phase 2: Global / Panel Models (3/3 plans) — completed 2026-08-21
      `ts_forecast_panel_by` — GlobalETS / GlobalTheta / GlobalCroston, statsforecast M4 parity (GLOB-01..03)

- [x] Phase 3: Classical & Multivariate Models (3/3 plans) — completed 2026-08-22
      `ts_forecast_by` methods `'GARCH'` / `'Kalman'`, and multivariate `ts_forecast_var_by` (VAR) (CLAS-01..03)

INTER-01 (intermittent-demand classification) descoped — user has a more advanced approach TBD.

</details>

<details>
<summary>✅ v0.8.0 — Ensemble Forecasting (Phases 4-6) — SHIPPED 2026-08-31</summary>

Full detail: [milestones/v0.8.0-ROADMAP.md](milestones/v0.8.0-ROADMAP.md) · Requirements: [milestones/v0.8.0-REQUIREMENTS.md](milestones/v0.8.0-REQUIREMENTS.md) · Audit: [milestones/v0.8.0-MILESTONE-AUDIT.md](milestones/v0.8.0-MILESTONE-AUDIT.md)

- [x] Phase 4: AutoEnsemble Surface + Combination Methods (2/2 plans) — completed 2026-08-30
      `ts_forecast_by(..., 'AutoEnsemble', ..., {top_k, combination_method, seasonal_period})` + six combination methods (Mean/Median/WeightedMSE/InverseAIC/Stacking/HorizonAdaptive) (ENS-01, COMB-01..04)

- [x] Phase 5: Explicit-Member Ensemble (2/2 plans) — completed 2026-08-31
      `ts_forecast_ensemble_by('table', grp, ds, y, members VARCHAR[], ...)` — user-named members + `build_forecaster` factory (26-member allowlist) (ENS-02)

- [x] Phase 6: Ensemble Intervals & Introspection (2/2 plans) — completed 2026-08-31
      Conformal intervals on ensembles via the existing path (EPI-01) + `ts_ensemble_inspect_by` / `ts_auto_ensemble_inspect_by` member/weight introspection (INSP-01)

Tech debt carried forward: `ts_cv_forecast_by('AutoEnsemble')` segfaults (crate/CV-native bug, worked around via manual per-fold loop); AutoEnsemble non-Mean combination weights return NULL (crate 0.15.3 exposes no inner-weight accessor); `build_forecaster` SeasonalWindowAverage `n_seasons=2` hardcoded (TODO ENS-03). See milestones/v0.8.0-MILESTONE-AUDIT.md.

</details>

## Next

v0.8.0 shipped. Planning next milestone — run `/gsd-new-milestone`.
