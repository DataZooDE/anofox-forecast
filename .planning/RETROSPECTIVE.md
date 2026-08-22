# Retrospective — anofox-forecast

Living retrospective across milestones. Newest milestone first.

## Milestone: v0.6.0 — Close the Crate→Extension Gap (Diagnostics + Model Coverage)

**Shipped:** 2026-08-22
**Phases:** 3 | **Plans:** 9 | **Commits:** 57 | **Diff:** +17,869 / −163 across 111 files

### What Was Built
- Statistical diagnostics: `ts_adf(_by)`, `ts_kpss(_by)`, `ts_stationarity(_by)` (four-way verdict), `ts_ljung_box_by`, `ts_durbin_watson_by`, `ts_jarque_bera_by`, `ts_residual_diagnostics_by` — statsmodels-cross-checked.
- Global/panel models: `ts_forecast_panel_by` (GlobalETS/GlobalTheta/GlobalCroston) — fit-once-emit-many native table function with ragged-panel alignment; statsforecast M4 parity (GlobalETS +1.8%, GlobalTheta −0.7%, GlobalCroston −6.9%).
- Classical/multivariate: `ts_forecast_by` methods `'GARCH'` (conditional volatility) and `'Kalman'` (state-space); new multivariate `ts_forecast_var_by` (VAR, long-format output). arch/statsmodels parity (GARCH 0.897, Kalman 1.000/0.992, VAR 1.000 exact).

### What Worked
- **Tracer-first MVP planning**: each phase led with one verified end-to-end vertical slice before expansion — caught integration issues early (e.g. the subselect macro gotcha surfaced in the Phase-2 tracer, then applied everywhere after).
- **Ground-truth verifiers**: verifiers ran the built extension binary rather than trusting SUMMARYs — every phase's forecasts were confirmed against a live `build/release/duckdb`.
- **Autonomous code-review + fix loop**: adversarial re-review after fixes caught real defects the happy-path verifier missed — spurious confidence intervals on GARCH/Kalman volatilities, a WASM `free()` layout UB, `unwrap_or(0)` overflow masking, and a deferred-error that was never actually thrown.
- **Cross-phase pattern reuse**: Phase 2's panel FFI/C++ was the direct analog for Phase 3's VAR surface; Phase-2 code-review lessons (checked_mul + error propagation, subselect macro) were baked into Phase-3 plans preemptively.

### What Was Inefficient
- **Each phase needed 2–3 code-review fix iterations to converge.** Several recurring bug classes (WASM allocator UB, spurious intervals, overflow handling) slipped past executors despite being known from prior phases — executor guidance could encode these as pre-flight checklists.
- **SUMMARY filename convention drift**: Phase 3's executor wrote `03-01-SUMMARY.md` (double-padded) vs the expected `03-1-SUMMARY.md`, breaking `has_summary` detection until renamed.
- **A verifier run dropped mid-response** (transient API error) with no VERIFICATION.md written; required a re-spawn. Verifiers now told to write the report early.
- **Phase 1 was executed in a prior session but never formally sealed** (no VERIFICATION.md), which blocked milestone auto-close until verified retroactively.

### Patterns Established
- **Table-in macro convention**: wrap `query_table(...)` in a subselect `(SELECT ... FROM query_table(...))` — a bare TABLE arg silently fails to register.
- **Additive FFI ABI extension**: append new `ForecastOptions` fields + run `make header` (cbindgen); backward-compatible with existing methods (integration-checker verified no offset breakage).
- **GARCH output = volatility (sqrt of `forecast_variance`)**, documented explicitly; never `predict()` (returns random innovations).
- Benchmarks/cross-checks always run under `benchmark/.venv`, via the `build/release/duckdb -unsigned` CLI subprocess to avoid the venv-vs-extension DuckDB version mismatch.

### Key Lessons
- Adversarial re-review is worth the iterations: it converted "verified green" phases into genuinely correct ones by catching edge cases (all-dropped panels, WASM free, exog-path interval leakage).
- Known-defect classes should be encoded into executor pre-flight guidance so they don't recur phase-to-phase.
- Formally seal every phase (VERIFICATION.md) even when work ships in an earlier session — milestone close depends on it.

### Cost Observations
- Model mix: planning/verification on Opus; researchers/executors/reviewers/fixers on Sonnet; plan-checker/integration-checker on Haiku.
- Delivered via `/gsd-autonomous --from 2`, with Phase 1 verified retroactively at close on user request.
- Worktrees disabled for the run (`workflow.use_worktrees=false`) to avoid the known isolation split-brain on this repo; executors ran sequentially on the main tree.

## Cross-Milestone Trends

_(first tracked milestone — trends accrue from v0.6.0 onward)_
