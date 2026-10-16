# Roadmap: anofox-forecast

## Milestones

- ✅ **v0.7.0 — Close the Crate→Extension Gap (Diagnostics + Model Coverage)** — Phases 1-3 (shipped 2026-08-22)
- ✅ **v0.8.0 — Ensemble Forecasting** — Phases 4-6 (shipped 2026-08-31)
- 🗄 **v0.9.0 — WASM Runtime Verification** — Phases 7-8 (parked; Phase 8 operator live-CI negative control outstanding)
- ✅ **v0.10.0 — Statistical Correctness** — Phases 9-18 (shipped 2026-10-16)

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

<details>
<summary>🗄 v0.9.0 — WASM Runtime Verification (Phases 7-8) — PARKED, phase dirs archived 2026-10-08</summary>

Phase artifacts (plans, summaries, verification): [milestones/v0.9.0-phases/](milestones/v0.9.0-phases/). Not yet closed via `/gsd-complete-milestone`.

- [x] Phase 7: WASM Node Harness + Local Green (1/1 plans) — completed 2026-09-02
      DuckDB-Wasm Node harness (`test/wasm/run.mjs` + `sqllogic.mjs`), `@duckdb/duckdb-wasm` engine-version pin, openssl `!wasm32` vcpkg guard (WASM-01, WASM-02, DEP-01, DEP-02). WASM-03 full-suite green deferred: 23 files fail on pre-existing `test/sql` API drift, not WASM.

- Phase 8: CI Gating + Dedicated Workflow + Badge (1/1 plans; verification deferred to operator)
      `wasm-runtime-test` gating job (curated subset, no `--all`), `WasmTest.yml`, README badge (CI-01..03) — implemented and locally verified (curated exit 0 → broken test exit 1 → reverted exit 0). Outstanding: push the branch, observe the gate green → red (deliberate curated break) → green in GitHub Actions, confirm the badge flips, record run URLs, then `/gsd-verify-work 8` and `/gsd-complete-milestone v0.9.0`.

</details>

<details>
<summary>✅ v0.10.0 — Statistical Correctness (Phases 9-18) — SHIPPED 2026-10-16</summary>

Full detail: [milestones/v0.10.0-ROADMAP.md](milestones/v0.10.0-ROADMAP.md) · Requirements: [milestones/v0.10.0-REQUIREMENTS.md](milestones/v0.10.0-REQUIREMENTS.md) · Audit: [milestones/v0.10.0-MILESTONE-AUDIT.md](milestones/v0.10.0-MILESTONE-AUDIT.md) · Phase artifacts: [milestones/v0.10.0-phases/](milestones/v0.10.0-phases/)

- [x] Phase 9: Reference Fixtures & Safe Dependency Bumps (3/3 plans) — completed 2026-10-09
- [x] Phase 10: Forecast Wrapper Delegates to Crate Models (10/10 plans) — completed 2026-10-09
- [x] Phase 11: Upstream anofox-forecast Correctness Fixes (11/11 plans + 11-12 addendum, upstream repo) — completed 2026-10-16
- [x] Phase 12: Statistics Marshalling Moves to Rust (8/8 plans) — completed 2026-10-10
- [x] Phase 13: Core Statistics, Metrics & Data Quality (4/4 plans) — completed 2026-10-11
- [x] Phase 14: tsfresh Feature Parity (4/4 plans) — completed 2026-10-12
- [x] Phase 15: Detection & Decomposition on fdars 0.47 (5/5 plans) — completed 2026-10-14
- [x] Phase 16: Conformal & Bootstrap Correctness (3/3 plans) — completed 2026-10-14
- [x] Phase 17: Upstream Consumption & Diagnostics Parity (3/3 plans) — completed 2026-10-15
- [x] Phase 18: Correctness Regression Suite, Audit & Docs (6/6 plans incl. gap plans 18-05 anofox-forecast 0.16.2, 18-06 ts_detect_periods fix) — completed 2026-10-16

65/65 requirements. Closed as an override closeout: gsd's staleness heuristic flagged all 10 phase verifications "stale" (all VERIFICATION.md status: passed; integration re-checked green at HEAD). 19 deferred items acknowledged (see STATE.md Deferred Items and the audit's tech_debt list).

</details>

## Next

Start the next milestone — `/gsd-new-milestone`.
