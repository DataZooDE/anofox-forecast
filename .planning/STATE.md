---
gsd_state_version: 1.0
milestone: v0.8.0
milestone_name: Ensemble Forecasting
status: ready_to_plan
last_updated: "2026-08-30T20:10:00.000Z"
last_activity: 2026-08-30
progress:
  total_phases: 3
  completed_phases: 0
  total_plans: 0
  completed_plans: 0
  percent: 0
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-08-30 — started v0.8.0 Ensemble Forecasting)

**Core value:** SQL users can combine multiple forecasting models per series — automatically or explicitly — with distribution-free prediction intervals and weight introspection, all without leaving DuckDB.
**Current focus:** Phase 4 — AutoEnsemble Surface + Combination Methods (ready to plan)

## Current Position

Phase: 4 of 6 (AutoEnsemble Surface + Combination Methods)
Plan: — (not yet planned)
Status: Ready to plan
Last activity: 2026-08-30 — Roadmap created for v0.8.0 (Phases 4-6), 8/8 requirements mapped

Progress: [░░░░░░░░░░] 0%

## Performance Metrics

**Velocity:**

- Total plans completed: 0 (this milestone)
- Average duration: -
- Total execution time: 0 hours

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 4. AutoEnsemble Surface + Combination Methods | 0/TBD | - | - |
| 5. Explicit-Member Ensemble | 0/TBD | - | - |
| 6. Ensemble Intervals & Introspection | 0/TBD | - | - |

**Recent Trend:**

- Last 5 plans: none yet this milestone
- Trend: -

*Updated after each plan completion*

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- Roadmap (v0.8.0): AutoEnsemble first (ENS-01, lower-risk `ts_forecast_by` method-string dispatch like GARCH/Kalman) carries the shared combination-method plumbing (COMB-01..04); explicit-member ensemble second (ENS-02, new member-list macro); intervals + introspection last (EPI-01, INSP-01) built on top of the ensemble surface.
- EPI-01 routes through the existing conformal path (split/adaptive/asymmetric/per-step, learn+apply) — reuse, do not build new interval machinery.
- DoD: runnable verified example + internal-consistency cross-check (combined == manual weighted combination of members) + docs + clean-machine/WASM load. No external reference library for ensembles.
- Panel/table-in macro convention (v0.7.0 lesson): table-in macros must wrap `query_table(...)` in a subselect, not pass a bare TABLE arg.

### Pending Todos

None yet.

### Blockers/Concerns

- None. Signature design (method-string vs dedicated macro for ENS-02) is a plan-phase concern, not a blocker.

## Deferred Items

Items acknowledged and deferred at milestone close, most recent first:

| Category | Item | Status | Deferred At | Milestone |
|----------|------|--------|-------------|-----------|
| ENS (future) | Custom hand-supplied combination weights (ENS-F1) | Deferred | 2026-08-30 | v0.8.0 |
| ENS (future) | Panel/multivariate ensembling (ENS-F2) | Deferred | 2026-08-30 | v0.8.0 |

## Session Continuity

**Resume file:** None

Last session: 2026-08-30T20:10:00.000Z
Stopped at: Roadmap created for v0.8.0 (Phases 4-6); REQUIREMENTS traceability filled (8/8 mapped).
Resume: /gsd-plan-phase 4 to plan the AutoEnsemble surface + combination methods.

## Operator Next Steps

- Plan the first phase with /gsd-plan-phase 4
