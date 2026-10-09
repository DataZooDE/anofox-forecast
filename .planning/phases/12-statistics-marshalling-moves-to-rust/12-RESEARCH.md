# Phase 12: Statistics Marshalling Moves to Rust - Research

**Researched:** 2026-10-09
**Domain:** C++/DuckDB FFI marshalling correctness (list reading, NULL/validity propagation), Rust NULL-handling policy design, SQL macro rewrites
**Confidence:** HIGH (every claim below is grounded in files read this session with line numbers; no web research was needed — this is pure in-repo investigation)

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

#### NULLs in list arguments
- **D-01:** One shared C++ list-marshalling helper (ToUnifiedFormat-based, returns values + validity) replaces the ~10 `ExtractListAsDouble` copies (metrics.cpp, conformal.cpp, bootstrap.cpp, diagnostics.cpp, ts_periods.cpp, ts_detrend.cpp, ts_seasonality.cpp, ts_changepoints.cpp, ts_features.cpp, ts_peaks.cpp, ts_decomposition.cpp). Constant/dictionary list arguments give correct per-row results.
- **D-02:** Paired metrics and paired conformal/bootstrap inputs use pairwise-complete observations (pair dropped if either side is NULL); this policy is implemented in Rust (validity passed through FFI), not in C++.
- **D-03:** Single-series diagnostics/analyses (ADF, KPSS, Ljung-Box, DW, JB, periods, seasonality, changepoints, peaks, decomposition, detrend, features on lists): interior NULLs → SQL error naming the position ("interior NULL at position k"); leading/trailing NULLs are trimmed (same rule as forecasting, Phase 10 D-12).
- **D-04:** If nothing remains after filtering/trimming → NULL result (not an error).

#### ts_diff_by
- **D-05:** `diff_order` means d-th order differencing (numpy `diff(n=d)`), computed by the Rust `diff`; new optional parameter `seasonal_lag` (lag-k difference y[t]−y[t−k]) for the old lag semantics. Breaking change documented in CHANGELOG.
- **D-06:** Output keeps one row per input row; the first d (or lag) rows per group are NULL so dates stay aligned.

#### Leftover C++ statistics
- **D-07:** Remove the orphaned `ts_backtest_native` table function (metrics computed in C++; unused by any macro since `ts_backtest_auto_by` was removed) and every stale `ts_backtest_auto_by` reference in tests/comments/docs.
- **D-08:** Hierarchy aggregation uses SQL `SUM` NULL semantics: all-NULL group → NULL; partially-NULL group → sum of present values; no NULL→0 imputation.
- **D-09:** Delete the dead `ts_filter.cpp` / `ts_imputation.cpp` scalar bodies (Register no-ops); the `_by` table macros cover these features. `ts_fill_nulls_mean_by` uses the Rust `fill_nulls_mean`; `drop_constant` uses the Rust tolerance (`is_constant`).
- **D-10:** Per-step conformal: residual count not a multiple of horizon → error naming both counts.

#### Aggregates & calendar
- **D-11:** `ts_forecast_agg` / `ts_features_agg` pass validity (no NULL-row splicing) so results equal `ts_forecast_by` / `ts_features` on the same series.
- **D-12:** Frequency detection in Rust (`detect_frequency`) for the agg paths; calendar stepping for monthly/quarterly/yearly: if all observed dates are month-ends → forecast month-ends, otherwise same day-of-month clamped to month length. `ts_forecast_exog_by` future dates use the same calendar-correct stepping (no 30-day month).

### Claude's Discretion
- Helper API shape, FFI validity plumbing details, test organisation (value assertions mandatory; SQL tests non-json-gated where possible).

### Deferred Ideas (OUT OF SCOPE)
- Conformal macro ordering/pairing (RUST-04) → Phase 16. Grep audit (RUST-10) → Phase 18.

### Phase Boundary (from CONTEXT.md)
C++ and SQL macros only marshal, group and dispatch. List arguments are read correctly for every row (ToUnifiedFormat), NULLs reach Rust as validity and Rust owns the missing-data policy, and the remaining SQL/C++ statistics (differencing, mean fill, backtest metrics, hierarchy sums, aggregate frequency stepping) are delegated to Rust or removed. Requirements RUST-01, -02, -03, -05, -06, -07, -08, -09. Conformal macro rework (RUST-04) is Phase 16; the grep-based "no stats in C++" audit (RUST-10) is Phase 18. Docs/skills for every changed behaviour are updated in this phase (replace "(fix planned for v0.10.0, Phase 12)" notes).
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| RUST-01 | All list-taking scalar functions read inputs via `ToUnifiedFormat` (constant/dictionary list arguments give correct per-row results) [D2] | Architecture Patterns D-01/D-02 section identifies the exact template (`ts_stats.cpp:99-135`) and every one of the ~10 call sites to fix |
| RUST-02 | List NULLs are passed to Rust as validity; Rust owns the policy: paired metrics/conformal use pairwise-complete observations; single-series diagnostics reject or explicitly handle interior NULLs instead of splicing; one shared C++ helper replaces the ~10 copies of `ExtractListAsDouble` [D1] | Architecture Patterns D-01/D-02/D-03 sections; Code Examples (shared helper + FFI validity convention); confirms which FFI functions already accept validity (6 diagnostics fns, 6 conformal fns) vs. which need extension (metrics, periods, detrend, decompose, seasonal_strength, features, peaks, changepoints, bootstrap) |
| RUST-03 | `ts_diff_by` computes d-th order differences via Rust `diff` (optionally a separate seasonal-lag parameter) [D3] | Architecture Patterns D-05/D-06 section: confirms `filter::diff` (`filter.rs:134-150`) already implements numpy `diff(n=d)` semantics exactly; identifies the UNNEST-zip realignment pattern needed; flags open question on NULL policy (Open Question 1) |
| RUST-05 | `ts_fill_nulls_mean_by` uses the Rust implementation [D7] | Architecture Patterns D-07/D-09 section: confirms `imputation::fill_nulls_mean` + `anofox_ts_fill_nulls_mean` FFI fn already exist and already accept validity; identifies the dead-but-correct reference implementation to copy the logic from (`ts_imputation.cpp:265-317`) |
| RUST-06 | `ts_backtest_native` either computes metrics per group via the Rust metric functions (unknown metric → error, missing bounds → NULL) or is removed together with stale `ts_backtest_auto_by` references; error sign convention is consistent across surfaces [D8, D16] | State of the Art / Deprecated section: full list of every stale reference found (tests, docs, skills, benchmark README); CMakeLists.txt:177, anofox_forecast_extension.cpp:177, src/include/ts_backtest_native.hpp removal sites identified; `RegisterTsEstimateBacktestMemoryFunction` confirmed as a *different*, unrelated, still-live function not to be removed |
| RUST-07 | Hierarchy aggregation keeps SQL `SUM` NULL semantics (all-NULL → NULL, no NULL→0 imputation) [D9] | Architecture Patterns D-08 section: exact bug line identified (`ts_aggregate_hierarchy.cpp:291`) |
| RUST-08 | `ts_forecast_agg` uses Rust frequency detection with calendar-aware month/year stepping; `ts_forecast_agg` and `ts_features_agg` pass validity instead of splicing NULL rows [D10, D11] | Architecture Patterns D-10/D-12 and D-11 sections: exact bug lines identified (`ts_forecast_agg.cpp:197,379-385`, `ts_features_agg.cpp:229`); existing (but not-yet-reused) calendar-clamp C++ pattern identified across 6 files; `detect_frequency` tie-break nondeterminism flagged as Open Question 2 |
| RUST-09 | Per-step conformal validates that the residual count is a multiple of horizon; drop_constant uses the Rust tolerance; `ts_forecast_exog_by` future-date generation is calendar-correct; dead `ts_filter.cpp`/`ts_imputation.cpp` scalar bodies removed or registered [D12–D15] | Architecture Patterns D-07/D-09 (drop_constant tolerance correction: actual tolerance is `f64::EPSILON`, not audit's stated "1e-10"), D-15 (30-day-month bound bug and fix) sections; conformal.cpp:1341-1344 per-step truncation bug identified (D-10's "per-step conformal" decision) |
</phase_requirements>

## Summary

This phase has one shape repeated ~12 times: a C++ scalar/table function reads a `LIST(DOUBLE)` argument with a private, buggy helper (`FlatVector::GetData` without `ToUnifiedFormat`, and/or NULLs silently dropped instead of passed through as validity), calls an FFI function that either already accepts a validity bitmask (diagnostics, most conformal variants) or does not (metrics, periods, detrend, decompose, seasonal_strength, features, peaks, changepoints, bootstrap), and the Rust side either already has a policy for missing data (none do, for these domains) or needs one built. Two already-correct templates exist in the codebase for the C++ half: `ts_stats.cpp:99-135` (`ExtractListDoubles` — full `ToUnifiedFormat` + validity bitmask output) and `ts_forecast_scalar.cpp:300-390` (same pattern, used per-row in a loop). These should become the one shared helper (D-01).

The biggest surprise: `src/table_functions/ts_filter.cpp` and `src/table_functions/ts_imputation.cpp` (699 lines total) are **already written correctly** against the FFI (`TsFillNullsMeanFunction` at `ts_imputation.cpp:265-317` calls `anofox_ts_fill_nulls_mean` with a real validity bitmask) but every single `Register*Function` in both files is a literal no-op (`// No-op: C++ extension uses table function, not scalar`) — none of this code is reachable from SQL. The live, buggy implementations are 100% in `ts_macros.cpp`'s pure-SQL `_by` macros (`AVG() OVER`, `LAG()`, `MIN()!=MAX()`). D-09's "delete the dead bodies" is literally correct (zero functional loss), but RUST-05/D-07's fix must come from somewhere else: the established `_by`-macro pattern (`GROUP BY group_col` + `LIST(value_col ORDER BY date_col)` fed into a scalar LIST function, as used by `ts_adf_by`/`ts_kpss_by`/`ts_detrend_by`, `ts_macros.cpp:2314-2322` etc.) is the right template, but it requires a *new*, properly-registered scalar function — not a resurrection of the dead one (though its body is a correct reference implementation to copy from).

A second structural finding: Phase 10's `prepare_series`/`finalize_output` (forecast.rs) **interpolates** interior gaps — it is not a template for D-03's "interior NULL at position k" error policy, which is the opposite behavior. Phase 12 needs its own, new shared helper (trim edges, then error on any remaining interior invalid position) distinct from Phase 10's forecasting-domain helper, even though both trim leading/trailing NULLs identically.

A third structural finding: ~52 of 68 `test/sql/*.test` files are `require json`-gated, including every one of the 7 table-function files this phase touches most (`ts_diff.test`, `ts_filter.test` is NOT gated but `ts_forecast_agg.test`, `ts_conformal.test`, `ts_features.test`, `ts_seasonality.test`, `ts_changepoints.test`, `ts_decomposition.test`, `ts_periods.test`, `ts_detrend.test`, `ts_peaks.test` all are). In this sandboxed environment the `json` extension is not bundled in `build/release` (no `.duckdb_extension` file found) and would need network access to autoload — meaning these tests may silently skip rather than run. New/changed tests for this phase's behavior must be added to non-gated files or gated test debt must be addressed, per CONTEXT.md's explicit discretion note ("SQL tests non-json-gated where possible").

**Primary recommendation:** Build one shared C++ marshalling helper (model: `ts_stats.cpp:99-135`) and one pair of shared Rust NULL-policy helpers (pairwise-complete-drop for paired metrics/conformal/bootstrap; trim-edges-then-error-on-interior for single-series diagnostics/detection), extend the ~10 FFI functions that currently lack a `validity` parameter to accept one (mirroring `anofox_ts_adf`'s existing signature), and re-point every one of the ~12 call sites + the 2 broken SQL macros (`ts_diff_by`, `ts_fill_nulls_mean_by`/`drop_constant`) through this new path — then delete the two fully-dead `.cpp` files and the `ts_backtest_native` surface outright.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| LIST(DOUBLE) → Rust marshalling (values + validity) | API/Backend (C++ FFI boundary) | — | Must read DuckDB vector formats (`ToUnifiedFormat`) correctly; this is pure marshalling, never arithmetic |
| Pairwise-complete / interior-NULL missing-data policy | Rust Core (`anofox-fcst-core`) | FFI boundary (passes validity through) | User's explicit guiding rule: "C++/SQL only marshal, group, dispatch" — all missing-data *decisions* belong in Rust |
| d-th order differencing (`ts_diff_by`) | Rust Core (`filter::diff`) | SQL macro (alignment/NULL-padding only) | Core math already exists in Rust; SQL macro's job is only to re-align output to one-row-per-input-row |
| Mean-fill / constant-drop (`ts_fill_nulls_mean_by`, `drop_constant`) | Rust Core (`imputation::fill_nulls_mean`, `filter::is_constant`) | SQL macro (GROUP BY + LIST + UNNEST dispatch) | Currently 100% SQL (AVG/MIN/MAX); must route through Rust per explicit decision D-09 |
| Hierarchy aggregation NULL semantics | SQL/C++ (DuckDB's own `SUM` semantics) | — | D-08 explicitly keeps this in SQL/C++ — no Rust involvement; just stop overriding NULL→0 before it reaches SUM |
| Backtest metrics (`ts_backtest_native`) | Removed | — | Dead/unused surface; metrics already covered by `ts_metrics_native.cpp`/scalar metric functions |
| Calendar-aware date stepping (agg frequency, exog future dates) | Rust Core (`gaps::detect_frequency` + new month-end logic) | C++ (consumes the Rust result; existing `Date::MonthDays` clamp logic in 6 C++ files stays for the non-agg paths) | D-12 requires Rust-side frequency detection; the existing C++ calendar-clamp code (ts_forecast_native.cpp etc.) is a *different*, already-correct-for-clamping pattern that needs a month-end-preservation enhancement, not a Rust rewrite |

## Package Legitimacy Audit

No new external packages are introduced by this phase — it is pure internal FFI/C++/SQL refactoring within already-vendored `anofox-forecast`/`anofox-regression`/`fdars-core` crates. No audit table required.

## Architecture Patterns

### System Architecture Diagram

```
SQL caller
   │
   ▼
┌─────────────────────────────────────────────────────────────┐
│  SQL macro layer (ts_macros.cpp)                              │
│   _by macros: GROUP BY group_col, LIST(value_col ORDER BY     │
│   date_col) → scalar/table function → (UNNEST for row-shaped  │
│   output; direct STRUCT for one-row-per-group output)         │
└─────────────────────────────────────────────────────────────┘
   │ (LIST(DOUBLE) argument, possibly containing SQL NULLs)
   ▼
┌─────────────────────────────────────────────────────────────┐
│  C++ scalar/table function (metrics.cpp, conformal.cpp,       │
│  bootstrap.cpp, diagnostics.cpp, ts_periods.cpp, ts_detrend.cpp,│
│  ts_seasonality.cpp, ts_changepoints.cpp, ts_features.cpp,     │
│  ts_peaks.cpp, ts_decomposition.cpp)                           │
│                                                                 │
│  NEW shared helper (model: ts_stats.cpp:99-135):               │
│    list_vec.ToUnifiedFormat(count, ...)   ← constant/dict-safe │
│    child_vec.ToUnifiedFormat(...)         ← per-row validity   │
│    returns (values: Vec<f64>, validity: Vec<u64> bitmask)      │
│    NULLs are NOT dropped here — passed through                 │
└─────────────────────────────────────────────────────────────┘
   │ (values ptr, validity ptr [nullable], length)
   ▼
┌─────────────────────────────────────────────────────────────┐
│  FFI boundary (crates/anofox-fcst-ffi/src/lib.rs)              │
│  EXTEND ~10 signatures to add `const uint64_t *validity`       │
│  (mirrors anofox_ts_adf's existing signature exactly)           │
│  build_values(values, validity, length) → Vec<f64> w/ NaN       │
│    for invalid (existing helper, lib.rs:132-153)                │
└─────────────────────────────────────────────────────────────┘
   │ (Vec<f64> with NaN sentinels, or raw values+validity slices)
   ▼
┌─────────────────────────────────────────────────────────────┐
│  NEW Rust NULL-policy layer (core, new module or added to      │
│  validation.rs/metrics.rs — Claude's Discretion on exact file) │
│    pairwise_complete(a, b) → drop index i if either NaN         │
│    trim_edges_reject_interior(values) → Result<Vec<f64>,        │
│      "interior NULL at position k"> ; None-if-nothing-left (D-04)│
└─────────────────────────────────────────────────────────────┘
   │ (clean Vec<f64>)
   ▼
Existing, unmodified pure-math functions (metrics::mae, filter::diff,
validation::adf, periods::*, peaks::detect_peaks, changepoint::*,
features::extract_features, decomposition::*, detrending::*,
seasonality::*)
```

### Recommended approach per capability

**D-01/D-02 (shared C++ helper + FFI validity):**
1. Extract one shared helper function (new header, e.g. `src/include/ts_list_marshal.hpp`) implementing the `ts_stats.cpp:99-135` `ExtractListDoubles` pattern exactly (full `ToUnifiedFormat` on both the list vector and its child vector; outputs `vector<double> out_values` + `vector<uint64_t> out_validity`).
2. Replace every one of the following private copies with a call to the shared helper: `metrics.cpp:13-30`, `conformal.cpp:14-?`, `bootstrap.cpp:13-?`, `ts_changepoints.cpp:28-?`, `ts_decomposition.cpp:22-?`, `ts_detrend.cpp:12-?`, `ts_features.cpp:16-?`, `ts_peaks.cpp:12-?`, `ts_periods.cpp:22-?` (note: this one already has a different, 4-arg signature `(list_vec, count, row_idx, out)` — check call sites before swapping), `ts_seasonality.cpp:16-?`. `diagnostics.cpp:31` (`ExtractListAsDoubleLocal`) already does the right thing for diagnostics' *purpose* (pre-filters NULLs) but wrongly discards position information — also needs to become validity-passing.
3. **Do not touch `ts_filter.cpp`/`ts_imputation.cpp`'s copy** — delete the whole file instead (D-09, see below).
4. Extend every FFI function that lacks a `validity` parameter to add one, following `anofox_ts_adf`'s exact pattern (`const uint64_t *validity` right after `values`, nullable, documented "`validity` may be null (all valid)"). Confirmed missing today: `anofox_ts_mae`/`mse`/`rmse`/`mape`/`smape`/`r2`/`bias`/`rmae`/`mase`/`quantile_loss`/`mqloss`/`coverage` (metrics.cpp's whole surface, `anofox_fcst_ffi.h:2018-2181`), `anofox_ts_detect_peaks`/`anofox_ts_analyze_peak_timing` (`:2419-2439`), `anofox_ts_detect_changepoints`/`anofox_ts_detect_changepoints_bocpd` (`:2572-2591`), `anofox_ts_detrend`/`anofox_ts_decompose`/`anofox_ts_seasonal_strength` (`:2445-2470`), `anofox_ts_features` (`:2598`), `anofox_ts_detect_periods`/`anofox_ts_detect_periods_flat` (`:2209-2242`), `anofox_ts_bootstrap_intervals`/`anofox_ts_bootstrap_quantiles` (`:3088-3115`).
5. Confirmed ALREADY accepting validity (no FFI change needed, only the C++ caller needs to stop pre-filtering and pass real validity): `anofox_ts_adf`/`kpss`/`stationarity`/`ljung_box`/`durbin_watson`/`jarque_bera` (`anofox_fcst_ffi.h:3384-3440+`), `anofox_ts_conformal_quantile`/`predict`/`predict_multi`/`predict_adaptive`/`predict_asymmetric`/`learn` (`:2909-3047`). These currently receive `nullptr` for validity at the call site because the C++ helper already stripped NULLs out before calling (e.g. `diagnostics.cpp:113` literally comments `/* validity= */ nullptr, // already filtered by ExtractListAsDoubleLocal`) — the fix here is purely in the C++ caller, zero FFI/header change.

**D-02 (pairwise-complete policy) — Rust side:**
`crates/anofox-fcst-core/src/metrics.rs` functions take plain `&[f64]`, no `Option`/validity concept at all (confirmed: `mae`/`mse`/`rmse`/`mape`/`smape`/`mase`/`r2`/`bias`/`rmae`/`quantile_loss`/`mqloss`/`coverage`, lines 46-364). Introduce one new function, e.g. `fn pairwise_complete(a: &[f64], a_valid: Option<&[bool]>, b: &[f64], b_valid: Option<&[bool]>) -> (Vec<f64>, Vec<f64>)`, called from the FFI layer (or a thin wrapper in metrics.rs) before delegating to the existing pure `mae`/`mse`/etc. Same pattern needed for conformal's residual/forecast pairing and bootstrap's residual/forecast pairing.

**D-03/D-04 (single-series interior-NULL-errors, edge-trim) — Rust side:**
This is explicitly **not** the same policy as Phase 10's `prepare_series` (which interpolates interior gaps — `forecast.rs:3022-3073`). Build a new, parallel helper (same file or a new small module) that: locates `first_idx`/`last_idx` of valid values exactly like `prepare_series` does (`forecast.rs:3041-3050`), but instead of calling `fill_nulls_interpolate` on the interior, scans the trimmed interior for any remaining invalid position and returns `Err(ForecastError::InvalidInput(format!("interior NULL at position {k}")))` (position `k` should be the ORIGINAL 0-based index, not the trimmed one, per D-03's wording). If after trimming nothing remains, return a sentinel the C++ caller maps to SQL NULL, not an error (D-04). This single helper is reusable for ADF/KPSS/Ljung-Box/DW/JB, periods, seasonality, changepoints, peaks, decomposition, detrend, and features — call it once per scalar function body, right after the shared C++ marshalling helper hands back `(values, validity)`.

**D-05/D-06 (`ts_diff_by`):**
`filter::diff(values: &[f64], order: usize) -> Result<Vec<f64>>` (`filter.rs:134-150`) already implements exactly numpy's `diff(n=d)` semantics — each of the `order` iterations does `result.windows(2).map(|w| w[1]-w[0])`, shrinking the vector length by 1 per iteration, so the final output length is `len - order`. The table macro must: (1) aggregate `LIST(value_col ORDER BY date_col)` per group (existing `_by` pattern), (2) call a (new) scalar wrapper around `anofox_ts_diff`, which currently has **no validity parameter** (`anofox_fcst_ffi.h:2795-2800`) — note `anofox_ts_diff`'s NULL-handling policy is NOT explicitly covered by D-03's "interior NULL at position k" list (D-03 only names ADF/KPSS/Ljung-Box/DW/JB/periods/seasonality/changepoints/peaks/decomposition/detrend/features — `ts_diff_by` is absent); this is an **open question** for the planner (see below), (3) left-pad the `order`-shorter Rust result with `order` NULLs so the SQL output keeps one row per input row (D-06), zipped back to `date_col` via parallel `UNNEST` (same technique `ts_forecast_exog_by`'s final SELECT already uses for multiple same-length lists, `ts_macros.cpp:1110-1118`).

**D-07/D-09 (`ts_fill_nulls_mean_by`, `drop_constant`):**
`imputation::fill_nulls_mean(values: &[Option<f64>]) -> Vec<f64>` (`imputation.rs:49-59`) and `filter::is_constant(values: &[Option<f64>]) -> bool` (`filter.rs:39-47`) already exist and are exactly what's needed. The FFI function `anofox_ts_fill_nulls_mean(values, validity, length, out_values, out_error)` (`anofox_fcst_ffi.h:2783-2787`) **already accepts validity** — no FFI change needed here. The dead `ts_imputation.cpp:265-317` (`TsFillNullsMeanFunction`) is a byte-for-byte correct reference implementation of the C++ call site to copy (not reuse verbatim, since `RegisterTsFillNullsMeanFunction` is a no-op and the whole file is slated for deletion) into whatever new scalar-function file replaces it, wired through the `_by`-macro `GROUP BY` + `LIST()` + scalar-call + `UNNEST` pattern. **Correction to the audit's D-14 claim:** `is_constant`'s actual tolerance is `f64::EPSILON` (`filter.rs:47`, `(v - first).abs() < f64::EPSILON` — i.e. ~2.22e-16, essentially exact equality), **not** "1e-10" as STAT-AUDIT-2026-10-08.md §D D14 states — the planner should write the DoD test against the real `f64::EPSILON` tolerance, not the audit's stated value.

**D-08 (hierarchy NULL→0):**
`src/table_functions/ts_aggregate_hierarchy.cpp:291`: `row.value = value_val.IsNull() ? 0.0 : value_val.GetValue<double>();` — this line is the entire bug. Fix: track a per-row validity flag alongside `row.value` (or skip the row from the sum entirely and track a separate "any valid in group" flag) so the downstream `SUM` can distinguish "all-NULL group" (→ NULL) from "some-NULL group" (→ sum of present values), matching D-08/SQL `SUM` semantics exactly. This capability stays in C++ per the Architectural Responsibility Map — no Rust/FFI involvement.

**D-10/D-12 (`ts_forecast_agg` frequency, calendar stepping):**
`ts_forecast_agg.cpp:379-385`: frequency is a median of raw microsecond diffs (`steps[steps.size()/2]`), not calendar-aware. Fix: replace with a call to `anofox_ts_detect_frequency` (already FFI-exposed, `anofox_fcst_ffi.h:2860-2863`, backed by `gaps::detect_frequency`, `gaps.rs:385-422`). **Known issue inherited, not introduced, by this fix:** `detect_frequency`'s tie-break (`gaps.rs:415-419`, `counts.into_iter().max_by_key(...)`) iterates a `std::collections::HashMap`, whose iteration order is nondeterministic across runs (Rust's default hasher is randomized per-process) — ties between equally-frequent diffs will not reproducibly pick the same mode. STATE.md flags this as WRAP-03 (Phase 13), but D-12 depends on a deterministic frequency result to decide "is this monthly/quarterly/yearly" before applying month-end logic — **the planner must decide whether Phase 12 needs its own narrow determinism fix now (e.g. break ties by smallest diff value) or can tolerate the existing nondeterminism for its calendar-stepping logic** (flagged in Open Questions).

Calendar-aware month/quarter/year *stepping* (as opposed to frequency *detection*) already has a correct, working C++ implementation — just not reused by `ts_forecast_agg`. `ts_forecast_scalar.cpp:190-216` and `ts_forecast_native.cpp:748-779` (plus `ts_forecast_ensemble_native.cpp:393`, `ts_forecast_var_native.cpp:556`, `ts_forecast_panel_native.cpp:523,654`) all independently implement: decompose `base_date` into year/month/day via `Date::Convert`, add `months_to_add` (scaled ×3 for quarterly, ×12 for yearly), handle month rollover, then `Date::MonthDays(new_year, new_month)` to clamp the day. This clamps day-of-month but does **not** implement D-12's "if all observed dates are month-ends → forecast month-ends" branch — that branch doesn't exist anywhere in the codebase yet and must be written fresh (check `day == Date::MonthDays(year, month)` for every observed date; if true for all, force the output day to the new month's last day instead of the clamped day). This new month-end-detection logic should be written once (ideally as one new shared helper, since the clamp logic itself is already duplicated 6×) and reused by both `ts_forecast_agg`'s new Rust-frequency path and whatever D-12 wants for `ts_forecast_exog_by`.

**D-11 (`ts_forecast_agg`/`ts_features_agg` NULL splicing):**
Both `ts_forecast_agg.cpp:197` (`if (!ts_data.validity.RowIsValid(ts_idx) || !val_data.validity.RowIsValid(val_idx)) { continue; }`) and `ts_features_agg.cpp:229` (identical `continue`-on-NULL pattern) **completely drop** rows with a NULL value from the aggregate state — the row never reaches `state.data->values`, and no validity bit is ever recorded for it. This is a genuine splice (not just an FFI gap): fix requires (1) NOT skipping the row — push a placeholder value (e.g. `0.0`) AND track a parallel validity vector in `TsForecastAggStateData`/`TsFeaturesAggStateData`, (2) threading that validity vector through to `anofox_ts_features`/`anofox_ts_forecast` once those FFI functions accept validity (features needs the D-01 FFI extension above; forecast already has validity support per Phase 10).

**D-15 (`ts_forecast_exog_by` 30-day-month bound):**
`ts_macros.cpp:1114`: `generate_series(last_date + _interval, last_date + (len * EXTRACT(EPOCH FROM _interval) || ' seconds')::INTERVAL, _interval)`. The STEP argument (`_interval`, third positional) is a real calendar `INTERVAL '1 month'`/`'3 month'`/`'1 year'` when frequency is `'mo'`/`'q'`/`'y'` (built in the `_freq` CTE, `ts_macros.cpp:1019-1026`), but the UPPER BOUND (second positional) converts that same interval to seconds via `EXTRACT(EPOCH FROM ...)`, which for a calendar interval with a month/year component uses a fixed 30-day-month / 365.25-day-year approximation (DuckDB interval-to-epoch convention) — a genuine mismatch between the step's real calendar length and the bound's assumed length. Over 12 monthly steps this is ~5-6 days of drift (365 real days vs. 360 assumed), which can under- or over-shoot the number of `generate_series` rows relative to `len((fcst).point)`, misaligning the final zipped `UNNEST`. Fix: compute the upper bound as `last_date + (len * _interval)` directly (interval multiplication, not epoch-seconds round-trip) — DuckDB supports `INTERVAL * INTEGER`.

**D-16 (error sign convention):**
`ts_backtest_native.cpp:818`ish computes `forecast - actual`; SQL-surface metrics (`metrics.rs`) use `actual - forecast` (confirm exact sign in `metrics.rs:48` `(a - f).abs()` — note `abs()` makes MAE/MSE/RMSE sign-irrelevant, so this convention mismatch only matters for signed quantities like `bias`). Moot once `ts_backtest_native` is deleted per D-07 (see below) — flag as resolved-by-removal, not resolved-by-fix.

### Anti-Patterns to Avoid
- **Reusing Phase 10's `prepare_series` for diagnostics/detection NULL handling** — it interpolates interior gaps; D-03 explicitly wants interior NULLs to be a hard error. These are different policies for different domains; do not merge them into one helper.
- **Resurrecting `ts_filter.cpp`/`ts_imputation.cpp`'s dead `Register*` bodies verbatim** — D-09 says delete the files; copy the *logic* of `TsFillNullsMeanFunction` into new code, don't un-no-op the old registration calls (that would leave two divergent surfaces: the existing broken `_by` macro AND a newly-live scalar function with a different name/signature history).
- **Treating `anofox_ts_adf`/`kpss`/etc.'s existing `validity` parameter as "already fixed"** — the FFI signature supports it, but every current C++ call site still pre-filters NULLs via `ExtractListAsDoubleLocal` and passes `nullptr`. The bug is 100% in the C++ caller for these six functions; don't waste effort touching the FFI header for them.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| d-th order time-series differencing | A new SQL/C++ diff loop | `filter::diff` (`filter.rs:134`) — already numpy-`diff(n=d)`-correct | Already written, already order-shrinks correctly; only needs an FFI validity param + SQL realignment |
| Mean imputation respecting validity | New C++ AVG-with-NULL-skip logic | `imputation::fill_nulls_mean` (`imputation.rs:49`) via the already-validity-aware `anofox_ts_fill_nulls_mean` FFI fn | FFI already accepts a validity bitmask; zero new Rust/FFI work needed, only new C++ call-site + macro rewrite |
| Constant-series detection with tolerance | A new epsilon comparison in SQL (`MIN()!=MAX()`) or in C++ (`ts_filter.cpp`'s dead `1e-10` comparison) | `filter::is_constant` (`filter.rs:39`, uses `f64::EPSILON`) | Single source of truth for the tolerance value — SQL `MIN()!=MAX()` and the dead C++ code's `1e-10` literal are two *different*, both-wrong tolerances today |
| Frequency/periodicity detection from timestamps | A new median-step calculation (what `ts_forecast_agg.cpp:379-385` currently does) | `gaps::detect_frequency` via `anofox_ts_detect_frequency` FFI fn | Already exists and is exposed; only the tie-break determinism is a known, separately-tracked gap |
| Pairwise-complete-observations filtering for paired metrics | A bespoke drop-if-either-NULL loop per metric function | One new shared Rust helper (to be written this phase) reused by every metric/conformal/bootstrap FFI entry point | D-02 explicitly centralizes this policy in Rust; writing it once avoids N slightly-different reimplementations |

**Key insight:** Nearly every piece of "missing" Rust logic for this phase already exists (`diff`, `fill_nulls_mean`, `is_constant`, `detect_frequency`) — the actual work is almost entirely (a) fixing C++ marshalling to stop dropping NULLs/validity before they reach the FFI boundary, and (b) adding validity parameters to the ~10 FFI functions that don't have them yet, following an existing in-repo template (`anofox_ts_adf`) rather than inventing a new FFI convention.

## Common Pitfalls

### Pitfall 1: Confusing "FFI accepts validity" with "bug is fixed"
**What goes wrong:** Six FFI functions (`anofox_ts_adf`/`kpss`/`stationarity`/`ljung_box`/`durbin_watson`/`jarque_bera`) and six conformal functions already have a `validity` parameter in their signature. A plan that only checks the FFI header will conclude these are "already correct."
**Why it happens:** The actual bug is one layer up — `diagnostics.cpp`'s `ExtractListAsDoubleLocal` (`:31-49`) filters NULLs out of the `vector<double>` *before* calling the FFI, then passes `nullptr` for validity with an inline comment `// already filtered`. The FFI's validity support is simply unused.
**How to avoid:** Verification for each of these six functions must assert that the C++ call site passes a *non-null, non-trivial* validity array when the input list has NULLs — not just that the FFI signature has a `validity` parameter.
**Warning signs:** A `grep` for `/* validity= */ nullptr` or equivalent comments in the affected files.

### Pitfall 2: Treating `ts_diff_by`'s NULL policy as already decided
**What goes wrong:** D-03 lists eleven specific single-series analyses with an explicit "interior NULL at position k" error policy. `ts_diff_by` / `filter::diff` is not on that list, and `diff`'s underlying math (`values.windows(2)`) will happily propagate NaN through arbitrary interior positions without erroring.
**Why it happens:** D-05/D-06 focus entirely on the *order semantics* (d-th order vs. lag-k) and output alignment; NULL-handling for `ts_diff_by` input itself was not decided in CONTEXT.md.
**How to avoid:** Flag this explicitly to the user/planner as a decision gap before writing the `ts_diff_by` task (see Open Questions) rather than assuming D-03's policy silently extends to it.

### Pitfall 3: `require json`-gated tests give false confidence
**What goes wrong:** A plan that adds assertions to `test/sql/ts_diff.test`, `ts_conformal.test`, `ts_features.test`, `ts_forecast_agg.test`, `ts_seasonality.test`, `ts_changepoints.test`, `ts_decomposition.test`, `ts_periods.test`, `ts_detrend.test`, or `ts_peaks.test` may never actually execute those assertions in CI/this sandbox, because all ten `require json` and the `json` extension is not vendored in `build/release` here (confirmed: no `.duckdb_extension` file found; `LOAD json` only worked via what is presumably a network-fetched autoload in an earlier ad-hoc check, which is not guaranteed available in every execution environment).
**Why it happens:** `require json` was likely added historically because some unrelated test in the same file used `to_json`, and the whole file inherited the gate.
**How to avoid:** Per CONTEXT.md's explicit discretion ("SQL tests non-json-gated where possible"), new value-assertion tests for this phase's fixes should go in new or existing non-gated files (`ts_metrics.test`, `ts_imputation.test`, `ts_aggregate_hierarchy.test` are confirmed NOT gated) wherever the function under test doesn't itself require `json` functions.
**Warning signs:** `grep -n "require json"` at the top of any `.test` file you're about to add assertions to.

### Pitfall 4: `ts_stats.cpp`'s `ExtractListDoubles` four-argument signature differs from the ten-site copy's three-argument signature
**What goes wrong:** `ts_stats.cpp:100` is `ExtractListDoubles(Vector&, idx_t count, idx_t row_idx, vector<double>&, vector<uint64_t>&)` (5 params, outputs both values and validity) while the buggy copies are `ExtractListAsDouble(Vector&, idx_t row_idx, vector<double>&)` (3 params, no `count`, no validity output, NULLs dropped). A naive find-and-replace of the function body without updating every call site's argument list will not compile.
**How to avoid:** Update every call site's argument list, not just the helper definition, when swapping in the shared helper.

## Code Examples

### The correct marshalling template (copy this, don't invent a new one)
```cpp
// Source: src/table_functions/ts_stats.cpp:99-135 (verified in this session)
static void ExtractListDoubles(Vector &list_vec, idx_t count, idx_t row_idx,
                               vector<double> &out_values,
                               vector<uint64_t> &out_validity) {
    UnifiedVectorFormat list_data;
    list_vec.ToUnifiedFormat(count, list_data);

    auto list_entries = UnifiedVectorFormat::GetData<list_entry_t>(list_data);
    auto list_idx = list_data.sel->get_index(row_idx);
    auto &list_entry = list_entries[list_idx];

    auto &child_vec = ListVector::GetEntry(list_vec);
    UnifiedVectorFormat child_data;
    child_vec.ToUnifiedFormat(ListVector::GetListSize(list_vec), child_data);
    auto child_values = UnifiedVectorFormat::GetData<double>(child_data);

    out_values.clear();
    out_validity.clear();
    idx_t list_size = list_entry.length;
    idx_t list_offset = list_entry.offset;
    out_values.resize(list_size);
    size_t validity_words = (list_size + 63) / 64;
    out_validity.resize(validity_words, 0);

    for (idx_t i = 0; i < list_size; i++) {
        idx_t child_idx = list_offset + i;
        auto unified_child_idx = child_data.sel->get_index(child_idx);
        if (child_data.validity.RowIsValid(unified_child_idx)) {
            out_values[i] = child_values[unified_child_idx];
            out_validity[i / 64] |= (1ULL << (i % 64));
        } else {
            out_values[i] = 0.0;
        }
    }
}
```

### The existing FFI validity-pointer convention to follow for new signatures
```c
// Source: src/include/anofox_fcst_ffi.h:3384-3389 (verified in this session)
bool anofox_ts_adf(const double *values,
                   const uint64_t *validity,   // nullable: NULL means "all valid"
                   size_t length,
                   int max_lags,
                   struct AnofoxStationarityResult *out_result,
                   struct AnofoxError *out_error);
```

### Existing `_by`-macro dispatch pattern for one-row-per-group output (model for new macros)
```sql
-- Source: src/macros/ts_macros.cpp:2314-2322 (ts_adf_by, verified in this session)
SELECT group_col, ts_adf(LIST(value_col::DOUBLE ORDER BY date_col), max_lags::INTEGER) AS adf
FROM query_table(source::VARCHAR)
GROUP BY group_col
```

### The dead-but-correct reference for mean-fill-with-validity (copy the logic, not the file)
```cpp
// Source: src/table_functions/ts_imputation.cpp:265-317 (verified in this session;
// RegisterTsFillNullsMeanFunction at :319-322 is a no-op — this body is NEVER called today)
static void TsFillNullsMeanFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    // ... ExtractListValues(list_vec, count, row_idx, values, validity); ...
    bool success = anofox_ts_fill_nulls_mean(
        values.data(),
        validity.empty() ? nullptr : validity.data(),
        values.size(),
        &out_values,
        &error
    );
    // ...
}
```

### The 30-day-month bound bug (fix by interval multiplication, not epoch round-trip)
```sql
-- Source: src/macros/ts_macros.cpp:1114 (verified in this session) — current (buggy) bound:
UNNEST(generate_series(last_date + (SELECT _interval FROM _freq),
    last_date + (len((fcst).point)::INTEGER * EXTRACT(EPOCH FROM (SELECT _interval FROM _freq)) || ' seconds')::INTERVAL,
    (SELECT _interval FROM _freq)))::TIMESTAMP AS date,
-- Suggested fix: last_date + (len((fcst).point)::INTEGER * (SELECT _interval FROM _freq))
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|---------------|--------|
| `FlatVector::GetData` without `ToUnifiedFormat` for LIST args | `ToUnifiedFormat` on both list and child vector (D-01) | This phase | Constant/dictionary-encoded LIST arguments (e.g. a literal `[0,0,0]` broadcast across rows) now give correct per-row results instead of all-but-one-row being wrong |
| NULLs silently dropped before reaching Rust | Validity bitmask threaded through FFI, policy decided in Rust | This phase | `ts_mae([1,NULL,3,4],[1,2,NULL,4])` goes from `0.333` (wrong, misaligned pairs) to `0` (correct, pairwise-complete) |
| `ts_diff_by` = lag-k difference via `LAG(v,d)` | d-th order difference via Rust `diff` | This phase (breaking change, documented in CHANGELOG per D-05) | Old `diff_order=2` meant "subtract the value 2 rows back"; new `diff_order=2` means "second difference" (numpy `diff(n=2)`) — users wanting the old behavior use the new `seasonal_lag` parameter |
| Hierarchy NULL→0 before SUM | SQL `SUM` NULL semantics (all-NULL→NULL, partial→sum of present) | This phase | A group with some NULL leaf values now sums correctly instead of treating missing leaves as zero revenue/quantity |
| Frequency detection via median µs-diff | Rust `detect_frequency` + calendar-aware month/quarter/year stepping | This phase | `ts_forecast_agg` on monthly data no longer drifts off month-end boundaries over a multi-step horizon |

**Deprecated/outdated:**
- `ts_backtest_native` table function: unused by any macro since `ts_backtest_auto_by` was removed in an earlier milestone; removed outright this phase (D-07), along with every stale reference in `test/sql/ts_varchar_edge_cases.test:87-97`, `test/sql/backtest_memory_investigation.sql`, `docs/dev/memory-patterns.md`, `test/wasm/README.md`, `benchmark/streaming_api_profiling/README.md`, `plugins/anofox-forecast/skills/anofox-forecast-backtest/SKILL.md`.
- `ts_filter.cpp` / `ts_imputation.cpp`: entirely dead code (every `Register*` function is a no-op); deleted this phase (D-09). `RegisterTsEstimateBacktestMemoryFunction` (a *different*, still-live function registered at `anofox_forecast_extension.cpp:142`) is unrelated and must NOT be removed when `ts_backtest_native` goes.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | D-03's "interior NULL at position k" error policy should also apply to `ts_diff_by`'s input (not just the 11 explicitly-named analyses) | Code Examples / Pitfall 2 | If wrong, `ts_diff_by` on a series with interior NULLs could silently propagate NaN through the diff output instead of erroring or trimming as the user actually wants — flagged as Open Question, not assumed in the plan |
| A2 | The right place for the new pairwise-complete and trim-reject-interior Rust helpers is either a new small module or additions to `metrics.rs`/`validation.rs` (not a new crate-level concept) | Architecture Patterns | Low risk — CONTEXT.md explicitly delegates "helper API shape" to Claude's Discretion; this is a style choice, not a behavior choice |
| A3 | `anofox_ts_detect_frequency`'s tie-break nondeterminism (`gaps.rs:415-419`) does not need to be fixed within Phase 12 to satisfy D-12, and the planner can choose to either tolerate it or add a narrow, local determinism fix | Code Examples (D-10/D-12) | If wrong, `ts_forecast_agg`'s calendar-stepping decision ("is this series monthly?") could flip between runs on a tied-frequency series, making D-12's acceptance test (month-end monthly agg forecasts stay month-end) flaky |

**Risk note on A1/A3:** both are flagged explicitly in Open Questions below for a planner/user decision before task-writing, per the provenance rules — they are genuine scope gaps in CONTEXT.md, not just low-confidence technical guesses.

## Open Questions

1. **Does `ts_diff_by` need its own interior-NULL policy, and what is it?**
   - What we know: D-03 names eleven specific analyses (ADF, KPSS, Ljung-Box, DW, JB, periods, seasonality, changepoints, peaks, decomposition, detrend, features) with an explicit interior-NULL-errors / edge-trim policy. `ts_diff_by` (D-05/D-06) is a distinct, separately-decided capability whose CONTEXT.md decisions focus only on diff-order semantics and row alignment.
   - What's unclear: whether a NULL inside the series fed to `ts_diff_by` should (a) propagate as NaN through the diff output at every downstream position (current `filter::diff` behavior, since it takes plain `&[f64]`), (b) error with "interior NULL at position k" like D-03's domains, or (c) something else (e.g. only error if the NULL is NOT at a position the `d`-th order diff would discard anyway).
   - Recommendation: Ask the user/flag at plan-check time; in the absence of an answer, the safest default consistent with D-03's spirit is to apply the same trim-edges/error-interior policy, since `ts_diff_by` is a single-series analysis like the others — but this is the planner's call, not something this research should decide unilaterally.

2. **Should Phase 12 fix `detect_frequency`'s HashMap tie-break nondeterminism now, or defer entirely to Phase 13 (WRAP-03)?**
   - What we know: STATE.md explicitly flags WRAP-03 as "note: tie nondeterminism fix is Phase 13 WRAP-03 — coordinate or fix here if needed for D-12." The function is used by this phase's `ts_forecast_agg` D-12 fix.
   - What's unclear: whether D-12's acceptance test ("month-end monthly agg forecasts stay month-end") can be written in a way that's insensitive to the tie-break (e.g. test data with an unambiguous, non-tied modal frequency), making the nondeterminism irrelevant to this phase's DoD, or whether a general-purpose test suite would hit ties often enough that a narrow local fix (e.g. break ties by smallest diff value, independent of HashMap iteration order) is needed now.
   - Recommendation: Write D-12's test fixtures with an unambiguous modal frequency first (sidesteps the issue); if the planner's task design can't avoid ties, do a narrow local determinism fix (not a full WRAP-03) scoped to `detect_frequency`'s tie-break only.

3. **Where should the new shared C++ marshalling helper and the new Rust NULL-policy helpers physically live?**
   - What we know: CONTEXT.md explicitly delegates "helper API shape" to Claude's Discretion.
   - What's unclear: whether the C++ helper becomes a new header (`src/include/ts_list_marshal.hpp`) or lives in an existing shared header; whether the Rust helpers go in a new module, `validation.rs`, or `metrics.rs`.
   - Recommendation: New header/module is cleaner (avoids coupling ten unrelated `.cpp` files to one arbitrary "owner" file) — but this is a style decision for the planner, not a blocking question.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Rust toolchain (rustup 1.89.0) | All Rust/FFI changes | ✓ (per STATE.md: "MSRV corrected 1.86 -> true 1.89") | 1.89.0 | — |
| `build/release` extension (Phase 10-current) | SQL verification against built extension | ✓ | current | — |
| DuckDB `unittest` binary (`build/release/test/unittest`) | Running `.test` sqllogictest files | ✓ | bundled with build | — |
| `json` DuckDB extension | ~10 of the 68 `.test` files that `require json` (none strictly required by this phase's own new tests, but 52/68 existing files are gated) | ✗ (no `.duckdb_extension` file found in `build/release`; would need network autoload) | — | Write new value-assertion tests in non-gated files (`ts_metrics.test`, `ts_imputation.test`, `ts_aggregate_hierarchy.test` confirmed non-gated) |
| `cargo fmt` / `cargo clippy` | DoD per CLAUDE.md memory note ("Run cargo fmt --check in DoD") | ✓ (rust toolchain present) | — | — |

**Missing dependencies with no fallback:** none blocking — the `json` gap only affects whether *existing* tests execute, not whether this phase's work can be done or verified.

**Missing dependencies with fallback:** `json` extension — new/changed test assertions for this phase's behavior should target non-json-gated files per CONTEXT.md's explicit discretion note.

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | No | N/A — extension has no auth surface |
| V3 Session Management | No | N/A |
| V4 Access Control | No | N/A |
| V5 Input Validation | Yes | FFI null-pointer checks (`check_null_pointers`, already present e.g. `lib.rs:6562-6568`) and `catch_unwind` panic boundaries (already present on every FFI fn) must be preserved/extended identically on every FFI signature this phase modifies; new validity-bitmask parameters must be documented as nullable and bounds-checked against `length.div_ceil(64)` exactly like `build_values` already does (`lib.rs:138`) |
| V6 Cryptography | No | N/A — no crypto in this phase's scope |

### Known Threat Patterns for this stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Out-of-bounds read on a malformed/short validity bitmask passed across the FFI boundary | Tampering / DoS | Always derive `validity_words = length.div_ceil(64)` from the trusted `length` parameter (never trust a separately-passed word count), exactly as `build_values` already does — every new FFI signature added this phase must follow this convention, not invent a new one |
| Rust panic on unexpected NaN/Inf/empty input propagating across FFI as a process abort | DoS | Every new/modified FFI function must stay wrapped in `catch_unwind(AssertUnwindSafe(...))` exactly like the existing `anofox_ts_adf` template — this is already the project convention, just needs to be preserved when extending signatures |
| Silent data loss (NULL rows dropped instead of erroring/propagating) masking a data-quality problem as a correctness bug rather than a surfaced error | Repudiation (data integrity) | This is precisely what D-01 through D-11 are fixing — the "mitigation" IS this phase's deliverable; no new control needed beyond what's already planned |

## Sources

### Primary (HIGH confidence — all read directly this session, with line numbers cited inline above)
- `.planning/phases/12-statistics-marshalling-moves-to-rust/12-CONTEXT.md` — locked decisions D-01..D-16
- `.planning/REQUIREMENTS.md` — RUST-01..03, 05..09
- `.planning/research/STAT-AUDIT-2026-10-08.md` §D — original finding catalog (D1-D16)
- `.planning/STATE.md` — Phase 10/11 handoff notes, deferred items
- `.planning/phases/10-forecast-wrapper-delegates-to-crate-models/10-01-SUMMARY.md`, `10-10-SUMMARY.md` — input-handling policy precedent
- `src/scalar_functions/metrics.cpp`, `conformal.cpp`, `bootstrap.cpp`, `diagnostics.cpp`
- `src/table_functions/ts_periods.cpp`, `ts_detrend.cpp`, `ts_seasonality.cpp`, `ts_changepoints.cpp`, `ts_features.cpp`, `ts_peaks.cpp`, `ts_decomposition.cpp`, `ts_stats.cpp`, `ts_filter.cpp`, `ts_imputation.cpp`, `ts_aggregate_hierarchy.cpp`, `ts_backtest_native.cpp`, `ts_forecast_native.cpp`
- `src/aggregate_functions/ts_forecast_agg.cpp`, `ts_features_agg.cpp`
- `src/macros/ts_macros.cpp` (ts_diff_by, ts_fill_nulls_mean_by, drop_constant_by, ts_forecast_exog_by, ts_adf_by/ts_kpss_by dispatch pattern)
- `src/include/anofox_fcst_ffi.h`, `src/include/anofox_forecast_extension.hpp`
- `crates/anofox-fcst-core/src/metrics.rs`, `filter.rs`, `imputation.rs`, `gaps.rs`, `validation.rs`, `forecast.rs`, `peaks.rs`, `changepoint.rs`
- `crates/anofox-fcst-ffi/src/lib.rs` (`build_values`, `anofox_ts_adf` implementation)
- `CMakeLists.txt`, `src/anofox_forecast_extension.cpp`, `Makefile`
- `test/sql/*.test` (require-json audit), `.planning/config.json` (nyquist_validation: false, security_enforcement: true)

### Secondary (MEDIUM confidence)
- None — no web/external documentation was consulted; this is a pure in-repo investigation phase.

### Tertiary (LOW confidence)
- None.

## Metadata

**Confidence breakdown:**
- Marshalling bug locations (D-01/D-02): HIGH — every file/line read directly, confirmed via grep + Read
- FFI validity-parameter gaps: HIGH — full `anofox_fcst_ffi.h` signatures read for every affected function
- Dead-code finding (ts_filter.cpp/ts_imputation.cpp): HIGH — full files read, every `Register*` body confirmed as a no-op comment, cross-checked against `anofox_forecast_extension.cpp`'s actual call list
- Calendar/frequency findings (D-10/D-12/D-15): HIGH for the bug locations (read directly); MEDIUM for the exact numerical drift magnitude of D-15 (reasoned from DuckDB's documented interval-epoch convention, not executed against a live query this session)
- is_constant tolerance correction: HIGH — read directly, contradicts the audit document's stated value

**Research date:** 2026-10-09
**Valid until:** No external dependency volatility (internal refactor only) — valid until this phase's PLAN.md is written; re-verify line numbers if other phases (11, 13) land first and touch the same files.
