# Phase 17: Upstream Consumption & Diagnostics Parity - Pattern Map

**Mapped:** 2026-10-12
**Files analyzed:** 14 (new/modified per RESEARCH.md "Recommended Project Structure")
**Analogs found:** 14 / 14 (all are in-place edits of existing files — this phase modifies, not creates)

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|---|---|---|---|---|
| `Cargo.toml` (workspace) | config | batch (dependency resolution) | itself (prior bump: Phase 9's 0.15.3→0.15.10 edit, git history) | exact (self-modification, no analog needed) |
| `crates/anofox-fcst-core/src/validation.rs` — `classify_stationarity` relabel | utility/transform | transform (pure function, bool pair → label) | same file, same function (in-place fix) | exact |
| `crates/anofox-fcst-core/src/validation.rs` — constant-series short-circuit | utility | transform | `crates/anofox-fcst-core/src/filter.rs::is_constant` (lines 39-48) | exact (existing helper to reuse) |
| `crates/anofox-fcst-core/src/validation.rs` — `ljung_box(..., fitted_params)` + df validation | service/wrapper | request-response (CRUD-like single compute) | same file's current `ljung_box` wrapper (lines 178-180) + `adf`/`kpss` wrappers above it for the Option<usize>-param convention | exact |
| `crates/anofox-fcst-core/src/validation.rs` — `adf`/`kpss` `regression` param threading | service/wrapper | request-response | same file's `adf(series, max_lags)`/`kpss(series, lags)` wrappers (immediately above `classify_stationarity`, lines ~80-123 per RESEARCH citation) | exact |
| `crates/anofox-fcst-ffi/src/lib.rs` — `anofox_ts_adf`/`anofox_ts_kpss` regression param | middleware (FFI boundary) | request-response | `anofox_ts_adf` itself (lines 7052-7100) — add one more `c_int`/enum-coded param following `max_lags`'s exact null-check → catch_unwind → `From` conversion shape | exact |
| `crates/anofox-fcst-ffi/src/lib.rs` — `anofox_ts_ljung_box` fitted_params param | middleware (FFI boundary) | request-response | `anofox_ts_ljung_box` itself (lines 7299-7339) — identical catch_unwind/init_error/null-check skeleton, add one `size_t`/`c_int` arg before `out_result` | exact |
| `src/include/anofox_fcst_ffi.h` | config (generated header) | — | itself (cbindgen output); regenerate via `cbindgen` or hand-sync to match `lib.rs` signatures | exact |
| `src/scalar_functions/diagnostics.cpp` — `TsAdfFunction`/`TsKpssFunction` regression arg | controller (scalar function) | request-response | `TsAdfFunction` itself (lines 44-120) — add a 3rd optional `ScalarArgReader<string_t>`-style reader for `regression VARCHAR`, following the existing `max_lags_reader` optional-arg pattern (lines 48-52, 83-86) | exact |
| `src/scalar_functions/diagnostics.cpp` — `TsLjungBoxFunction` fitted_params arg | controller (scalar function) | request-response | `TsLjungBoxFunction` itself (lines 449-685) + `TsAdfFunction`'s optional-arg reader pattern | exact |
| `src/macros/ts_macros.cpp` — `ts_adf_by`/`ts_kpss_by`/`ts_stationarity_by` `regression :=` named param | route (SQL macro) | request-response | `ts_adf_by` entry itself (lines 2418-2430) — `{"max_lags", "-1"}` named-default pattern extends directly to `{"regression", "'c'"}` | exact |
| `src/macros/ts_macros.cpp` — `ts_ljung_box_by` `fitted_params :=` named param | route (SQL macro) | request-response | `ts_ljung_box_by` entry itself (lines 2464-2474) — `{"lags", "-1"}` named-default pattern extends to `{"fitted_params", "0"}` | exact |
| `docs/api/10-diagnostics.md` | config/docs | — | itself (rewrite Caveats section lines 169-212) | exact |
| `validation/reference/r/common.R` — add `trend` series | utility (R fixture gen) | batch | `reference_series()` itself (lines 51-61) — append-only RNG discipline | exact |
| `validation/reference/r/gen_forecast_reference.R` — `trend` family block | utility (R fixture gen) | batch | existing `rw` family block in the same generator (model-applicability scoping: naive/rwf_drift/ses/holt/ets_auto/auto_arima/thetaf, no SNaive/HW/Croston) | exact |
| `crates/anofox-fcst-core/tests/theta_croston_fitted.rs` | test | transform (assertion update) | itself — update first test only, per its own doc comment (lines 20-27) | exact |
| `test/sql/ts_diagnostics.test`, `ts_forecast_ets_model.test`, `ts_native_param_validation.test` | test | request-response (SQL assertions) | themselves — update hardcoded critical-value / "unstable" assertions | exact |
| new FCST-09 parity test(s) | test | batch (statistical comparison) | `crates/anofox-fcst-core/tests/theta_croston_fitted.rs` (fixture-consuming Rust integration test pattern) + `fixtures/r_forecast/*.json` loader convention | role-match |
| Monte Carlo coverage test | test | batch (simulation) | no direct analog in repo; use `validation::predict_with_intervals` + a seeded `rand`/`rand_chacha` RNG loop, per RESEARCH's Architectural Responsibility Map ("pure statistical simulation... no SQL/C++ needed") | no analog (see below) |

## Pattern Assignments

### `crates/anofox-fcst-core/src/validation.rs` (service/utility, transform)

**Analog:** itself — `classify_stationarity` (lines 124-131), `stationarity()` (lines 134-146), `ljung_box` wrapper (lines 178-180)

**Current (buggy) core pattern** (lines 124-131):
```rust
pub fn classify_stationarity(adf_is_stationary: bool, kpss_is_stationary: bool) -> &'static str {
    match (adf_is_stationary, kpss_is_stationary) {
        (true, true) => "stationary",
        (true, false) => "trend_stationary",
        (false, false) => "difference_stationary",
        (false, true) => "non_stationary",
    }
}
```
Fix per D-04 (only 3 of 4 arms change label; `(true,true)` stays `"stationary"`):
```rust
pub fn classify_stationarity(adf_is_stationary: bool, kpss_is_stationary: bool) -> &'static str {
    match (adf_is_stationary, kpss_is_stationary) {
        (true, true) => "stationary",
        (true, false) => "difference_stationary",
        (false, false) => "non_stationary",
        (false, true) => "trend_stationary",
    }
}
```

**Constant-series short-circuit** — reuse `filter::is_constant` exactly (its `f64::EPSILON` tolerance, don't reimplement):
```rust
// Source: crates/anofox-fcst-core/src/filter.rs:39-48 (read this session)
pub fn is_constant(values: &[Option<f64>]) -> bool {
    let non_null: Vec<f64> = values.iter().filter_map(|v| *v).collect();
    if non_null.len() < 2 { return true; }
    let first = non_null[0];
    non_null.iter().all(|v| (v - first).abs() < f64::EPSILON)
}
```
`stationarity(series: &[f64])` must call an equivalent all-equal check on its plain slice (wrap in `Some(..)` to reuse `is_constant`, or inline the identical tolerance) BEFORE calling `adf`/`kpss`, short-circuiting to a `"constant"` verdict.

**Ljung-Box df validation (D-06)** — new, no existing precedent in-crate (the upstream `ljung_box` silently floors df at 0 via `saturating_sub`); add validation in this wrapper, consistent with "statistics live in Rust" project rule:
```rust
// Current (hardcoded fitted_params=0):
pub fn ljung_box(residuals: &[f64], lags: Option<usize>) -> LjungBoxOut {
    validation::ljung_box(residuals, lags, 0).into()
}
// Target shape — thread fitted_params, validate df > 0 before calling the crate:
pub fn ljung_box(residuals: &[f64], lags: Option<usize>, fitted_params: usize) -> Result<LjungBoxOut, String> {
    let resolved_lags = lags.unwrap_or_else(|| (residuals.len() / 5).min(10));
    if resolved_lags <= fitted_params {
        return Err(format!(
            "ts_ljung_box: fitted_params ({}) must be less than lags ({}) — degrees of freedom would be <= 0",
            fitted_params, resolved_lags
        ));
    }
    Ok(validation::ljung_box(residuals, lags, fitted_params).into())
}
```

**`regression` param threading for `adf`/`kpss` wrappers** — follow the existing `Option<usize>` lags/max_lags convention these wrappers already use; thread a `regression: &str` (or small enum parsed from `'c'`/`'ct'`) through to `anofox_forecast::validation::adf_test_with_options` (crate-side, already supports it). KPSS has no crate-side regression param in 0.16.0 (Pitfall 4) — scope per Open Question 1 recommendation (Option a): accept the param syntactically in the KPSS wrapper but either error on `'ct'` with an explicit "not yet implemented" message, or always run Level KPSS and document it — this decision belongs to the plan, not restated here.

---

### `crates/anofox-fcst-ffi/src/lib.rs` (middleware/FFI boundary, request-response)

**Analog:** `anofox_ts_adf` (lines 7052-7100) and `anofox_ts_ljung_box` (lines 7299-7339) — both already read in full this session.

**Exact skeleton to replicate when adding a new scalar param** (shown for `anofox_ts_adf`; the `regression`/`fitted_params` additions follow this identical shape — null-check unaffected since the new param is a plain `c_int`/`size_t`, not a pointer):
```rust
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_adf(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    max_lags: c_int,
    out_result: *mut AnofoxStationarityResult,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);
    let ptrs = &[values as *const core::ffi::c_void, out_result as *const core::ffi::c_void];
    if check_null_pointers(out_error, ptrs) { return false; }
    *out_result = AnofoxStationarityResult::default();
    if length == 0 { return true; }
    let max_lags_opt: Option<usize> = if max_lags < 0 { None } else { Some(max_lags as usize) };
    let result = catch_unwind(AssertUnwindSafe(|| {
        let series = build_values(values, validity, length);
        anofox_fcst_core::adf(&series, max_lags_opt)
    }));
    match result {
        Ok(r) => { *out_result = r.into(); true }
        Err(_) => { set_error(out_error, ErrorCode::PanicCaught, "Panic in anofox_ts_adf"); false }
    }
}
```
For `regression`, since the crate side wants a typed enum (`AdfRegression::Constant`/`ConstantTrend`), the FFI boundary should accept a small `c_int` discriminant (0='c', 1='ct') rather than a raw C string — avoids a new string-marshalling path across FFI; parse the discriminant into the Rust enum inside the `catch_unwind` closure, matching the `max_lags`-style `if < 0 { None }` parse-at-the-boundary convention already used throughout this file.

For `anofox_ts_ljung_box`'s `fitted_params` addition, the wrapped core function now returns `Result<LjungBoxOut, String>` (per D-06 validation above) — the FFI function must convert the `Err(String)` arm to `set_error(out_error, ErrorCode::InvalidInput, &msg); false`, following the exact `Ok/Err` match shape `anofox_ts_series_gate` already uses for its `Result`-returning core call (lines ~7170-7177, read this session: `Ok(Err(e)) => { set_error(out_error, ErrorCode::InvalidInput, &e.to_string()); false }`).

---

### `src/scalar_functions/diagnostics.cpp` (controller/scalar function, request-response)

**Analog:** `TsAdfFunction` (lines 44-120) for the optional-argument reader pattern.

**Imports** (lines 17-24): `#include "anofox_forecast_extension.hpp"`, `"anofox_fcst_ffi.h"`, `"duckdb.hpp"`, `"duckdb/common/exception.hpp"`, `"duckdb/function/scalar_function.hpp"`, `"duckdb/parser/parsed_data/create_scalar_function_info.hpp"`, `"duckdb/common/types/vector.hpp"`, `"ts_list_marshal.hpp"`.

**Optional-arg pattern to replicate for `regression`/`fitted_params`** (lines 47-52, 83-86):
```cpp
bool has_max_lags = (args.ColumnCount() >= 2);
unique_ptr<ScalarArgReader<int32_t>> max_lags_reader;
if (has_max_lags) {
    max_lags_reader = make_uniq<ScalarArgReader<int32_t>>(args.data[1], count);
}
// ... per row:
int32_t max_lags_val = -1;
if (has_max_lags && !max_lags_reader->IsNull(row_idx)) {
    max_lags_val = max_lags_reader->Get(row_idx);
}
```
A new `regression VARCHAR` 3rd argument follows this exact shape with `ScalarArgReader<string_t>` (or map to int discriminant in C++ before calling FFI, matching the FFI recommendation above). New overloads must be added to `ScalarFunctionSet adf_set`/`kpss_set` in `RegisterTsAdfFunction`/`RegisterTsKpssFunction` (and the `anofox_fcst_ts_adf` alias set) mirroring the existing 1-arg/2-arg dual registration (lines 134-150).

**Error handling pattern**: `if (!ok) { FlatVector::SetNull(result, row_idx, true); continue; }` (line 97) — for the new `InvalidInput` (df<=0, bad regression string) errors, the convention elsewhere in this codebase (per CLAUDE.md's Error Handling section) is `throw DuckDB::Exception` rather than silently nulling; check how other validating scalar functions in this file surface `ErrorCode::InvalidInput` (grep `InvalidInputException` in this file) before choosing null-vs-throw for the new validation paths — flag this as a plan-time decision, not fully resolved by the existing TsAdfFunction excerpt (which only has out-of-range/panic errors, not user-input-validation errors).

---

### `src/macros/ts_macros.cpp` (route/SQL macro, request-response)

**Analog:** `ts_adf_by` (lines 2415-2430), `ts_ljung_box_by` (lines 2463-2474) — both read in full.

**Named-default parameter pattern to extend**:
```cpp
{"ts_adf_by", {"source", "group_col", "date_col", "value_col", nullptr},
 {{"max_lags", "-1"}, {nullptr, nullptr}},
R"(
SELECT group_col, ts_adf(LIST(value_col::DOUBLE ORDER BY date_col), max_lags::INTEGER) AS adf
FROM query_table(source::VARCHAR)
GROUP BY group_col
)",
    "ADF stationarity test per group. ...",
    "SELECT group_col, (adf).statistic, (adf).p_value FROM ts_adf_by('sales', product_id, ds, y)",
    "diagnostics"},
```
Add `{"regression", "'c'"}` to the named-defaults list and `, regression::VARCHAR` to the macro body's positional call — exactly the shape RESEARCH.md's own Code Examples section already drafted. Same shape for `ts_kpss_by`/`ts_stationarity_by` (if the KPSS/stationarity macros also expose `regression`) and `{"fitted_params", "0"}` on `ts_ljung_box_by`.

---

### `validation/reference/r/common.R` (R fixture gen, batch)

**Analog:** `reference_series()` itself (lines 51-61), already fully read:
```r
reference_series <- function() {
  RNGkind("Mersenne-Twister", "Inversion", "Rejection")
  set.seed(20261008)
  air <- AirPassengers
  rw <- cumsum(rnorm(200))
  ar1 <- as.numeric(arima.sim(list(ar = 0.7), n = 300))
  inter <- ifelse(runif(120) < 0.3, rpois(120, 3) + 1, 0)
  list(air = air, rw = rw, ar1 = ar1, inter = inter)
}
```
**Pattern:** append a new `trend <- cumsum(rnorm(150, mean = 0.3, sd = 1))` draw AFTER `inter`'s `runif(120)` call, and add `trend = trend` as the LAST entry of the returned list — never insert earlier in the function body, or every existing fixture downstream of that RNG draw silently changes on regeneration (byte-identical-regeneration discipline, stated explicitly in this file's own header comment, lines 1-9).

---

### `validation/reference/r/gen_diagnostics_reference.R` (R fixture gen, batch) — for reference only, not modified per D-06's note that fixtures already cover fitdf0/fitdf2

**Analog/context:** `diag_tests()` (lines 48-58) — shows the existing `urca`/`tseries` call shapes (`ur.df(y, type="drift"/"trend", lags=..., selectlags="AIC")`, `tseries::kpss.test(y, null="Level", lshort=TRUE)`) that any new KPSS-trend fixture generation (if the KPSS 'ct' scope is expanded) would extend with `tseries::kpss.test(y, null="Trend")`, matching `urdf_block`'s extractor pattern (lines 29-37) for packaging teststat/cval into fixture JSON.

---

### Test files (test, request-response / batch)

**Analogs:** `test/sql/ts_diagnostics.test` (hardcoded cv assertions, lines 70-87), `test/sql/ts_forecast_ets_model.test`/`ts_native_param_validation.test` ("unstable" MAA/MAdA rejection assertions, lines ~82-92 / ~147-155), `crates/anofox-fcst-core/tests/theta_croston_fitted.rs` (doc-commented "expected to break" pattern, lines 20-27).

**Pattern:** these are in-place numeric/assertion updates, not new test file structures — follow each file's own existing SQL/Rust assertion idiom; do not introduce a new test framework or helper.

---

### Monte Carlo coverage test (FCST-09, D-10) — No Analog

No existing test in the repo runs a seeded simulation loop computing empirical interval coverage. Build it directly against `anofox_fcst_core`'s existing Rust API (`validation::predict_with_intervals` / the ETS/SES forecast wrapper used elsewhere in `crates/anofox-fcst-core/src/forecast.rs`), seeded via `rand`/the crate's existing RNG usage pattern in `arima.rs`/wherever bootstrap sampling already seeds a PRNG (search `rand::rngs` usage in `crates/anofox-fcst-core/src/` for the project's existing seeding convention before inventing a new one).

## Shared Patterns

### FFI null-pointer-check + catch_unwind wrapper
**Source:** every `anofox_ts_*` function in `crates/anofox-fcst-ffi/src/lib.rs` (e.g. `anofox_ts_adf` lines 7052-7100, `anofox_ts_kpss` lines 7194-7232, `anofox_ts_ljung_box` lines 7299-7339)
**Apply to:** every modified FFI function in this phase — preserve `init_error` → `check_null_pointers` → `*out_result = Default` → `length==0` early-return → `catch_unwind(AssertUnwindSafe(...))` → `match result { Ok/Err }` shape exactly; do not add new panic surface outside the `catch_unwind` closure.

### Optional-argument `ScalarArgReader` pattern
**Source:** `TsAdfFunction` in `src/scalar_functions/diagnostics.cpp` lines 47-52, 83-86
**Apply to:** `regression`/`fitted_params` new scalar arguments across `TsAdfFunction`, `TsKpssFunction`, `TsLjungBoxFunction`.

### Named-default macro parameter
**Source:** `ts_adf_by`/`ts_ljung_box_by` entries in `src/macros/ts_macros.cpp` (`{"max_lags", "-1"}`, `{"lags", "-1"}`)
**Apply to:** `{"regression", "'c'"}` and `{"fitted_params", "0"}` additions.

### Byte-identical fixture regeneration discipline (append-only RNG draws)
**Source:** `validation/reference/r/common.R` header comment (lines 1-9) and `reference_series()` itself
**Apply to:** the new `trend` series addition (D-08) and any new R diagnostics fixture extension.

### "Expected to break, update don't delete" test-evolution discipline
**Source:** `crates/anofox-fcst-core/tests/theta_croston_fitted.rs` doc comment (lines 20-27)
**Apply to:** the three SQL test files flagged in RESEARCH.md's Common Pitfalls (`ts_diagnostics.test`, `ts_forecast_ets_model.test`, `ts_native_param_validation.test`) — update assertions to the new correct behavior, document the before/after in CHANGELOG.md per D-11, never silently delete the failing assertion.

## No Analog Found

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| Monte Carlo coverage test (new file/test fn, location TBD by plan) | test | batch (simulation) | No existing seeded-simulation-loop test in the repo; build directly from `anofox_fcst_core`'s forecast/interval API per RESEARCH's Architectural Responsibility Map, following the project's existing (not-yet-located) RNG-seeding convention — plan should grep `rand::` usage in `crates/anofox-fcst-core/src/` before finalizing the seed/loop shape |
| `src/include/anofox_fcst_ffi.h` | config (generated) | — | Not hand-written; regenerate via `cbindgen` per `crates/anofox-fcst-ffi/cbindgen.toml`, or hand-sync — no "pattern" to copy beyond keeping it in lockstep with `lib.rs` |

## Metadata

**Analog search scope:** `crates/anofox-fcst-core/src/validation.rs`, `crates/anofox-fcst-core/src/filter.rs`, `crates/anofox-fcst-ffi/src/lib.rs` (lines 7040-7345), `src/scalar_functions/diagnostics.cpp` (lines 1-220, headers), `src/macros/ts_macros.cpp` (lines 2395-2480), `validation/reference/r/common.R`, `validation/reference/r/gen_diagnostics_reference.R`
**Files scanned:** 8 read directly this session (all excerpts above verified by direct Read/Bash cat, not recalled)
**Pattern extraction date:** 2026-10-12

## PATTERN MAPPING COMPLETE
