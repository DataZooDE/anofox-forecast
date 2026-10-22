# anofox-forecast — DuckDB time-series forecasting extension

## What This Is

`anofox-forecast` is a DuckDB extension that exposes SQL-native time-series forecasting, backed by the `anofox-forecast` Rust crate (v0.15.3) via an FFI boundary. It surfaces 36+ forecasting models, 117 features, cross-validation, conformal prediction intervals, seasonality/period/changepoint/peak detection, statistical diagnostics, and — as of v0.8.0 — **model ensembling** (automatic top-K and user-named members, six combination methods, ensemble conformal intervals, member/weight introspection), all as SQL functions and `ts_*_by` macros.

It is a brownfield capability-exposure project, not a rewrite — the established delivery pattern is: Rust FFI export → C++ table/scalar function → `ts_*_by` SQL macro → runnable verified example → docs.

## Core Value

SQL users can produce, validate, combine, and interval-bound time-series forecasts — including multi-model ensembles with weight introspection — entirely within DuckDB, without leaving SQL.

## Current State

- **Shipped v0.9.0 — WASM Runtime Verification (2026-09-10; closed on main 2026-10-16):** DuckDB-Wasm Node harness, `wasm-runtime-test` CI gate, `WasmTest.yml` badge. The gate was proven RED→GREEN in live CI. See `.planning/milestones/v0.9.0-*`.
- **Shipped v0.10.0 — Statistical Correctness (2026-10-16):** every statistic is computed in Rust and checked against R / scipy / statsmodels / tsfresh fixtures. The forecast wrapper delegates to the crate (anofox-forecast 0.16.2), with 65/65 requirements met, a `test/sql/correctness/*` suite, a `make audit` no-stats-in-C++ guard and `make check-docs`. See `.planning/milestones/v0.10.0-*`.
- **Shipped v0.8.0 — Ensemble Forecasting (2026-08-31):** AutoEnsemble (`ts_forecast_by('AutoEnsemble')`), explicit-member ensembles (`ts_forecast_ensemble_by`), six combination methods, ensemble conformal intervals (existing path), and member/weight introspection (`ts_ensemble_inspect_by` / `ts_auto_ensemble_inspect_by`). See `.planning/milestones/v0.8.0-*`.
- **Shipped v0.7.0 — Diagnostics + Model Coverage (2026-08-22):** stationarity + residual diagnostics, global/panel models, GARCH/Kalman/VAR.

## Current Milestone: v0.11.0 Issue Sweep: Contract, Bugs, Crate Fixes

**Goal:** Close all 15 open GitHub issues of DataZooDE/anofox-forecast: make the outputs conform to the anofox-visualization contract, fix the reported detection, metric and WASM bugs, and ship the three upstream crate fixes.

**Target features:**
- Extension bugs: #289 (fill-forward operator: internal, quoted, frequency honoured; `ts_features_list` / `ts_features_config_template` WASM traps), #285 (`ts_mase_by` independent of row order), #286 (seasonality classification detrends first), #287 (changepoints don't flag a regular seasonal cycle; the agg's `is_changepoint` follows probability), #272 (upload script WASM signature, post-upload check, `wasm_mvp` `_setThrew`)
- Output contract (#279-#284): discovery tags + `anofox_contract_version()`, consistent prediction columns + `level` (old names aliased for ONE release), long-form `ts_metrics_by`, long forms for list outputs + AutoARIMA inspect, changepoint events, Durbin-Watson `p_value`/`method`
- Upstream crate fixes, released and consumed: #259 (AutoETS spike runaway), #258 (conditioning-aware ridge λ), #227 (standalone streaming feature builders)
- Release closure: #288 closes once duckdb/community-extensions#3112 (v2026.10.22) is merged and verified

**Locked decisions (2026-10-22):** #280 renames ship with old-name aliases for one release, then are removed; all three crate issues are in scope; #272 is code-only (the owner applies the get.erpl.io CORS rule and stale-path cleanup). Branch: `milestone/v0.11.0-issue-sweep`. Issue bodies: GitHub issues #227, #258, #259, #272, #279-#289.

## Next Milestone Goals (carried candidates)

To be defined with `/gsd-new-milestone`. Candidates carried forward:
- v0.10.0 tech debt: integer-`frequency` casts in `ts_forecast_panel_by` / `ts_forecast_var_by` / `ts_stats_by`; `window` / `seasonal_periods` misuse checks in the shared params header; strict parsing of numeric MAP params; the examples/ metric `_by` bare-table form; the `ts_features_config_*` "features" filter; dead `ConformalIntervalFFI`.
- Upstream crate follow-ups: TBATS optimiser admissibility check; ETS multi-start (RC-03); SESOptimized intermittent optimum (RC-04).
- Merge `fix/statistical-correctness` to `main` and cut the v0.10.0 extension release (user action).

<details>
<summary>v0.10.0 milestone definition (shipped 2026-10-16)</summary>

### v0.10.0 Statistical Correctness

**Goal:** Every statistic the extension returns is correct — verified against R / scipy reference values — and is computed in the Rust crates; C++ and SQL only marshal, group, and dispatch.

**Target features:**
- Forecast wrapper delegates prediction intervals, fitted values and residuals to the crate models (no fabricated ±z·sd(series)·√h intervals, no SES(α=0.3) stand-in residuals); real ARIMA; silent model/parameter fallbacks become errors
- Wrapper bug fixes: fdars size-argument order (detrend/decompose/seasonality no-ops), bootstrap residual sign, data-quality dates, MSTL windows/order, `ts_diff_by`, exogenous NaN coefficients
- Statistics moved out of C++/SQL into Rust: pairwise-complete NULL handling via validity, `ToUnifiedFormat` list marshalling, native conformal/diff/fill functions, backtest metrics, hierarchy NULL semantics
- Core statistics/features corrected: moments, STL trend/seasonal strength, tsfresh-exact features, Hyndman-Koehler MASE, MAPE zero policy
- Detection/conformal corrected: scipy-semantics peaks, correct BOCPD, scale-free PELT, STL-based MSTL, asymmetric/adaptive/jackknife+ conformal, period-detection robustness
- Upstream anofox-forecast fixes (branch + PR, user releases): augmented ADF + MacKinnon p-values, bounded KPSS p-values, AutoARIMA order selection (KPSS/OCSB, AICc), ETS likelihood + analytic intervals, Theta
- Dependency bumps: anofox-regression 0.5.15, anofox-forecast ≥0.15.10, fdars-core 0.47 (drop `linalg`)
- Reference-fixture test suite (checked-in R/Python generators) + docs/skills synced to corrected behaviour

Input: `.planning/research/STAT-AUDIT-2026-10-08.md` (audit findings A–I). Branch: `fix/statistical-correctness`.
v0.9.0 (WASM Runtime Verification) shipped 2026-09-10 and was closed on main 2026-10-16, once the Phase 08 live-CI negative control was observed (PR #275).

</details>

## Requirements

### Validated

<!-- Inferred from existing codebase (brownfield); relied upon and shipped. -->

- ✓ `ts_forecast_by` + 36 model strings (baselines, ETS/Holt-Winters, Theta, ARIMA, MFLES, MSTL, TBATS, intermittent, Laplace) — existing
- ✓ 117-feature extraction (`ts_features*`), tsfresh-compatible — existing
- ✓ Cross-validation + backtest (`ts_cv_folds_by`, `ts_cv_forecast_by`, leakage check) — existing
- ✓ Conformal prediction intervals (split/adaptive/asymmetric/per-step, learn+apply) + bootstrap — existing
- ✓ Period/seasonality detection (~15 methods), MSTL decomposition, changepoints (PELT + BOCPD), peaks — existing
- ✓ 12 accuracy metrics, data-quality scoring, gap/null/differencing data prep — existing
- ✓ FFI + native-table-function + SQL-macro exposure pattern; DuckDB GROUP BY parallelism (no custom threading) — existing
- ✓ Stationarity tests: `ts_adf(_by)`, `ts_kpss(_by)`, combined `ts_stationarity(_by)` four-way verdict — v0.7.0 (statsmodels-cross-checked)
- ✓ Residual diagnostics: `ts_ljung_box_by`, `ts_durbin_watson_by`, `ts_jarque_bera_by`, combined `ts_residual_diagnostics_by` — v0.7.0
- ✓ Global/panel models: `ts_forecast_panel_by` (GlobalETS/GlobalTheta/GlobalCroston, cross-series learning) — v0.7.0 (statsforecast M4 parity)
- ✓ Classical models: `ts_forecast_by` methods `'GARCH'` (conditional volatility) and `'Kalman'` (state-space) — v0.7.0
- ✓ Multivariate: `ts_forecast_var_by` (VAR, N value columns → per-variable long-format forecasts) — v0.7.0 (statsmodels VAR parity)
- ✓ Milestone DoD upheld for every new function: runnable verified `examples/*.sql`, committed benchmark parity, `docs/api/` + `docs/reference/models/`, statsmodels/arch/R cross-checks
- ✓ AutoEnsemble SQL surface: `ts_forecast_by(..., 'AutoEnsemble', ..., {top_k, combination_method, seasonal_period})` (top-K across ARIMA/ETS/Theta) — v0.8.0 (Mean cross-check exact) (ENS-01)
- ✓ Six combination methods exposed (Mean, Median, WeightedMSE, InverseAIC, Stacking, HorizonAdaptive) across AutoEnsemble + explicit-member surfaces — v0.8.0 (COMB-01..04)
- ✓ Explicit-member ensemble: `ts_forecast_ensemble_by('table', grp, ds, y, members VARCHAR[], ...)` + `build_forecaster` factory (26-member allowlist, 10 blocked with clear errors) — v0.8.0 (ENS-02)
- ✓ Ensemble prediction intervals via the existing conformal path (`ts_cv_folds_by` + `ts_conformal_calibrate`/apply; per-fold loop for the scalar ensemble surfaces) — v0.8.0 (EPI-01)
- ✓ Ensemble introspection: `ts_ensemble_inspect_by` (members + weights) / `ts_auto_ensemble_inspect_by` (selected members + MSE score + rank) — v0.8.0 (INSP-01)

- ✓ Reference-fixture harness (`validation/reference/`: R + uv-pinned Python generators → versioned JSON fixtures → Rust loader with value assertions) — v0.10.0 Phase 9 (TEST-01)
- ✓ Forecast wrapper delegates intervals/fitted/residuals/model identity to the crate (no fabricated ±z·sd(series)·√h bands, no SES stand-in residuals); real ARIMA/SARIMA via arima_order/seasonal_order; crate exog fits; silent fallbacks → SQL errors on every surface incl. ts_forecast_agg — v0.10.0 Phase 10 (FCST-01..08)
- ✓ anofox-regression =0.5.15 + anofox-forecast 0.15.10 (multi-scale exog OLS matches numpy lstsq; MFLES #219 + panel NaN guards); MSRV corrected to 1.89 — v0.10.0 Phase 9 (DEP-01/02/04)
- ✓ anofox-forecast 0.16.1 consumed with no [patch]/git/path override; ADF/KPSS `regression := 'c'|'ct'` (statsmodels/R-tseries exact, local trend KPSS), standard four-way `ts_stationarity` verdict with explicit `constant`/NULL; model-aware Ljung-Box `fitted_params` (R `Box.test(fitdf)` exact); 36-check R `forecast` parity suite + Monte Carlo interval coverage (±3pp) — v0.10.0 Phase 17 (UPST-06, UPST-01, DIAG-03..05, FCST-09)
- ✓ anofox-forecast 0.16.2 (R KalmanForecast ARIMA/SARIMA intervals) published and consumed — AutoARIMA AirPassengers interval at R parity, zero AutoARIMA R-parity exceptions — v0.10.0 Phase 18 gap plan 18-05
- ✓ Regression-proofing: json statically linked (50 gated tests run), `test/sql/correctness/*` pins every fixed function (inventory gate), length-only Rust-test sweep, `make audit` no-stats-in-C++/SQL guard + CI workflow, permanent `make check-docs` snippet checker, v0.10.0 breaking-changes CHANGELOG + release notes — v0.10.0 Phase 18 (TEST-02, TEST-03, RUST-10, DOCS-01, DOCS-02)

- ✓ All wrapper-level statistical bugs from the 2026-10-08 audit fixed with value-asserting tests — Phases 10-18 — v0.10.0
- ✓ No statistical computation remains in C++ or SQL macros — enforced by `make audit` (Phase 18) — v0.10.0
- ✓ Core statistics, features, detection and conformal outputs match R/scipy/tsfresh references — Phases 13-16, pinned in Phase 18 — v0.10.0
- ✓ Upstream anofox-forecast diagnostics/model-selection fixes merged and consumed — anofox-forecast 0.16.1, Phase 17 — v0.10.0
- ✓ Dependencies bumped — anofox-regression 0.5.15 (Phase 9), fdars-core 0.47 (Phase 15), anofox-forecast 0.16.2 (Phase 18) — v0.10.0

### Active

<!-- v0.11.0 requirements: see .planning/REQUIREMENTS.md (REQ-IDs mapped to phases in ROADMAP.md). -->

- [ ] v0.11.0: close issues #227 #258 #259 #272 #279-#289 (see REQUIREMENTS.md)



Deferred from v0.8.0 (candidates for a future milestone):
- **CV ensemble params:** `ts_cv_forecast_native.cpp` never parses `ensemble_top_k`/`ensemble_method`, so CV backtests use top_k=3/Mean. The former `ts_cv_forecast_by('AutoEnsemble')` segfault is gone on anofox-forecast ≥0.16.1 (re-checked 2026-10-16 at v0.10.0: 2 folds × 7 forecasts, all non-NULL).
- **AutoEnsemble inner combination weights (upstream):** crate 0.15.3 exposes no accessor for the inner ensemble's non-Mean weights; `ts_auto_ensemble_inspect_by` returns NULL weight for WeightedMSE/InverseAIC/Stacking/HorizonAdaptive. Upstream crate enhancement.
- **ENS-F1** custom hand-supplied combination weights; **ENS-F2** panel/global + multivariate (VAR) ensembling; per-member ensemble parameters — all explicitly deferred at v0.8.0 start.
- `build_forecaster` SeasonalWindowAverage `n_seasons=2` hardcoded (TODO ENS-03) — Forecaster trait has no pre-fit series-length hook.

Deferred from v0.7.0 (candidates for a future milestone):
- Intermittent-demand classification (ADI/CV² taxonomy) — INTER-01 descoped; user has a more advanced approach TBD
- Prediction intervals for the new global/panel + GARCH/Kalman/VAR surfaces (route through the existing conformal path)
- VAR automatic lag-order selection (AIC/BIC); per-panel VAR; GARCH advanced coefficient overrides beyond p/q

### Out of Scope

- Anomaly detection (Mahalanobis/Parade/ZBank) — deferred to a later milestone despite `anomaly` feature being compiled in; not selected for v1
- Hierarchical reconciliation (MinTrace/BottomUp/TopDown/MiddleOut) — large standalone capability, own milestone
- Forecastability / triage (AMI, GCMI, transfer entropy, Lyapunov, STI, `run_triage`) — requires enabling the `forecastability` crate feature; deferred
- Multicollinearity / VIF — deferred with the exogenous-regression track
- Power transforms (Box-Cox / Yeo-Johnson) and scaling/rolling/EWM transforms — deferred (pairs with global-regression-fe work later)
- Extra conformal methods (IDR, QRA, CQR, EnbPI, binned) and extra changepoint algorithms (Binseg/BottomUp/Dynp/Window/KernelCpd) — existing coverage sufficient for now
- Outlier detection, model persistence (save/load), feature selection — deferred

## Context

- **Delivery pattern (established):** new capability = Rust FFI `#[no_mangle] pub extern "C"` export in `crates/anofox-fcst-ffi` → C++ table/scalar/aggregate function in `src/` → registration in `src/anofox_forecast_extension.cpp` → user-facing `ts_*_by` macro in `src/macros/ts_macros.cpp` → `examples/*.sql` → `docs/`.
- **Crate features currently enabled:** `anomaly`, `serde`, and default `postprocess` (→ `distributional`). NOT enabled: `forecastability`, `seasonal-detection`, `parallel`. The diagnostics and models in this milestone live under already-enabled features (`crate::validation`, `crate::models::*`), so no new feature flags are required for v1 scope.
- **Global models** are panel/batch forecasters (`GlobalETS`/`GlobalTheta`/`GlobalCroston`, `crate::batch`) — they cross-learn across series, so the SQL surface must accept a grouped panel, not a single series. This differs from the per-series `ts_forecast_by` dispatch and needs design attention.
- **VAR** is multivariate — output/interface shape differs from univariate models; may warrant its own function rather than a `method` string on `ts_forecast_by`.
- **Diagnostics** operate on residuals or a raw series and return scalar/struct verdicts — natural fit for scalar functions + `_by` macros, mirroring the metrics functions.
- Verified reference: docs SQL examples must be run through the built extension, not eyeballed (established rule from PR #230).
- **Shipped v0.7.0** (2026-08-22): +17.9k LOC across 57 commits / 111 files. Extension now surfaces 36 forecasting models (incl. GARCH, Kalman, panel Global*, multivariate VAR via `ts_forecast_var_by`) plus 7 statistical-diagnostic functions. `arch` added to `benchmark/.venv` (comparison group) for GARCH parity. New non-globbed C++ sources (`diagnostics.cpp`, `ts_forecast_panel_native.cpp`, `ts_forecast_var_native.cpp`) are explicitly listed in CMakeLists.
- **Panel/table-in macro convention (v0.7.0 lesson):** table-in macros must wrap `query_table(...)` in a subselect `(SELECT ... FROM query_table(...))`; a bare TABLE arg silently fails to register.
- **Shipped v0.8.0** (2026-08-31): +13.9k LOC across 57 files / 3 phases / 6 plans. Extension now surfaces AutoEnsemble (`ts_forecast_by('AutoEnsemble')`) + explicit-member ensembles (`ts_forecast_ensemble_by`) with six combination methods, ensemble conformal intervals (existing path), and member/weight introspection (`ts_ensemble_inspect_by` / `ts_auto_ensemble_inspect_by`). Two new non-globbed C++ sources (`ts_forecast_ensemble_native.cpp`, `ts_ensemble_inspect_native.cpp`) explicitly listed in CMakeLists. No external reference library for ensembles — verification is internal-consistency cross-checks (combined == manual weighted combination of members).
- **`ts_cv_forecast_by('AutoEnsemble')`** no longer segfaults (anofox-forecast ≥0.16.1; re-checked at v0.10.0 close), but the CV native still ignores the ensemble params. The per-fold `_ts_forecast_scalar` workaround can be retired.

## Constraints

- **Tech stack**: DuckDB extension built for v2.0.0 (stable), v1.5.6 and v1.4.5 LTS; Rust 1.89+ core via FFI; C++17. No new languages.
- **Architecture**: Parallelism stays at the DuckDB GROUP BY / scalar-function layer — no custom threading or table-in/table-out (established project rule).
- **Dependencies**: anofox-forecast 0.16.2, anofox-regression =0.5.15, fdars-core 0.47 (as of v0.10.0). Upstream fixes ship as crate releases consumed from crates.io; no local `[patch]` for anofox-forecast.
- **Correctness**: Statistics are computed in Rust only; every statistical output is checked against a reference implementation (R / scipy / statsmodels / tsfresh) via checked-in fixtures — length-only tests do not count.
- **Compatibility**: Must build and load across Linux/macOS/Windows and WASM; OpenSSL stays statically linked; verify clean-machine load (not just green CI).
- **Verification**: Every new SQL function must be exercised by a runnable example against the built extension before it counts as done.

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Scope milestone to diagnostics + model coverage (defer anomaly, reconciliation, triage) | Both chosen themes reuse already-enabled crate features and the existing exposure pattern; lower risk than new-feature-flag work | ✓ Good — v0.7.0 shipped all diagnostics + 6 new models via the existing pattern with no new crate feature flags |
| Expose diagnostics as scalar functions + `ts_*_by` macros | Mirrors existing metrics surface; returns scalar/struct verdicts per series | ✓ Good — 7 diagnostic functions shipped, statsmodels-cross-checked |
| Global/panel models need a panel-aware SQL surface | GlobalETS/Theta/Croston cross-learn across series; per-series `ts_forecast_by` dispatch is insufficient | ✓ Good — `ts_forecast_panel_by` fit-once-emit-many native table function delivered |
| VAR is a dedicated multivariate function (`ts_forecast_var_by`), not a `method` string | Multivariate I/O shape (N cols → N forecasts) differs from univariate `ts_forecast_by` | ✓ Good — long-format `{variable, forecast_date, forecast_value}` surface delivered |
| Definition of done = example + benchmark parity + docs + reference cross-check | User requires all four validation signals for every item | ✓ Good — upheld for all 13 requirements |
| ForecastOptions FFI ABI extended additively for GARCH/Kalman params | Backward-compatible with existing univariate methods; avoids a parallel options struct | ✓ Good — integration-checker confirmed no ABI breakage across pre-milestone methods |
| Autonomous code-review + fix loop after each phase | Happy-path verifiers miss edge cases (spurious intervals, WASM free UB, overflow) | ⚠️ Revisit — caught real bugs, but each phase needed 2–3 fix iterations; consider tightening executor guidance to prevent recurrence |
| AutoEnsemble & explicit-member ensembles share `parse_combination_method` + `build_forecaster` back-ends (v0.8.0) | One combination-method parser + one name→forecaster factory keep all six methods consistent across three SQL surfaces | ✓ Good — integration-checker confirmed identical method behavior across `ts_forecast_by('AutoEnsemble')`, `ts_forecast_ensemble_by`, `ts_ensemble_inspect_by` |
| Default combination method = Mean (override crate's WeightedMSE default) (v0.8.0) | Makes the internal-consistency cross-check (combined == arithmetic mean of members) the default, most-verifiable path | ✓ Good — Mean cross-check diff=0.0 across all ensemble surfaces |
| Explicit-member ensemble as a dedicated `ts_forecast_ensemble_by` macro, not a method-string (v0.8.0) | A `VARCHAR[]` member list + method doesn't fit the single method-string of `ts_forecast_by`; ScalarFunction dispatch mirrors `_ts_forecast_scalar` | ✓ Good — new native ScalarFunction + CMakeLists entry; per-series long-format output |
| EPI-01 reuses the existing conformal path (no new interval machinery) (v0.8.0) | Conformal is model-agnostic on residual columns; DoD says reuse, don't rebuild | ✓ Good — bounds produced for both surfaces; surfaced a pre-existing `ts_cv_forecast_by('AutoEnsemble')` segfault (worked around + documented as tech debt) |
| INSP-01 scoped to what crate 0.15.3 exposes: full weights for explicit-member, NULL weights for AutoEnsemble non-Mean (v0.8.0) | Crate has no accessor for AutoEnsemble's inner combination weights; re-implementing selection would be fragile/drift-prone | ✓ Good — honest surface; limitation documented; upstream enhancement filed as tech debt |
| Verify executor/agent reports against git+disk, re-run every example against the built extension (v0.8.0) | Two executors fabricated commit hashes/SUMMARY files; three long-running agents died on transient API errors mid-run | ✓ Good — caught + reconciled all fabrications; per-task commits protected work across agent deaths |
| Reference-fixture harness before any statistical fix (v0.10.0) | Length-only tests let every audited bug ship; fixes need checked-in R/scipy/numpy reference values | ✓ Good — Phase 9 fixtures regenerate byte-identically; 125-entry pre-bump snapshot proved only MFLES/Laplace moved |
| MSRV 1.89 (v0.10.0) | trueno (via anofox-forecast) needs AVX-512 intrinsics stabilised in 1.89; 1.86 claim was false | ✓ Good — `cargo +1.88.0` refused, `+1.89.0` builds/tests; CI rust-msrv job |
| Pass crate-native intervals through even where known-miscalibrated (v0.10.0 P10) | No fabrication; calibration fixed at the source (Phase 11 upstream) rather than masked in the wrapper | ✓ Good — 0.16.1 intervals reach R parity except one documented AutoARIMA AirPassengers interval gap (RC-05, crate SARIMA variance; upstream issue drafted) |
| SQL `'Holt'` = ETS(A,A,N), R `holt()`'s definition (v0.10.0 P17, D-13) | `HoltLinearTrend::auto()` diverged from R by 3–13x on parity fixtures | ✓ Good — R-parity point deviation cut 6.86x (AirPassengers), 13.41x (rw), 3.01x (trend) |
| Upstream fixes ship as crate releases, never local [patch] (v0.10.0 P18, user 2026-10-16) | Extension must consume what crates.io serves; a local patch hides release drift | ✓ Good — v0.16.2 merged + published upstream, then consumed from crates.io |
| Every R-parity known gap must cite an evidenced root cause, never a loosened tolerance (v0.10.0 P17, D-12) | Prevents tolerance-widening from masking real bugs | ✓ Good — RC-01..RC-05 table; extension-side cause fixed, 3 crate-side causes drafted as upstream issues with repros |

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd-transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd-complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

---
*Last updated: 2026-10-22 after starting milestone v0.11.0*
