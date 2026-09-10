# Roadmap: anofox-forecast

## Milestones

- ✅ **v0.7.0 — Close the Crate→Extension Gap (Diagnostics + Model Coverage)** — Phases 1-3 (shipped 2026-08-22)
- ✅ **v0.8.0 — Ensemble Forecasting** — Phases 4-6 (shipped 2026-08-31)
- ✅ **v0.9.0 — WASM Runtime Verification** — Phases 7-8 (shipped 2026-09-10)

## Phases

<details>
<summary>✅ v0.7.0 — Diagnostics + Model Coverage (Phases 1-3) — SHIPPED 2026-08-22</summary>

Full detail: [milestones/v0.7.0-ROADMAP.md](milestones/v0.7.0-ROADMAP.md) · Requirements: [milestones/v0.7.0-REQUIREMENTS.md](milestones/v0.7.0-REQUIREMENTS.md) · Audit: [milestones/v0.7.0-MILESTONE-AUDIT.md](milestones/v0.7.0-MILESTONE-AUDIT.md)

- [x] Phase 1: Statistical Diagnostics (3/3 plans) — completed 2026-08-21
- [x] Phase 2: Global / Panel Models (3/3 plans) — completed 2026-08-21
- [x] Phase 3: Classical & Multivariate Models (3/3 plans) — completed 2026-08-22

</details>

<details>
<summary>✅ v0.8.0 — Ensemble Forecasting (Phases 4-6) — SHIPPED 2026-08-31</summary>

Full detail: [milestones/v0.8.0-ROADMAP.md](milestones/v0.8.0-ROADMAP.md) · Requirements: [milestones/v0.8.0-REQUIREMENTS.md](milestones/v0.8.0-REQUIREMENTS.md) · Audit: [milestones/v0.8.0-MILESTONE-AUDIT.md](milestones/v0.8.0-MILESTONE-AUDIT.md)

- [x] Phase 4: AutoEnsemble Surface + Combination Methods (2/2 plans) — completed 2026-08-30
- [x] Phase 5: Explicit-Member Ensemble (2/2 plans) — completed 2026-08-31
- [x] Phase 6: Ensemble Intervals & Introspection (2/2 plans) — completed 2026-08-31

</details>

<details>
<summary>✅ v0.9.0 — WASM Runtime Verification (Phases 7-8) — SHIPPED 2026-09-10</summary>

Full detail: [milestones/v0.9.0-ROADMAP.md](milestones/v0.9.0-ROADMAP.md) · Requirements: [milestones/v0.9.0-REQUIREMENTS.md](milestones/v0.9.0-REQUIREMENTS.md) · Audit: [milestones/v0.9.0-MILESTONE-AUDIT.md](milestones/v0.9.0-MILESTONE-AUDIT.md)

- [x] Phase 7: WASM Node Harness + Local Green (1/1 plans) — completed 2026-09-02
      Node harness boots DuckDB-Wasm, `LOAD`s the built `.wasm`, runs the curated `test/sql` subset green; `@duckdb/duckdb-wasm` ABI-pinned; `openssl` made `!wasm32` (WASM-01/02/03, DEP-01/02)
- [x] Phase 8: CI Gating + Dedicated Workflow + Badge (1/1 plans) — completed 2026-09-10
      `wasm-runtime-test` gating job + dedicated `WasmTest.yml` + README badge; gate proven green→red→green locally, live-CI observation waived on local evidence (CI-01/02/03)

Tech debt carried forward: full `test/sql` `--all` suite has 188 pre-existing test-debt failures / 23 files (out-of-scope triage; CI gates the curated subset by design); Phase 8 live-CI negative-control confirmation pending on next push to `main`. See milestones/v0.9.0-MILESTONE-AUDIT.md.

</details>
