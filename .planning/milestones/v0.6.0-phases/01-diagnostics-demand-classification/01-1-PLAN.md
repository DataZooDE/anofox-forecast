---
phase: 01-diagnostics-demand-classification
plan: 1
type: execute
wave: 1
depends_on: []
files_modified:
  - crates/anofox-fcst-core/src/lib.rs
  - crates/anofox-fcst-core/src/validation.rs
  - crates/anofox-fcst-ffi/src/lib.rs
  - crates/anofox-fcst-ffi/src/types.rs
  - src/scalar_functions/diagnostics.cpp
  - src/include/anofox_forecast_extension.hpp
  - src/anofox_forecast_extension.cpp
  - src/macros/ts_macros.cpp
  - Makefile
  - examples/diagnostics/stationarity.sql
  - docs/api/10-diagnostics.md
  - benchmark/diagnostics/reference_values.py
  - benchmark/diagnostics/run_anofox.py
  - benchmark/diagnostics/README.md
  - test/sql/ts_diagnostics.test
autonomous: true
requirements: [STAT-01]
estimate:
  tokens: 90000
  raw_tokens: 90000
  tasks: 3
  confidence: low
must_haves:
  truths:
    - "A user can call ts_adf(LIST(y ORDER BY ds)) and ts_adf_by('tbl', grp, ds, y) and receive a STRUCT with statistic, p_value, and used lag per series (STAT-01)"
    - "The ts_adf result numerically cross-checks against statsmodels adfuller within documented tolerance (statistic rtol 0.01, p_value rtol 0.10)"
    - "examples/diagnostics/stationarity.sql runs end-to-end against the built extension and prints ADF results"
    - "docs/api/10-diagnostics.md documents ts_adf / ts_adf_by including the constant-only regression caveat"
  artifacts:
    - crates/anofox-fcst-core/src/validation.rs
    - src/scalar_functions/diagnostics.cpp
    - examples/diagnostics/stationarity.sql
    - docs/api/10-diagnostics.md
    - benchmark/diagnostics/run_anofox.py
    - test/sql/ts_diagnostics.test
  key_links:
    - "cbindgen build.rs regenerates src/include/anofox_fcst_ffi.h from the new FFI types in crates/anofox-fcst-ffi (never hand-edited)"
    - "src/scalar_functions/diagnostics.cpp is added to the CMake source globbing / build and RegisterTsAdfFunction is called in LoadInternal"
    - "ts_adf_by macro composes LIST(value ORDER BY date) GROUP BY group_col and calls the ts_adf scalar"
---

<objective>
Deliver ADF stationarity testing (STAT-01) end-to-end as the tracer slice for Phase 1: one diagnostic (`ts_adf` / `ts_adf_by`) wired through ALL FIVE layers of the exposure stack — Rust core re-export → FFI export → C++ STRUCT scalar → extension registration → `ts_*_by` SQL macro — plus a runnable example, a docs page, and a statsmodels numeric cross-check harness. This proves the complete architecture on the agent's best early-context tokens before the remaining six functions expand out from it.

Purpose: Establish and validate the exact 5-layer recipe (new core `validation` module, new FFI result struct + cbindgen regeneration, new `diagnostics.cpp` scalar file added to the build, new registration block, new macro category, new examples/docs/benchmark directories) so plans 01-2 and 01-3 only add functions, not new infrastructure.
Output: A working `ts_adf` / `ts_adf_by` verified against statsmodels, the scaffolding all remaining diagnostics reuse, and the Definition of Done satisfied for STAT-01.
</objective>

<execution_context>
@$HOME/.claude/gsd-core/workflows/execute-plan.md
@$HOME/.claude/gsd-core/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/ROADMAP.md
@.planning/STATE.md
@.planning/phases/01-diagnostics-demand-classification/01-CONTEXT.md
@.planning/phases/01-diagnostics-demand-classification/01-RESEARCH.md
@.planning/codebase/CONVENTIONS.md
@.planning/codebase/TESTING.md
@./.claude/CLAUDE.md
</context>

<artifacts_this_phase_produces>
This tracer plan introduces the following NEW symbols and files (plans 01-2 / 01-3 extend the same files, adding more symbols):

- Rust core: `crates/anofox-fcst-core/src/validation.rs` (new module) wrapping `anofox_forecast::validation`; `pub mod validation;` + `pub use validation::*;` in `crates/anofox-fcst-core/src/lib.rs`. This plan adds the ADF wrapper `adf(series: &[f64], max_lags: Option<usize>) -> StationarityOut`.
- FFI: `AnofoxStationarityResult` struct in `crates/anofox-fcst-ffi/src/types.rs`; `anofox_ts_adf(...)` export in `crates/anofox-fcst-ffi/src/lib.rs`. cbindgen regenerates `AnofoxStationarityResult` + `anofox_ts_adf` into `src/include/anofox_fcst_ffi.h`.
- C++ scalar: `src/scalar_functions/diagnostics.cpp` (new file) with `TsAdfFunction` + `RegisterTsAdfFunction`; declaration `RegisterTsAdfFunction(ExtensionLoader&)` in `src/include/anofox_forecast_extension.hpp`; call in `LoadInternal` in `src/anofox_forecast_extension.cpp`.
- SQL: `ts_adf` scalar (registered above) + `ts_adf_by` macro in `src/macros/ts_macros.cpp` under a new `"diagnostics"` category.
- Assets: `examples/diagnostics/stationarity.sql`, `docs/api/10-diagnostics.md`, `benchmark/diagnostics/{reference_values.py,run_anofox.py,README.md}`, `test/sql/ts_diagnostics.test`.
</artifacts_this_phase_produces>

<tasks>

<task type="tracer" tdd="true">
  <name>Task 1: Wire ts_adf through all five layers (core → FFI → C++ scalar → registration → macro)</name>
  <files>crates/anofox-fcst-core/src/validation.rs, crates/anofox-fcst-core/src/lib.rs, crates/anofox-fcst-ffi/src/types.rs, crates/anofox-fcst-ffi/src/lib.rs, src/scalar_functions/diagnostics.cpp, src/include/anofox_forecast_extension.hpp, src/anofox_forecast_extension.cpp, src/macros/ts_macros.cpp, Makefile</files>
  <read_first>
    - crates/anofox-fcst-core/src/bootstrap.rs (lines 1-50) — precedent for a core module that wraps an `anofox_forecast::` submodule and re-exports flat result structs
    - crates/anofox-fcst-core/src/lib.rs (lines 1-24) — where to add `pub mod validation;` and `pub use validation::*;`
    - crates/anofox-fcst-ffi/src/lib.rs (lines 60-180) — build_values (line 91), copy_string_to_buffer (line 121), check_null_pointers/init_error/set_error usage, and anofox_ts_stats as the structural analog (single series in, flat struct out, catch_unwind)
    - crates/anofox-fcst-ffi/src/types.rs (lines 155-236) — `#[repr(C)]` struct + `Default` + `From` impl pattern
    - crates/anofox-fcst-ffi/build.rs (lines 8-31) — confirms cbindgen writes src/include/anofox_fcst_ffi.h at build time
    - src/scalar_functions/bootstrap.cpp (lines 1-182) — ExtractListAsDouble helper (line 13), STRUCT return via child_list_t + LogicalType::STRUCT + StructVector::GetEntries, and the dual registration (ts_* + anofox_fcst_ts_* alias) with FunctionDescription
    - src/anofox_forecast_extension.cpp (lines 119-149) — registration block placement
    - src/include/anofox_forecast_extension.hpp (lines 113-114) — RegisterTsBootstrap* declaration style
    - src/macros/ts_macros.cpp (lines 13-27 struct TsTableMacro; lines 2015-2023 ts_mae_by) — macro table entry shape, named_params slot, category string
  </read_first>
  <behavior>
    Tests written first (RED), then implementation until GREEN:
    - Rust core unit test in validation.rs: `adf` on a deterministic random-walk-like series returns a finite statistic and `used_lag >= 0`; on a strongly mean-reverting series returns a more-negative statistic than on the random walk.
    - Rust core unit test: series of length < 4 yields NaN statistic (crate contract) without panicking.
    - FFI parity test (crates/anofox-fcst-ffi/tests/ or inline): `anofox_ts_adf` on the same series produces the same statistic as the core `adf` call within 1e-9.
    - SQL smoke assertion (deferred to Task 3's test file, but the scalar must support it): `ts_adf(LIST(...))` returns a non-null STRUCT with a DOUBLE `statistic`, DOUBLE `p_value`, BIGINT `lags`.
  </behavior>
  <action>
    Implement ADF exposure through every layer, ADF-only (no KPSS/residual functions — those are plans 01-2/01-3).

    Layer 0 (core): Create crates/anofox-fcst-core/src/validation.rs as a thin wrapper module over `anofox_forecast::validation` (mirror how bootstrap.rs wraps `anofox_forecast::postprocess`). Define a flat, owned result type (e.g. `StationarityOut { statistic, p_value, lags, is_stationary, cv_1pct, cv_5pct, cv_10pct }`) and `pub fn adf(series: &[f64], max_lags: Option<usize>) -> StationarityOut` that calls `anofox_forecast::validation::adf_test(series, max_lags)` and copies fields out of the crate's `StationarityResult`/`CriticalValues`. Add `pub mod validation;` and `pub use validation::*;` to crates/anofox-fcst-core/src/lib.rs. Add a `#[cfg(test)] mod tests` per CONVENTIONS.md using `approx::assert_relative_eq!` for the behavior tests above.

    Layer 1 (FFI): In crates/anofox-fcst-ffi/src/types.rs add `#[repr(C)] pub struct AnofoxStationarityResult { statistic: c_double, p_value: c_double, lags: size_t, is_stationary: bool, cv_1pct: c_double, cv_5pct: c_double, cv_10pct: c_double }` with a `Default` impl (NaN doubles, lags 0, is_stationary false) and a `From<anofox_fcst_core::StationarityOut>` impl. In crates/anofox-fcst-ffi/src/lib.rs add `#[no_mangle] pub unsafe extern "C" fn anofox_ts_adf(values, validity, length, max_lags: c_int, out_result: *mut AnofoxStationarityResult, out_error: *mut AnofoxError) -> bool` following the anofox_ts_stats shape: init_error, check_null_pointers on {values, out_result}, early-return true with Default result when length == 0, then catch_unwind(AssertUnwindSafe) wrapping `let series = build_values(values, validity, length); let ml = if max_lags < 0 { None } else { Some(max_lags as usize) }; anofox_fcst_core::adf(&series, ml)`. Use build_values (NaN-for-NULL), NOT build_series. On Ok write `*out_result = r.into(); true`; on panic set_error(PanicCaught) and return false. Do NOT hand-edit src/include/anofox_fcst_ffi.h — cbindgen regenerates it (verify in Task 2).

    Layer 2 (C++ scalar): Create src/scalar_functions/diagnostics.cpp. Include the same headers as bootstrap.cpp. Copy the ExtractListAsDouble helper (or a local equivalent). Implement `static void TsAdfFunction(DataChunk&, ExpressionState&, Vector& result)`: read args.data[0] (LIST(DOUBLE)) and, if present, args.data[1] (max_lags INTEGER, default handled by macro so accept 1- or 2-arg overloads); get StructVector::GetEntries(result) for statistic/p_value/lags (and optionally is_stationary/cv_* — include all seven STRUCT fields to match the docs); per row: null-guard the list, ExtractListAsDouble, call anofox_ts_adf(values.data(), nullptr, values.size(), max_lags_or_-1, &r, &err); on failure SetNull(result, row, true); else write DOUBLE/BIGINT/BOOLEAN fields. Implement `void RegisterTsAdfFunction(ExtensionLoader& loader)` building the STRUCT return type via child_list_t<LogicalType> (statistic DOUBLE, p_value DOUBLE, lags BIGINT, is_stationary BOOLEAN, cv_1pct DOUBLE, cv_5pct DOUBLE, cv_10pct DOUBLE) and registering a ScalarFunctionSet "ts_adf" with two overloads — {LIST(DOUBLE)} and {LIST(DOUBLE), INTEGER} — plus the `anofox_fcst_ts_adf` alias (mirror bootstrap.cpp's dual registration + FunctionDescription with category "diagnostics"). Ensure diagnostics.cpp is compiled: if the Makefile/CMake source list globs src/scalar_functions/*.cpp it is automatic; otherwise add diagnostics.cpp to the extension source list (check CMakeLists.txt / the extension config referenced by the Makefile) — this is the "add to build" key link.

    Layer 3 (registration): Add `void RegisterTsAdfFunction(ExtensionLoader &loader);` to src/include/anofox_forecast_extension.hpp near the bootstrap declarations. In src/anofox_forecast_extension.cpp LoadInternal, after the metrics/bootstrap registration blocks, add a `// Register Diagnostic functions (STAT-01..03, RESID-01..04)` comment and call `RegisterTsAdfFunction(loader);`.

    Layer 4 (macro): In src/macros/ts_macros.cpp add a `ts_adf_by` entry to the ts_table_macros array following the ts_mae_by shape, with params {"source","group_col","date_col","value_col", nullptr}, a named_params entry for max_lags defaulting to -1, body `SELECT group_col, ts_adf(LIST(value_col::DOUBLE ORDER BY date_col), max_lags) AS adf FROM query_table(source::VARCHAR) GROUP BY group_col`, a description, an example, and category "diagnostics".
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast && cargo test -p anofox-fcst-core validation:: -- --nocapture && cargo test -p anofox-fcst-ffi</automated>
  </verify>
  <done>Core `adf` and FFI `anofox_ts_adf` exist, unit + parity tests pass, and all five layers (core module, FFI export, diagnostics.cpp scalar, registration call, ts_adf_by macro) are in place. diagnostics.cpp is part of the extension build source list.</done>
  <reversibility rating="reversible">New files and additive edits; no existing behavior changed. STRUCT field set can be revised before 01-2/01-3 build on it.</reversibility>
</task>

<task type="auto">
  <name>Task 2: Build the extension and prove ts_adf / ts_adf_by run end-to-end in SQL</name>
  <files>test/sql/ts_diagnostics.test</files>
  <read_first>
    - test/sql/ts_diff.test — DuckDB .test format: LOAD, CREATE TABLE, `query`/`statement ok`, `----` expected-result blocks, and the `SELECT ABS(x - expected) < tol` numeric-assertion idiom (TESTING.md lines 177-193)
    - crates/anofox-fcst-ffi/build.rs — cbindgen writes src/include/anofox_fcst_ffi.h during `make rust`
  </read_first>
  <precondition>The extension build toolchain (make + duckdb extension-ci-tools) is available and previously produced ./build/release or ./build/debug — CI is green per recent commits, so this holds.</precondition>
  <action>
    Build the FFI crate and the extension so the new symbols are live. Run `make rust` (regenerates src/include/anofox_fcst_ffi.h via cbindgen; confirm `anofox_ts_adf` and `AnofoxStationarityResult` now appear in that header — do NOT hand-edit the header, only confirm cbindgen produced them). Then build the extension (`make debug` or `make`). If diagnostics.cpp was not picked up (link/symbol error for RegisterTsAdfFunction), fix the extension source list (CMakeLists.txt or the config the Makefile includes) and rebuild.

    Create test/sql/ts_diagnostics.test with a small deterministic multi-series table (two groups, ~40 points each) and assert: (a) `ts_adf(LIST(y ORDER BY ds))` returns a non-null STRUCT; (b) the STRUCT exposes statistic/p_value/lags with correct types; (c) `ts_adf_by('tbl', grp, ds, y)` returns one row per group with a non-null `adf` STRUCT; (d) a numeric sanity assertion (e.g. `(adf).lags >= 0` and `(adf).p_value BETWEEN 0 AND 1`). Keep values deterministic so the test is reproducible. Run the SQL test through the built extension.
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast && make rust && grep -q "anofox_ts_adf" src/include/anofox_fcst_ffi.h && make debug && (make test_debug ARGS="test/sql/ts_diagnostics.test" 2>/dev/null || ./build/debug/test/unittest test/sql/ts_diagnostics.test)</automated>
  </verify>
  <done>cbindgen-regenerated header contains anofox_ts_adf; the extension builds and loads; ts_diagnostics.test passes with ts_adf and ts_adf_by returning correct STRUCTs per series.</done>
</task>

<task type="auto">
  <name>Task 3: Example, docs page, and statsmodels cross-check harness for ts_adf (Definition of Done)</name>
  <files>examples/diagnostics/stationarity.sql, docs/api/10-diagnostics.md, benchmark/diagnostics/reference_values.py, benchmark/diagnostics/run_anofox.py, benchmark/diagnostics/README.md</files>
  <read_first>
    - examples/metrics/synthetic_metrics_examples.sql (lines 1-30) — example header/run-comment/LOAD convention and `.print` section style
    - docs/api/09-evaluation-metrics.md — docs page structure (function signature, parameters, return STRUCT, example, notes) to mirror for the new 10-diagnostics.md
    - benchmark/m4/baseline_benchmark/run.py (lines 1-31) — benchmark script conventions (shared runner, parquet fixtures, comparison harness)
  </read_first>
  <action>
    Satisfy the Definition of Done for STAT-01 (runnable example + docs entry + numeric reference cross-check).

    examples/diagnostics/stationarity.sql: new file under a new examples/diagnostics/ dir. Header comment with the run command (`./build/release/duckdb < examples/diagnostics/stationarity.sql`), LOAD anofox_forecast, create a small synthetic multi-series table, and demonstrate both `ts_adf(LIST(y ORDER BY ds))` and `ts_adf_by('tbl', grp, ds, y)`, projecting `(adf).statistic`, `(adf).p_value`, `(adf).lags`. Only ADF here; plans 01-2/01-3 will append KPSS/stationarity/residual sections to this same file. Verify it runs end-to-end against the built extension.

    docs/api/10-diagnostics.md: new docs page (slot after 09-evaluation-metrics.md). Document `ts_adf` and `ts_adf_by`: signature, the LIST(DOUBLE) + optional max_lags INTEGER (default auto/AIC) parameters, the returned STRUCT fields, and TWO explicit caveats required by RESEARCH.md open questions: (1) the crate uses a constant-only ('c') ADF regression in v0.15.3 — the 'ct'/'n' regression modes from CONTEXT are NOT yet functional; state that clearly (do not silently imply they work); (2) p-values are approximate (MacKinnon lookup table, not full regression) — same caveat statsmodels itself carries. Leave clearly-marked section stubs for KPSS / combined stationarity / residual diagnostics so 01-2 and 01-3 fill them in.

    benchmark/diagnostics/: new dir. reference_values.py generates statsmodels reference values via `statsmodels.tsa.stattools.adfuller(series, regression='c', autolag='AIC')` for a small deterministic fixture series and writes them (JSON or parquet). run_anofox.py runs the same series through the built extension's `ts_adf` and asserts numeric parity: statistic within rtol=0.01, p_value within rtol=0.10 (document these tolerances and WHY in README.md — approximate p-value tables). README.md explains how to run the cross-check and lists which statsmodels functions each diagnostic maps to (adfuller now; kpss/acorr_ljungbox/durbin_watson/jarque_bera reserved for 01-2/01-3). If statsmodels is unavailable in the environment, the script must fail loudly with an install hint, not silently pass.
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast && ./build/debug/duckdb < examples/diagnostics/stationarity.sql && test -f docs/api/10-diagnostics.md && python3 benchmark/diagnostics/reference_values.py && python3 benchmark/diagnostics/run_anofox.py</automated>
  </verify>
  <done>stationarity.sql runs clean against the built extension; docs/api/10-diagnostics.md documents ts_adf with both required caveats; the cross-check confirms ts_adf matches statsmodels adfuller within documented tolerances.</done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| SQL query → C++ scalar | User-supplied LIST(DOUBLE) and max_lags cross into the extension |
| C++ scalar → Rust FFI | Raw pointers + length cross the FFI boundary |

## STRIDE Threat Register

| Threat ID | Category | Component | Severity | Disposition | Mitigation Plan |
|-----------|----------|-----------|----------|-------------|-----------------|
| T-01-01 | Tampering | anofox_ts_adf FFI entry | high | mitigate | check_null_pointers on {values, out_result} + init_error at entry (established pattern, anofox_ts_stats:148-154) |
| T-01-02 | Denial of Service | Rust ADF computation | high | mitigate | catch_unwind(AssertUnwindSafe) wraps the crate call; panic → set_error + return false |
| T-01-03 | Denial of Service | Empty/short series | medium | mitigate | `length == 0` early-returns Default result; crate returns NaN (not panic) for n<4 |
| T-01-04 | Tampering | max_lags c_int cast | medium | mitigate | `max_lags < 0 → None else Some(max_lags as usize)` clamp at FFI boundary |
| T-01-SC | Tampering | python statsmodels/scipy install for benchmark | low | accept | statsmodels/scipy are established, widely-used scientific packages; benchmark-only (not shipped in the extension); no new runtime dependency added to the extension itself |
</threat_model>

<verification>
- `cargo test -p anofox-fcst-core validation::` and `cargo test -p anofox-fcst-ffi` pass (core + FFI parity)
- `make rust` regenerates src/include/anofox_fcst_ffi.h containing `anofox_ts_adf` (cbindgen, not hand-edited)
- Extension builds and loads; test/sql/ts_diagnostics.test passes
- examples/diagnostics/stationarity.sql runs end-to-end against the built extension
- benchmark cross-check confirms parity with statsmodels adfuller within documented tolerances
- docs/api/10-diagnostics.md documents ts_adf with the constant-only-regression and approximate-p-value caveats
</verification>

<success_criteria>
STAT-01 is Complete per the Definition of Done: ts_adf / ts_adf_by return statistic + p_value + lag per series, verified in SQL, documented in docs/api/, and numerically cross-checked against statsmodels. The full 5-layer scaffolding (core validation module, FFI struct, diagnostics.cpp in the build, diagnostics registration block, diagnostics macro category, examples/diagnostics + docs/api/10-diagnostics + benchmark/diagnostics dirs) exists for 01-2 and 01-3 to extend.
</success_criteria>

<output>
Create `.planning/phases/01-diagnostics-demand-classification/01-1-SUMMARY.md` when done. The SUMMARY MUST record: the exact STRUCT field order chosen for ts_adf, the final name of the core wrapper type/fn, whether diagnostics.cpp was auto-globbed or explicitly added to the build, and the benchmark tolerances used — 01-2 and 01-3 depend on these.
</output>
