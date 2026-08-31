---
gsd_state_version: 1.0
milestone: v0.8.0
milestone_name: Ensemble Forecasting
status: Awaiting next milestone
stopped_at: Phase 06 complete — all phases complete
last_updated: "2026-08-31T18:38:24.447Z"
last_activity: 2026-08-31
last_activity_desc: Milestone v0.8.0 completed and archived
state_head: 4e26afbbca9a43ebf6b930640005e3c55d5c841d
progress:
  total_phases: 3
  completed_phases: 3
  total_plans: 6
  completed_plans: 6
  percent: 100
current_phase: 06
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-08-30 — started v0.8.0 Ensemble Forecasting)

**Core value:** SQL users can combine multiple forecasting models per series — automatically or explicitly — with distribution-free prediction intervals and weight introspection, all without leaving DuckDB.
**Current focus:** Planning next milestone (v0.8.0 shipped 2026-08-31) — run /gsd-new-milestone

## Current Position

Phase: Milestone v0.8.0 complete
Plan: —
Status: Awaiting next milestone
Last activity: 2026-08-31 — Milestone v0.8.0 completed and archived

## Performance Metrics

**Velocity:**

- Total plans completed: 6 (this milestone)
- Average duration: -
- Total execution time: 0 hours

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 4. AutoEnsemble Surface + Combination Methods | 0/TBD | - | - |
| 5. Explicit-Member Ensemble | 0/TBD | - | - |
| 6. Ensemble Intervals & Introspection | 0/TBD | - | - |
| 04 | 2 | - | - |
| 05 | 2 | - | - |
| 06 | 2 | - | - |

**Recent Trend:**

- Last 5 plans: none yet this milestone
- Trend: -

*Updated after each plan completion*
**Per-Plan Metrics:**

| Plan | Duration | Tasks | Files |
|------|----------|-------|-------|
| Phase 04-autoensemble-surface-combination-methods P01 | 571 | 3 tasks | 7 files |
| Phase 04 P02 | 2084 | 2 tasks | 3 files |
| Phase 05-explicit-member-ensemble P01 | 811 | 3 tasks | 10 files |
| Phase 05-explicit-member-ensemble P02 | 362 | 2 tasks | 3 files |
| Phase 06-ensemble-intervals-introspection P01 | 572 | 3 tasks | 10 files |
| Phase 06 P02 | 35 | 3 tasks | 5 files |

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- Roadmap (v0.8.0): AutoEnsemble first (ENS-01, lower-risk `ts_forecast_by` method-string dispatch like GARCH/Kalman) carries the shared combination-method plumbing (COMB-01..04); explicit-member ensemble second (ENS-02, new member-list macro); intervals + introspection last (EPI-01, INSP-01) built on top of the ensemble surface.
- EPI-01 routes through the existing conformal path (split/adaptive/asymmetric/per-step, learn+apply) — reuse, do not build new interval machinery.
- DoD: runnable verified example + internal-consistency cross-check (combined == manual weighted combination of members) + docs + clean-machine/WASM load. No external reference library for ensembles.
- Panel/table-in macro convention (v0.7.0 lesson): table-in macros must wrap `query_table(...)` in a subselect, not pass a bare TABLE arg.
- [Phase 04]: AutoEnsemble default combination_method is Mean (overrides crate WeightedMSE default); empty string maps to Mean at parse_combination_method
- [Phase 04]: CombinationMethod imported via public re-export anofox_forecast::models::ensemble::CombinationMethod (model submodule is private)
- [Phase 04]: Plan 02 adds ZERO source wiring — parse_combination_method (Plan 01) already accepts all six methods; this plan exercises and documents them
- [Phase 04]: Mean vs Median demonstrability: skewed series (exp growth + spikes) shows delta 1.45-2.69 per step, confirming COMB-01 requirement
- [Phase 05]: ScalarFunction dispatch for _ts_forecast_ensemble_native (not TableFunction) — matches ts_forecast_by macro's unnest pattern and avoids streaming table function overhead
- [Phase 05]: build_forecaster: exhaustive 36-variant ModelType match with 10 blocked variants returning InvalidParameter — foundation for Phase 05-02 full allowlist
- [Phase 05]: Null-delimited member buffer (members_buf + members_buf_len) for FFI marshal — avoids over-read, defensive members_count assertion
- [Phase 05]: Section 4 error demonstrations placed at END of example file — no in-script error-capture idiom; sections 1-3 run clean as a pipe
- [Phase 05]: PR #230 rule enforced — all 7 SQL snippets in reference doc and API entry run through built extension before committing
- [Phase 06]: Two-function INSP-01 design: ts_ensemble_inspect_by (explicit-member) + ts_auto_ensemble_inspect_by (AutoEnsemble) — inputs differ materially
- [Phase 06]: NULL weights pointer convention signals absent weight column to C++ (AutoEnsemble non-Mean); rank CTE derived in macro via ROW_NUMBER
- [Phase 06]: EPI-01: _ts_forecast_scalar used in manual fold loop instead of ts_cv_forecast_by for AutoEnsemble (crash workaround)
- [Phase 06]: ts_conformal_calibrate uses MAP syntax not STRUCT to avoid json extension dependency

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

Last session: 2026-08-31T13:36:55.019Z
Stopped at: Phase 06 complete — all phases complete
Resume: /gsd-plan-phase 4 to plan the AutoEnsemble surface + combination methods.

## Operator Next Steps

- Start the next milestone with /gsd-new-milestone
