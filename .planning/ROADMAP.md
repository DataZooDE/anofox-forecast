# Roadmap: anofox-forecast

## Milestones

- ✅ **v0.6.0 — Close the Crate→Extension Gap (Diagnostics + Model Coverage)** — Phases 1-3 (shipped 2026-08-22)

## Phases

<details>
<summary>✅ v0.6.0 — Diagnostics + Model Coverage (Phases 1-3) — SHIPPED 2026-08-22</summary>

Full detail: [milestones/v0.6.0-ROADMAP.md](milestones/v0.6.0-ROADMAP.md) · Requirements: [milestones/v0.6.0-REQUIREMENTS.md](milestones/v0.6.0-REQUIREMENTS.md) · Audit: [milestones/v0.6.0-MILESTONE-AUDIT.md](milestones/v0.6.0-MILESTONE-AUDIT.md)

- [x] Phase 1: Statistical Diagnostics (3/3 plans) — completed 2026-08-21
      `ts_adf(_by)`, `ts_kpss(_by)`, `ts_stationarity(_by)`, `ts_ljung_box_by`, `ts_durbin_watson_by`, `ts_jarque_bera_by`, `ts_residual_diagnostics_by` (STAT-01..03, RESID-01..04)
- [x] Phase 2: Global / Panel Models (3/3 plans) — completed 2026-08-21
      `ts_forecast_panel_by` — GlobalETS / GlobalTheta / GlobalCroston, statsforecast M4 parity (GLOB-01..03)
- [x] Phase 3: Classical & Multivariate Models (3/3 plans) — completed 2026-08-22
      `ts_forecast_by` methods `'GARCH'` / `'Kalman'`, and multivariate `ts_forecast_var_by` (VAR) (CLAS-01..03)

INTER-01 (intermittent-demand classification) descoped — user has a more advanced approach TBD.

</details>

## Next

No active milestone. Start the next one with `/gsd-new-milestone`.
