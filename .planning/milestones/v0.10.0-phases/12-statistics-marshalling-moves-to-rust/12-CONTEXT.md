# Phase 12: Statistics Marshalling Moves to Rust - Context

**Gathered:** 2026-10-09
**Status:** Ready for planning

<domain>
## Phase Boundary

C++ and SQL macros only marshal, group and dispatch. List arguments are read correctly for every row (ToUnifiedFormat), NULLs reach Rust as validity and Rust owns the missing-data policy, and the remaining SQL/C++ statistics (differencing, mean fill, backtest metrics, hierarchy sums, aggregate frequency stepping) are delegated to Rust or removed. Requirements RUST-01, -02, -03, -05, -06, -07, -08, -09. Conformal macro rework (RUST-04) is Phase 16; the grep-based "no stats in C++" audit (RUST-10) is Phase 18. Audit input: `.planning/research/STAT-AUDIT-2026-10-08.md` §D (D1–D16). Docs/skills for every changed behaviour are updated in this phase (replace "(fix planned for v0.10.0, Phase 12)" notes).

</domain>

<decisions>
## Implementation Decisions

### NULLs in list arguments
- **D-01:** One shared C++ list-marshalling helper (ToUnifiedFormat-based, returns values + validity) replaces the ~10 `ExtractListAsDouble` copies (metrics.cpp, conformal.cpp, bootstrap.cpp, diagnostics.cpp, ts_periods.cpp, ts_detrend.cpp, ts_seasonality.cpp, ts_changepoints.cpp, ts_features.cpp, ts_peaks.cpp, ts_decomposition.cpp). Constant/dictionary list arguments give correct per-row results.
- **D-02:** Paired metrics and paired conformal/bootstrap inputs use pairwise-complete observations (pair dropped if either side is NULL); this policy is implemented in Rust (validity passed through FFI), not in C++.
- **D-03:** Single-series diagnostics/analyses (ADF, KPSS, Ljung-Box, DW, JB, periods, seasonality, changepoints, peaks, decomposition, detrend, features on lists): interior NULLs → SQL error naming the position ("interior NULL at position k"); leading/trailing NULLs are trimmed (same rule as forecasting, Phase 10 D-12).
- **D-04:** If nothing remains after filtering/trimming → NULL result (not an error).

### ts_diff_by
- **D-05:** `diff_order` means d-th order differencing (numpy `diff(n=d)`), computed by the Rust `diff`; new optional parameter `seasonal_lag` (lag-k difference y[t]−y[t−k]) for the old lag semantics. Breaking change documented in CHANGELOG.
- **D-06:** Output keeps one row per input row; the first d (or lag) rows per group are NULL so dates stay aligned.

### Leftover C++ statistics
- **D-07:** Remove the orphaned `ts_backtest_native` table function (metrics computed in C++; unused by any macro since `ts_backtest_auto_by` was removed) and every stale `ts_backtest_auto_by` reference in tests/comments/docs.
- **D-08:** Hierarchy aggregation uses SQL `SUM` NULL semantics: all-NULL group → NULL; partially-NULL group → sum of present values; no NULL→0 imputation.
- **D-09:** Delete the dead `ts_filter.cpp` / `ts_imputation.cpp` scalar bodies (Register no-ops); the `_by` table macros cover these features. `ts_fill_nulls_mean_by` uses the Rust `fill_nulls_mean`; `drop_constant` uses the Rust tolerance (`is_constant`).
- **D-10:** Per-step conformal: residual count not a multiple of horizon → error naming both counts.

### Aggregates & calendar
- **D-11:** `ts_forecast_agg` / `ts_features_agg` pass validity (no NULL-row splicing) so results equal `ts_forecast_by` / `ts_features` on the same series.
- **D-12:** Frequency detection in Rust (`detect_frequency`) for the agg paths; calendar stepping for monthly/quarterly/yearly: if all observed dates are month-ends → forecast month-ends, otherwise same day-of-month clamped to month length. `ts_forecast_exog_by` future dates use the same calendar-correct stepping (no 30-day month).

### Claude's Discretion
- Helper API shape, FFI validity plumbing details, test organisation (value assertions mandatory; SQL tests non-json-gated where possible).

</decisions>

<code_context>
## Existing Code Insights

### Reusable Assets
- Template for correct list marshalling: `src/table_functions/ts_stats.cpp:99-135`, `src/scalar_functions/ts_forecast_scalar.cpp:330-395`.
- Rust: `filter.rs::diff`, `imputation.rs::fill_nulls_mean`, `filter.rs::is_constant`, `gaps.rs::detect_frequency` (note: tie nondeterminism fix is Phase 13 WRAP-03 — coordinate or fix here if needed for D-12), metrics.rs functions.
- Phase 10: `prepare_series`/`finalize_output` (edge-NULL trim policy) in forecast.rs; `src/include/ts_forecast_params.hpp`.

### Established Patterns
- FFI validity bitmasks (64-bit words) already used by `anofox_ts_forecast`; extend to metrics/diagnostics FFI.
- DoD: cargo fmt/clippy/test; rebuild (`RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release`); SQL tests; make test_release; docs snippets run against the built extension.

### Integration Points
- `src/macros/ts_macros.cpp` (ts_diff_by :322, ts_fill_nulls_mean_by :303, drop_constant, ts_forecast_exog_by date bound), `src/table_functions/ts_backtest_native.cpp` (remove), `ts_aggregate_hierarchy.cpp:292`, `ts_forecast_agg.cpp` frequency :408, `ts_features_agg.cpp:231`, `conformal.cpp:1342`, CMakeLists source list, `anofox_forecast_extension.cpp` registration.

</code_context>

<specifics>
## Specific Ideas

- Success evidence: `ts_mae([1,NULL,3,4],[1,2,NULL,4])` = 0; `ts_mae(a,[0,0,0])` over 5 rows correct on every row; `ts_diff_by(...,2)` = numpy diff(n=2); month-end monthly agg forecasts stay month-end.

</specifics>

<deferred>
## Deferred Ideas

- Conformal macro ordering/pairing (RUST-04) → Phase 16. Grep audit (RUST-10) → Phase 18.

</deferred>
