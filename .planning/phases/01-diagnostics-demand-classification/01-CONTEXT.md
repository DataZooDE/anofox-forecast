# Phase 1: Diagnostics & Demand Classification - Context

**Gathered:** 2026-08-21
**Status:** Ready for planning

<domain>
## Phase Boundary

Expose the crate's statistical validation surface to SQL: stationarity tests (ADF, KPSS, combined verdict) and residual diagnostics (Ljung-Box, Durbin-Watson, Jarque-Bera, combined adequacy report). Each is delivered through the established exposure pattern — Rust FFI export → C++ scalar function → `ts_*` + `ts_*_by` macro → runnable example → docs — and cross-checked against statsmodels/R.

**Rescoped:** Intermittent-demand classification (INTER-01) is REMOVED from this phase. The user has a more advanced approach than the standard Syntetos-Boylan ADI/CV² taxonomy and will specify it separately. INTER-01 is deferred (see REQUIREMENTS.md).

Phase 1 now covers requirements STAT-01, STAT-02, STAT-03, RESID-01, RESID-02, RESID-03, RESID-04.

</domain>

<decisions>
## Implementation Decisions

### Function surface & return shape
- Deliver each capability as a scalar function returning a STRUCT per series, plus a `ts_*_by` macro (mirrors existing `ts_stats` / metrics pattern; composes with DuckDB GROUP BY for parallelism).
- Ship both per-test functions (`ts_adf`, `ts_kpss`, `ts_ljung_box`, `ts_durbin_watson`, `ts_jarque_bera`) and combined functions (`ts_stationarity`, `ts_residual_diagnostics`).
- p-values come from the standard approximation tables the crate already uses (MacKinnon for ADF; Kwiatkowski/KPSS tables). No new statistical table work.
- Naming follows the existing `ts_<name>` + `ts_<name>_by` convention exactly.

### Statistical parameters & defaults
- ADF lag selection: AIC automatic (statsmodels default), with an override parameter.
- ADF regression: constant `'c'` default; allow `'ct'` (constant+trend) and `'n'` (none).
- KPSS null: level stationarity `'c'` default; allow `'ct'`.
- Ljung-Box lags: `min(10, n/5)` heuristic default, override allowed.
- All defaults match statsmodels conventions so the reference cross-check is apples-to-apples.

### Residual diagnostics input & adequacy verdict
- Input: user supplies a residual column directly (the diagnostics operate on residuals).
- Significance level: `alpha = 0.05` default, configurable.
- Adequacy rule (RESID-04): Ljung-Box p > alpha (no residual autocorrelation) is the pass/fail gate; Jarque-Bera (normality) and Durbin-Watson (≈2) are advisory fields in the report.
- Combined return: one STRUCT carrying all three test statistics/p-values plus the overall pass/fail verdict.

### Claude's Discretion
- Exact STRUCT field names and ordering, FFI struct layout, and C++ registration details follow existing codebase conventions.
- Whether `ts_stationarity` internally reuses the `ts_adf`/`ts_kpss` FFI calls or calls a dedicated combined FFI entry point — pick whatever the crate exposes most cleanly (`crate::validation::test_stationarity` if available).

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- Existing scalar-function + STRUCT-return pattern: `src/scalar_functions/` (metrics.cpp) and `ts_stats` in `src/table_functions/`.
- FFI metric functions in `crates/anofox-fcst-ffi/src/lib.rs` (`anofox_ts_mae`, etc.) are the closest analog for new `anofox_ts_adf` / `anofox_ts_kpss` / residual-test exports.
- Crate side: `crate::validation` module already implements `adf_test`, `kpss_test`, `test_stationarity`, `ljung_box`, `durbin_watson`, `jarque_bera`, `box_pierce`, `diagnose_residuals` (v0.15.3).
- `ts_*_by` macro pattern in `src/macros/ts_macros.cpp` (e.g. `ts_mae_by`).

### Established Patterns
- Registration in `src/anofox_forecast_extension.cpp` LoadInternal (metric registration section is the template).
- FFI boundary marshals validity bitmaps → `Vec<Option<f64>>` via `build_series()`.
- Examples live in `examples/<category>/*.sql`; docs in `docs/api/`.

### Integration Points
- New FFI exports → `crates/anofox-fcst-ffi/src/lib.rs` (+ core wrappers in `crates/anofox-fcst-core/src/lib.rs`).
- New C++ scalar functions → `src/scalar_functions/` (a new `diagnostics.cpp` or extend existing), registered in `src/anofox_forecast_extension.cpp`.
- New macros → `src/macros/ts_macros.cpp`.
- New examples → `examples/diagnostics/` (new category dir).
- New docs → `docs/api/` (a diagnostics/validation page).

</code_context>

<specifics>
## Specific Ideas

- Reference cross-check target: statsmodels `adfuller`, `kpss`, `acorr_ljungbox`, `durbin_watson`, `jarque_bera` (and R equivalents) — capture reference values in the benchmark/validation harness so the examples assert numeric parity.
- Definition of Done (from REQUIREMENTS.md) applies to every function: runnable verified example + docs/api entry + numeric reference cross-check.

</specifics>

<deferred>
## Deferred Ideas

- **INTER-01 — intermittent-demand classification.** User has a "much more advanced approach" than standard ADI/CV² Syntetos-Boylan taxonomy. Removed from Phase 1; to be specified and scheduled separately. Do NOT build a placeholder ADI/CV² classifier.

</deferred>
