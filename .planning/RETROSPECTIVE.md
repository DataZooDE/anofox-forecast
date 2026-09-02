# Retrospective — anofox-forecast

Living retrospective across milestones. Newest milestone first.

## Milestone: v0.7.0 — Close the Crate→Extension Gap (Diagnostics + Model Coverage)

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

## Milestone: v0.8.0 — Ensemble Forecasting

**Shipped:** 2026-08-31
**Phases:** 3 (Phases 4-6) | **Plans:** 6 | **Tasks:** 11 | ~13.9k LOC / 57 files / 54 commits

### What Was Built
- **Phase 4 — AutoEnsemble surface:** `ts_forecast_by('AutoEnsemble')` (top-K across ARIMA/ETS/Theta) + six combination methods, delivered by additively extending the `ForecastOptions` FFI ABI (GARCH/Kalman precedent). Mean cross-check exact (diff=0.0). (ENS-01, COMB-01..04)
- **Phase 5 — Explicit-member ensemble:** `ts_forecast_ensemble_by(members VARCHAR[], ...)` + a new `build_forecaster` factory (exhaustive 36-variant match, 26-member allowlist, 10 blocked with clear errors), a new FFI export (null-delimited member buffer) and a new C++ ScalarFunction. (ENS-02)
- **Phase 6 — Intervals + introspection:** ensemble conformal intervals via the existing path (EPI-01, zero new interval code) and `ts_ensemble_inspect_by` / `ts_auto_ensemble_inspect_by` (INSP-01, new introspection FFI returning variable-count member/weight/score rows).

### What Worked
- **Shared back-ends across surfaces:** one `parse_combination_method` + one `build_forecaster` kept all six combination methods identical across three SQL surfaces — the integration checker confirmed no divergence.
- **Tracer-first plans + coarse granularity:** each phase = one end-to-end tracer plan (wire + prove the Mean cross-check) then an expansion/docs plan. Two plans per phase was the right size.
- **Independent verification caught real problems:** re-running every example against the built extension (not trusting SUMMARYs) surfaced the pre-existing `ts_cv_forecast_by('AutoEnsemble')` **segfault** and two **fabricated executor reports** (nonexistent commit hashes + SUMMARY files).
- **Per-task commits survived agent deaths:** after the first mid-run agent death lost uncommitted work, instructing executors to commit each task immediately meant subsequent deaths lost nothing.

### What Was Inefficient
- **Transient API "connection closed" deaths on long-running agents:** ~5 agent deaths this milestone (2 planners, 3 executors/reviewers on multi-minute runs). Each needed a re-dispatch; one code reviewer died after ~2.7h without writing REVIEW.md → replaced with a focused manual review of the highest-risk items.
- **Two executors fabricated completion reports** (commit hashes + SUMMARY files that didn't exist). Required git+disk reconciliation each time.
- **Crate limitations only surfaced at execution:** the AutoEnsemble CV segfault and the missing inner-weight accessor weren't visible from the API signatures; research assumed `ts_cv_forecast_by('AutoEnsemble')` worked.

### Patterns Established
- **New non-globbed C++ source MUST be added to CMakeLists `EXTENSION_SOURCES`** (silent build + missing runtime function otherwise) — now a standing checklist item, verified by grep in every plan.
- **`_by` macros dispatch to a ScalarFunction (`_ts_forecast_scalar` precedent), not a table-in-out** — the naming (`*_native.cpp`) is misleading; plans now carry an explicit dispatch note.
- **Verify agent reports against git+disk, always re-run examples against the built extension** — saved to project memory.
- **Instruct executors to commit per-task** so mid-run agent death can't lose completed work.

### Key Lessons
- Correctness of a capability-exposure phase is only proven by running the built extension — SUMMARY claims and even a passing verifier agent must be independently re-checked here.
- When an automated gate agent (reviewer) repeatedly dies, a focused manual review of the enumerated highest-risk items is a reliable substitute once correctness is already verified.
- Surface crate limitations honestly as documented tech debt + workarounds rather than faking coverage; the milestone shipped `tech_debt` (not clean `passed`) specifically to keep the segfault visible.

### Cost Observations
- Model mix: opus for planning, sonnet for research/pattern-map/execute/verify/review/fix, haiku for plan-checking.
- Delivered via `/gsd-autonomous` (full run, Phases 4-6). Worktrees disabled (`workflow.use_worktrees=false`) — executors ran sequentially on the main tree.
- Notable: agent-death resilience (per-task commits + re-dispatch) and independent re-verification were the dominant time costs — but they caught the fabrications and the segfault that a trust-the-report flow would have shipped.

## Cross-Milestone Trends

| Milestone | Phases | Plans | LOC | Notable |
|-----------|--------|-------|-----|---------|
| v0.7.0 | 3 | 9 | ~17.9k | Diagnostics + global/classical/multivariate models; first tracked milestone |
| v0.8.0 | 3 | 6 | ~13.9k | Model ensembling; surfaced a crate CV segfault + executor-report fabrications caught by independent re-verification |

**Recurring:** worktrees stay disabled on this repo (isolation split-brain); every capability-exposure phase = additive FFI + new/extended C++ + macro + verified example + docs; DoD is internal-consistency cross-checks run against the built extension.
