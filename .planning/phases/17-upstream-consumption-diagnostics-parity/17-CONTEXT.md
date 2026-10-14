# Phase 17: Upstream Consumption & Diagnostics Parity - Context

**Gathered:** 2026-10-12
**Status:** Ready for planning

<domain>
## Phase Boundary

The extension runs on the corrected upstream crate. SQL users get statsmodels/R-matching stationarity verdicts, a model-aware Ljung-Box test, and point/interval parity with R `forecast` across the core model set. Requirements: UPST-06, DIAG-03, DIAG-04, DIAG-05, FCST-09. Audit input: `.planning/research/STAT-AUDIT-2026-10-08.md` §C (C1–C6), §A1 and §H.

Execution note: planned in parallel with Phases 15/16, but EXECUTES after Phase 16 completes. It changes Cargo.toml/Cargo.lock (Phase 15 also bumps fdars there) and shifts forecast outputs; serial execution by user decision 2026-10-12.

</domain>

<decisions>
## Implementation Decisions

### Consuming the upstream crate (UPST-06)
- **D-01:** Bump `anofox-forecast` 0.15.10 → the released `0.16.0` from crates.io: workspace `Cargo.toml` plus `Cargo.lock`. No interim `[patch]`, because the release exists. The existing `[patch.crates-io]` entry (argmin) is unrelated and stays. At phase close there is no anofox-forecast `[patch]`/git/path override.
- **D-02:** AutoARIMA's order-selection fixes (upstream PR #223, still a draft) are NOT in 0.16.0. Phase 17 proceeds with 0.16.0. In the FCST-09 parity tests AutoARIMA is a documented known gap (own tolerance, or order-mismatch note) with a tracked follow-up. UPST-06 closes for 0.16.0; UPST-01 closes when a later release containing #223 is consumed (a small follow-up bump, tracked in STATE.md).
- **D-03:** Executes after Phases 15 and 16 (Cargo.lock overlap, forecast numbers shift).

### Diagnostics (DIAG-03, DIAG-04, DIAG-05)
- **D-04:** `ts_stationarity_by` / `classify_stationarity` use the standard ADF/KPSS four-way mapping (statsmodels convention):
  - ADF rejects H0 and KPSS does not → `stationary`
  - ADF does not reject and KPSS rejects → `non_stationary`
  - neither rejects → `trend_stationary`
  - both reject → `difference_stationary`

  A constant series gets an explicit `constant` verdict (C4). Breaking label change, documented in the CHANGELOG.
- **D-05:** A `regression := 'c' | 'ct'` parameter (default `'c'`, statsmodels/urca naming) on `ts_adf_by`, `ts_kpss_by` and `ts_stationarity_by`, so trend variants and the trend-stationary verdict are reachable. `ts_adf_by` / `ts_kpss_by` return the corrected 0.16.0 statistics and p-values (augmented ADF with MacKinnon p-values; KPSS p bounded [0.01, 0.10]) matching the Phase 11 fixtures.
- **D-05a:** anofox-forecast 0.16.0 has no trend-KPSS variant, so KPSS `'ct'` is implemented in the extension's Rust core. Residuals come from an OLS fit on constant + time trend; the statistic uses the same Bartlett long-run variance as level KPSS; critical values are the tseries Trend values (0.119 / 0.146 / 0.176 / 0.216) with the p-value interpolated and bounded to [0.01, 0.10]. Checked against R `tseries::kpss.test(x, null = "Trend")` (installed). With this the full four-way mapping works under `'ct'`. An upstream port to anofox-forecast is deferred. *(Added 2026-10-12 after research, user decision.)*
- **D-06:** `ts_ljung_box_by` takes a named parameter `fitted_params := 0` (default), with df = lags − fitted_params; df ≤ 0 raises an error. The p-value matches R `Box.test(..., type = "Ljung-Box", fitdf = p + q)` (C5).
- **D-07:** `docs/api/10-diagnostics.md` is rewritten to claim only what fixtures verify (C6). Every snippet runs against the built extension via the snippet harness.

### Forecast parity with R `forecast` (FCST-09) and plan split
- **D-08:** Reference: a checked-in R script (`forecast` installed) runs naive, snaive, rwf(drift), ses, holt, hw, ets, auto.arima, thetaf and croston on AirPassengers plus 3 seeded simulated series (local level, trend, intermittent). Extend the existing `validation/reference/r/gen_forecast_reference.R` and the `fixtures/r_forecast/` files (air_passengers.json, simulated_series.json from Phase 9) rather than starting over. Fixtures hold point forecasts and 95% bounds.
- **D-09:** Tolerances per model, documented in `validation/reference/README.md`: deterministic baselines (Naive/SNaive/RWD/Croston) to about 1e-8; optimised models (SES/Holt/HW/ETS/Theta) to a documented relative tolerance, since optimisers differ. AutoARIMA per D-02.
- **D-10:** A seeded Monte Carlo coverage test for the local-level case (about 2,000 repetitions): ETS(A,N,N)/SES 95% intervals must cover within ±3pp of nominal.
- **D-11:** 3 plans: (1) bump to 0.16.0 + ADF/KPSS from SQL + four-way stationarity mapping + `regression` parameter [UPST-06, DIAG-03]; (2) Ljung-Box `fitted_params` + diagnostics docs [DIAG-04, DIAG-05]; (3) R forecast parity fixtures + Monte Carlo coverage [FCST-09]. New SQL assertions go in a NON-json-gated reference test file. Breaking changes get CHANGELOG before/after rows; docs and skills are updated.

### Post-planning decision (user, 2026-10-13)
- **D-12:** Root-cause the R-parity gaps before accepting them. The planner measured HW / ETS 'AAA' about 21% off R on AirPassengers, 'Holt' 11.5% off, and AutoETS selecting (M,A,M) vs R's (M,Ad,M). 17-03 starts with a diagnosis task: compare the extension wrapper's call path (model mapping, seasonal variant, damping, initialisation, parameter defaults, data handling) against calling anofox-forecast 0.16.0 directly on the same fixture, and both against R. If the cause is extension-side, fix it in 17-03 and tighten the tolerances to what is achieved. If crate-side, file an upstream issue with a minimal reproduction and keep a documented known gap citing the cause. Only gaps with an identified cause may be recorded as known gaps.

- **D-13:** If the 17-03 diagnosis confirms it, the SQL model name 'Holt' is remapped to the crate's ETS(A,A,N), which is the definition of R's `holt()`. Pre-diagnosis this shrinks the gap from 11.5% to 1.7% on AirPassengers. Breaking output change, with CHANGELOG before/after rows. (User, 2026-10-14.)

### Claude's Discretion
- Exact tolerance values per model (each justified), the Monte Carlo seed and size, simulated-series parameters, and where the parity test lives (Rust integration test over the core forecast wrapper and/or SQL).

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- Existing R reference infrastructure from Phase 9: `validation/reference/r/gen_forecast_reference.R`, `gen_diagnostics_reference.R`, `common.R`. Fixtures: `fixtures/r_forecast/{air_passengers,simulated_series}.json` and `fixtures/r_diagnostics/stationarity_portmanteau.json`.
- Phase 11 upstream fixtures for the ADF/KPSS values (in the upstream repo tests/data/r_reference and the extension's validation dir).
- The non-gated `*_reference.test` SQL pattern and the snippet harness from Phases 13–15.

### Established Patterns
- SQL macros: `ts_adf_by` at `src/macros/ts_macros.cpp` around :2413 (`max_lags := -1`); the KPSS/stationarity/Ljung-Box macros are nearby. Diagnostics core in `crates/anofox-fcst-core/src/validation.rs` (classify_stationarity around :124, Ljung-Box around :179 per the audit).
- The CLAUDE.md constraint: OpenSSL static, verify the clean-machine load after dependency bumps; `cargo fmt --check` in the DoD.

### Integration Points
- The anofox-forecast 0.16.0 bump also changes ETS/Theta/TBATS/MSTL/GARCH outputs, so forecast-dependent tests and docs shift. Phase 16's conformal changes interact with forecast intervals.
- Memory: `ts_cv_forecast_by('AutoEnsemble')` segfaulted on crate 0.15.3. Re-check after the bump.

</code_context>

<specifics>
## Specific Ideas

- ROADMAP anchors: `ts_stationarity_by` returns trend-stationary on a trend-stationary fixture and an explicit verdict for a constant series (previously "non_stationary"); the Ljung-Box p-value matches R `Box.test(fitdf = p + q)`; point and 95% forecasts are within documented tolerance of R for the 10 models; local-level coverage is within ±3pp.
- DoD as in earlier phases, plus `cargo tree` showing anofox-forecast 0.16.0 and no override.

</specifics>

<deferred>
## Deferred Ideas

- Port the KPSS trend variant (D-05a) upstream into anofox-forecast.
- A follow-up bump to the anofox-forecast release containing AutoARIMA #223 (closes UPST-01, tightens the AutoARIMA parity tolerance).

</deferred>
