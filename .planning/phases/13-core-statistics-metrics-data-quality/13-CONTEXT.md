# Phase 13: Core Statistics, Metrics & Data Quality - Context

**Gathered:** 2026-10-10
**Status:** Ready for planning

<domain>
## Phase Boundary

Correct the core statistics in crates/anofox-fcst-core (stats.rs, quality.rs, metrics.rs, gaps.rs) and their SQL surfaces: ts_stats moments and strengths, entropy/n_gaps semantics, data-quality scoring (dates, frequency, behavioural score), MAPE zero policy, Hyndman-Koehler MASE, deterministic frequency detection docs. Requirements WRAP-02, WRAP-03, STAT-01..06. Audit input: `.planning/research/STAT-AUDIT-2026-10-08.md` §E (E1–E10) and G23/G24. Phase 12 already made detect_frequency's tie-break deterministic (partial WRAP-03) and month-end calendar stepping (G24 for forecasts). Docs/skills updated in this phase (replace "(fix planned for v0.10.0, Phase 13)" notes); values asserted against scipy/R fixtures via validation/reference.

</domain>

<decisions>
## Implementation Decisions

- **D-01:** ts_stats skewness/kurtosis = scipy `skew(bias=False)` / `kurtosis(bias=False)` (Fisher G1, excess G2); bimodality coefficient uses the SAS small-sample formula with those G1/G2.
- **D-02:** Trend and seasonal strength use the Wang/Hyndman STL definitions: F_T = max(0, 1 − Var(R)/Var(T+R)), F_S = max(0, 1 − Var(R)/Var(S+R)), from STL (MSTL when several periods; prefer the crate's STL/MSTL implementation if correct vs R stats::stl, else core). Period from input or auto-detected; seasonal strength NULL when no period is available/detected. EDA skill thresholds updated accordingly. Reference: R stats::stl + manual F_T/F_S (feasts not installed).
- **D-03:** `entropy` becomes real approximate entropy (Pincus 1991, m=2, r=0.2·sd), matching a reference implementation; documentation states the parameters.
- **D-04:** `n_gaps` = number of missing periods (expected timestamps absent given the detected/declared frequency); new column `n_gap_runs` keeps the old meaning (number of gap intervals). Breaking change documented.
- **D-05:** `ts_data_quality_by` passes dates and honours frequency and n_short; gap/temporal scores reflect real gaps; quality mode/tie logic deterministic.
- **D-06:** `behavioral_score` drops the ACF1>0.95 penalty; covers constancy, outliers and zero-runs only; documented.
- **D-07:** MAPE returns NULL if any (pairwise-complete) actual is 0; docs point to sMAPE/MASE for intermittent data.
- **D-08:** MASE is Hyndman-Koehler: `ts_mase(actual, forecast, insample, seasonal_period := 1)` scaled by the in-sample seasonal-naive MAE (lag = seasonal_period); the old (actual, forecast, baseline) form errors with a message pointing to `ts_rmae`. Breaking change documented. Table/`_by` metric surfaces updated consistently.
- **D-09:** detect_frequency docs state the unit (microseconds) correctly (WRAP-03 remainder); gap fill for monthly/quarterly/yearly preserves month-end alignment if not already covered by Phase 12's calendar rule.

### Claude's Discretion
- Internal structure, reference-fixture generation details, tolerance choices (documented in validation/reference/README.md).

</decisions>

<code_context>
## Existing Code Insights

- core: stats.rs:190-224 (moments, bimodality), :465 (n_gaps), :525-600 (strengths, entropy); quality.rs:66 (compute_data_quality), :178 (find_mode), :252 (behavioral_score); metrics.rs:113 (mape), :165 (mase); gaps.rs:165,:420.
- FFI: lib.rs ~4235 (data quality dates=None). SQL surfaces: ts_stats(_by), ts_data_quality(_by), ts_mase/ts_mape scalars + table metric functions (ts_metrics_native.cpp).
- Phase 9 harness: validation/reference (scipy moments fixture already exists); Phase 12 shared list reader / Rust NULL policy must be used by any changed scalar.
- DoD: cargo fmt/clippy/test, `RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release`, non-gated SQL tests, make test_release, docs snippets against the built extension.

</code_context>

<specifics>
## Specific Ideas

- Audit evidence: x20 kurt −0.096 vs scipy +0.277; bimodality 0.600 vs 0.466; RW seasonality 0.96 vs F_S 0.05; data quality with 20 gaps reported n_gaps 0 / temporal 1.0.

</specifics>

<deferred>
## Deferred Ideas

None.

</deferred>
