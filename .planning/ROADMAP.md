# Roadmap: anofox-forecast

## Milestones

- ✅ **v0.7.0 — Close the Crate→Extension Gap (Diagnostics + Model Coverage)** — Phases 1-3 (shipped 2026-08-22)
- ✅ **v0.8.0 — Ensemble Forecasting** — Phases 4-6 (shipped 2026-08-31)
- 🔨 **v0.9.0 — WASM Runtime Verification** — Phases 7-8 (in progress)

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

### v0.9.0 — WASM Runtime Verification (Phases 7-8)

- [ ] **Phase 7: WASM Node Harness + Local Green** — Boot DuckDB-Wasm, LOAD the built `.wasm`, run the full `test/sql` suite green locally (WASM-01, WASM-02, WASM-03, DEP-01, DEP-02)
- [ ] **Phase 8: CI Gating + Dedicated Workflow + Badge** — Gate the harness in CI so WASM regressions fail the build, with an independently observable workflow + README badge (CI-01, CI-02, CI-03)

## Phase Details

### Phase 7: WASM Node Harness + Local Green
**Goal**: A developer can run one command locally that boots DuckDB-Wasm, loads the freshly-built `anofox_forecast.duckdb_extension.wasm`, and runs the entire `test/sql` suite green — with the engine version pinned to match the built DuckDB and OpenSSL no longer compiled for the WASM target.
**Depends on**: Nothing (first phase of this milestone; ports anofox-statistics PR #131)
**Requirements**: WASM-01, WASM-02, WASM-03, DEP-01, DEP-02
**Success Criteria** (what must be TRUE):
  1. Running the harness (`node test/wasm/run.mjs` or equivalent) boots DuckDB-Wasm on the `eh` bundle with `pthreadWorker=null` and `web-worker@1.2.0` pinned, serves the built `.wasm` over localhost via a version-agnostic `<version>/<platform>/` path, and `FORCE INSTALL` + `LOAD`s it with no load error.
  2. The sqllogictest-subset runner executes a `.test` file end-to-end: it re-opens the DB and re-`LOAD`s the extension per file for catalog isolation, and results formatted through `::VARCHAR` match native sqllogictest output for DECIMAL-bearing queries (no unscaled-integer mismatch).
  3. The full 66-file `test/sql/**/*.test` suite passes against the built `.wasm`, and every test that is genuinely infeasible on WASM appears in an explicit skip-list with a documented per-entry reason.
  4. `@duckdb/duckdb-wasm` and `web-worker@1.2.0` are pinned to versions whose engine ABI matches the built DuckDB version, and the procedure to verify/update that version match is documented.
  5. `vcpkg.json` declares `openssl` as a `!wasm32` dependency, and a WASM build confirms Emscripten no longer compiles OpenSSL for the WASM target (native builds still link it).
**Plans**: 1 plan
- [ ] 07-01-wasm-node-harness-PLAN.md — Port the DuckDB-Wasm Node harness (run.mjs/sqllogic.mjs/package.json/README), apply the openssl !wasm32 vcpkg guard, and run the full 66-file test/sql suite green against the rebuilt .wasm

### Phase 8: CI Gating + Dedicated Workflow + Badge
**Goal**: WASM load/runtime regressions fail CI automatically, and WASM health is independently observable via a dedicated workflow and a README badge — the local green from Phase 7 is now enforced on every relevant build.
**Depends on**: Phase 7 (the harness must exist and pass locally before CI can gate on it)
**Requirements**: CI-01, CI-02, CI-03
**Success Criteria** (what must be TRUE):
  1. A gating CI job `needs:` the WASM build, downloads the `wasm_eh` artifact, runs the harness, and turns the build red on any WASM load or runtime error (verified by an intentionally broken `.wasm` / failing test producing a red run).
  2. A dedicated WASM workflow — separate from the main distribution pipeline — runs the harness so WASM status is observable on its own without inspecting the full release pipeline.
  3. The README shows a WASM status badge wired to the dedicated WASM workflow that flips between passing and failing in step with that workflow's latest run.
**Plans**: TBD

## Progress

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 7. WASM Node Harness + Local Green | 0/1 | Not started | - |
| 8. CI Gating + Dedicated Workflow + Badge | 0/TBD | Not started | - |

## Next

Plan Phase 7 — run `/gsd-plan-phase 7`.
