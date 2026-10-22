---
gsd_state_version: "1.0"
milestone: v0.11.0
milestone_name: "Issue Sweep: Contract, Bugs, Crate Fixes"
status: planning
last_updated: "2026-10-22T20:52:46.020Z"
last_activity: 2026-10-22
progress:
  total_phases: 0
  completed_phases: 0
  total_plans: 0
  completed_plans: 0
  percent: 0
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-10-16 — v0.10.0 Statistical Correctness shipped)

**Core value:** Every statistic the extension returns is correct — verified against R / scipy / statsmodels / tsfresh reference values — and is computed in the Rust crates; C++ and SQL only marshal, group, and dispatch.
**Current focus:** Planning next milestone (`/gsd-new-milestone`)

## Current Position

Phase: Not started (defining requirements)
Plan: —
Status: Defining requirements
Last activity: 2026-10-22 — Milestone v0.11.0 started

## Performance Metrics

**Velocity:**

- Total plans completed: 56 (this milestone)
- Average duration: -
- Total execution time: 0 hours

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 9 | 3 | - | - |
| 10 | 10 | - | - |
| 12 | 8 | - | - |
| 13 | 4 | - | - |
| 14 | 4 | - | - |
| 15 | 5 | - | - |
| 16 | 3 | - | - |
| 17 | 3 | - | - |
| 11 | 11 | - | - |
| 18 | 5 | - | - |

*Updated after each plan completion*
**Per-Plan Metrics:**

| Plan | Duration | Tasks | Files |
|------|----------|-------|-------|
| Phase 09 P01 | 15min | 3 tasks | 8 files |
| Phase 09 P02 | 25min | 3 tasks | 12 files |
| Phase 09 P03 | 50min | 3 tasks | 20 files |
| Phase 10 P01 | 75min | 3 tasks | 3 files |
| Phase 10 P02 | 120min | 3 tasks | 14 files |
| Phase 10 P03 | ~45min | 3 tasks | 8 files |
| Phase 10 P04 | ~3h | 3 tasks | 12 files |
| Phase 10 P05 | ~2h | 3 tasks | 5 files |
| Phase 10 P06 | ~3h | 3 tasks | 17 files |
| Phase 10 P07 | ~1h | 3 tasks | 19 files |
| Phase 10 P08 | ~1h | 3 tasks | 16 files |
| Phase 10 P09 | ~1h | 2 tasks | 25 files |
| Phase 10 P10 | 50min | 3 tasks | 15 files |
| Phase 11 P01 | ~20min | 2 tasks | 2 files |

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- Roadmap (v0.10.0): Phase 9 = fixture harness (TEST-01) + low-risk bumps (DEP-01/02/04); every later phase asserts values against its fixtures.
- Roadmap: DEP-03 (fdars 0.47) placed with WRAP-01 + DET-* in Phase 15 — the FdMatrix port rewrites the same call sites; DET-01 peak re-implementation lands before/with the bump.
- Roadmap: RUST-04 moved into Phase 16 (Conformal) — CONF-04 adaptive scaling and per-group calibration reshape the same calibrate/apply SQL signatures; design once.
- Roadmap: Phase 11 (upstream DIAG-01/02, UPST-01..05) runs in `~/projects/rust/anofox-forecast` on a fix branch, independent of Phases 12-16; Phase 17 consumes it (UPST-06, DIAG-03..05) and owns FCST-09 R parity (needs UPST-01..04).
- Granularity is coarse, but 65 requirements under the 4-10 req/phase sizing bound yields 10 phases.
- [Phase 9]: fc_block() indexes R forecast interval columns positionally (not by name) because thetaf() labels columns differently from every other model
- [Phase 9]: Package-legitimacy gate for 09-02/09-03 installs (7 packages) pre-approved by human 2026-10-09; recorded verbatim in 09-01-SUMMARY.md
- [Phase 9]: 09-02: multiscale_ols fixture draws all 8 cases from ONE shared rng(1) stream in a fixed order — verified computationally that only the full chained order reproduces the audit-pinned exog_typical coefficients; an isolated fresh-rng computation does not
- [Phase 9]: 09-02: constcol is rank-deficient (constant column collapses with intercept) alongside collinear/collinear_big — confirmed via lstsq rank for every case before writing full_rank/note fields
- [Phase 9]: Phase 9: bumped anofox-regression=0.5.15/anofox-forecast=0.15.10 clean (0 new Cargo.lock crates); MSRV corrected 1.86 -> true 1.89 (trueno AVX-512), enforced by cargo + new CI job — 125-entry pre-bump snapshot proved only MFLES/AutoMFLES/Laplace point forecasts moved; cargo +1.88.0 refusal + +1.89.0 success proved the real MSRV floor
- [Phase 10]: SeasonalNaive period<2 delegates to crate Naive (bit-identical point forecasts) rather than erroring on the crate's p>=2 minimum
- [Phase 10]: MSE computed from raw crate residuals (scalar) independent of the exported residuals array's strict length-match safety check, so AutoARIMA's differencing-shortened residuals still produce a real MSE
- [Phase 10]: FFI ForecastResult n_fitted/residuals shared-length bug documented but left unfixed — reserved for plan 10-02, which owns the generated C header
- [Phase 10]: [Phase 10 P02]: arima_order/seasonal_order encoded as NUL-terminated [c_char;32] FFI strings (mirrors seasonal_periods_str); seasonal_order=[0,0,0] is explicitly non-seasonal
- [Phase 10]: [Phase 10 P02]: Fixed pre-existing FFI n_fitted/residuals length-sharing bug (found in 10-01) with an additive ForecastResult.n_residuals field
- [Phase 10]: [Phase 10 P02]: FCST-03 SQL test documents two verified crate-internal coincidences (Theta/CrostonClassic both reduce to SES(0.1) in-sample; SES's real MSE ~2090.284 near the old 2090.28 fabrication) rather than loosening blindly
- [Phase 10]: AutoETS catch_unwind fallback deleted outright (InvalidInput naming AutoETS) after confirming anofox-forecast 0.15.10 no longer panics on constant series
- [Phase 10]: ForecastOptionsExog -> ForecastOptions conversion lets forecast_with_exog's no-regressor branch delegate to forecast() itself instead of maintaining a second hand-written dispatch
- [Phase 10]: build_forecaster gained an n_obs parameter so SeasonalWindowAverage ensemble members compute n_seasons adaptively instead of a hard-coded 2
- [Phase 10]: Deleted wrapper OLS exog path outright (fit_ols_regression/apply_regression/forecast_*_with_exog + ARIMAX/ThetaX/MFLESX names) since the crate's own exog support already does its own internal OLS pre-regression
- [Phase 10]: A regressor constant in history is dropped before the crate ever sees it (0 effect by construction), collapsing 'all regressors constant' onto the existing no-regressor forecast() delegation path
- [Phase 10]: [Phase 10 P05]: SeasonalES/SeasonalESOptimized/HoltWinters/MSTL/TBATS staleness in ts_model_distinctness.test traced entirely to 10-03's resolve_period change — both masked sections re-blessed together
- [Phase 10]: [Phase 10 P05]: native sqllogictest harness aborts the rest of a .test FILE on first unexpected failure, hiding later-in-file staleness until earlier failures are fixed and the file re-run
- [Phase 10]: Phase-local snippet harness forked from quick task 261009-d02 with an --out flag; --all surfaced and fixed ~50 pre-existing, Phase-10-unrelated doc bugs across 5 files (wrong column names, missing fixtures, stale macro arg counts)
- [Phase 10]: ensemble_explicit.sql's intentional Section-4 error-demo ending (pre-existing since Phase 5) left unrestructured; two ols_fit_predict_by regression-backtest bugs in the backtesting examples deferred as out-of-scope, unrelated to Phase 10
- [Phase 10]: [Phase 10 P07]: Each per-model page's Signature block kept as a SYNOPSIS entry (narrative placeholder identifiers), not rewritten to run against a real fixture — the sibling SQL Example block is what proves the model
- [Phase 10]: Fixed stale ts_detect_periods_by workaround references (known-NULL-bug macro) to ts_detect_periods_agg across the state-space/Theta/multi-seasonal reference pages
- [Phase 10]: [Phase 10 P09]: ensemble_explicit.md/ensemble_inspect.md's seasonal_period 0/1 are tested-identical (both auto-detect; only >1 wins) — corrected from the plan's assumed 'explicit 1 = non-seasonal' after direct verification against the built extension
- [Phase 10]: [Phase 10 P09]: Phase-wide sweep reworded 53 '(being corrected in v0.10.0, Phase N)' notes across 13 out-of-scope docs/skills files to '(tracked for Phase N)' per the plan's explicit sweep instruction
- [Phase 10]: ts_forecast_agg now validates/threads params via a shared ts_forecast_params.hpp header, provably identical to ts_forecast_by (FCST-06 closed)
- [Phase 10]: Theta/CrostonClassic identical fitted arrays confirmed as genuine anofox-forecast 0.15.10 behaviour (not a wrapper bug), pinned by a Rust test, routed to Phase 11 UPST-04
- [Phase 11]: D-01a confirmed verbatim — branch from 1ae5cfc (released v0.15.10, unmerged perf/v1.0-validation-hardening); opened sync PR #220 to main, not merged
- [Phase 11]: Pristine baseline on BASE_REF has 0 pre-existing test failures; one test-only clippy::approx_constant finding fixed and captured as 11-baseline-lint.patch (not committed upstream)

### Pending Todos

None yet.

- [Phase 11, 2026-10-16]: Deferred verification passed after user sign-off — ETS MAM optimum (α≈0.79/γ≈0.21, better loglik than R) accepted; TBATS width divergence vs statsforecast on 2/4 fixtures accepted + documented as known limitation; untracked 11-12 AutoARIMA work backfilled (11-12-ADDENDUM.md)
- [Phase 18, 2026-10-15]: User publishes anofox-forecast v0.16.2 (PR #230, R KalmanForecast ARIMA intervals); fold the bump into v0.10.0 as a Phase 18 gap item — RC-05 AutoARIMA AirPassengers interval flips from known gap to hard parity

### Blockers/Concerns

- [Next] DuckDB 2.0 compat port done on chore/duckdb2-compat (quick 261016-tjw, draft PR #277, CI run 38969012711 green on 1.4.5/1.5.6/2.0-dev). Merge + release still needed before community-extensions PR #3112 can pass (registry default build = DuckDB 2.0-dev).

- [Phase 13/15] STL is shared: STAT-02 uses the crate `seasonality::stl::STL` — validate vs R `stats::stl` in Phase 13; DET-04 reuses or fixes it.
- [Phase 15] fdars 0.47 bump: ~30 mechanical compile errors in 4 files plus a WASM (`js` feature) build check — use the local wasm_eh recipe.
- [Phase 16] Jackknife+ needs leave-one-out refits, which may not fit the GROUP BY/scalar architecture; rename/remove is an allowed CONF-05 outcome.
- [All] Numerical outputs change broadly (intervals, MSE, MASE, strengths, features); existing SQL tests/docs examples will shift — track for TEST-03 / DOCS-02.
- [All] Verify executor reports against git + disk and run `cargo fmt --check` in the DoD (prior milestones: fabricated SUMMARYs, fmt-only CI failures).

### Quick Tasks Completed

| # | Description | Date | Commit | Directory |
|---|-------------|------|--------|-----------|
| 261009-d02 | Docs/README/skills accuracy pass after Phase 9 | 2026-10-09 | 59e8e26 | [261009-d02-docs-readme-skills-accuracy-pass](./quick/261009-d02-docs-readme-skills-accuracy-pass/) |
| 261016-tjw | DuckDB 2.0-dev compat port (compat header, WASM Rust 1.86 pin, 2.0 CI job) | 2026-10-17 | 720c7c7 | [261016-tjw-port-anofox-statistics-duckdb-2-0-compat](./quick/261016-tjw-port-anofox-statistics-duckdb-2-0-compat/) |
| 3 | Fix DuckDB v2.0.0 build (explicit extension target names, no-OpenSSL banner gate, duckdb-next CI -> v2.0.0) | 2026-10-21 | 4cab513 | — |
| 261022-laz | Make DuckDB v2.0.0 the stable build line (submodules, distribution CI, keep v1.5.6 + v1.4.5 lines, retire duckdb-next, docs) | 2026-10-22 | 7f33142 | [261022-laz-make-duckdb-v2-0-0-the-stable-build-line](./quick/261022-laz-make-duckdb-v2-0-0-the-stable-build-line/) |

## Deferred Verification

| Phase | State | Resume |
|-------|-------|--------|

## Deferred Items

Items acknowledged and deferred at milestone close, most recent first:

| Category | Item | Status | Deferred At | Milestone |
|----------|------|--------|-------------|-----------|
| verification_gaps | Phase 08 (archived v0.9.0): 08-VERIFICATION.md | RESOLVED 2026-10-16 (live negative control, PR #275) | 2026-10-16 | v0.10.0 |
| deferred_items | Phase 10/deferred-items.md: 1.  test/sql/ts_model_distinctness.test  SeasonalES/SeasonalESOptimized expected values are stale (pre-existin | acknowledged | 2026-10-16 | v0.10.0 |
| deferred_items | Phase 10/deferred-items.md: 2. Theta and CrostonClassic produce identical in-sample fitted arrays and MSE on non-seasonal, no-zero series  | acknowledged | 2026-10-16 | v0.10.0 |
| deferred_items | Phase 10/deferred-items.md: 3. Unparsable numeric params values silently fall back to defaults on every MAP-param surface  -   Discovered  | acknowledged | 2026-10-16 | v0.10.0 |
| deferred_items | Phase 10/deferred-items.md: 4. docs/api/00-api-design.md lists stale ts_forecast_agg STRUCT field names  -   Discovered during:   Plan 10- | acknowledged | 2026-10-16 | v0.10.0 |
| deferred_items | Phase 10/deferred-items.md: 5. json-gated agg test files expect pre-Phase-10 (and now pre-10-10) behaviour  -   Discovered during:   Plan  | acknowledged | 2026-10-16 | v0.10.0 |
| deferred_items | Phase 12/deferred-items.md: (a) Several aggregate functions still drop NULL-valued rows instead of a trim/error/pairwise policy  -   Disco | acknowledged | 2026-10-16 | v0.10.0 |
| deferred_items | Phase 12/deferred-items.md: (b) Native detection table functions still impute NULL leaf values as 0  -   Discovered during:   Plan 12-08 T | acknowledged | 2026-10-16 | v0.10.0 |
| deferred_items | Phase 12/deferred-items.md: (c) Correct-but-private list-marshalling helpers not yet consolidated onto  ts_list_marshal.hpp   -   Discover | acknowledged | 2026-10-16 | v0.10.0 |
| deferred_items | Phase 12/deferred-items.md: (d)  is_constant 's absolute  f64::EPSILON  tolerance is not scale-aware  -   Discovered during:   Plan 12-08  | acknowledged | 2026-10-16 | v0.10.0 |
| deferred_items | Phase 12/deferred-items.md: (e)  detect_frequency 's microsecond return unit is undocumented at the public API boundary  -   Discovered du | acknowledged | 2026-10-16 | v0.10.0 |
| deferred_items | Phase 12/deferred-items.md: (f) Conformal NaN-residual handling has no documented finite-sample contract  -   Discovered during:   Plan 12 | acknowledged | 2026-10-16 | v0.10.0 |
| deferred_items | Phase 13/deferred-items.md: Plan 13-02  -   Pre-existing  cargo clippy --all-targets -- -D warnings  failure, unrelated to this plan's fil | acknowledged | 2026-10-16 | v0.10.0 |
| deferred_items | Phase 13/deferred-items.md: Plan 13-03  -    ParseFrequencyWithType 's numeric multiplier is discarded for calendar (Monthly/Quarterly/Yea | acknowledged | 2026-10-16 | v0.10.0 |
| deferred_items | Phase 13/deferred-items.md: Plan 13-04  -   D-08 scalar/table signature conflict, resolved (recorded for the user).   D-08 writes the new  | acknowledged | 2026-10-16 | v0.10.0 |
| deferred_items | Phase 16/deferred-items.md:   ts_coverage_by / ts_quantile_loss_by (found in 16-03, pre-existing):   the macro bodies pass  source  straig | acknowledged | 2026-10-16 | v0.10.0 |
| deferred_items | Phase 18/deferred-items.md: Plan 18-01  -   Bare  frequency  parameter passed to a VARCHAR-typed native function in  ts_forecast_panel_by  | acknowledged | 2026-10-16 | v0.10.0 |
| deferred_items | Phase 18/deferred-items.md: Plan 18-02  -   Docs/examples still use the bare table-name form of the metric  _by  family.   18-02 Task 1's  | acknowledged | 2026-10-16 | v0.10.0 |
| deferred_items | Phase 18/deferred-items.md: Plan 18-04  -    examples/backtesting/m5_backtest_examples.sql  and  examples/metrics/README.md  still use the | acknowledged | 2026-10-16 | v0.10.0 |
| WASM (future) | Browser-based (not just Node) WASM E2E harness (WASM-F1) | Deferred | 2026-09-01 | v0.9.0 |
| WASM (future) | Shared-memory `wasm_threads` build — blocked upstream (WASM-F2) | Deferred | 2026-09-01 | v0.9.0 |
| ENS (future) | Custom hand-supplied combination weights (ENS-F1) | Deferred | 2026-08-30 | v0.8.0 |
| ENS (future) | Panel/multivariate ensembling (ENS-F2) | Deferred | 2026-08-30 | v0.8.0 |

## Session Continuity

Last session: 2026-10-15T04:59:02.258Z
Stopped at: Phase 18 complete — all phases complete
Resume file: None
Next: `/gsd-plan-phase 9`

## Operator Next Steps

- Start the next milestone with /gsd-new-milestone
