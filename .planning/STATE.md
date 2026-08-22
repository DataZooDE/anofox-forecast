---
gsd_state_version: 1.0
status: Awaiting next milestone
stopped_at: Phase 01 complete, ready to plan Phase 02
last_updated: "2026-08-22T14:10:15.749Z"
last_activity: 2026-08-22
last_activity_desc: Milestone v0.6.0 completed and archived
state_head: 225595f6416012451d58ffb2a91bf19cf37ce997
progress:
  total_phases: 3
  completed_phases: 3
  total_plans: 9
  completed_plans: 9
  percent: 100
current_phase: 02
current_phase_name: Global / Panel Models
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-08-21)

**Core value:** SQL users can validate whether a series/model is statistically sound (stationarity, residual adequacy, demand regime) and can reach the crate's higher-coverage models (global + classical) — all without leaving DuckDB.
**Current focus:** Phase 03 — Classical & Multivariate Models

## Current Position

Phase: Milestone v0.6.0 complete
Plan: —
Status: Awaiting next milestone
Last activity: 2026-08-22 — Milestone v0.6.0 completed and archived

## Performance Metrics

**Velocity:**

- Total plans completed: 9
- Average duration: -
- Total execution time: 0 hours

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 1. Diagnostics & Demand Classification | 0/TBD | - | - |
| 2. Global / Panel Models | 0/TBD | - | - |
| 3. Classical & Multivariate Models | 0/TBD | - | - |
| 02 | 3 | - | - |
| 03 | 3 | - | - |
| 01 | 3 | - | - |

**Recent Trend:**

- Last 5 plans: none yet
- Trend: -

*Updated after each plan completion*
**Per-Plan Metrics:**

| Plan | Duration | Tasks | Files |
|------|----------|-------|-------|
| Phase 01-diagnostics-demand-classification P01-1 | 120 | 3 tasks | 16 files |
| Phase 02-global-panel-models P1 | 90 | 3 tasks | 11 files |
| Phase 02 P2 | 17 min | 3 tasks | 7 files |
| Phase 02-global-panel-models P3 | 25 | 2 tasks | 10 files |
| Phase 03 P01 | 38 | 3 tasks | 6 files |
| Phase 03-classical-multivariate-models P02 | 11 min | 3 tasks | 10 files |
| Phase 03-classical-multivariate-models P03 | 9 min | 4 tasks | 30 files |

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
- [Phase 02]: Use GlobalCroston::new()/sba() constructors: CrostonVariant private type mismatch — global_croston::CrostonVariant ≠ croston::CrostonVariant, with_variant() fails at compile time
- [Phase 02]: Add variant_str param to forecast_panel_impl: threads Croston variant from FFI outer wrapper through testable inner function; all 8-arg call sites updated
- [Phase 02]: Fix model_name from hardcoded 'GlobalETS' to actual method string: 02-1 tracer hardcoded result; GlobalTheta/GlobalCroston now correctly self-name
- [Phase 02]: CLI subprocess for panel queries: build/release/duckdb -unsigned avoids venv duckdb v1.5.1 / extension v1.5.4 version mismatch
- [Phase 02]: Per-series date re-alignment: panel function aligns to shared grid; restore correct M4 horizon dates via forecast_step
- [Phase 02]: MAX_SERIES=500 for global panel benchmark: GlobalETS Reduced pool takes ~18s for 500 series vs ~6 min for all 4,227
- [Phase 02]: statsforecast reference: GlobalETS->AutoETS, GlobalTheta->AutoTheta, GlobalCroston->CrostonOptimized (pinned v1.4.0 has no Global* variants)
- [Phase 03]: GARCH output is sqrt(forecast_variance(h)) — volatility not variance; forecast_variance gives analytical conditional variance vs predict() which gives simulated innovations
- [Phase 03]: ts_forecast_by routes through _ts_forecast_scalar (scalar_functions/), not _ts_forecast_native (table_functions/); both files have independent ValidateParams
- [Phase 03]: Named param 'p' (not 'order') for VAR lag order — ORDER is a SQL reserved keyword causing parser error at macro registration time
- [Phase 03]: date_col passed as explicit 6th VARCHAR arg to _ts_forecast_var_native; Bind resolves by name (not identifier substitution in macro body)
- [Phase 03]: SELECT * in ts_forecast_var_by macro outer query — avoids referencing date column by its runtime string value in the static template
- [Phase 03]: VARForecastResult variable-major flat buffer: variable names stay in C++ BindData.value_col_names and are emitted at Finalize time, never crossing the FFI boundary
- [Phase 03]: v1 is single-panel VAR (no group_col): one VAR(p) fit for the entire input table; per-panel VAR deferred to v2
- [Phase 03]: arch path chosen for GARCH benchmark (arch 8.0.0 installed); VAR benchmark uses synthetic VAR(1) data since no multivariate M4 exists; both anofox and statsmodels use OLS → exact MAE parity ratio=1.000

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

Last session: 2026-08-21T22:41:27.804Z
Stopped at: Phase 01 complete, ready to plan Phase 02
Resume: /gsd-autonomous --from 2  (Phase 2 needs a panel-aware SQL surface design — discuss first). Note: set workflow.use_worktrees=false to avoid worktree split-brain.

## Operator Next Steps

- Start the next milestone with /gsd-new-milestone
