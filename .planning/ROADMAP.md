# Roadmap: anofox-forecast

## Milestones

- ✅ **v0.7.0 — Close the Crate→Extension Gap (Diagnostics + Model Coverage)** — Phases 1-3 (shipped 2026-08-22)
- ✅ **v0.8.0 — Ensemble Forecasting** — Phases 4-6 (shipped 2026-08-31)
- 🗄 **v0.9.0 — WASM Runtime Verification** — Phases 7-8 (parked; Phase 8 operator live-CI negative control outstanding)
- 🚧 **v0.10.0 — Statistical Correctness** — Phases 9-18 (in progress)

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
<summary>🗄 v0.9.0 — WASM Runtime Verification (Phases 7-8) — PARKED, phase dirs archived 2026-10-08</summary>

Phase artifacts (plans, summaries, verification): [milestones/v0.9.0-phases/](milestones/v0.9.0-phases/). Not yet closed via `/gsd-complete-milestone`.

- [x] Phase 7: WASM Node Harness + Local Green (1/1 plans) — completed 2026-09-02
      DuckDB-Wasm Node harness (`test/wasm/run.mjs` + `sqllogic.mjs`), `@duckdb/duckdb-wasm` engine-version pin, openssl `!wasm32` vcpkg guard (WASM-01, WASM-02, DEP-01, DEP-02). WASM-03 full-suite green deferred: 23 files fail on pre-existing `test/sql` API drift, not WASM.

- Phase 8: CI Gating + Dedicated Workflow + Badge (1/1 plans; verification deferred to operator)
      `wasm-runtime-test` gating job (curated subset, no `--all`), `WasmTest.yml`, README badge (CI-01..03) — implemented and locally verified (curated exit 0 → broken test exit 1 → reverted exit 0). Outstanding: push the branch, observe the gate green → red (deliberate curated break) → green in GitHub Actions, confirm the badge flips, record run URLs, then `/gsd-verify-work 8` and `/gsd-complete-milestone v0.9.0`.

</details>

### 🚧 v0.10.0 — Statistical Correctness (Phases 9-18)

**Milestone Goal:** Every statistic the extension returns is correct — verified against R / scipy / statsmodels / tsfresh reference values via checked-in fixtures — and is computed in the Rust crates; C++ and SQL only marshal, group and dispatch. Input: `.planning/research/STAT-AUDIT-2026-10-08.md` (findings A–I). Branch: `fix/statistical-correctness`.

**Phase Numbering:**

- Integer phases (9, 10, 11): Planned milestone work
- Decimal phases (e.g. 10.1): Urgent insertions (marked with INSERTED)

- [x] **Phase 9: Reference Fixtures & Safe Dependency Bumps** - Checked-in R/Python fixture harness + anofox-regression 0.5.15, anofox-forecast 0.15.10, honest MSRV (completed 2026-10-09)
- [x] **Phase 10: Forecast Wrapper Delegates to Crate Models** - Intervals, fitted values, residuals and model identity come from the crate; real ARIMA; no silent fallbacks (completed 2026-10-09)
- [ ] **Phase 11: Upstream anofox-forecast Correctness Fixes** - Augmented ADF + MacKinnon, bounded KPSS, AutoARIMA order selection, ETS likelihood/intervals, Theta — as PR(s) in the upstream repo
- [ ] **Phase 12: Statistics Marshalling Moves to Rust** - ToUnifiedFormat + validity for list args, Rust-owned NULL policy, diff/fill/backtest/hierarchy/agg delegated to Rust
- [ ] **Phase 13: Core Statistics, Metrics & Data Quality** - scipy-exact moments, STL F_T/F_S strengths, Hyndman-Koehler MASE, MAPE zero policy, real gap/frequency data quality
- [ ] **Phase 14: tsfresh Feature Parity** - All 117 features match tsfresh on reference series or carry a documented deviation
- [ ] **Phase 15: Detection & Decomposition on fdars 0.47** - fdars 0.47 bump fixes detrend/seasonality no-ops; scipy peaks, correct BOCPD/PELT, STL-based MSTL, robust period detection
- [ ] **Phase 16: Conformal & Bootstrap Correctness** - Horizon-ordered, per-group conformal surface computed in Rust; correct split/asymmetric/adaptive/jackknife+ and bootstrap sign
- [ ] **Phase 17: Upstream Consumption & Diagnostics Parity** - Extension consumes the released upstream fixes; stationarity verdicts, model-aware Ljung-Box and R `forecast` parity verified from SQL
- [ ] **Phase 18: Correctness Regression Suite, Audit & Docs** - Value-asserting tests everywhere, SQL correctness suite, no-stats-in-C++ audit, docs/skills/CHANGELOG synced

## Phase Details

### Phase 9: Reference Fixtures & Safe Dependency Bumps

**Goal**: Every later fix can be proven against checked-in R/Python reference values, and the extension runs on the low-risk dependency bumps — with exogenous regression already producing correct coefficients and the stated MSRV matching reality.
**Depends on**: Nothing (first phase of v0.10.0)
**Requirements**: TEST-01, DEP-01, DEP-02, DEP-04
**Success Criteria** (what must be TRUE):

  1. Running the checked-in generators under `validation/reference/` (R + Python, package versions recorded) regenerates the versioned JSON/CSV fixtures, a Rust test loads a fixture (e.g. AirPassengers, scipy moments) and asserts values to a stated tolerance, and the README documents regeneration commands, tool versions and per-family tolerances.
  2. With anofox-regression `=0.5.15`, `ts_forecast_exog_by` on the multi-scale-regressor fixture yields a regressor coefficient matching numpy `lstsq` (≈ −8.8, previously −79), and a 15k-row exogenous fit completes without the O(n²) memory blow-up (previously ~1.7 GB).
  3. With anofox-forecast `≥0.15.10`, the MFLES #219 runaway reproducer returns bounded forecasts, panel forecasts on the NaN-guard fixture contain no NaN, and point forecasts of all other `ts_forecast_by` models are unchanged vs the 0.15.3 snapshot outside the documented MFLES/panel/Laplace fixes.
  4. The MSRV stated in README / CLAUDE.md / docs and the CI toolchain pin equal the minimum Rust version the dependency tree actually requires, and a build on exactly that toolchain succeeds.

**Plans**: 3/3 plans executed

Plans:
**Wave 1**

- [x] 09-01-PLAN.md — R reference generators + fixtures, Rust fixture loader + provenance contract, package-legitimacy gate (wave 1)

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 09-02-PLAN.md — uv Python generators (moments, multi-scale OLS, panel NaN guard), self-consistency tests, harness README (wave 2)

**Wave 3** *(blocked on Wave 2 completion)*

- [x] 09-03-PLAN.md — 0.15.3 snapshot, anofox-regression =0.5.15 / anofox-forecast 0.15.10 bump with OLS/memory/MFLES/panel proofs, MSRV 1.89 + CI pin, SQL check on built extension (wave 3)

### Phase 10: Forecast Wrapper Delegates to Crate Models

**Goal**: `ts_forecast_by`, `ts_forecast_exog_by` and the ensemble surfaces return the prediction intervals, fitted values, residuals and model identity the crate model actually produced — no fabricated point ± z·sd(series)·√h intervals, no SES(α=0.3) stand-in residuals, no fixed-coefficient "ARIMA", no silent model/parameter fallbacks.
**Depends on**: Phase 9 (fixtures; anofox-forecast 0.15.10 interval/exog API; anofox-regression 0.5.15)
**Requirements**: FCST-01, FCST-02, FCST-03, FCST-04, FCST-05, FCST-06, FCST-07, FCST-08
**Success Criteria** (what must be TRUE):

  1. On AirPassengers, 95% h=1 intervals differ per model and come from the crate's `predict_with_intervals` — Naive half-width ≈ R `naive()` ±66 instead of 234 for every model; no model reproduces the old point ± z·sd(series)·√h width; models without native intervals return NULL bounds; Laplace distributional intervals pass through unchanged; any remaining z uses the exact Φ⁻¹ (level 0.80 → 1.2816, not the step-table 1.0).
  2. In-sample fitted values, residuals and MSE come from the crate model: SES, Holt, AutoARIMA, AutoETS, Theta, HW, MSTL and Croston no longer share the identical MSE 2090.28, and each reported MSE equals the mean of that model's crate residuals² over non-interpolated points.
  3. `'ARIMA'` with a user order (or the documented default) returns a real crate ARIMA fit whose forecasts match R `Arima()` at that order within 1%, never the AR(1)-with-coefficient-0.5; on the exog path AutoARIMA actually runs its order search (the returned model name shows the selected order), Optimized/Dynamic Theta stay themselves, ETS/GARCH/Kalman options are honoured, and a regressor that is constant in history yields a finite forecast with a 0 effect (not all-NaN).
  4. Every former silent fallback is an SQL error or explicitly reported — AutoETS crate failure, `'ETS'` without a spec, AutoMFLES/AutoMSTL aliases, MSTL/TBATS/HW without a seasonal period (no default 12 / 2), unknown `kalman_model`, misspelled panel Croston variant, invalid UTF-8 model name, unparsable seasonal-period entries — and the returned model name always identifies the model that actually ran.
  5. An ensemble member built by `build_forecaster` (e.g. `'ETS'`) produces exactly the same forecast as the standalone model with the same parameters; an all-NULL series errors, a valid NaN is treated as missing, and regressor NULLs are handled via validity — all documented in `docs/api/`.

**Plans**: 10/10 plans executed

Plans:
**Wave 1**

- [x] 10-01-PLAN.md — crate intervals/fitted/residuals passthrough, crate baselines, input policy + MSE, R-fixture tests (wave 1)

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 10-02-PLAN.md — real crate ARIMA via arima_order/seasonal_order, strict FFI option parsing, C++ param keys, NULL-safe agg/CV, SQL test (wave 2)

**Wave 3** *(blocked on Wave 2 completion)*

- [x] 10-03-PLAN.md — silent fallbacks → errors, 'ETS' = AutoETS, shared constructors (ensemble == standalone), ensemble intervals (wave 3)

**Wave 4** *(blocked on Wave 3 completion)*

- [x] 10-04-PLAN.md — exog via crate exog support, constant regressors, regressor validity, SQL exog params + NULL-safe lists (wave 4)

**Wave 5** *(blocked on Wave 4 completion)*

- [x] 10-05-PLAN.md — self-checking runnable example, natively-run SQL test migration, green make test_release (wave 5)

**Wave 6** *(blocked on Wave 5 completion)*

- [x] 10-06-PLAN.md — 07-forecasting.md, CHANGELOG breaking changes, models skill, CV docs, README, examples (wave 6)

**Waves 7-9** *(sequential — shared snippet harness files)*

- [x] 10-07-PLAN.md — reference pages: baseline, exponential smoothing, Croston/ADIDA (wave 7)
- [x] 10-08-PLAN.md — reference pages: state-space, Theta, multi-seasonal (wave 8)
- [x] 10-09-PLAN.md — reference pages: AutoTBATS, IMAPA/TSB, Global*, Laplace, GARCH, ensembles + note sweep (wave 9)

**Wave 10** *(gap closure — blocked on Wave 9 completion)*

- [x] 10-10-PLAN.md — gap closure: ts_forecast_agg params validation/threading/confidence_level parity with ts_forecast_by (shared params header), Theta/CrostonClassic fitted identity pinned + routed to Phase 11, agg docs/CHANGELOG truth pass (wave 10)

### Phase 11: Upstream anofox-forecast Correctness Fixes

**Goal**: The upstream crate's stationarity tests, AutoARIMA order selection, ETS, Theta and interval-producing models are statistically correct against R / statsmodels / arch, delivered as reviewed PR(s) the user can merge and publish while extension-side phases continue.
**Depends on**: Phase 9 (reference generators reused for upstream fixtures). Works in `~/projects/rust/anofox-forecast` on a fix branch; independent of Phases 10 and 12-16.
**Requirements**: DIAG-01, DIAG-02, UPST-01, UPST-02, UPST-03, UPST-04, UPST-05
**Success Criteria** (what must be TRUE):

  1. Upstream ADF is augmented (lagged Δy in the regression), supports constant and constant+trend, selects lags by AIC/BIC and reports MacKinnon p-values; the statistic matches statsmodels `adfuller` / `urca::ur.df` on fixtures and Monte-Carlo rejection rate at 5% on an IMA(1,1) unit root is ≈5% (previously 63.5%). KPSS p-values are bounded to [0.01, 0.10] with in-table interpolation matching `tseries::kpss.test`.
  2. AutoARIMA selects d by KPSS and D by a seasonal test (OCSB or seasonal strength) and ranks candidates by AICc on a common likelihood basis; in simulation, random walks select a (0,1,0)-class model at a frequency close to R `auto.arima` (~70%, previously 1%) and stationary AR(0.7) gets d≥1 at a frequency close to R (~19%, previously 98%).
  3. ETS multiplicative-error likelihood uses Σ ln|ŷ| over all observations with the usual parameter constraints, AirPassengers MAM smoothing parameters are near R `ets` (α/γ ≈ 0.40, previously 0.9999), and ETS intervals use Hyndman et al. class-1/2 analytic variances (simulation for class 3) with Monte-Carlo coverage verified.
  4. Theta (STM) estimates α by SES optimisation with the standard interval formula and no random-walk bias vs R `thetaf`; TBATS/MSTL/GARCH interval widths match statsforecast/arch within the upstream validation tolerance, or those models return NULL intervals with documentation.
  5. All changes sit on a fix branch in `~/projects/rust/anofox-forecast` with PR(s) open, upstream `cargo test` / `cargo fmt --check` / `cargo clippy` green, and CHANGELOG entries describing each numerical change.

**Plans**: 9/11 plans executed

Plans:

- [ ] 11-11-PLAN.md

**Waves 1-10** *(strictly sequential — all plans share one upstream working tree; each fix branch starts from the base ref chosen in 11-01, never from a sibling branch)*

- [x] 11-01-PLAN.md — base-ref decision checkpoint (origin/main is v0.15.9; released 0.15.10 is on unmerged perf branch) + clean fmt/clippy/test baseline env contract (wave 1)
- [x] 11-02-PLAN.md — fix/adf-kpss-correctness: augmented ADF (n/c/ct, AIC/BIC/t-stat/fixed), MacKinnon p-values + critical values, bounded KPSS p-value, urca/tseries/statsmodels fixtures, MC size, PR (wave 2)
- [x] 11-03-PLAN.md — fix/theta-alpha-intervals: STM alpha + l0 by SES SSE, thetaf drift factor and intervals, thetaf fixtures, PR (wave 3)
- [x] 11-04-PLAN.md — fix/auto-arima-order-selection (1/2): ndiffs-KPSS d, nsdiffs-seas D before d, common-basis AICc with constant in both scorers, RW/AR(0.7) frequencies, draft PR (wave 4)
- [x] 11-05-PLAN.md — fix/auto-arima-order-selection (2/2): R-default Hyndman-Khandakar stepwise (max P/Q 2, max_order 5, constant moves), AirPassengers, M4 gate, CHANGELOG, PR ready (wave 5)
- [x] 11-06-PLAN.md — fix/ets-likelihood-intervals (1/3): Σ ln|ŷ| over all obs, R likelihood convention, usual constraints, R restrict=TRUE AutoETS pool, draft PR (wave 6)
- [x] 11-07-PLAN.md — fix/ets-likelihood-intervals (2/3): Hyndman class-1/2 analytic + class-3 simulated ETS intervals, MC coverage (wave 7)
- [x] 11-08-PLAN.md — fix/ets-likelihood-intervals (3/3): SES/Holt/HoltWinters/SeasonalES intervals via the ETS helper, CHANGELOG, PR ready (wave 8)
- [x] 11-09-PLAN.md — fix/tbats-mstl-garch-intervals (1/2): GARCH root-cause spike vs statsforecast + true DGP, then calibrated intervals or point-only (D-10), draft PR (wave 9)
- [ ] 11-10-PLAN.md — fix/tbats-mstl-garch-intervals (2/2): TBATS state-space variance, MSTL trend-model intervals, coverage, CHANGELOG, PR ready (wave 10)

### Phase 12: Statistics Marshalling Moves to Rust

**Goal**: C++ and SQL macros only marshal, group and dispatch — list arguments are read correctly for every row, NULLs reach Rust as validity with Rust owning the missing-data policy, and the remaining SQL/C++ statistics (differencing, mean fill, backtest metrics, hierarchy sums, aggregate frequency stepping) are delegated to Rust.
**Depends on**: Phase 10 (forecast input-handling policy from FCST-08 that `ts_forecast_agg` aligns to; exog path from FCST-05)
**Requirements**: RUST-01, RUST-02, RUST-03, RUST-05, RUST-06, RUST-07, RUST-08, RUST-09
**Success Criteria** (what must be TRUE):

  1. `ts_mae([1,NULL,3,4],[1,2,NULL,4])` returns 0 (pairwise-complete; previously 0.333) and `ts_mae(a, [0,0,0])` over a 5-row table returns the correct value on every row (previously 1 then NULL×4); one shared C++ list helper replaces the ~10 `ExtractListAsDouble` copies, and single-series diagnostics given interior NULLs error or report explicitly instead of splicing.
  2. `ts_diff_by(..., 2)` equals numpy `diff(n=2)` (second-order difference, not lag-2) with the seasonal lag available as a separate parameter; `ts_fill_nulls_mean_by` output equals the Rust `fill_nulls_mean`; `drop_constant` applies the Rust tolerance so SQL and Rust paths classify a near-constant series identically.
  3. `ts_backtest_native` either computes per-group metrics through the Rust metric functions (unknown metric → error, missing bounds → NULL, error = actual − forecast consistently with SQL) or is removed together with every stale `ts_backtest_auto_by` reference; hierarchy aggregation returns NULL for an all-NULL group and equals SQL `SUM` for partially-NULL groups.
  4. `ts_forecast_agg` on month-end monthly data emits month-end forecast dates with no day drift (Rust frequency detection, calendar stepping), `ts_forecast_exog_by` future dates are calendar-correct across months of different lengths, and `ts_forecast_agg` / `ts_features_agg` on a series with interior NULLs match `ts_forecast_by` / `ts_features` (validity passed, no splicing).
  5. Per-step conformal given a residual count that is not a multiple of the horizon returns an error naming both counts, and the dead `ts_filter.cpp` / `ts_imputation.cpp` scalar bodies are removed or registered with tests.

**Plans**: 6/8 plans executed

Plans:
**Wave 1**

- [x] 12-01-PLAN.md — Tracer: shared C++ list reader + Rust missing-data policy module; paired metrics pairwise-complete end-to-end (ts_mae 0 / constant-list per row) (wave 1)

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 12-02-PLAN.md — Rust single-series gate (trim edges, interior-NULL error, empty -> NULL); diagnostics, period estimators, seasonality; per-row argument reads (wave 2)

**Wave 3** *(blocked on Wave 2 completion)*

- [x] 12-03-PLAN.md — Gate for detrend/decompose, peaks, MSTL, BOCPD; ts_features_agg validity + ts_features_by parity (D-11); dead list bodies removed (wave 3)

**Wave 4** *(blocked on Wave 3 completion)*

- [x] 12-04-PLAN.md — Conformal + bootstrap on shared reader with Rust NULL classes; per-step residual-count/horizon error (D-10) (wave 4)

**Wave 5** *(blocked on Wave 4 completion)*

- [x] 12-05-PLAN.md — Rust calendar rule + deterministic detect_frequency; ts_forecast_agg validity + month-end dates; ts_forecast_by on the same rule (wave 5)

**Wave 6** *(blocked on Wave 5 completion)*

- [x] 12-06-PLAN.md — _ts_future_dates: calendar-correct ts_forecast_exog_by dates; ensemble/panel/var/native steppers on the Rust rule (wave 6)

**Wave 7** *(blocked on Wave 6 completion)*

- [ ] 12-07-PLAN.md — ts_diff_by d-th order via Rust diff (+ seasonal_lag), ts_fill_nulls_mean_by / ts_drop_constant_by via Rust; dead ts_filter/ts_imputation deleted (wave 7)

**Wave 8** *(blocked on Wave 7 completion)*

- [ ] 12-08-PLAN.md — Hierarchy SQL SUM NULL semantics; _ts_backtest_native + stale ts_backtest_auto_by refs removed; phase-wide docs/skills/CHANGELOG sweep + deferred items (wave 8)

### Phase 13: Core Statistics, Metrics & Data Quality

**Goal**: `ts_stats`, the accuracy metrics and data-quality scoring return the textbook statistic they are named after — scipy-exact moments, Wang/Hyndman STL strengths, Hyndman-Koehler MASE, an explicit MAPE zero policy, and data-quality/frequency results that reflect the real dates.
**Depends on**: Phase 12 (metric scalar marshalling + validity)
**Requirements**: WRAP-02, WRAP-03, STAT-01, STAT-02, STAT-03, STAT-04, STAT-05, STAT-06
**Success Criteria** (what must be TRUE):

  1. `ts_stats` skewness/kurtosis equal scipy `skew(bias=False)` / `kurtosis(bias=False)` to 1e-10 on fixtures (x20 kurtosis +0.277, previously −0.096), and the bimodality coefficient uses the SAS small-sample formula (0.466, previously 0.600 — which crossed the 0.555 threshold).
  2. Trend and seasonal strength equal the Wang/Hyndman STL F_T / F_S definitions on R `feasts` reference fixtures (a random walk's seasonal strength ≈ 0.05, previously 0.96) and the EDA skill thresholds are updated; entropy and gap-count outputs are correctly named and documented (missing periods vs gap intervals distinguishable); `behavioral_score` no longer penalises a predictable smooth series without a documented rationale.
  3. `ts_mase` is Hyndman-Koehler — scaled by the in-sample seasonal-naive MAE with a seasonal-period parameter — matching R `forecast::accuracy()` MASE on fixtures, with the old relative-MAE result available only as `ts_rmae`; MAPE with zero actuals follows one explicit policy (NULL, or documented exclusion with the excluded count reported).
  4. `ts_data_quality_by` on a series with known gaps reports the real gap count and a temporal score below 1.0 (previously always 0 gaps / 1.0), honours frequency and n_short, and returns identical results across repeated runs on tied-mode data.
  5. `detect_frequency` returns the same answer across runs on tied step data with its µs unit documented, and monthly/quarterly/yearly gap fill on month-end data preserves month-end dates (2024-01-31 → 2024-02-29 → 2024-03-31, not the 1st of the month).

**Plans**: TBD

### Phase 14: tsfresh Feature Parity

**Goal**: All 117 features returned by `ts_features*` match tsfresh on reference series, or carry a documented, intentional deviation.
**Depends on**: Phase 12 (`ts_features_agg` validity; shared list marshalling)
**Requirements**: FEAT-01, FEAT-02, FEAT-03, FEAT-04, FEAT-05
**Success Criteria** (what must be TRUE):

  1. `partial_autocorrelation` lags 1–N match statsmodels `pacf(method='ld')` / tsfresh via Durbin-Levinson (lags 3–5 no longer reuse the lag-2 formula).
  2. `mean_second_derivative_central` (with the ½ factor), `last_location_of_max/min` ((idx+1)/n), `time_reversal_asymmetry_statistic`, signed `agg_linear_trend` rvalue and bias-corrected skewness/kurtosis equal tsfresh on the fixture series.
  3. `fft_coefficient` is unnormalised and computed via FFT (O(n log n) instead of the O(n²) DFT), and spectral centroid/variance use the one-sided tsfresh `fft_aggregated` definition and match tsfresh.
  4. `sample_entropy` (≈ 1.099 on the reference series, previously 0.981), `binned_entropy`, `benford_correlation`, `permutation_entropy`, `lempel_ziv_complexity` and `number_peaks` match tsfresh, or are renamed with their definitions documented.
  5. A tsfresh-generated fixture over ≥4 series covers all 117 features, and a test asserts every feature within tolerance or its presence on a documented intentional-deviation list.

**Plans**: TBD

### Phase 15: Detection & Decomposition on fdars 0.47

**Goal**: Detrending, seasonality classification, period detection, peaks, changepoints and MSTL decomposition return correct, non-trivial results matching numpy / scipy / R — running on fdars-core 0.47, whose FdMatrix API structurally prevents the swapped size-argument bug that made detrend/decompose/seasonality no-ops.
**Depends on**: Phase 12 (shared list helper + validity in the detection scalar functions)
**Requirements**: DEP-03, WRAP-01, DET-01, DET-02, DET-03, DET-04, DET-05, DET-06, DET-07
**Success Criteria** (what must be TRUE):

  1. fdars-core 0.47 with features `["parallel"]` (no `linalg`) builds natively and for WASM and `cargo tree` no longer contains anofox-regression 0.4.0; `ts_detrend` / `ts_detrend_by` on a cubic series return a trend matching numpy `polyfit` to 1e-8 (previously trend = 0, detrended = input), `'quadratic'` maps to degree 2 or errors, `ts_decompose_seasonal` returns non-trivial components, and `classify_seasonality` labels a strong sine as seasonal (previously non-seasonal / NaN).
  2. Peak detection follows scipy `find_peaks` semantics (indices, plateaus, prominence) and matches scipy on fixtures — every reference peak found at the right index and no minima reported (previously 51 found vs scipy's 66, only 31 matching).
  3. BOCPD detects a 3σ step change and stays quiet on stationary noise (normalised Student-t predictive, correct Normal-Gamma update, truncation that keeps probability mass); PELT returns the same changepoints for a series and for that series ×100 (previously 32 spurious changepoints), with median-based L1 cost and working pruning.
  4. MSTL processes periods shortest-first with Loess-based STL components matching R `forecast::mstl` / statsmodels `MSTL` within documented tolerance, and even-window moving averages are correct (a constant series of 1s yields a trend of exactly 1.0, previously 1.05).
  5. FFT period detection on trend + seasonal data returns the seasonal period (not n) and handles odd n; ACF period accepts lag 2 and requires significance; regression-period defaults find period 12; Lomb-Scargle rejects invalid inputs; seasonal-strength variants detrend first and wavelet strength is ≈ 0 on white noise (or the method is removed); `is_constant` uses a scale-relative tolerance and edge-NULL interpolation behaviour is documented.

**Plans**: TBD

### Phase 16: Conformal & Bootstrap Correctness

**Goal**: Conformal and bootstrap intervals have their nominal coverage and correct shape end-to-end — horizon order and pairing preserved in SQL, scores and empirical coverage computed in Rust, per-group calibration available, and each conformal variant implementing the method it is named after.
**Depends on**: Phase 12 (shared list helper and pairwise-complete validity in the conformal scalars)
**Requirements**: RUST-04, CONF-01, CONF-02, CONF-03, CONF-04, CONF-05
**Success Criteria** (what must be TRUE):

  1. `ts_conformal_by`, `ts_conformal_apply_by` and `ts_interval_width_by` order and pair by a date/step column, so the interval at step h belongs to the forecast at step h (previously ordered by forecast value, with lower/upper sorted independently); residuals and empirical (not nominal) coverage are computed in Rust; `ts_conformal_calibrate` calibrates per group (two groups with different residual scales get different quantiles).
  2. Bootstrap intervals on a right-skewed residual fixture place the long tail on the correct side (residual sign fixed; paths no longer mirrored).
  3. Split conformal returns infinite/NULL width (or errors) when ⌈(n+1)(1−α)⌉ > n instead of clamping to the max residual, rejects α ∉ (0,1), and filters NaN residuals without panicking.
  4. Asymmetric conformal uses the α/2 and 1−α/2 quantiles of signed residuals, with simulated coverage within ±2pp of nominal (previously ~40% too wide); adaptive conformal uses |r|/d scores at calibration and d-scaling at prediction, giving the same interval for a row regardless of what else is in the batch.
  5. `'JackknifePlus'` either implements jackknife+ (verified against a reference implementation) or is renamed/removed, with the old name producing a clear error.

**Plans**: TBD

### Phase 17: Upstream Consumption & Diagnostics Parity

**Goal**: The extension runs on the corrected upstream crate, and SQL users get statsmodels/R-matching stationarity verdicts, a model-aware Ljung-Box test, and point/interval parity with R `forecast` across the core model set.
**Depends on**: Phase 11 (upstream PRs merged and released by the user; interim `[patch]` allowed), Phase 10 (crate intervals surfaced by the wrapper)
**Requirements**: UPST-06, DIAG-03, DIAG-04, DIAG-05, FCST-09
**Success Criteria** (what must be TRUE):

  1. The extension builds and passes its tests against the upstream fix branch via an interim `[patch]`, then against the user-published release; at milestone close no anofox-forecast `[patch]` / git / path override remains in `Cargo.toml`.
  2. `ts_adf_by` / `ts_kpss_by` return the corrected statistics and p-values from SQL (matching the Phase 11 fixtures), and `ts_stationarity_by` uses the standard ADF/KPSS four-way mapping — returning trend-stationary on a trend-stationary fixture and an explicit verdict for a constant series (previously "non_stationary").
  3. `ts_ljung_box_by(..., fitted_params := p + q)` uses df = lags − p − q with a p-value matching R `Box.test(..., fitdf = p + q)`, and `docs/api/10-diagnostics.md` makes only claims these fixtures verify, with every snippet run against the built extension.
  4. On AirPassengers and simulated-series fixtures, point forecasts and 95% intervals for Naive, SNaive, RWD, SES, Holt, HW, AutoETS, AutoARIMA, Theta and Croston are within documented tolerance of R `forecast`, and Monte-Carlo interval coverage for the local-level case is within ±3pp of nominal.

**Plans**: TBD

### Phase 18: Correctness Regression Suite, Audit & Docs

**Goal**: The corrected behaviour is pinned against regression, guarded against statistics creeping back into C++/SQL, and accurately described in docs, skills and release notes.
**Depends on**: Phase 17 (and all of Phases 9-16)
**Requirements**: TEST-02, TEST-03, RUST-10, DOCS-01, DOCS-02
**Success Criteria** (what must be TRUE):

  1. Every test in the modules touched this milestone asserts values against a fixture or a closed-form expectation — a sweep finds no remaining length-only assertions in those modules.
  2. `test/sql/correctness/*.test` pins the corrected result of every user-facing function fixed this milestone and passes against the built extension.
  3. An audit script (run in CI or as a test) fails when statistical arithmetic appears in `src/**/*.cpp` or macro SQL outside a documented marshalling allow-list, and passes on HEAD.
  4. `docs/` and `plugins/anofox-forecast/skills/` (bodies and frontmatter descriptions) describe the corrected definitions — interval source, MASE, strengths, features, diagnostics, conformal variants — with every changed SQL snippet executed against the built extension.
  5. CHANGELOG / release notes contain a breaking-change section listing every behaviour change that alters numerical output.

**Plans**: TBD

## Execution Notes

- **Parallel track:** Phase 11 runs in the separate upstream repo (`~/projects/rust/anofox-forecast`, new fix branch, PR(s) for the user to review/merge/publish). It does not block Phases 12-16; its numbering is early so the PR lead time overlaps extension work. Phase 17 is the consumption gate.
- **FCST-09 split:** Phase 10 verifies crate-sourced intervals (e.g. Naive ±66 on AirPassengers) without claiming R parity; the full R `forecast` parity requirement (FCST-09) lives in Phase 17 because ETS/Theta/AutoARIMA parity needs UPST-01..04.
- **fdars coupling:** DEP-03 sits with WRAP-01 and DET-* in Phase 15 because the 0.47 FdMatrix port rewrites the same call sites (detrending.rs, seasonality.rs, periods.rs, peaks.rs). Re-implementing peaks in core (DET-01) before/with the bump avoids porting peaks.rs twice.
- **Conformal surface coupling:** RUST-04 (horizon order, per-group calibration, Rust-side residuals/coverage) is in Phase 16, not Phase 12, because CONF-04 (adaptive difficulty scaling at calibrate and apply) changes the same `ts_conformal_calibrate` / apply signatures — the SQL surface is redesigned once.
- **STL sharing:** STAT-02 (Phase 13) uses the crate's `seasonality::stl::STL` for F_T/F_S; validate it against R `stats::stl` there. DET-04 (Phase 15) reuses or fixes the same STL.
- **Research-heavy phases:** 11 (MacKinnon response surfaces, OCSB, Hyndman ETS variance classes), 14 (exact tsfresh definitions), 15 (scipy `find_peaks` semantics, Adams & MacKay BOCPD, Loess STL), 16 (jackknife+ needs leave-one-out refits — rename/remove is an allowed outcome).
- **Universal DoD (every requirement):** value assertions against a checked-in fixture or closed form; `cargo test` + `cargo fmt --check` + `cargo clippy` green; affected SQL run against the built extension; docs/skill text matches the new behaviour.

## Progress

**Execution Order:**
Phases execute in numeric order: 9 → 10 → 11 → 12 → 13 → 14 → 15 → 16 → 17 → 18 (Phase 11 work proceeds in the upstream repo and its release can land any time before Phase 17).

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 9. Reference Fixtures & Safe Dependency Bumps | 3/3 | Complete    | 2026-10-09 |
| 10. Forecast Wrapper Delegates to Crate Models | 10/10 | Complete    | 2026-10-09 |
| 11. Upstream anofox-forecast Correctness Fixes | 9/11 | In Progress|  |
| 12. Statistics Marshalling Moves to Rust | 6/8 | In Progress|  |
| 13. Core Statistics, Metrics & Data Quality | 0/TBD | Not started | - |
| 14. tsfresh Feature Parity | 0/TBD | Not started | - |
| 15. Detection & Decomposition on fdars 0.47 | 0/TBD | Not started | - |
| 16. Conformal & Bootstrap Correctness | 0/TBD | Not started | - |
| 17. Upstream Consumption & Diagnostics Parity | 0/TBD | Not started | - |
| 18. Correctness Regression Suite, Audit & Docs | 0/TBD | Not started | - |

## Next

Plan Phase 9 — run `/gsd-plan-phase 9`.
