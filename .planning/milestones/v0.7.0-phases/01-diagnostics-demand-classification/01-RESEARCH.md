# Phase 1: Statistical Diagnostics — Research

**Researched:** 2026-08-21
**Domain:** Crate-to-SQL exposure: `anofox-forecast::validation` → FFI → C++ scalar → macro
**Confidence:** HIGH (all findings from direct file reads of source-of-truth files this session)

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- Deliver each capability as a scalar function returning a STRUCT per series, plus a `ts_*_by` macro (mirrors existing `ts_stats` / metrics pattern; composes with DuckDB GROUP BY for parallelism).
- Ship both per-test functions (`ts_adf`, `ts_kpss`, `ts_ljung_box`, `ts_durbin_watson`, `ts_jarque_bera`) and combined functions (`ts_stationarity`, `ts_residual_diagnostics`).
- p-values come from the standard approximation tables the crate already uses (MacKinnon for ADF; Kwiatkowski/KPSS tables). No new statistical table work.
- Naming follows the existing `ts_<name>` + `ts_<name>_by` convention exactly.
- ADF lag selection: AIC automatic (statsmodels default), with an override parameter.
- ADF regression: constant `'c'` default; allow `'ct'` (constant+trend) and `'n'` (none).
- KPSS null: level stationarity `'c'` default; allow `'ct'`.
- Ljung-Box lags: `min(10, n/5)` heuristic default, override allowed.
- All defaults match statsmodels conventions so the reference cross-check is apples-to-apples.
- Input: user supplies a residual column directly (the diagnostics operate on residuals).
- Significance level: `alpha = 0.05` default, configurable.
- Adequacy rule (RESID-04): Ljung-Box p > alpha (no residual autocorrelation) is the pass/fail gate; Jarque-Bera (normality) and Durbin-Watson (≈2) are advisory fields in the report.
- Combined return: one STRUCT carrying all three test statistics/p-values plus the overall pass/fail verdict.

### Claude's Discretion
- Exact STRUCT field names and ordering, FFI struct layout, and C++ registration details follow existing codebase conventions.
- Whether `ts_stationarity` internally reuses the `ts_adf`/`ts_kpss` FFI calls or calls a dedicated combined FFI entry point — pick whatever the crate exposes most cleanly (`crate::validation::test_stationarity` if available).

### Deferred Ideas (OUT OF SCOPE)
- INTER-01 — intermittent-demand classification. User has a "much more advanced approach" than standard ADI/CV² Syntetos-Boylan taxonomy. Removed from Phase 1; to be specified and scheduled separately. Do NOT build a placeholder ADI/CV² classifier.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| STAT-01 | `ts_adf` / `ts_adf_by`: ADF test returning statistic, p-value, lag | Crate: `adf_test(&[f64], Option<usize>) -> StationarityResult`; verified signatures below |
| STAT-02 | `ts_kpss` / `ts_kpss_by`: KPSS test returning statistic, p-value | Crate: `kpss_test(&[f64], Option<usize>) -> StationarityResult`; verified signatures below |
| STAT-03 | `ts_stationarity` / `ts_stationarity_by`: combined ADF+KPSS verdict | Crate: `test_stationarity(&[f64]) -> (StationarityResult, StationarityResult, &'static str)`; FFI adapter needed |
| RESID-01 | `ts_ljung_box` / `ts_ljung_box_by`: Ljung-Box white-noise test | Crate: `ljung_box(&[f64], Option<usize>, usize) -> LjungBoxResult`; verified |
| RESID-02 | `ts_durbin_watson` / `ts_durbin_watson_by`: DW statistic | Crate: `durbin_watson(&[f64]) -> DurbinWatsonResult`; verified |
| RESID-03 | `ts_jarque_bera` / `ts_jarque_bera_by`: JB normality test | Crate: `jarque_bera(&[f64]) -> JarqueBeraResult`; verified |
| RESID-04 | `ts_residual_diagnostics_by`: combined residual report with pass/fail | Crate: `diagnose_residuals(&[f64], usize) -> ResidualDiagnostics`; verified |
</phase_requirements>

---

## Summary

Phase 1 is a pure exposure phase: the statistical algorithms already exist in `anofox-forecast 0.15.3` under `crate::validation`. Zero new math is required. The work is wiring seven functions through the established 5-layer stack: (1) add `pub use` re-exports in `anofox-fcst-core/src/lib.rs`, (2) add FFI exports in `crates/anofox-fcst-ffi/src/lib.rs`, (3) add C++ scalar functions in a new `src/scalar_functions/diagnostics.cpp`, (4) register them in `src/anofox_forecast_extension.cpp` and `src/include/anofox_forecast_extension.hpp`, (5) add `ts_*_by` macros in `src/macros/ts_macros.cpp`.

All seven functions consume a `&[f64]` slice (no date column needed — these are value-only statistics), returning flat result structs. All return types are fully flat (`f64`, `usize`, `bool`, `&'static str`) — no heap allocations in the result structs, so FFI layout is straightforward. A STRUCT return pattern already exists in `src/scalar_functions/bootstrap.cpp` and `src/scalar_functions/conformal.cpp`; new diagnostic scalars follow exactly that pattern.

**Primary recommendation:** Model new diagnostic scalar functions on `bootstrap.cpp:TsBootstrapIntervalsFunction` (the simplest existing STRUCT-returning scalar) rather than on the metric functions (which return scalar `f64` values). The `_by` macros route through a new `_ts_diagnostics_native` table function or — simpler — directly through the scalar using `LIST(value ORDER BY date) GROUP BY group_col`, matching how `ts_inspect_by` and `ts_explain_by` work.

**Important discovery:** The `validation` module is NOT yet re-exported from `anofox-fcst-core/src/lib.rs` and no FFI functions for it exist yet. `test_stationarity` returns a tuple `(StationarityResult, StationarityResult, &'static str)` which requires a thin wrapper in core to flatten into a FFI-friendly struct.

---

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Statistical computation (ADF, KPSS, LB, DW, JB) | Rust Core (anofox-forecast crate) | — | All math already implemented in `crate::validation` |
| FFI boundary / NULL handling | Rust FFI (`anofox-fcst-ffi`) | — | Standard pattern: build_series + catch_unwind |
| STRUCT construction / DuckDB type system | C++ Scalar Function | — | StructVector::GetEntries pattern from bootstrap.cpp |
| SQL surface / `_by` grouping | SQL Macro (`ts_macros.cpp`) | — | `LIST(val ORDER BY ds) GROUP BY group_col` idiom |
| Series validation (min length, NULL filtering) | FFI boundary | — | Reject before Rust call if n < minimum |

---

## Section 1: Crate API Surface (Verified)

### 1.1 Stationarity Functions

**File:** `/tmp/.../crate/anofox-forecast-0.15.3/src/validation/stationarity.rs`

#### `StationarityResult` (lines 7–18)
[VERIFIED: /tmp/.../anofox-forecast-0.15.3/src/validation/stationarity.rs:7-18]

```
pub struct StationarityResult {
    pub statistic: f64,      // ADF: t-statistic (negative → stationary); KPSS: KPSS stat (positive)
    pub p_value: f64,        // approximate p-value
    pub lags: usize,         // number of lags used
    pub is_stationary: bool, // true if series appears stationary at 5% level
    pub critical_values: CriticalValues,  // cv_1pct, cv_5pct, cv_10pct
}
```

#### `CriticalValues` (lines 21–29)
[VERIFIED: /tmp/.../anofox-forecast-0.15.3/src/validation/stationarity.rs:21-29]

```
pub struct CriticalValues {
    pub cv_1pct: f64,
    pub cv_5pct: f64,
    pub cv_10pct: f64,
}
```

#### `adf_test` (lines 42–100)
[VERIFIED: /tmp/.../anofox-forecast-0.15.3/src/validation/stationarity.rs:42-100]

```rust
pub fn adf_test(series: &[f64], max_lags: Option<usize>) -> StationarityResult
```

- `max_lags = None` → automatic AIC selection: `max_lags = floor((n-1)^(1/3))`, clamped to `min(max_lags, n/2-1).max(1)`
- Returns NaN statistic if `n < 4`
- ADF regression: constant included (MacKinnon approximation). Critical values: cv_1pct=-3.43, cv_5pct=-2.86, cv_10pct=-2.57
- p-value: MacKinnon lookup table (9 breakpoints). Series is stationary if `t_stat < cv_5pct` (-2.86)
- **Note:** The crate's `adf_test` always uses constant regression (`'c'`). No `regression` parameter in v0.15.3. CONTEXT's `'ct'`/`'n'` modes are NOT currently in this function — the planner must expose only `'c'` mode for now, or expose the `max_lags` override and document the constant-only regression.

#### `kpss_test` (lines 279–357)
[VERIFIED: /tmp/.../anofox-forecast-0.15.3/src/validation/stationarity.rs:279-357]

```rust
pub fn kpss_test(series: &[f64], lags: Option<usize>) -> StationarityResult
```

- `lags = None` → `floor(4 * (n/100)^0.25)`, clamped to `min(lags, n/2).max(1)`
- Returns NaN statistic if `n < 4`
- Level stationarity (`'c'`): demeaning only; Bartlett kernel HAC variance
- Critical values: cv_1pct=0.739, cv_5pct=0.463, cv_10pct=0.347
- `is_stationary = stat < cv_5pct` (0.463)
- KPSS p-value: piecewise linear approximation (lines 360–375)

#### `test_stationarity` (lines 385–398)
[VERIFIED: /tmp/.../anofox-forecast-0.15.3/src/validation/stationarity.rs:385-398]

```rust
pub fn test_stationarity(series: &[f64]) -> (StationarityResult, StationarityResult, &'static str)
```

- Calls `adf_test(series, None)` and `kpss_test(series, None)` with defaults
- Conclusion string is one of exactly: `"stationary"`, `"non_stationary"`, `"inconclusive"`
  - "stationary": `adf.is_stationary && kpss.is_stationary`
  - "non_stationary": `!adf.is_stationary && !kpss.is_stationary`
  - "inconclusive": all other combinations

**STAT-03 gap:** The crate returns a `(&'static str, _, _)` tuple. The CONTEXT decision asks for a "four-way verdict (stationary / trend-stationary / difference-stationary / non-stationary)". The crate only returns three values ("stationary", "non_stationary", "inconclusive"). The planner must reconcile: either (a) map "inconclusive" to "inconclusive" in SQL and document the difference from the CONTEXT spec, or (b) add a thin Rust wrapper in `anofox-fcst-core` that maps the combination to the four-way taxonomy. **This is a planning decision, documented as an open question below.**

### 1.2 Residual Diagnostic Functions

**File:** `/tmp/.../crate/anofox-forecast-0.15.3/src/validation/residual_tests.rs`

#### `LjungBoxResult` (lines 6–16)
[VERIFIED: /tmp/.../anofox-forecast-0.15.3/src/validation/residual_tests.rs:6-16]

```
pub struct LjungBoxResult {
    pub statistic: f64,  // Q statistic
    pub p_value: f64,
    pub lags: usize,
    pub df: usize,       // degrees of freedom (lags - fitted_params, min 1)
}
```

#### `ljung_box` (lines 37–95)
[VERIFIED: /tmp/.../anofox-forecast-0.15.3/src/validation/residual_tests.rs:37-95]

```rust
pub fn ljung_box(residuals: &[f64], lags: Option<usize>, fitted_params: usize) -> LjungBoxResult
```

- `lags = None` → `min(10, n/5).max(1)`, then clamped to `n-1`
- Returns NaN if `n < 3`
- `fitted_params` adjusts df: `df = max(1, lags.saturating_sub(fitted_params))`
- Constant residuals → `statistic=0.0, p_value=1.0`
- SQL callers passing residuals from a model fit should use `fitted_params=0` (consistent with conservative Ljung-Box)

#### `DurbinWatsonResult` (lines 98–119)
[VERIFIED: /tmp/.../anofox-forecast-0.15.3/src/validation/residual_tests.rs:98-119]

```
pub struct DurbinWatsonResult {
    pub statistic: f64,                    // range [0, 4]
    pub interpretation: AutocorrelationType,
}

pub enum AutocorrelationType {
    PositiveStrong,  // DW < 0.5
    PositiveWeak,    // 0.5 <= DW < 1.5
    None,            // 1.5 <= DW <= 2.5
    NegativeWeak,    // 2.5 < DW < 3.5
    NegativeStrong,  // DW >= 3.5
}
```

#### `durbin_watson` (lines 131–173)
[VERIFIED: /tmp/.../anofox-forecast-0.15.3/src/validation/residual_tests.rs:131-173]

```rust
pub fn durbin_watson(residuals: &[f64]) -> DurbinWatsonResult
```

- Returns NaN if `n < 2`
- Zero residuals → `statistic=2.0, interpretation=None`
- Constant residuals → `statistic=0.0`
- Does NOT return a p-value (DW tables are complex; standard practice is to report the statistic only)
- `AutocorrelationType` must be serialized to a VARCHAR in the STRUCT (e.g., `"none"`, `"positive_weak"`, etc.)

#### `JarqueBeraResult` (lines 374–385)
[VERIFIED: /tmp/.../anofox-forecast-0.15.3/src/validation/residual_tests.rs:374-385]

```
pub struct JarqueBeraResult {
    pub statistic: f64,
    pub p_value: f64,
    pub skewness: f64,
    pub excess_kurtosis: f64,
}
```

#### `jarque_bera` (lines 395–428)
[VERIFIED: /tmp/.../anofox-forecast-0.15.3/src/validation/residual_tests.rs:395-428]

```rust
pub fn jarque_bera(residuals: &[f64]) -> JarqueBeraResult
```

- Returns all NaN if `n < 3`
- Constant residuals → `statistic=0.0, p_value=1.0, skewness=0.0, excess_kurtosis=0.0`
- p-value from chi-squared(df=2) survival function

#### `ResidualDiagnostics` (lines 431–439)
[VERIFIED: /tmp/.../anofox-forecast-0.15.3/src/validation/residual_tests.rs:431-439]

```
pub struct ResidualDiagnostics {
    pub ljung_box: LjungBoxResult,
    pub durbin_watson: DurbinWatsonResult,
    pub jarque_bera: JarqueBeraResult,
    pub mean: f64,
    pub variance: f64,
    pub n: usize,
}
```

- `is_adequate(alpha)` → `ljung_box.p_value > alpha` (the adequacy gate per CONTEXT)

#### `diagnose_residuals` (lines 478–498)
[VERIFIED: /tmp/.../anofox-forecast-0.15.3/src/validation/residual_tests.rs:478-498]

```rust
pub fn diagnose_residuals(residuals: &[f64], fitted_params: usize) -> ResidualDiagnostics
```

- Calls `ljung_box(residuals, None, fitted_params)`, `durbin_watson(residuals)`, `jarque_bera(residuals)`
- `mean` and `variance` (sample, df=n-1) computed inline
- Always returns (never fails) — result fields will be NaN for short series

### 1.3 Module Accessibility

[VERIFIED: /tmp/.../anofox-forecast-0.15.3/src/validation/mod.rs:1-63]

The `anofox_forecast::validation` module is:
- `pub mod validation` at crate root (unconditionally — not feature-gated)
- `aid` submodule is `#[cfg(feature = "postprocess")]` only
- `stationarity`, `residual_tests`, `diagnostics` submodules are always compiled

[VERIFIED: /home/simonm/projects/duckdb/anofox-forecast/Cargo.toml:13-14]

Current workspace dependency: `anofox-forecast = { version = "0.15.3", features = ["anomaly", "serde"] }`. The `postprocess` feature is the crate default but is NOT in the workspace `features` list. **This does not matter** for Phase 1 because the required functions (`stationarity`, `residual_tests`) are unconditionally available. No feature flag changes needed.

[VERIFIED: /home/simonm/projects/duckdb/anofox-forecast/crates/anofox-fcst-core/src/lib.rs:1-108]

The `anofox-fcst-core` crate does NOT currently re-export any `validation` items. Phase 1 must add these re-exports to `crates/anofox-fcst-core/src/lib.rs`.

---

## Section 2: The 5-Layer Exposure Recipe

The proven pattern for adding a new scalar function + `_by` macro. Every layer has a verified precedent.

### Layer 0: Crate — re-export from `anofox-fcst-core`

**File:** `crates/anofox-fcst-core/src/lib.rs`
[VERIFIED: /home/simonm/projects/duckdb/anofox-forecast/crates/anofox-fcst-core/src/lib.rs:1-108]

Add a new `pub mod validation` or individual `pub use` lines:

```rust
// New additions for Phase 1
pub use anofox_forecast::validation::{
    adf_test, kpss_test, test_stationarity,
    StationarityResult, CriticalValues,
};
pub use anofox_forecast::validation::{
    ljung_box, durbin_watson, jarque_bera, box_pierce, diagnose_residuals,
    LjungBoxResult, DurbinWatsonResult, DurbinWatsonInterpretation,
    JarqueBeraResult, ResidualDiagnostics, AutocorrelationType,
};
```

**Note on `test_stationarity` tuple:** The function returns `(StationarityResult, StationarityResult, &'static str)`. The FFI layer should call `adf_test` and `kpss_test` separately and implement the verdict logic inline in the FFI function — this avoids passing a Rust tuple through the FFI boundary and gives full control over the four-way classification if desired.

### Layer 1: FFI — `crates/anofox-fcst-ffi/src/lib.rs`

**Precedent:** `anofox_ts_stats` (lines 139–180) — best structural analog: single series input, flat result struct output.
[VERIFIED: /home/simonm/projects/duckdb/anofox-forecast/crates/anofox-fcst-ffi/src/lib.rs:139-180]

**Pattern per function:**

```rust
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_adf(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    max_lags: c_int,          // -1 = auto (None)
    out_result: *mut AdfResult,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);
    // null pointer check
    let result = catch_unwind(AssertUnwindSafe(|| {
        let series = build_values(values, validity, length);  // NULLs → NaN
        let max_lags_opt = if max_lags < 0 { None } else { Some(max_lags as usize) };
        anofox_fcst_core::adf_test(&series, max_lags_opt)
    }));
    match result {
        Ok(r) => { *out_result = r.into(); true }
        Err(_) => { set_error(...); false }
    }
}
```

**NULL handling:** For diagnostic tests, NULLs in the series should be treated as NaN and propagated (the crate functions operate on `&[f64]`, not `Vec<Option<f64>>`). Use `build_values()` (line 91–112) instead of `build_series()` to get NaN for missing values. The crate's NaN-propagation behavior is already tested (verified in `residual_tests.rs` tests at lines 1009–1039).

**New FFI result structs** (add to `crates/anofox-fcst-ffi/src/types.rs`):

```c
// ADF / KPSS share StationarityResult layout
typedef struct {
    double statistic;
    double p_value;
    uintptr_t lags;
    bool is_stationary;
    double cv_1pct;
    double cv_5pct;
    double cv_10pct;
} AnofoxStationarityResult;

typedef struct {
    double adf_statistic;
    double adf_p_value;
    uintptr_t adf_lags;
    bool adf_is_stationary;
    double kpss_statistic;
    double kpss_p_value;
    uintptr_t kpss_lags;
    bool kpss_is_stationary;
    char verdict[32];  // "stationary" | "non_stationary" | "inconclusive"
} AnofoxCombinedStationarityResult;

typedef struct {
    double statistic;
    double p_value;
    uintptr_t lags;
    uintptr_t df;
} AnofoxLjungBoxResult;

typedef struct {
    double statistic;
    char interpretation[24];  // "none" | "positive_weak" | "positive_strong" | ...
} AnofoxDurbinWatsonResult;

typedef struct {
    double statistic;
    double p_value;
    double skewness;
    double excess_kurtosis;
} AnofoxJarqueBeraResult;

typedef struct {
    double lb_statistic;
    double lb_p_value;
    uintptr_t lb_lags;
    double dw_statistic;
    char dw_interpretation[24];
    double jb_statistic;
    double jb_p_value;
    double jb_skewness;
    double jb_excess_kurtosis;
    double mean;
    double variance;
    uintptr_t n;
    bool is_adequate;  // lb_p_value > alpha
    double alpha;
} AnofoxResidualDiagnosticsResult;
```

### Layer 2: C++ Scalar Function — `src/scalar_functions/diagnostics.cpp`

**Precedent:** `src/scalar_functions/bootstrap.cpp` for STRUCT-returning scalar functions.
[VERIFIED: /home/simonm/projects/duckdb/anofox-forecast/src/scalar_functions/bootstrap.cpp:52-182]

The STRUCT-returning scalar pattern:

```cpp
// Step 1: Build return type in RegisterTs*Function
child_list_t<LogicalType> struct_children;
struct_children.push_back(make_pair("statistic", LogicalType(LogicalTypeId::DOUBLE)));
struct_children.push_back(make_pair("p_value",   LogicalType(LogicalTypeId::DOUBLE)));
struct_children.push_back(make_pair("lags",       LogicalType(LogicalTypeId::BIGINT)));
auto result_type = LogicalType::STRUCT(std::move(struct_children));

// Step 2: Register as ScalarFunction with result_type
ScalarFunctionSet adf_set("ts_adf");
adf_set.AddFunction(ScalarFunction(
    {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},  // input: LIST(DOUBLE)
    result_type,
    TsAdfFunction
));

// Step 3: In TsAdfFunction, write to struct entries
static void TsAdfFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    idx_t count = args.size();
    auto &struct_entries = StructVector::GetEntries(result);  // matches struct_children order
    auto &stat_out  = *struct_entries[0];
    auto &pval_out  = *struct_entries[1];
    auto &lags_out  = *struct_entries[2];

    auto stat_data  = FlatVector::GetData<double>(stat_out);
    auto pval_data  = FlatVector::GetData<double>(pval_out);
    auto lags_data  = FlatVector::GetData<int64_t>(lags_out);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(values_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }
        vector<double> values;
        ExtractListAsDouble(values_vec, row_idx, values);

        AnofoxStationarityResult r;
        AnofoxError error;
        bool ok = anofox_ts_adf(values.data(), /* validity= */ nullptr,
                                values.size(), /* max_lags= */ -1,
                                &r, &error);
        if (!ok) { FlatVector::SetNull(result, row_idx, true); continue; }
        stat_data[row_idx] = r.statistic;
        pval_data[row_idx] = r.p_value;
        lags_data[row_idx] = (int64_t)r.lags;
    }
}
```

**Input type:** `LIST(DOUBLE)` — users pass `LIST(value ORDER BY date)` from a GROUP BY query, matching how `ts_mae`, `ts_bootstrap_intervals`, etc. are called.

**VARCHAR fields** (for `dw_interpretation`, `verdict`): Write as `FlatVector::GetData<string_t>(varchar_entry)[row_idx] = StringVector::AddString(varchar_entry, ...)`.

### Layer 3: Registration — `src/anofox_forecast_extension.cpp`

**Precedent:** lines 120–131 in the metrics registration block.
[VERIFIED: /home/simonm/projects/duckdb/anofox-forecast/src/anofox_forecast_extension.cpp:120-131]

Add after the bootstrap block:
```cpp
// Diagnostic tests (STAT-01..03, RESID-01..04)
RegisterTsAdfFunction(loader);
RegisterTsKpssFunction(loader);
RegisterTsStationarityFunction(loader);
RegisterTsLjungBoxFunction(loader);
RegisterTsDurbinWatsonFunction(loader);
RegisterTsJarqueBeraFunction(loader);
RegisterTsResidualDiagnosticsFunction(loader);
```

Also add declarations to `src/include/anofox_forecast_extension.hpp`.

### Layer 4: SQL Macro — `src/macros/ts_macros.cpp`

**Precedent:** `ts_mae_by` (line 2017).
[VERIFIED: /home/simonm/projects/duckdb/anofox-forecast/src/macros/ts_macros.cpp:2017-2023]

The diagnostic `_by` macros group the value column into a list and call the scalar:

```cpp
// ts_adf_by(source, group_col, date_col, value_col)
{"ts_adf_by", {"source", "group_col", "date_col", "value_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT group_col, ts_adf(LIST(value_col::DOUBLE ORDER BY date_col)) AS adf
FROM query_table(source::VARCHAR)
GROUP BY group_col
)",
    "ADF stationarity test per group. Returns STRUCT(statistic, p_value, lags, is_stationary).",
    "SELECT group_col, (adf).statistic, (adf).p_value FROM ts_adf_by('sales', product_id, ds, y)",
    "diagnostics"},
```

Similarly for `ts_kpss_by`, `ts_stationarity_by`, `ts_ljung_box_by`, `ts_durbin_watson_by`, `ts_jarque_bera_by`, `ts_residual_diagnostics_by`.

**Named parameter for optional args** (e.g., lags override): use the `named_params` slot in `TsTableMacro`:
```cpp
{{"max_lags", "-1"}, {nullptr, nullptr}}   // -1 = auto
```

---

## Section 3: STRUCT Return Handling in DuckDB

**STRUCT-returning scalar functions exist and are the right pattern.**

[VERIFIED: /home/simonm/projects/duckdb/anofox-forecast/src/scalar_functions/bootstrap.cpp:117-182]

The full recipe:

1. `child_list_t<LogicalType> struct_children;` — build type descriptor
2. `auto result_type = LogicalType::STRUCT(std::move(struct_children));` — build return type
3. Pass `result_type` as the return type in `ScalarFunction(...)` constructor
4. In the execute function: `auto &struct_entries = StructVector::GetEntries(result);` — parallel order with `struct_children`
5. For scalar fields: `FlatVector::GetData<double>(*struct_entries[i])[row_idx] = value;`
6. For NULL rows: `FlatVector::SetNull(result, row_idx, true);` — marks the whole STRUCT as NULL

No table function or additional indirection is needed. The CONTEXT decision for scalar+STRUCT is confirmed to be achievable with the existing scalar function infrastructure.

---

## Section 4: Reference Cross-Check Harness

**Current benchmark pattern:**
[VERIFIED: /home/simonm/projects/duckdb/anofox-forecast/benchmark/m4/baseline_benchmark/run.py:1-31]

Benchmarks under `benchmark/m4/*/` are Python scripts using a shared `src/common/benchmark_runner.py`. They compare DuckDB extension output against statsforecast or M4 reference values.

**For Phase 1, the cross-check is a Python script, not an M4 competition benchmark.** The DoD requires numeric cross-check against statsmodels/R. Recommended location: `benchmark/diagnostics/` (new directory). Structure:

```
benchmark/diagnostics/
├── reference_values.py     # Generate reference values via statsmodels adfuller, kpss, acorr_ljungbox, durbin_watson, jarque_bera
├── run_anofox.py           # Run same series through DuckDB ts_adf_by etc., compare outputs
└── fixtures/
    └── test_series.parquet # Small deterministic series (30-200 obs)
```

**Reference functions to call (statsmodels):**
- ADF: `statsmodels.tsa.stattools.adfuller(series, maxlag=None, regression='c', autolag='AIC')`
- KPSS: `statsmodels.tsa.stattools.kpss(series, regression='c', nlags='auto')`
- Ljung-Box: `statsmodels.stats.diagnostic.acorr_ljungbox(residuals, lags=[10])`
- Durbin-Watson: `statsmodels.stats.stattools.durbin_watson(residuals)`
- Jarque-Bera: `statsmodels.stats.stattools.jarque_bera(residuals)` or `scipy.stats.jarque_bera`

**Tolerance note:** The crate uses simplified p-value approximations (piecewise tables, not the exact MacKinnon regression). Numeric parity will be approximate. Cross-check should use a 5–10% relative tolerance on p-values, not exact equality. Statistic values (before p-value lookup) should match more closely.

**Example output location:** `examples/diagnostics/` (new directory):
```
examples/diagnostics/
├── stationarity.sql        # ts_adf_by, ts_kpss_by, ts_stationarity_by examples
└── residual_diagnostics.sql  # ts_ljung_box_by, ts_durbin_watson_by, ts_jarque_bera_by, ts_residual_diagnostics_by
```

---

## Section 5: Common Pitfalls and Landmines

### Pitfall 1: `build_series` vs `build_values` for diagnostic functions

**What goes wrong:** Using `build_series()` (returns `Vec<Option<f64>>`) with `adf_test(&[f64], ...)` requires unwrapping Options. Alternatively, using `build_values()` (returns `Vec<f64>` with NaN for NULLs) means the crate functions receive NaN values — which the tests confirm propagate through to NaN outputs.
[VERIFIED: /home/simonm/projects/duckdb/anofox-forecast/crates/anofox-fcst-ffi/src/lib.rs:89-112]

**How to avoid:** Use `build_values()` for diagnostic functions — NaN-for-NULL is the correct behavior (not masking missings as zeros). The crate's NaN propagation is verified behavior.

### Pitfall 2: Minimum series length not checked before FFI call

**What goes wrong:** ADF and KPSS return NaN statistic for `n < 4`; Ljung-Box and Jarque-Bera return NaN for `n < 3`; Durbin-Watson returns NaN for `n < 2`. These are handled by the crate, but the C++ layer should still check `length == 0` before calling (as `anofox_ts_stats` does at line 157).
[VERIFIED: /home/simonm/projects/duckdb/anofox-forecast/crates/anofox-fcst-ffi/src/lib.rs:157-159]

**How to avoid:** Add `if (length == 0) { FlatVector::SetNull(result, row_idx, true); continue; }` in the C++ execute function.

### Pitfall 3: `test_stationarity` tuple return — do not pass through FFI

**What goes wrong:** `test_stationarity` returns `(StationarityResult, StationarityResult, &'static str)`. Passing a tuple through FFI requires either a flat struct or returning two out-ptrs. The `&'static str` `"inconclusive"` cannot be directly copied into a `char[]` without `copy_string_to_buffer`.
[VERIFIED: /tmp/.../anofox-forecast-0.15.3/src/validation/stationarity.rs:385-398]

**How to avoid:** Call `adf_test` + `kpss_test` separately in the FFI function for `ts_stationarity`, implement the verdict logic (`adf.is_stationary && kpss.is_stationary → "stationary"` etc.) inline in the FFI, and copy to a `char[32]` verdict buffer using `copy_string_to_buffer`.

### Pitfall 4: Four-way verdict vs crate's three-way verdict

**What goes wrong:** CONTEXT.md specifies a four-way verdict: "stationary / trend-stationary / difference-stationary / non-stationary". The crate only produces three strings: "stationary", "non_stationary", "inconclusive".
[VERIFIED: /tmp/.../anofox-forecast-0.15.3/src/validation/stationarity.rs:389-396]

**How to avoid:** The four-way taxonomy (trend-stationary = KPSS rejects but ADF doesn't, difference-stationary = ADF rejects but KPSS doesn't) requires crate changes beyond v0.15.3, OR a mapping implemented in the FFI wrapper. The planner must decide: expose the crate's three-way verdict and document the "inconclusive" state, or add a thin wrapper. Recommend exposing crate's three values plus renaming "inconclusive" to the appropriate four-way label based on which test rejects:
- ADF rejects + KPSS does not reject → "stationary"
- ADF does not reject + KPSS rejects → "non_stationary" (or "difference-stationary" if differencing is implied)
- Both reject → "non_stationary" (strong evidence)
- Neither rejects → "stationary" or "inconclusive" depending on interpretation

The four-way mapping is achievable in the FFI without crate changes — document the specific boolean logic in the plan.

### Pitfall 5: `AutocorrelationType` enum — must convert to VARCHAR

**What goes wrong:** `DurbinWatsonResult.interpretation` is a Rust enum (`AutocorrelationType::PositiveStrong` etc.). FFI cannot pass an enum directly; C++ does not know this type.
[VERIFIED: /tmp/.../anofox-forecast-0.15.3/src/validation/residual_tests.rs:108-119]

**How to avoid:** Convert to string in the FFI: match on the enum variant and `copy_string_to_buffer` one of `"positive_strong"`, `"positive_weak"`, `"none"`, `"negative_weak"`, `"negative_strong"` into a `char[24]` field.

### Pitfall 6: `anofox_fcst_ffi.h` — remember to declare new FFI functions

**What goes wrong:** The C++ code includes `anofox_fcst_ffi.h`. New FFI functions must be declared there, or C++ will not see them.
[VERIFIED: /home/simonm/projects/duckdb/anofox-forecast/src/include/anofox_fcst_ffi.h (filename confirmed)]

**How to avoid:** Add C declarations for all new `anofox_ts_adf`, `anofox_ts_kpss`, etc. functions and their result struct types to `src/include/anofox_fcst_ffi.h`.

### Pitfall 7: Numeric parity vs statsmodels will not be exact

**What goes wrong:** The crate's ADF p-value uses a 9-entry lookup table, not the full MacKinnon regression. KPSS p-value uses a piecewise linear approximation. These will not match statsmodels exactly.
[VERIFIED: /tmp/.../anofox-forecast-0.15.3/src/validation/stationarity.rs:247-266, 360-375]

**How to avoid:** Document in the `docs/api/` entry that p-values are approximate (same caveat as statsmodels' own disclaimer). In the cross-check script, use `rtol=0.10` (10% relative tolerance) for p-values, `rtol=0.01` for test statistics.

### Pitfall 8: `fitted_params` parameter for Ljung-Box

**What goes wrong:** `ljung_box` takes `fitted_params: usize` for df adjustment. When residuals come from a model with p+q fitted parameters (ARIMA), the df should be `lags - (p+q)`. When called from SQL on raw residuals with no model context, `fitted_params=0` is correct.

**How to avoid:** Expose `fitted_params` as an optional SQL parameter defaulting to `0`. Document this clearly in examples.

---

## Section 6: Per-Requirement Implementation Notes

### STAT-01: `ts_adf` / `ts_adf_by`

- **FFI function:** `anofox_ts_adf(values, validity, length, max_lags: c_int, out: *mut AnofoxStationarityResult, error) -> bool`
- **SQL input:** `ts_adf(LIST(DOUBLE)) → STRUCT(statistic DOUBLE, p_value DOUBLE, lags BIGINT, is_stationary BOOLEAN, cv_1pct DOUBLE, cv_5pct DOUBLE, cv_10pct DOUBLE)`
- **Named param:** `max_lags INTEGER DEFAULT -1` (-1 → AIC automatic)
- **Minimum n:** 4 (returns NaN for shorter — document in SQL description)
- **`_by` macro:** `ts_adf_by(source, group_col, date_col, value_col, max_lags:=-1)`

### STAT-02: `ts_kpss` / `ts_kpss_by`

- **FFI function:** `anofox_ts_kpss(values, validity, length, lags: c_int, out: *mut AnofoxStationarityResult, error) -> bool`
- **SQL input:** `ts_kpss(LIST(DOUBLE)) → STRUCT(statistic, p_value, lags, is_stationary, cv_1pct, cv_5pct, cv_10pct)`
- **Named param:** `lags INTEGER DEFAULT -1` (-1 → automatic)
- **Minimum n:** 4
- **Note:** KPSS test statistic is positive (larger = more non-stationary); opposite direction from ADF

### STAT-03: `ts_stationarity` / `ts_stationarity_by`

- **FFI function:** `anofox_ts_stationarity(values, validity, length, out: *mut AnofoxCombinedStationarityResult, error) -> bool`
  - Calls `adf_test` and `kpss_test` with defaults, maps verdict inline
- **SQL input:** `ts_stationarity(LIST(DOUBLE)) → STRUCT(adf_statistic, adf_p_value, adf_lags, kpss_statistic, kpss_p_value, kpss_lags, verdict VARCHAR)`
- **Verdict enum values (FFI produces):** See Pitfall 4 — implement four-way classification in FFI wrapper
- **No named params** (fixed defaults per CONTEXT)

### RESID-01: `ts_ljung_box` / `ts_ljung_box_by`

- **FFI function:** `anofox_ts_ljung_box(values, validity, length, lags: c_int, fitted_params: c_int, out: *mut AnofoxLjungBoxResult, error) -> bool`
- **SQL input:** `ts_ljung_box(LIST(DOUBLE)) → STRUCT(statistic, p_value, lags BIGINT, df BIGINT)`
- **Named params:** `lags INTEGER DEFAULT -1`, `fitted_params INTEGER DEFAULT 0`
- **Minimum n:** 3

### RESID-02: `ts_durbin_watson` / `ts_durbin_watson_by`

- **FFI function:** `anofox_ts_durbin_watson(values, validity, length, out: *mut AnofoxDurbinWatsonResult, error) -> bool`
- **SQL input:** `ts_durbin_watson(LIST(DOUBLE)) → STRUCT(statistic DOUBLE, interpretation VARCHAR)`
- **`interpretation` VARCHAR values** (FFI converts enum): `"positive_strong"`, `"positive_weak"`, `"none"`, `"negative_weak"`, `"negative_strong"`
- **No p-value** (DW has no closed-form p-value in the crate)
- **Minimum n:** 2

### RESID-03: `ts_jarque_bera` / `ts_jarque_bera_by`

- **FFI function:** `anofox_ts_jarque_bera(values, validity, length, out: *mut AnofoxJarqueBeraResult, error) -> bool`
- **SQL input:** `ts_jarque_bera(LIST(DOUBLE)) → STRUCT(statistic, p_value, skewness, excess_kurtosis)`
- **Minimum n:** 3

### RESID-04: `ts_residual_diagnostics` / `ts_residual_diagnostics_by`

- **FFI function:** `anofox_ts_residual_diagnostics(values, validity, length, fitted_params: c_int, alpha: c_double, out: *mut AnofoxResidualDiagnosticsResult, error) -> bool`
- **SQL input:** `ts_residual_diagnostics(LIST(DOUBLE)) → STRUCT(lb_statistic, lb_p_value, lb_lags, dw_statistic, dw_interpretation, jb_statistic, jb_p_value, jb_skewness, jb_excess_kurtosis, is_adequate BOOLEAN, alpha DOUBLE)`
- **Named params:** `fitted_params INTEGER DEFAULT 0`, `alpha DOUBLE DEFAULT 0.05`
- **Adequacy gate:** `is_adequate = lb_p_value > alpha` (Ljung-Box gate per CONTEXT)

---

## Architecture Patterns

### System Architecture Diagram

```
SQL User
  │  ts_adf_by('sales', product_id, ds, y)
  ▼
ts_macros.cpp  [ts_adf_by macro]
  │  SELECT group_col, ts_adf(LIST(y ORDER BY ds)) FROM ... GROUP BY group_col
  ▼
scalar_functions/diagnostics.cpp  [TsAdfFunction]
  │  StructVector::GetEntries(result) → write statistic, p_value, lags fields
  │  ExtractListAsDouble(list_vec, row_idx, values)
  │  anofox_ts_adf(values.data(), nullptr, values.size(), -1, &r, &err)
  ▼
crates/anofox-fcst-ffi/src/lib.rs  [anofox_ts_adf]
  │  catch_unwind { build_values → anofox_fcst_core::adf_test }
  ▼
crates/anofox-fcst-core/src/lib.rs  [re-exports anofox_forecast::validation::adf_test]
  ▼
anofox-forecast 0.15.3  [adf_test(&[f64], Option<usize>) → StationarityResult]
```

### Recommended File Layout for Phase 1

```
crates/anofox-fcst-core/src/lib.rs       # +pub use anofox_forecast::validation::{...}
crates/anofox-fcst-ffi/src/lib.rs        # +anofox_ts_adf, anofox_ts_kpss, ..., anofox_ts_residual_diagnostics
crates/anofox-fcst-ffi/src/types.rs      # +AnofoxStationarityResult, AnofoxCombinedStationarityResult, ...
src/include/anofox_fcst_ffi.h            # +C declarations for all new FFI functions + structs
src/scalar_functions/diagnostics.cpp     # TsAdfFunction, TsKpssFunction, ..., TsResidualDiagnosticsFunction
src/include/anofox_forecast_extension.hpp  # +void RegisterTsAdf..., RegisterTsResidualDiagnostics...
src/anofox_forecast_extension.cpp        # +RegisterTs*Function(loader) calls
src/macros/ts_macros.cpp                 # +ts_adf_by, ts_kpss_by, ..., ts_residual_diagnostics_by macros
examples/diagnostics/stationarity.sql
examples/diagnostics/residual_diagnostics.sql
docs/api/10-diagnostics.md              # New doc page (number TBD; slot after 09-evaluation-metrics.md)
benchmark/diagnostics/reference_values.py
benchmark/diagnostics/run_anofox.py
benchmark/diagnostics/fixtures/test_series.parquet
```

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead |
|---------|-------------|-------------|
| ADF test statistics | Custom regression in C++ | `anofox_forecast::validation::adf_test` — already implemented |
| Chi-squared p-values for Ljung-Box / JB | `lgamma`, continued fraction | `crate::validation::residual_tests::chi_squared_sf` — already in crate |
| STRUCT type construction | Custom type dispatch | `LogicalType::STRUCT(child_list_t<LogicalType>)` — see bootstrap.cpp:123 |
| Group-by dispatching | Custom threading | DuckDB GROUP BY + scalar function; the `_by` macro pattern |
| NaN propagation for missing values | Custom guard clauses | `build_values()` in FFI — gives NaN-for-NULL automatically |

---

## State of the Art

| Old Approach | Current Approach | Impact |
|--------------|------------------|--------|
| No SQL diagnostic tests | Phase 1 exposes them | SQL users can validate without leaving DuckDB |
| `anofox-fcst-core` has no validation re-exports | Phase 1 adds them | Enables FFI access |
| No `diagnostics.cpp` scalar file | Phase 1 creates it | Clean separation from metrics.cpp |

**Not applicable:** No deprecated patterns involved — this is new exposure, not migration.

---

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | ADF critical values in the crate are hardcoded for constant-regression only (cv_1pct=-3.43, cv_5pct=-2.86, cv_10pct=-2.57); `'ct'` and `'n'` regression types are not supported in v0.15.3 | Stationarity, Pitfall 4 | If crate is updated to support regression type, the FFI parameter must be added |
| A2 | The `postprocess` feature being absent from workspace dependency features does not affect the validation module (which is unconditionally compiled) | Section 1.3 | If the crate's `lib.rs` feature-gates `pub mod validation`, Phase 1 would need a Cargo.toml change |
| A3 | `copy_string_to_buffer` from `crates/anofox-fcst-ffi/src/lib.rs` line 121 is the correct pattern for `char[]` fields | Section 2 Layer 1 | If signature changed, the helper must be located |
| A4 | The four-way verdict (stationary/trend-stationary/difference-stationary/non-stationary) can be derived from the two boolean flags `adf.is_stationary` and `kpss.is_stationary` in the FFI without crate changes | Pitfall 4, STAT-03 | The mapping interpretation (which combination = which label) must be verified against textbook definitions before shipping |

---

## Open Questions

1. **Four-way verdict label mapping for STAT-03**
   - What we know: crate returns three strings; CONTEXT asks for four-way
   - What's unclear: exact mapping of (adf_is_stationary, kpss_is_stationary) → label. Standard textbook: (ADF rejects, KPSS doesn't) = stationary; (ADF doesn't, KPSS rejects) = difference-stationary; (ADF rejects, KPSS rejects) = contradictory/inconclusive; (neither rejects) = non-stationary.
   - Recommendation: Implement in FFI wrapper as a match on (bool, bool), document all four cases in SQL description. No crate change required.

2. **`ts_adf` regression parameter**
   - What we know: crate only implements constant (`'c'`) regression in v0.15.3
   - What's unclear: whether CONTEXT's `'ct'` / `'n'` modes are expected to be functional in Phase 1
   - Recommendation: Expose only `'c'` mode and document clearly; add an `[ASSUMED]` override parameter that is accepted but no-ops for now, with a TODO for when the crate is updated

3. **SQL function naming: `ts_adf` takes a LIST input, not individual columns**
   - What we know: metrics functions like `ts_mae(LIST(...), LIST(...))` follow this pattern
   - What's unclear: whether callers prefer `ts_adf(value_col)` (aggregate-like) vs `ts_adf(LIST(value_col ORDER BY ds))` (explicit)
   - Recommendation: Use explicit LIST input (mirrors `ts_bootstrap_intervals`); the `_by` macro hides this with `LIST(...) GROUP BY`

---

## Environment Availability

Step 2.6: SKIPPED — Phase 1 is code/C++ extension changes only; no external tools, services, or runtimes beyond the project's own build system are introduced. Build environment verified already working (CI green per recent commits).

---

## Validation Architecture

`workflow.nyquist_validation` is explicitly `false` in `.planning/config.json` — this section is skipped.

---

## Security Domain

`security_enforcement` is enabled (present in config, no explicit `false`). `security_asvs_level: 1`.

### Applicable ASVS Categories for Phase 1

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | No | N/A — extension doesn't authenticate |
| V3 Session Management | No | N/A |
| V4 Access Control | No | N/A — DuckDB handles access |
| V5 Input Validation | Yes | FFI null-pointer checks; min-length guards |
| V6 Cryptography | No | N/A — statistical computations only |

### Known Threat Patterns

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| NULL pointer dereference at FFI boundary | Tampering | `check_null_pointers(out_error, ptrs)` at entry of every FFI function (established pattern) |
| Integer overflow in `max_lags` cast | Tampering | Clamp at FFI: `max_lags < 0 → None; else Some(max_lags as usize)` with range check |
| Panic in Rust statistical computation | DoS | `catch_unwind(AssertUnwindSafe(...))` wraps every FFI call (established pattern) |
| Empty/zero-length series | DoS | Check `length == 0` before calling crate (established pattern — see anofox_ts_stats:157) |

---

## Sources

### Primary (HIGH confidence — direct file reads this session)

- `crates/anofox-fcst-ffi/src/lib.rs` — FFI pattern (build_series, catch_unwind, anofox_ts_stats, anofox_ts_mae, metric helper)
- `src/scalar_functions/bootstrap.cpp` — STRUCT-returning scalar pattern (TsBootstrapIntervalsFunction, RegisterTsBootstrapIntervalsFunction)
- `src/scalar_functions/metrics.cpp` — scalar registration pattern (RegisterTsMaeFunction)
- `src/macros/ts_macros.cpp` — TsTableMacro, ts_mae_by, ts_stats_by, ts_inspect_by macros
- `src/anofox_forecast_extension.cpp` — registration call-site, registration block structure
- `src/include/anofox_forecast_extension.hpp` — all existing RegisterTs* declarations
- `crates/anofox-fcst-core/src/lib.rs` — what is/isn't currently re-exported
- `/tmp/.../anofox-forecast-0.15.3/src/validation/mod.rs` — module structure, feature gates, re-exports
- `/tmp/.../anofox-forecast-0.15.3/src/validation/stationarity.rs` — adf_test, kpss_test, test_stationarity, StationarityResult, CriticalValues (full source + tests)
- `/tmp/.../anofox-forecast-0.15.3/src/validation/residual_tests.rs` — ljung_box, durbin_watson, jarque_bera, diagnose_residuals, all result structs (full source + tests)
- `/tmp/.../anofox-forecast-0.15.3/src/validation/diagnostics.rs` — ModelDiagnostics (for context)
- `Cargo.toml` (workspace) — anofox-forecast dependency version and features
- `/tmp/.../anofox-forecast-0.15.3/Cargo.toml` — feature flag definitions (postprocess, default)
- `.planning/config.json` — nyquist_validation=false, security_enforcement=true

### Tertiary (LOW confidence — not verified this session)

- statsmodels API surface for reference cross-check functions — assumed from training knowledge; verify against statsmodels docs before writing cross-check script

---

## Metadata

**Confidence breakdown:**
- Crate API surface: HIGH — read source files directly
- FFI pattern: HIGH — read existing functions in lib.rs
- STRUCT return pattern: HIGH — read bootstrap.cpp in full
- Macro pattern: HIGH — read ts_macros.cpp at relevant sections
- Four-way verdict mapping: ASSUMED (A4) — textbook knowledge, not verified against a primary source
- Numeric cross-check tolerance: ASSUMED (A7) — based on reading the p-value approximation code

**Research date:** 2026-08-21
**Valid until:** Stable until anofox-forecast crate is updated beyond v0.15.3 (the API surface is pinned)
