# Roadmap: anofox-forecast

## Milestones

- ✅ **v0.7.0 — Close the Crate→Extension Gap (Diagnostics + Model Coverage)** — Phases 1-3 (shipped 2026-08-22)
- ✅ **v0.8.0 — Ensemble Forecasting** — Phases 4-6 (shipped 2026-08-31)
- ✅ **v0.9.0 — WASM Runtime Verification** — Phases 7-8 (shipped 2026-09-10; closed on main 2026-10-16)
- ✅ **v0.10.0 — Statistical Correctness** — Phases 9-18 (shipped 2026-10-16)
- 🚧 **v0.11.0 — Issue Sweep: Contract, Bugs, Crate Fixes** — Phases 19-25 (in progress)

## Phases

<details>
<summary>✅ v0.7.0 — Diagnostics + Model Coverage (Phases 1-3) — SHIPPED 2026-08-22</summary>

Full detail: [milestones/v0.7.0-ROADMAP.md](milestones/v0.7.0-ROADMAP.md) · Requirements: [milestones/v0.7.0-REQUIREMENTS.md](milestones/v0.7.0-REQUIREMENTS.md) · Audit: [milestones/v0.7.0-MILESTONE-AUDIT.md](milestones/v0.7.0-MILESTONE-AUDIT.md)

- [x] Phase 1: Statistical Diagnostics (3/3 plans) — completed 2026-08-21
      `ts_adf(_by)`, `ts_kpss(_by)`, `ts_stationarity(_by)`, `ts_ljung_box_by`, `ts_durbin_watson_by`, `ts_jarque_bera_by`, `ts_residual_diagnostics_by` (STAT-01..03, RESID-01..04)

- [x] Phase 2: Global / Panel Models (3/3 plans) — completed 2026-08-21
      `ts_forecast_panel_by` — GlobalETS / GlobalTheta / GlobalCroston, statsforecast M4 parity (GLOB-01..03)

- [x] Phase 3: Classical & Multivariate Models (3/3 plans) — completed 2026-08-22
      `ts_forecast_by` methods `'GARCH'` / `'Kalman'`, and multivariate `ts_forecast_var_by` (VAR) (CLAS-01..03)

INTER-01 (intermittent-demand classification) descoped — user has a more advanced approach TBD.

</details>

<details>
<summary>✅ v0.8.0 — Ensemble Forecasting (Phases 4-6) — SHIPPED 2026-08-31</summary>

Full detail: [milestones/v0.8.0-ROADMAP.md](milestones/v0.8.0-ROADMAP.md) · Requirements: [milestones/v0.8.0-REQUIREMENTS.md](milestones/v0.8.0-REQUIREMENTS.md) · Audit: [milestones/v0.8.0-MILESTONE-AUDIT.md](milestones/v0.8.0-MILESTONE-AUDIT.md)

- [x] Phase 4: AutoEnsemble Surface + Combination Methods (2/2 plans) — completed 2026-08-30
      `ts_forecast_by(..., 'AutoEnsemble', ..., {top_k, combination_method, seasonal_period})` + six combination methods (Mean/Median/WeightedMSE/InverseAIC/Stacking/HorizonAdaptive) (ENS-01, COMB-01..04)

- [x] Phase 5: Explicit-Member Ensemble (2/2 plans) — completed 2026-08-31
      `ts_forecast_ensemble_by('table', grp, ds, y, members VARCHAR[], ...)` — user-named members + `build_forecaster` factory (26-member allowlist) (ENS-02)

- [x] Phase 6: Ensemble Intervals & Introspection (2/2 plans) — completed 2026-08-31
      Conformal intervals on ensembles via the existing path (EPI-01) + `ts_ensemble_inspect_by` / `ts_auto_ensemble_inspect_by` member/weight introspection (INSP-01)

Tech debt carried forward: `ts_cv_forecast_by('AutoEnsemble')` segfaults (crate/CV-native bug, worked around via manual per-fold loop); AutoEnsemble non-Mean combination weights return NULL (crate 0.15.3 exposes no inner-weight accessor); `build_forecaster` SeasonalWindowAverage `n_seasons=2` hardcoded (TODO ENS-03). See milestones/v0.8.0-MILESTONE-AUDIT.md.

</details>

<details>
<summary>✅ v0.9.0 — WASM Runtime Verification (Phases 7-8) — SHIPPED 2026-09-10 (closed on main 2026-10-16)</summary>

Full detail: [milestones/v0.9.0-ROADMAP.md](milestones/v0.9.0-ROADMAP.md) · Requirements: [milestones/v0.9.0-REQUIREMENTS.md](milestones/v0.9.0-REQUIREMENTS.md) · Audit: [milestones/v0.9.0-MILESTONE-AUDIT.md](milestones/v0.9.0-MILESTONE-AUDIT.md) · Phase artifacts: [milestones/v0.9.0-phases/](milestones/v0.9.0-phases/)

- [x] Phase 7: WASM Node Harness + Local Green (1/1 plans) — completed 2026-09-02
      DuckDB-Wasm Node harness (`test/wasm/run.mjs` + `sqllogic.mjs`), `@duckdb/duckdb-wasm` engine-version pin, openssl `!wasm32` vcpkg guard (WASM-01, WASM-02, DEP-01, DEP-02).

- [x] Phase 8: CI Gating + Dedicated Workflow + Badge (1/1 plans) — completed 2026-09-10, live-CI verified 2026-10-16
      `wasm-runtime-test` gating job (curated subset), `WasmTest.yml`, README badge (CI-01..03). Live negative control (draft PR #275): gate RED on run 38905747917, GREEN after revert on run 38914644475.

The 2026-09-10 close lived only on a side branch (9976346) and was restored to main on 2026-10-16.

</details>

<details>
<summary>✅ v0.10.0 — Statistical Correctness (Phases 9-18) — SHIPPED 2026-10-16</summary>

Full detail: [milestones/v0.10.0-ROADMAP.md](milestones/v0.10.0-ROADMAP.md) · Requirements: [milestones/v0.10.0-REQUIREMENTS.md](milestones/v0.10.0-REQUIREMENTS.md) · Audit: [milestones/v0.10.0-MILESTONE-AUDIT.md](milestones/v0.10.0-MILESTONE-AUDIT.md) · Phase artifacts: [milestones/v0.10.0-phases/](milestones/v0.10.0-phases/)

- [x] Phase 9: Reference Fixtures & Safe Dependency Bumps (3/3 plans) — completed 2026-10-09
- [x] Phase 10: Forecast Wrapper Delegates to Crate Models (10/10 plans) — completed 2026-10-09
- [x] Phase 11: Upstream anofox-forecast Correctness Fixes (11/11 plans + 11-12 addendum, upstream repo) — completed 2026-10-16
- [x] Phase 12: Statistics Marshalling Moves to Rust (8/8 plans) — completed 2026-10-10
- [x] Phase 13: Core Statistics, Metrics & Data Quality (4/4 plans) — completed 2026-10-11
- [x] Phase 14: tsfresh Feature Parity (4/4 plans) — completed 2026-10-12
- [x] Phase 15: Detection & Decomposition on fdars 0.47 (5/5 plans) — completed 2026-10-14
- [x] Phase 16: Conformal & Bootstrap Correctness (3/3 plans) — completed 2026-10-14
- [x] Phase 17: Upstream Consumption & Diagnostics Parity (3/3 plans) — completed 2026-10-15
- [x] Phase 18: Correctness Regression Suite, Audit & Docs (6/6 plans incl. gap plans 18-05 anofox-forecast 0.16.2, 18-06 ts_detect_periods fix) — completed 2026-10-16

65/65 requirements. Closed as an override closeout: gsd's staleness heuristic flagged all 10 phase verifications "stale" (all VERIFICATION.md status: passed; integration re-checked green at HEAD). 19 deferred items acknowledged (see STATE.md Deferred Items and the audit's tech_debt list).

</details>

### 🚧 v0.11.0 — Issue Sweep: Contract, Bugs, Crate Fixes (Phases 19-25)

**Milestone Goal:** Close all 15 open GitHub issues of DataZooDE/anofox-forecast (#227, #258, #259, #272, #279-#289): fix the reported WASM, detection and metric bugs, make the outputs conform to the anofox-visualization `integration-contract.md`, and ship the three upstream crate fixes. Branch: `milestone/v0.11.0-issue-sweep`. Primary build: DuckDB v2.0.0 (`DUCKDB_VERSION=v2.0.0 make release`); the v1.5.6 and v1.4.5 LTS CI lines stay green.

**Phase Numbering:**

- Integer phases (19, 20, 21): Planned milestone work
- Decimal phases (e.g. 20.1): Urgent insertions (marked with INSERTED)

- [x] **Phase 19: WASM Traps & Distribution Pipeline** - The #289 WASM traps and the fill-forward hang are fixed; the upload script and a post-upload check stop broken WASM artifacts; wasm_mvp is decided; the CORS/stale-path changes are handed to the owner (completed 2026-10-23)
- [ ] **Phase 20: Detection & Metric Bug Fixes** - `ts_mase_by` no longer depends on row order; seasonality classification agrees with `ts_stats_by`; changepoints ignore regular seasonal troughs and agree with their probability; period 7 is pinned
- [ ] **Phase 21: Upstream AutoETS Spike & Ridge Conditioning Fixes** - AutoETS stops turning one-off spikes into a trend (anofox-forecast); the fold-level ridge chooses λ by conditioning (anofox-regression); delivered as upstream PRs for release
- [ ] **Phase 22: Upstream Streaming Feature Builders** - anofox-forecast's regression feature engineering becomes standalone `(series, t, cfg) -> row` builders shared by the per-series and panel fits (upstream PR for release)
- [ ] **Phase 23: Contract Discovery & Prediction Columns** - `anofox_contract_version()`; conformal, exog and VAR outputs use caller-named `id`/`ds` and `yhat` columns, with old names aliased for one release; every interval output carries `level`
- [ ] **Phase 24: Contract Long Forms, Events & Test Outputs** - Long-form metrics and list outputs, a filled explain residual, changepoint events, a Durbin-Watson `p_value`, and contract tags on every result producer
- [ ] **Phase 25: Crate Consumption, Release & Issue Closure** - Consume the new crate releases, complete AutoARIMA inspection, verify the community registry, cut the CalVer release and close all 15 issues

## Phase Details

### Phase 19: WASM Traps & Distribution Pipeline

**Goal**: The extension runs in DuckDB-Wasm without the #289 traps, and the release pipeline can no longer publish a WASM artifact that fails to LOAD (#272).
**Depends on**: Nothing (first phase of v0.11.0; independent of every other phase)
**Requirements**: BUG-01, BUG-02, DIST-01, DIST-02, DIST-03, DIST-04
**Success Criteria** (what must be TRUE):

  1. `ts_fill_forward_operator` is no longer part of the public, documented surface: it is removed, or renamed to an internal underscore name. If it is kept, a call with quoted identifiers (a column name containing a space) and a non-default `frequency` returns correctly filled rows natively and in the DuckDB-Wasm Node harness, without hanging or trapping. `ts_fill_forward_by` output is unchanged, pinned by a sqllogictest.
  2. `ts_features_list()` and `ts_features_config_template()` each return 117 rows in the native build and in DuckDB-Wasm (wasm_eh) on Node, and both queries run in the curated WASM subset that gates CI.
  3. A WASM artifact produced by `scripts/extension-upload.sh` has exactly one `duckdb_signature` custom section (the placeholder is replaced, not followed by a second section). A post-upload check downloads each uploaded artifact, asserts the section count is 1 and LOADs the WASM build in DuckDB-Wasm on Node, and it fails on a deliberately double-signed artifact.
  4. The `wasm_mvp` build either LOADs in DuckDB-Wasm on Node, or the `_setThrew is not defined` root cause is written up with a recorded decision (fix it, or stop shipping wasm_mvp and change the build/upload matrix to match).
  5. A handover note gives the owner the exact get.erpl.io CORS rule (GET/HEAD from `*`) and the list of stale old-version paths, ready to apply without further investigation.

**Plans:** 4/4 plans complete

Plans:
**Wave 1**

- [x] 19-01-PLAN.md — BUG-01: `ts_fill_forward_operator` becomes a deprecated macro over `ts_fill_forward_by` (C++ nested-query operator deleted), pinned natively and in the curated DuckDB-Wasm subset (wave 1)
- [x] 19-02-PLAN.md — DIST-01/02/04: upload script replaces the WASM signature in place behind guards, post-upload check in the deploy job (S3 download, section count, DuckDB-Wasm LOAD), get.erpl.io owner handover doc (wave 1)

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 19-03-PLAN.md — BUG-02: `ts_features_list()` / `ts_features_config_template()` WASM trap (libc free of Rust-allocated memory) fixed via a paired FFI free function, init_global state, 117-row tests in the curated subset (wave 2)

**Wave 3** *(blocked on Wave 2 completion)*

- [x] 19-04-PLAN.md — DIST-03/04: branch CI evidence, harness `--bundle mvp`, wasm_mvp `_setThrew` root cause + user decision (checkpoint), CHANGELOG #272, #272 comment (wave 3)

### Phase 20: Detection & Metric Bug Fixes

**Goal**: The metric and detection functions give correct, order-independent answers on the series and panels reported in #285, #286 and #287.
**Depends on**: Nothing (independent of Phase 19; runs after it on the milestone branch)
**Requirements**: BUG-03, BUG-04, BUG-05, BUG-06, BUG-07
**Success Criteria** (what must be TRUE):

  1. `ts_mase_by` returns the same value for a series whether its rows arrive in date order, in reverse, or through the #285 `UNION ALL` filtered by a subquery (previously NULL), and that value equals the Hyndman-Koehler MASE computed on the date-sorted series.
  2. `ts_classify_seasonality_by` classifies the #286 strong weekly series as seasonal (not `non_seasonal`, never NaN), and its `seasonal_strength` is consistent with its own `cycle_strengths` and with `ts_stats_by`'s seasonal strength on the same series.
  3. On the #287 panel, `ts_detect_changepoints_by` and `ts_detect_changepoints_agg` flag none of the troughs of the regular weekly cycle and still flag series B's level shift on 2024-02-20.
  4. In `ts_detect_changepoints_agg`, `is_changepoint` is true exactly when `changepoint_probability` reaches the threshold (no unflagged row at p = 0.996), and `ts_detect_periods_by` returns period 7 on the #287 panel.
  5. Each fix is computed in Rust and pinned by a value-asserting test under `test/sql/correctness/` built from the issue's reproducer; `make audit` and `cargo fmt --check` stay green.

**Plans**: TBD

### Phase 21: Upstream AutoETS Spike & Ridge Conditioning Fixes

**Goal**: The upstream crates stop extrapolating a one-off spike as a persistent trend (AutoETS) and stop over-shrinking well-conditioned folds (ridge), delivered as PRs the user can merge and publish while the extension phases continue.
**Depends on**: Nothing. Works in `~/projects/rust/anofox-forecast` (CRATE-01) and `~/projects/rust/anofox-regression` (CRATE-02) on fix branches; runs in parallel with Phases 19-20 and 23-24.
**Requirements**: CRATE-01, CRATE-02
**Success Criteria** (what must be TRUE):

  1. On the spike-runaway fixture (10 series, period 12, spike share ≈ 0.25), AutoETS's mean forecast level stays at or below 2.0× the spike-robust median, asserted by an upstream test. On non-spiky fixtures the AutoETS selections and forecasts are unchanged, and the R `ets` parity checks still pass.
  2. The anofox-regression fold-level ridge solver chooses a small λ on a well-conditioned seasonal-dummy design and λ = 1.0 on an ill-conditioned one (condition number above the documented threshold), asserted by tests on both designs. On the well-conditioned fold the seasonal amplitude is no longer damped relative to the unpenalised fit.
  3. Each fix sits on its own branch with a PR open, upstream `cargo test`, `cargo clippy` and `cargo fmt --check` green, and a CHANGELOG entry describing the numerical change.

**Plans**: TBD
**Note**: The anofox-forecast branch also carries the crate-side accessor that CTR-10 (Phase 25) needs. In crate 0.16.2, `SARIMA::fitted_values()` returns `None` and the non-seasonal ARIMA returns differenced-scale fitted values, so level-scale in-sample fitted values for the AutoARIMA `ArimaExplanation` must come from the crate, not the wrapper.

### Phase 22: Upstream Streaming Feature Builders

**Goal**: anofox-forecast's regression feature engineering is a set of standalone, streaming-friendly builders that both the per-series `RegressionForecaster` and a global/panel fit call.
**Depends on**: Nothing. Works in `~/projects/rust/anofox-forecast` on its own branch; can share one crate release with the Phase 21 AutoETS fix.
**Requirements**: CRATE-03
**Success Criteria** (what must be TRUE):

  1. Lags, auto_lags, trend_type, differencing, Fourier, rolling, exog features and event_distance are public builders of the shape `(series, t, cfg) -> feature row` that write into a caller-owned, reusable scratch row without per-row allocation.
  2. `RegressionForecaster` is rewired onto the builders and reproduces its pre-refactor coefficients and forecasts on the existing regression fixtures (bit-identical, or within a stated tolerance with a documented cause).
  3. A test fits a multi-series panel by visiting rows one at a time through the builders and accumulating XᵀX / Xᵀy without materialising the design matrix, and its solution matches the materialised-matrix fit.
  4. The branch has a PR open with upstream `cargo test`, `cargo clippy` and `cargo fmt --check` green, and a CHANGELOG entry and API docs describing the builders.

**Plans**: TBD

### Phase 23: Contract Discovery & Prediction Columns

**Goal**: anofox-visualization can discover the contract version and plot every forecast and interval output as a `prediction` without renaming a column, while existing queries keep working for one release through aliases.
**Depends on**: Nothing hard (extension-side; runs after Phase 20 on the milestone branch)
**Requirements**: CTR-01, CTR-03, CTR-04, CTR-05, CTR-06
**Success Criteria** (what must be TRUE):

  1. `SELECT anofox_contract_version()` returns `'1'`.
  2. `ts_conformal_by`, `ts_conformal_apply_by`, `ts_conformal_calibrate` and `ts_interval_width_by` return the caller's group and date column names (e.g. `id`, `ds`, not literal `group_col` / `date_col`) and `yhat` / `yhat_lower` / `yhat_upper`. A query written against the old names still runs in this release, and the deprecation is documented.
  3. `ts_forecast_exog_by` returns `ds` (with `date` still available), and `ts_forecast_var_by` returns the `ts_forecast_by` columns with `id` = the variable and `yhat` (plus bounds where the model provides intervals), with `variable` / `forecast_value` still available.
  4. Every forecast and conformal output that has an interval carries a `level` column equal to the requested confidence level, so the outputs of two calls at 0.80 and 0.95 union into nested fans without hand-added columns.
  5. sqllogictests pin both the new names and the aliases on the v2.0.0, v1.5.6 and v1.4.5 LTS builds; the affected examples, `docs/api/` pages and plugin skills use the new names, and `make check-docs` is green.

**Plans**: TBD

### Phase 24: Contract Long Forms, Events & Test Outputs

**Goal**: Every result producer returns a contract-shaped table (summary, long form, events or test) and is discoverable through `duckdb_functions()` tags, so the visualization conformance harness no longer needs a hand-curated registry.
**Depends on**: Phase 20 (the events and period long forms build on the corrected changepoints and periods), Phase 23 (the tags cover the renamed prediction outputs)
**Requirements**: CTR-02, CTR-07, CTR-08, CTR-09, CTR-11, CTR-12
**Success Criteria** (what must be TRUE):

  1. A long-form `ts_metrics_by` returns one row per (id, model, metric, value) in the contract's `summary` schema, and every value equals the matching existing wide `ts_*_by` metric macro, which stay unchanged.
  2. Long-form companions of `ts_forecast`, `ts_forecast_agg`, `ts_mstl_decomposition_by`, `ts_forecast_explain_by`, `ts_detrend_by`, `ts_detect_peaks_by` and `ts_detect_periods_by` return one row per time point, component, peak or period, carrying `id` and `ds` (a decomposition as `id, ds, component, value`), and agree element by element with the list form. `ts_forecast_explain_by` returns a non-NULL `residual` component.
  3. A changepoint events output returns one row per detected changepoint as `id, ds, kind = 'changepoint', severity, value` (value = the changepoint probability), with exactly the rows `ts_detect_changepoints_by` flags on the #287 panel.
  4. `ts_durbin_watson_by` and the `dw_*` fields of `ts_residual_diagnostics_by` return `method = 'Durbin-Watson'` and a documented approximate `p_value`, computed in Rust and checked against an R `lmtest::dwtest` (or statsmodels) reference within a stated tolerance.
  5. `duckdb_functions()` lists every result producer with `tags['anofox.contract'] = '1'` and the correct `tags['anofox.output']`, following the anofox-statistics pattern; data-preparation and bookkeeping functions stay untagged, and a test asserts the tagged set.

**Plans**: TBD

### Phase 25: Crate Consumption, Release & Issue Closure

**Goal**: v0.11.0 ships on the released upstream crates with AutoARIMA inspection complete, the community registry serves the current release, and all 15 issues are closed.
**Depends on**: Phases 19-24; user-published crates.io releases of the Phase 21-22 work; the external merge of duckdb/community-extensions#3112 by the DuckDB maintainers (DIST-05)
**Requirements**: CRATE-04, CTR-10, DIST-05, REL-01
**Success Criteria** (what must be TRUE):

  1. `Cargo.toml` pins the newly published anofox-forecast and anofox-regression versions from crates.io with no `[patch]`, git or path override. The R-parity suite, `test/sql/correctness/*`, `make audit` and `cargo fmt --check` pass, and `ts_forecast_by(..., 'AutoETS', ...)` on the spike fixture stays at or below 2.0× the spike-robust median from SQL.
  2. `ts_forecast_inspect_by(..., 'AutoARIMA')` returns level-scale fitted values and residuals, the selected order, and coefficients named `ar1`, `ma1`, `sar1`, … aligned with their values, checked against an R `Arima()` fit at the same order for both a non-seasonal and a seasonal selection.
  3. After `FORCE INSTALL anofox_forecast FROM community`, the loaded extension reports the release from duckdb/community-extensions#3112 (v2026.10.22) on DuckDB v2.0.0 and v1.5.6, and #288 is closed with that evidence.
  4. The CHANGELOG (including the deprecation note for the #280 aliases) and release notes are written, a CalVer release is cut from the main merge commit with the v2.0.0, v1.5.6 and v1.4.5 LTS CI lines green, and the community descriptor is bumped.
  5. All 15 issues (#227, #258, #259, #272, #279-#289) are closed, each with a reference to its fixing PR.

**Plans**: TBD

## Execution Notes

- **Parallel track:** Phases 21 and 22 run in the upstream repos (`~/projects/rust/anofox-forecast`, `~/projects/rust/anofox-regression`) on fix branches, with PRs for the user to review, merge and publish. They are numbered early so the crates.io release lead time overlaps the extension work in Phases 19-20 and 23-24. Phase 25 is the consumption gate.
- **Extension phases are sequential:** 19 → 20 → 23 → 24 → 25 run one after another on `milestone/v0.11.0-issue-sweep` (`workflow.use_worktrees` is off).
- **CTR-10 placement (checked on the built extension, 2026-10-22):** `ts_forecast_inspect_by(..., 'AutoARIMA')` returns 0 fitted values, `order_p/d/q` read 0/0/0 even when coefficients come back, and `feature_names` is NULL. In crate 0.16.2 `SARIMA::fitted_values()` returns `None`, and the non-seasonal ARIMA returns differenced-scale values. The crate-side accessor rides on the Phase 21 anofox-forecast branch; the extension-side mapping fix (order fields, coefficient names, level-scale fitted values) lands in Phase 25 with the crate consumption.
- **Alias policy (locked 2026-10-22):** the #280 renames keep the old names as aliases for exactly one release, with a deprecation note. Removing them is a Future Requirement.
- **External gates:** crates.io releases are published by the user; duckdb/community-extensions#3112 is merged by DuckDB maintainers. DIST-05 can be verified as soon as #3112 lands, ahead of the rest of Phase 25.
- **Owner actions (out of scope):** applying the get.erpl.io CORS rule and deleting stale paths; Phase 19 only documents them.
- **Universal DoD (every requirement):** value assertions against a fixture, reproducer or closed form; `cargo test`, `cargo fmt --check` and `cargo clippy` green; `make audit` green (no statistics in C++/SQL); affected SQL run against the built v2.0.0 extension; examples and docs pass `make check-docs`; R / scipy / statsmodels reference checks wherever a statistic changes; v1.5.6 and v1.4.5 LTS CI lines green.
- **No UI phases:** "long form" and "component" in Phases 23-24 refer to SQL table shapes. This project has no frontend, so no phase carries a UI hint.

## Progress

**Execution Order:**
Extension phases execute in numeric order: 19 → 20 → 23 → 24 → 25. Phases 21 and 22 run in the upstream repos in parallel, and their crates.io releases must land before Phase 25.

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 19. WASM Traps & Distribution Pipeline | 4/4 | Complete    | 2026-10-23 |
| 20. Detection & Metric Bug Fixes | 0/TBD | Not started | - |
| 21. Upstream AutoETS Spike & Ridge Conditioning Fixes | 0/TBD | Not started | - |
| 22. Upstream Streaming Feature Builders | 0/TBD | Not started | - |
| 23. Contract Discovery & Prediction Columns | 0/TBD | Not started | - |
| 24. Contract Long Forms, Events & Test Outputs | 0/TBD | Not started | - |
| 25. Crate Consumption, Release & Issue Closure | 0/TBD | Not started | - |

## Next

Plan Phase 19 — run `/gsd-discuss-phase 19` or `/gsd-plan-phase 19`. Start Phases 21 and 22 early in the upstream repos so the crate releases overlap the extension work.
