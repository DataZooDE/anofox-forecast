---
gsd_state_version: 1.0
current_phase: 02
current_phase_name: Global / Panel Models
status: executing
stopped_at: Completed 02-global-panel-models plan 1 (GlobalETS tracer)
last_updated: "2026-08-21T19:46:19.919Z"
last_activity: 2026-08-21
last_activity_desc: Phase 02 execution started
state_head: 559ea2f3cc539880f3f4727293d0142b038b1926
progress:
  total_phases: 3
  completed_phases: 0
  total_plans: 6
  completed_plans: 4
  percent: 0
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-08-21)

**Core value:** SQL users can validate whether a series/model is statistically sound (stationarity, residual adequacy, demand regime) and can reach the crate's higher-coverage models (global + classical) — all without leaving DuckDB.
**Current focus:** Phase 02 — Global / Panel Models

## Current Position

Phase: 02 (Global / Panel Models) — EXECUTING
Plan: 2 of 3
Status: Ready to execute
Last activity: 2026-08-21 — Phase 02 execution started

Progress: [░░░░░░░░░░] 0%

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
| Phase 02-global-panel-models P1 | 90 | 3 tasks | 11 files |

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
- [Phase 02]: GlobalAutoETS safe_period=1 for seasonal_period=0: prevents t%period panic, has_seasonal=false means non-seasonal candidates only
- [Phase 02]: PanelForecastError wrapper for dual-crate FFI boundary: anofox_forecast::ForecastError != anofox_fcst_core::ForecastError, no From impl cross-crate
- [Phase 02]: Subselect TABLE arg pattern in macros: query_table() direct as TABLE arg silently fails macro registration; use (SELECT ... FROM query_table(...)) instead

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

**Resume file:** None

Last session: 2026-08-21T19:46:19.908Z
Stopped at: Completed 02-global-panel-models plan 1 (GlobalETS tracer)
Resume: /gsd-autonomous --from 2  (Phase 2 needs a panel-aware SQL surface design — discuss first). Note: set workflow.use_worktrees=false to avoid worktree split-brain.
