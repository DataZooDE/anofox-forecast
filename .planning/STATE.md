---
gsd_state_version: 1.0
current_phase: 01
current_phase_name: Statistical Diagnostics
status: executing
stopped_at: Completed 01-1-PLAN.md (STAT-01 ADF tracer)
last_updated: "2026-08-21T10:09:06.487Z"
last_activity: 2026-08-21
last_activity_desc: Phase 01 execution started
state_head: 3e5f2defd8b2a227f04deebf2cbab3b7e97aaf99
progress:
  total_phases: 3
  completed_phases: 0
  total_plans: 3
  completed_plans: 3
  percent: 33
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-08-21)

**Core value:** SQL users can validate whether a series/model is statistically sound (stationarity, residual adequacy, demand regime) and can reach the crate's higher-coverage models (global + classical) — all without leaving DuckDB.
**Current focus:** Phase 01 — Statistical Diagnostics

## Current Position

Phase: 01 (Statistical Diagnostics) — EXECUTING
Plan: 2 of 3
Status: Ready to execute
Last activity: 2026-08-21 — Phase 01 execution started

Progress: [███░░░░░░░] 33%

## Performance Metrics

**Velocity:**

- Total plans completed: 0
- Average duration: -
- Total execution time: 0 hours

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 1. Diagnostics & Demand Classification | 0/TBD | - | - |
| 2. Global / Panel Models | 0/TBD | - | - |
| 3. Classical & Multivariate Models | 0/TBD | - | - |

**Recent Trend:**

- Last 5 plans: none yet
- Trend: -

*Updated after each plan completion*
**Per-Plan Metrics:**

| Plan | Duration | Tasks | Files |
|------|----------|-------|-------|
| Phase 01-diagnostics-demand-classification P01-1 | 120 | 3 tasks | 16 files |

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- Roadmap: Diagnostics first (lower risk — scalar functions mirroring existing metrics), global/panel models second (new panel-aware SQL surface is design risk), VAR/multivariate last (new I/O shape requires dedicated function design)
- Diagnostics: Will be exposed as scalar functions + `ts_*_by` macros, mirroring the existing `ts_metrics_*` surface
- Global models: `ts_forecast_by` per-series dispatch is insufficient; panel-aware surface design must be settled in Phase 2 plan before implementation
- VAR: Dedicated multivariate function anticipated (`ts_forecast_var_by`); column-mapping API design deferred to Phase 3 plan
- [Phase 01]: Behavioral cross-check instead of exact numeric parity for ADF; statsmodels and anofox use different lag selection formulas
- [Phase 01]: CLI subprocess in run_anofox.py to avoid Python duckdb package version mismatch (venv v1.5.1 vs extension v1.5.4)

### Pending Todos

None yet.

### Blockers/Concerns

- Phase 2: Panel-aware SQL surface for GlobalETS/Theta/Croston needs design decision before coding starts — flag in Phase 2 plan
- Phase 3: VAR multivariate I/O shape (N input columns → N forecast columns) is novel; dedicated function design required

### Execution Notes (Phase 1)

- **statsmodels cross-check must use the benchmark uv venv, NOT system python3.** `statsmodels 0.14.5` + `scipy 1.15.3` live in `benchmark/.venv` (transitive via tsfresh); system `python3` lacks them. Run all `benchmark/diagnostics/*.py` cross-check scripts with `benchmark/.venv/bin/python` (or `cd benchmark && uv run python ...`). Plans' verify commands that say `python3 benchmark/diagnostics/...` should be adapted to `benchmark/.venv/bin/python benchmark/diagnostics/...`.

## Deferred Items

Items acknowledged and deferred at milestone close, most recent first:

| Category | Item | Status | Deferred At | Milestone |
|----------|------|--------|-------------|-----------|
| *(none)* | | | | |

## Session Continuity

Last session: 2026-08-21T10:09:06.472Z
Stopped at: Completed 01-1-PLAN.md (STAT-01 ADF tracer)
Resume file: None
