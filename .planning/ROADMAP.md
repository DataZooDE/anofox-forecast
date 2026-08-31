# Roadmap: anofox-forecast

## Milestones

- ✅ **v0.7.0 — Close the Crate→Extension Gap (Diagnostics + Model Coverage)** — Phases 1-3 (shipped 2026-08-22)
- 🚧 **v0.8.0 — Ensemble Forecasting** — Phases 4-6 (active)

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

### v0.8.0 — Ensemble Forecasting (Phases 4-6)

- [x] **Phase 4: AutoEnsemble Surface + Combination Methods** - Per-series AutoEnsemble (top-K across ARIMA/ETS/Theta) with all six combination methods exposed (completed 2026-08-30)
- [x] **Phase 5: Explicit-Member Ensemble** - User names member models + a combination method; extension fits and combines each (completed 2026-08-31)
- [ ] **Phase 6: Ensemble Intervals & Introspection** - Distribution-free conformal intervals on ensembles + selected-members/weights inspection

## Phase Details

### Phase 4: AutoEnsemble Surface + Combination Methods

**Goal**: SQL users can produce an AutoEnsemble forecast per series (crate auto-fits ARIMA/ETS/Theta, ranks by error, combines top-K) and choose any of the six combination methods that govern how members are blended.
**Depends on**: Phase 3 (existing `ts_forecast_by` per-series method-string dispatch — the lower-risk delivery vehicle used for GARCH/Kalman in v0.7.0)
**Requirements**: ENS-01, COMB-01, COMB-02, COMB-03, COMB-04
**Success Criteria** (what must be TRUE):

  1. A user can run an AutoEnsemble forecast per series setting `top_k`, `combination_method`, and `seasonal_period`, and get one blended forecast row-set per series back.
  2. A user can select `Mean` or `Median` combination and see the point forecast change accordingly (COMB-01).
  3. A user can select `WeightedMSE` or `InverseAIC` combination and see error/information-weighted blending applied (COMB-02).
  4. A user can select `Stacking` (learned non-negative holdout weights) or `HorizonAdaptive` (per-step rolling-origin weights) and get a valid forecast (COMB-03, COMB-04).
  5. For a fixed `Mean` combination, the ensemble point forecast equals the manual arithmetic mean of the individual member forecasts computed independently (internal-consistency cross-check), and a runnable `examples/*.sql` demonstrates it against the built extension.

**Plans**: 2/2 plans executed

- [x] 04-01-PLAN.md — Tracer: wire `'AutoEnsemble'` end-to-end (Rust core + FFI + C++) and demonstrate the Mean cross-check (ENS-01, COMB-01)
- [x] 04-02-PLAN.md — Expand to the other five combination methods + Mean-vs-Median demonstrability, and document the surface (COMB-01..04)

### Phase 5: Explicit-Member Ensemble

**Goal**: SQL users can name an explicit list of member models plus a combination method; the extension fits each named member per series and combines them, reusing the combination-method plumbing from Phase 4.
**Depends on**: Phase 4 (combination-method surface + FFI ensemble path)
**Requirements**: ENS-02
**Success Criteria** (what must be TRUE):

  1. A user can call an explicit-member ensemble macro (e.g. `ts_forecast_ensemble_by`) passing a member-model list and a `combination_method`, and get a per-series blended forecast back.
  2. The same six combination methods available to AutoEnsemble in Phase 4 apply to the explicit-member surface and produce the expected blend.
  3. For a fixed combination method, the explicit-member ensemble forecast equals the manual weighted combination of each named member's independently-computed forecast (internal-consistency cross-check).
  4. A runnable `examples/*.sql` exercises the explicit-member surface against the built extension and is verified.

**Plans**: 2/2 plans executed

- [x] 05-01-PLAN.md — Tracer: wire `ts_forecast_ensemble_by` end-to-end (build_forecaster factory + forecast_explicit_ensemble + anofox_ts_forecast_ensemble FFI + new C++ function + CMakeLists + macro) and prove the Mean cross-check (ENS-02)
- [x] 05-02-PLAN.md — Expand to the full 26-member allowlist + all six combination methods + error tests, and document the surface (ENS-02)

### Phase 6: Ensemble Intervals & Introspection

**Goal**: SQL users can attach distribution-free prediction intervals to an ensemble forecast through the existing conformal path, and inspect which member models were selected and their combination weights per series.
**Depends on**: Phase 4 and Phase 5 (an ensemble point-forecast surface must exist to wrap with intervals and to introspect)
**Requirements**: EPI-01, INSP-01
**Success Criteria** (what must be TRUE):

  1. A user can learn-then-apply conformal prediction intervals on an ensemble point forecast and get lower/upper bounds per horizon step, routed through the existing conformal machinery (EPI-01).
  2. A user can query which member models an ensemble selected, per series (INSP-01).
  3. A user can query the combination weight assigned to each selected member, per series, and the weights are consistent with the combination method chosen (INSP-01).
  4. A runnable `examples/*.sql` demonstrates both intervals and introspection against the built extension and is verified.

**Plans**: TBD

## Progress

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 4. AutoEnsemble Surface + Combination Methods | 2/2 | Complete    | 2026-08-30 |
| 5. Explicit-Member Ensemble | 2/2 | Complete    | 2026-08-31 |
| 6. Ensemble Intervals & Introspection | 0/TBD | Not started | - |

## Next

Active milestone: v0.8.0 — Ensemble Forecasting. Plan the first phase with `/gsd-plan-phase 4`.
