---
gsd_state_version: "1.0"
milestone: v0.10.0
milestone_name: Statistical Correctness
status: planning
last_updated: "2026-10-08T21:01:41.000Z"
last_activity: 2026-10-08
progress:
  total_phases: 10
  completed_phases: 0
  total_plans: 0
  completed_plans: 0
  percent: 0
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-10-08 — started v0.10.0 Statistical Correctness)

**Core value:** Every statistic the extension returns is correct — verified against R / scipy / statsmodels / tsfresh reference values — and is computed in the Rust crates; C++ and SQL only marshal, group, and dispatch.
**Current focus:** Phase 9 — Reference Fixtures & Safe Dependency Bumps

## Current Position

Phase: 9 of 18 (Reference Fixtures & Safe Dependency Bumps) — 1st of 10 phases in v0.10.0 (Phases 9-18)
Plan: Not started
Status: Ready to plan
Last activity: 2026-10-08 — Roadmap created (10 phases, 65/65 v1 requirements mapped)

Progress: [░░░░░░░░░░] 0%

## Performance Metrics

**Velocity:**

- Total plans completed: 0 (this milestone)
- Average duration: -
- Total execution time: 0 hours

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| - | - | - | - |

*Updated after each plan completion*

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- Roadmap (v0.10.0): Phase 9 = fixture harness (TEST-01) + low-risk bumps (DEP-01/02/04); every later phase asserts values against its fixtures.
- Roadmap: DEP-03 (fdars 0.47) placed with WRAP-01 + DET-* in Phase 15 — the FdMatrix port rewrites the same call sites; DET-01 peak re-implementation lands before/with the bump.
- Roadmap: RUST-04 moved into Phase 16 (Conformal) — CONF-04 adaptive scaling and per-group calibration reshape the same calibrate/apply SQL signatures; design once.
- Roadmap: Phase 11 (upstream DIAG-01/02, UPST-01..05) runs in `~/projects/rust/anofox-forecast` on a fix branch, independent of Phases 12-16; Phase 17 consumes it (UPST-06, DIAG-03..05) and owns FCST-09 R parity (needs UPST-01..04).
- Granularity is coarse, but 65 requirements under the 4-10 req/phase sizing bound yields 10 phases.

### Pending Todos

None yet.

### Blockers/Concerns

- [Phase 11 → 17] Upstream release is a user action (review, merge, publish). Phase 17 may build on an interim `[patch]`, but UPST-06 cannot close until a released anofox-forecast version is consumed.
- [Phase 13/15] STL is shared: STAT-02 uses the crate `seasonality::stl::STL` — validate vs R `stats::stl` in Phase 13; DET-04 reuses or fixes it.
- [Phase 15] fdars 0.47 bump: ~30 mechanical compile errors in 4 files plus a WASM (`js` feature) build check — use the local wasm_eh recipe.
- [Phase 16] Jackknife+ needs leave-one-out refits, which may not fit the GROUP BY/scalar architecture; rename/remove is an allowed CONF-05 outcome.
- [All] Numerical outputs change broadly (intervals, MSE, MASE, strengths, features); existing SQL tests/docs examples will shift — track for TEST-03 / DOCS-02.
- [All] Verify executor reports against git + disk and run `cargo fmt --check` in the DoD (prior milestones: fabricated SUMMARYs, fmt-only CI failures).

## Deferred Verification

| Phase | State | Resume |
|-------|-------|--------|
| 8 (v0.9.0, parked) | verification_deferred_human | Operator pushes branch, observes `wasm-runtime-test` green → red → green + badge flip, then `/gsd-verify-work 8` and `/gsd-complete-milestone v0.9.0` (phase dirs in `milestones/v0.9.0-phases/`) |

## Deferred Items

Items acknowledged and deferred at milestone close, most recent first:

| Category | Item | Status | Deferred At | Milestone |
|----------|------|--------|-------------|-----------|
| WASM (future) | Browser-based (not just Node) WASM E2E harness (WASM-F1) | Deferred | 2026-09-01 | v0.9.0 |
| WASM (future) | Shared-memory `wasm_threads` build — blocked upstream (WASM-F2) | Deferred | 2026-09-01 | v0.9.0 |
| ENS (future) | Custom hand-supplied combination weights (ENS-F1) | Deferred | 2026-08-30 | v0.8.0 |
| ENS (future) | Panel/multivariate ensembling (ENS-F2) | Deferred | 2026-08-30 | v0.8.0 |

## Session Continuity

Last session: 2026-10-08T21:01:41.000Z
Stopped at: v0.10.0 roadmap created (ROADMAP.md, STATE.md, REQUIREMENTS.md traceability)
Resume file: None
Next: `/gsd-plan-phase 9`
