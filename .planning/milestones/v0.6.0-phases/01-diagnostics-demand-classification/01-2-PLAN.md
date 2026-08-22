---
phase: 01-diagnostics-demand-classification
plan: 2
type: execute
wave: 2
depends_on: ["01-1"]
files_modified:
  - crates/anofox-fcst-core/src/validation.rs
  - crates/anofox-fcst-core/src/lib.rs
  - crates/anofox-fcst-ffi/src/types.rs
  - crates/anofox-fcst-ffi/src/lib.rs
  - src/scalar_functions/diagnostics.cpp
  - src/include/anofox_forecast_extension.hpp
  - src/anofox_forecast_extension.cpp
  - src/macros/ts_macros.cpp
  - examples/diagnostics/stationarity.sql
  - docs/api/10-diagnostics.md
  - benchmark/diagnostics/reference_values.py
  - benchmark/diagnostics/run_anofox.py
  - benchmark/diagnostics/README.md
  - test/sql/ts_diagnostics.test
autonomous: true
requirements: [STAT-02, STAT-03]
estimate:
  tokens: 85000
  raw_tokens: 85000
  tasks: 3
  confidence: low
must_haves:
  truths:
    - "A user can call ts_kpss(LIST(y ORDER BY ds)) and ts_kpss_by('tbl', grp, ds, y) and receive a STRUCT with statistic, p_value, lags, is_stationary per series (STAT-02)"
    - "A user can call ts_stationarity(LIST(y ORDER BY ds)) and ts_stationarity_by('tbl', grp, ds, y) and receive a STRUCT carrying ADF fields, KPSS fields, and a four-way verdict VARCHAR (STAT-03)"
    - "The four-way verdict is derived from the (adf.is_stationary, kpss.is_stationary) boolean pair via the exact truth table in this plan and documented in docs/api/10-diagnostics.md"
    - "ts_kpss numerically cross-checks against statsmodels kpss within documented tolerance (statistic rtol 0.05, p_value rtol 0.10)"
    - "examples/diagnostics/stationarity.sql runs end-to-end and prints KPSS and combined-stationarity results alongside the existing ADF section"
  artifacts:
    - crates/anofox-fcst-core/src/validation.rs
    - src/scalar_functions/diagnostics.cpp
    - examples/diagnostics/stationarity.sql
    - docs/api/10-diagnostics.md
    - benchmark/diagnostics/run_anofox.py
    - test/sql/ts_diagnostics.test
  key_links:
    - "cbindgen build.rs regenerates src/include/anofox_fcst_ffi.h with the new AnofoxCombinedStationarityResult struct and anofox_ts_kpss / anofox_ts_stationarity exports (never hand-edited)"
    - "RegisterTsKpssFunction and RegisterTsStationarityFunction are declared in anofox_forecast_extension.hpp and called in LoadInternal after RegisterTsAdfFunction"
    - "ts_kpss_by and ts_stationarity_by macros compose LIST(value ORDER BY date) GROUP BY group_col under the existing diagnostics category"
---

<objective>
Complete the stationarity family started by the 01-1 tracer: add KPSS testing (`ts_kpss` / `ts_kpss_by`, STAT-02) and the combined ADF+KPSS four-way verdict (`ts_stationarity` / `ts_stationarity_by`, STAT-03). Both functions ADD to the existing 5-layer scaffolding created in 01-1 (core `validation` module, FFI struct pattern in `types.rs`, `diagnostics.cpp` scalar file, the diagnostics registration block, the `diagnostics` macro category, and the `examples/diagnostics/` + `docs/api/10-diagnostics.md` + `benchmark/diagnostics/` assets). No new infrastructure is introduced.

Purpose: Deliver the full stationarity verdict surface so SQL users can classify a series as stationary / trend-stationary / difference-stationary / non-stationary in one call, cross-checked against statsmodels.
Output: Working `ts_kpss` / `ts_kpss_by` and `ts_stationarity` / `ts_stationarity_by` verified in SQL, documented, and numerically cross-checked, satisfying the Definition of Done for STAT-02 and STAT-03.
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
@.planning/phases/01-diagnostics-demand-classification/01-1-PLAN.md
@.planning/phases/01-diagnostics-demand-classification/01-1-SUMMARY.md
@.planning/codebase/CONVENTIONS.md
@./.claude/CLAUDE.md
</context>

<coordination_note>
Plans 01-2 and 01-3 run in the same wave and BOTH edit these shared files: `crates/anofox-fcst-core/src/validation.rs`, `crates/anofox-fcst-core/src/lib.rs`, `crates/anofox-fcst-ffi/src/types.rs`, `crates/anofox-fcst-ffi/src/lib.rs`, `src/scalar_functions/diagnostics.cpp`, `src/include/anofox_forecast_extension.hpp`, `src/anofox_forecast_extension.cpp`, `src/macros/ts_macros.cpp`, `examples/diagnostics/stationarity.sql`, `docs/api/10-diagnostics.md`, `benchmark/diagnostics/*`, and `test/sql/ts_diagnostics.test`. Apply ONLY additive edits — append new functions/structs/registration calls/macro entries next to the existing ADF ones from 01-1. Do NOT rewrite or reorder existing content. If a merge collision is detected (another plan touched the same anchor), re-read the file and re-apply your addition below the current tail of the relevant block. This plan adds the KPSS + combined-stationarity symbols; 01-3 adds the residual-diagnostics symbols — they do not name-collide.
</coordination_note>

<artifacts_this_phase_produces>
This plan ADDS the following NEW symbols to the 01-1 scaffolding (does not create new files, except none — all target files already exist from 01-1):

- Rust core (`crates/anofox-fcst-core/src/validation.rs`): `pub fn kpss(series: &[f64], lags: Option<usize>) -> StationarityOut` (reusing the `StationarityOut` type introduced by 01-1); `pub fn stationarity(series: &[f64]) -> CombinedStationarityOut` where `CombinedStationarityOut` is a new flat struct carrying both ADF and KPSS fields plus a `verdict: String` (or `&'static str`) computed from the four-way truth table below. Re-exported via the existing `pub use validation::*;` in `lib.rs`.
- FFI (`crates/anofox-fcst-ffi/src/types.rs`): reuse `AnofoxStationarityResult` for KPSS; add `#[repr(C)] pub struct AnofoxCombinedStationarityResult` (ADF fields, KPSS fields, `char verdict[32]`) with `Default` + `From<...>` impls. Exports `anofox_ts_kpss` and `anofox_ts_stationarity` in `lib.rs`. cbindgen regenerates both into `src/include/anofox_fcst_ffi.h`.
- C++ scalar (`src/scalar_functions/diagnostics.cpp`): `TsKpssFunction` + `RegisterTsKpssFunction`; `TsStationarityFunction` + `RegisterTsStationarityFunction`. Declarations added to `src/include/anofox_forecast_extension.hpp`; calls added to `LoadInternal` in `src/anofox_forecast_extension.cpp` immediately after `RegisterTsAdfFunction(loader);`.
- SQL macros (`src/macros/ts_macros.cpp`): `ts_kpss_by` and `ts_stationarity_by` entries under the existing `"diagnostics"` category.
- Assets: KPSS + combined-stationarity sections appended to `examples/diagnostics/stationarity.sql`, `docs/api/10-diagnostics.md` (filling the stubs 01-1 left), `benchmark/diagnostics/*` (kpss cross-check), and `test/sql/ts_diagnostics.test`.
</artifacts_this_phase_produces>

<four_way_verdict_truth_table>
STAT-03 requires a four-way label. The crate's `test_stationarity` returns only a three-way string, so this plan derives the four-way verdict in the FFI (or core wrapper) from the two rejection booleans `adf.is_stationary` (ADF rejects the unit-root null → series is stationary-side) and `kpss.is_stationary` (KPSS does NOT reject level-stationarity → series is level-stationary-side). Note that `is_stationary` in the crate means "the test's evidence points to stationarity": for ADF `is_stationary = statistic < cv_5pct` (unit-root null rejected); for KPSS `is_stationary = statistic < cv_5pct` (level-stationarity null NOT rejected).

Implement EXACTLY this mapping (state it verbatim in docs/api/10-diagnostics.md):

| adf.is_stationary (ADF rejects unit root) | kpss.is_stationary (KPSS fails to reject level-stationarity) | verdict            | interpretation |
|-------------------------------------------|-------------------------------------------------------------|--------------------|----------------|
| true                                      | true                                                        | "stationary"       | Both agree: series is stationary. |
| true                                      | false                                                       | "difference_stationary" | ADF says stationary but KPSS rejects level-stationarity → trend present around a stationary process; the standard reading is a trend-stationary/difference-stationary regime. Label "difference_stationary". |
| false                                     | true                                                        | "trend_stationary" | ADF cannot reject a unit root but KPSS does not reject level-stationarity → borderline; standard reading treats this as trend-stationary. Label "trend_stationary". |
| false                                     | false                                                       | "non_stationary"   | Both indicate non-stationarity (unit root present, level-stationarity rejected). |

This is the textbook ADF/KPSS cross-tabulation (see RESEARCH.md Pitfall 4 / Open Question 1). Document all four rows and state that the mapping is derived from two independent tests, not a single crate verdict; note that the crate's own `test_stationarity` collapses the two mixed cases into "inconclusive" and this function refines them into the four-way taxonomy per the table above.
</four_way_verdict_truth_table>

<tasks>

<task type="auto" tdd="true">
  <name>Task 1: Add KPSS + combined stationarity through core → FFI (STAT-02, STAT-03 layers 0-1)</name>
  <files>crates/anofox-fcst-core/src/validation.rs, crates/anofox-fcst-core/src/lib.rs, crates/anofox-fcst-ffi/src/types.rs, crates/anofox-fcst-ffi/src/lib.rs</files>
  <read_first>
    - .planning/phases/01-diagnostics-demand-classification/01-1-SUMMARY.md — the EXACT StationarityOut field order/name, the core wrapper fn signature style, and the AnofoxStationarityResult layout chosen by 01-1 (reuse them verbatim)
    - crates/anofox-fcst-core/src/validation.rs — the existing `adf` wrapper + StationarityOut type from 01-1; add `kpss` and `stationarity` beside it following the same shape
    - crates/anofox-fcst-ffi/src/lib.rs — the `anofox_ts_adf` export from 01-1 (structural template: init_error, check_null_pointers, length==0 early return, catch_unwind(AssertUnwindSafe), build_values, r.into()); copy_string_to_buffer helper for the verdict char[] field
    - crates/anofox-fcst-ffi/src/types.rs — the AnofoxStationarityResult struct + Default + From impl added by 01-1 (reuse it for KPSS; add AnofoxCombinedStationarityResult beside it)
    - RESEARCH.md Section 1.1 (kpss_test, test_stationarity signatures) and Section 2 Layer 1 (AnofoxCombinedStationarityResult layout)
  </read_first>
  <behavior>
    Tests written first (RED), then implementation until GREEN:
    - Rust core unit test in validation.rs: `kpss` on a stationary white-noise series returns a small positive statistic with is_stationary=true; on a random walk returns a larger statistic (more evidence against level-stationarity). Use approx::assert_relative_eq! only where a deterministic value is known; otherwise assert ordering/sign.
    - Rust core unit test: `kpss` on series length < 4 yields NaN statistic without panicking (crate contract).
    - Rust core unit test: `stationarity` returns "stationary" for a strongly mean-reverting series and "non_stationary" for a pure random walk; assert the verdict string is one of the four allowed labels.
    - Rust core unit test: the four-way mapping matches the truth table for all four (bool, bool) input combinations (construct the two StationarityOut values directly and assert the label — pure function test, no data needed).
    - FFI parity test: anofox_ts_kpss statistic equals core kpss within 1e-9; anofox_ts_stationarity verdict char[] decodes to the same string as core stationarity.
  </behavior>
  <action>
    Add KPSS and combined stationarity to the existing core and FFI layers, ADDITIVE only (do not touch the ADF code from 01-1).

    Layer 0 (core, crates/anofox-fcst-core/src/validation.rs): Add `pub fn kpss(series: &[f64], lags: Option<usize>) -> StationarityOut` calling `anofox_forecast::validation::kpss_test(series, lags)` and copying fields into the SAME StationarityOut type 01-1 introduced (statistic, p_value, lags, is_stationary, cv_1pct, cv_5pct, cv_10pct). Add a new flat owned type `CombinedStationarityOut` with fields: adf_statistic, adf_p_value, adf_lags, adf_is_stationary, kpss_statistic, kpss_p_value, kpss_lags, kpss_is_stationary, verdict (String). Add `pub fn stationarity(series: &[f64]) -> CombinedStationarityOut` that calls the crate's `adf_test(series, None)` and `kpss_test(series, None)` separately (NOT the tuple-returning `test_stationarity` — RESEARCH Pitfall 3), then computes `verdict` via a private `fn classify_stationarity(adf_is_stationary: bool, kpss_is_stationary: bool) -> &'static str` implementing the four_way_verdict_truth_table EXACTLY. The existing `pub use validation::*;` in lib.rs already re-exports these; confirm no additional lib.rs edit is needed beyond that (add explicit re-exports only if `*` does not cover the new names). Add the `#[cfg(test)] mod tests` cases from the behavior block.

    Layer 1 (FFI): In crates/anofox-fcst-ffi/src/types.rs, reuse AnofoxStationarityResult for KPSS (no new struct needed — KPSS shares the StationarityResult layout). Add `#[repr(C)] pub struct AnofoxCombinedStationarityResult { adf_statistic: c_double, adf_p_value: c_double, adf_lags: size_t, adf_is_stationary: bool, kpss_statistic: c_double, kpss_p_value: c_double, kpss_lags: size_t, kpss_is_stationary: bool, verdict: [c_char; 32] }` with a `Default` impl (NaN doubles, zero lags, false flags, zeroed verdict buffer) and a `From<anofox_fcst_core::CombinedStationarityOut>` impl that copies the numeric fields and uses copy_string_to_buffer for verdict. In crates/anofox-fcst-ffi/src/lib.rs add `anofox_ts_kpss(values, validity, length, lags: c_int, out_result: *mut AnofoxStationarityResult, out_error: *mut AnofoxError) -> bool` mirroring anofox_ts_adf but calling `anofox_fcst_core::kpss(&series, if lags < 0 { None } else { Some(lags as usize) })`, and `anofox_ts_stationarity(values, validity, length, out_result: *mut AnofoxCombinedStationarityResult, out_error) -> bool` (no lags/regression param — fixed defaults per CONTEXT) calling `anofox_fcst_core::stationarity(&series)`. Use build_values (NaN-for-NULL), length==0 early-returns Default, catch_unwind on the crate call. Do NOT hand-edit src/include/anofox_fcst_ffi.h — cbindgen regenerates it (verified in Task 2).
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast && cargo test -p anofox-fcst-core validation::tests::kpss -- --nocapture && cargo test -p anofox-fcst-core validation::tests::stationarity -- --nocapture && cargo test -p anofox-fcst-ffi</automated>
  </verify>
  <acceptance_criteria>
    - Core `kpss` and `stationarity` exist and pass the unit tests including the exhaustive four-combination truth-table test.
    - CombinedStationarityOut.verdict is always one of {"stationary","difference_stationary","trend_stationary","non_stationary"}.
    - FFI `anofox_ts_kpss` and `anofox_ts_stationarity` exist, parity tests pass, and neither hand-edits the cbindgen header.
  </acceptance_criteria>
  <done>Core KPSS + combined stationarity and their FFI exports exist; unit + parity + truth-table tests pass; edits are additive to the 01-1 code.</done>
  <reversibility rating="reversible">Additive functions/structs; no existing behavior changed.</reversibility>
</task>

<task type="auto">
  <name>Task 2: C++ scalars + registration + macros, then build and prove ts_kpss / ts_stationarity in SQL (STAT-02, STAT-03 layers 2-4)</name>
  <files>src/scalar_functions/diagnostics.cpp, src/include/anofox_forecast_extension.hpp, src/anofox_forecast_extension.cpp, src/macros/ts_macros.cpp, test/sql/ts_diagnostics.test</files>
  <read_first>
    - src/scalar_functions/diagnostics.cpp — the TsAdfFunction + RegisterTsAdfFunction from 01-1 (copy the STRUCT-build + StructVector::GetEntries + ExtractListAsDouble + null-guard pattern for KPSS; for the verdict VARCHAR field follow the FlatVector::GetData<string_t> + StringVector::AddString idiom noted in RESEARCH Section 2 Layer 2)
    - src/anofox_forecast_extension.cpp — the diagnostics registration block from 01-1 (RegisterTsAdfFunction call); add the two new calls immediately after it
    - src/include/anofox_forecast_extension.hpp — the RegisterTsAdfFunction declaration from 01-1; add the two new declarations beside it
    - src/macros/ts_macros.cpp — the ts_adf_by entry from 01-1 (copy its shape, named_params slot, category "diagnostics"); RESEARCH Section 2 Layer 4 for the ts_kpss_by / ts_stationarity_by macro bodies
    - test/sql/ts_diagnostics.test — the 01-1 ADF assertions (append KPSS + stationarity assertions using the same deterministic two-group fixture)
    - crates/anofox-fcst-ffi/build.rs — cbindgen writes src/include/anofox_fcst_ffi.h during `make rust`
  </read_first>
  <precondition>The extension build toolchain (make + duckdb extension-ci-tools) is available and 01-1 previously produced ./build/debug — CI is green per recent commits, so this holds.</precondition>
  <action>
    Wire the C++ / SQL layers, build, and test. ADDITIVE edits only — append beside the ADF symbols from 01-1.

    Layer 2 (C++ scalar, src/scalar_functions/diagnostics.cpp): Add `static void TsKpssFunction(...)` returning the SAME seven-field STRUCT as ts_adf (statistic, p_value, lags, is_stationary, cv_1pct, cv_5pct, cv_10pct), reading args.data[0] (LIST(DOUBLE)) and optional args.data[1] (lags INTEGER), calling `anofox_ts_kpss(values.data(), nullptr, values.size(), lags_or_-1, &r, &err)`. Add `void RegisterTsKpssFunction(ExtensionLoader&)` building that STRUCT type and registering a ScalarFunctionSet "ts_kpss" with {LIST(DOUBLE)} and {LIST(DOUBLE), INTEGER} overloads plus the `anofox_fcst_ts_kpss` alias and FunctionDescription category "diagnostics" (mirror RegisterTsAdfFunction). Add `static void TsStationarityFunction(...)` returning a STRUCT with fields adf_statistic DOUBLE, adf_p_value DOUBLE, adf_lags BIGINT, adf_is_stationary BOOLEAN, kpss_statistic DOUBLE, kpss_p_value DOUBLE, kpss_lags BIGINT, kpss_is_stationary BOOLEAN, verdict VARCHAR; call `anofox_ts_stationarity(values.data(), nullptr, values.size(), &r, &err)`; write the numeric fields, and write verdict via StringVector::AddString on the verdict char[] (convert the C string to a DuckDB string_t). Add `void RegisterTsStationarityFunction(ExtensionLoader&)` registering ScalarFunctionSet "ts_stationarity" with a single {LIST(DOUBLE)} overload plus the `anofox_fcst_ts_stationarity` alias and category "diagnostics". Null-guard each list and length==0 as in ts_adf.

    Layer 3 (registration): Add `void RegisterTsKpssFunction(ExtensionLoader &loader);` and `void RegisterTsStationarityFunction(ExtensionLoader &loader);` to src/include/anofox_forecast_extension.hpp beside the RegisterTsAdfFunction declaration. In src/anofox_forecast_extension.cpp LoadInternal, immediately after `RegisterTsAdfFunction(loader);`, add `RegisterTsKpssFunction(loader);` and `RegisterTsStationarityFunction(loader);`.

    Layer 4 (macros, src/macros/ts_macros.cpp): Add `ts_kpss_by` entry: params {"source","group_col","date_col","value_col", nullptr}, named_param `{"lags","-1"}`, body `SELECT group_col, ts_kpss(LIST(value_col::DOUBLE ORDER BY date_col), lags) AS kpss FROM query_table(source::VARCHAR) GROUP BY group_col`, a description, an example, category "diagnostics". Add `ts_stationarity_by` entry: same params, no named_params, body `SELECT group_col, ts_stationarity(LIST(value_col::DOUBLE ORDER BY date_col)) AS stationarity FROM query_table(source::VARCHAR) GROUP BY group_col`, description noting the four-way verdict, example projecting `(stationarity).verdict`, category "diagnostics".

    Build: `make rust` (confirm anofox_ts_kpss, anofox_ts_stationarity, AnofoxCombinedStationarityResult now appear in src/include/anofox_fcst_ffi.h — cbindgen, do NOT hand-edit), then `make debug`. If a link error for the new Register* symbols appears, the file is already in the build from 01-1 — recheck the header declarations match the definitions.

    Test (test/sql/ts_diagnostics.test): append to the existing 01-1 fixture: assert (a) `ts_kpss(LIST(y ORDER BY ds))` returns a non-null STRUCT with statistic/p_value/lags and `(kpss).p_value BETWEEN 0 AND 1`; (b) `ts_kpss_by(...)` returns one row per group; (c) `ts_stationarity(LIST(y ORDER BY ds))` returns a non-null STRUCT whose `(stationarity).verdict` is IN ('stationary','difference_stationary','trend_stationary','non_stationary'); (d) `ts_stationarity_by(...)` returns one row per group. Keep values deterministic.
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast && make rust && grep -q "anofox_ts_kpss" src/include/anofox_fcst_ffi.h && grep -q "anofox_ts_stationarity" src/include/anofox_fcst_ffi.h && make debug && (make test_debug ARGS="test/sql/ts_diagnostics.test" 2>/dev/null || ./build/debug/test/unittest test/sql/ts_diagnostics.test)</automated>
  </verify>
  <acceptance_criteria>
    - cbindgen-regenerated header contains anofox_ts_kpss and anofox_ts_stationarity.
    - Extension builds and loads; ts_kpss / ts_kpss_by return the seven-field STRUCT per series.
    - ts_stationarity / ts_stationarity_by return a STRUCT whose verdict is always one of the four allowed labels.
  </acceptance_criteria>
  <done>Extension builds; ts_diagnostics.test passes with KPSS and combined-stationarity assertions; all edits additive to 01-1.</done>
</task>

<task type="auto">
  <name>Task 3: Example, docs, and statsmodels cross-check for KPSS + stationarity (Definition of Done, STAT-02 STAT-03)</name>
  <files>examples/diagnostics/stationarity.sql, docs/api/10-diagnostics.md, benchmark/diagnostics/reference_values.py, benchmark/diagnostics/run_anofox.py, benchmark/diagnostics/README.md</files>
  <read_first>
    - examples/diagnostics/stationarity.sql — the ADF section 01-1 wrote; append KPSS and stationarity sections in the same style (header run-comment, LOAD, synthetic table already defined — reuse it)
    - docs/api/10-diagnostics.md — the ts_adf entry 01-1 wrote and the clearly-marked KPSS / combined-stationarity STUBS it left; fill those stubs
    - benchmark/diagnostics/reference_values.py and run_anofox.py — the ADF cross-check 01-1 wrote; extend both to add KPSS reference + parity, and add the stationarity verdict smoke check
    - benchmark/diagnostics/README.md — the statsmodels function map 01-1 started (adfuller); fill in the kpss row
  </read_first>
  <action>
    Satisfy the Definition of Done for STAT-02 and STAT-03. ADDITIVE to the 01-1 assets.

    examples/diagnostics/stationarity.sql: append a KPSS section demonstrating `ts_kpss(LIST(y ORDER BY ds))` and `ts_kpss_by('tbl', grp, ds, y)` projecting `(kpss).statistic`, `(kpss).p_value`, `(kpss).is_stationary`; and a combined-stationarity section demonstrating `ts_stationarity(...)` and `ts_stationarity_by(...)` projecting `(stationarity).verdict` alongside the ADF and KPSS statistics. Reuse the synthetic multi-series table 01-1 created. Verify the whole file runs end-to-end.

    docs/api/10-diagnostics.md: fill the KPSS stub — document `ts_kpss` / `ts_kpss_by`: signature, LIST(DOUBLE) + optional lags INTEGER (default auto), returned STRUCT fields, the caveat that KPSS statistic is POSITIVE and opposite-signed from ADF (larger = more evidence against level-stationarity), and that the crate implements level-stationarity ('c') only in v0.15.3 — the 'ct' mode from CONTEXT is not yet functional (state clearly). Fill the combined-stationarity stub — document `ts_stationarity` / `ts_stationarity_by`: the returned STRUCT (ADF fields, KPSS fields, verdict VARCHAR), and reproduce the four-row truth table VERBATIM from this plan's four_way_verdict_truth_table, explaining that the verdict is derived from two independent tests and how it refines the crate's three-way "inconclusive" into a four-way taxonomy. Leave the residual-diagnostics stubs for 01-3.

    benchmark/diagnostics/: extend reference_values.py to also emit statsmodels KPSS reference via `statsmodels.tsa.stattools.kpss(series, regression='c', nlags='auto')` (capture the FutureWarning-safe call) for the same deterministic fixture. Extend run_anofox.py to run `ts_kpss` through the built extension and assert statistic within rtol=0.05 and p_value within rtol=0.10 (KPSS p-values are piecewise-linear approximations — looser than ADF), and to run `ts_stationarity` and assert the verdict is one of the four labels (no statsmodels equivalent for the combined verdict — smoke check only). Update README.md: add the kpss row to the statsmodels function map and document the KPSS tolerance and WHY (approximate p-value table). If statsmodels is unavailable, fail loudly with an install hint.
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast && ./build/debug/duckdb < examples/diagnostics/stationarity.sql && grep -q "ts_kpss" docs/api/10-diagnostics.md && grep -q "ts_stationarity" docs/api/10-diagnostics.md && python3 benchmark/diagnostics/reference_values.py && python3 benchmark/diagnostics/run_anofox.py</automated>
  </verify>
  <acceptance_criteria>
    - stationarity.sql runs clean and shows KPSS + combined-stationarity output.
    - docs/api/10-diagnostics.md documents ts_kpss (with the positive-statistic + level-only caveats) and ts_stationarity (with the verbatim four-row truth table).
    - The cross-check confirms ts_kpss matches statsmodels kpss within documented tolerances and ts_stationarity produces a valid four-way verdict.
  </acceptance_criteria>
  <done>DoD satisfied for STAT-02 and STAT-03: runnable example, docs entries, and numeric cross-check all pass.</done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| SQL query → C++ scalar | User-supplied LIST(DOUBLE) and lags cross into the extension |
| C++ scalar → Rust FFI | Raw pointers + length cross the FFI boundary; verdict char[] copied back |

## STRIDE Threat Register

| Threat ID | Category | Component | Severity | Disposition | Mitigation Plan |
|-----------|----------|-----------|----------|-------------|-----------------|
| T-01-05 | Tampering | anofox_ts_kpss / anofox_ts_stationarity FFI entry | high | mitigate | init_error + check_null_pointers on {values, out_result} at entry (reuse the anofox_ts_adf pattern from 01-1) |
| T-01-06 | Denial of Service | Rust KPSS / combined computation | high | mitigate | catch_unwind(AssertUnwindSafe) wraps the crate calls; panic → set_error + return false |
| T-01-07 | Tampering | verdict char[32] buffer copy | medium | mitigate | copy_string_to_buffer truncates to buffer size; verdict strings are fixed known labels ≤ 22 chars, well under 32 |
| T-01-08 | Denial of Service | Empty/short series | medium | mitigate | length==0 early-returns Default; crate returns NaN (not panic) for n<4 |
| T-01-SC | Tampering | python statsmodels install for benchmark | low | accept | statsmodels is an established scientific package; benchmark-only, not shipped in the extension; no new runtime dependency (already accepted in 01-1) |
</threat_model>

<verification>
- `cargo test -p anofox-fcst-core validation::` (KPSS + stationarity + truth-table tests) and `cargo test -p anofox-fcst-ffi` pass
- `make rust` regenerates src/include/anofox_fcst_ffi.h containing anofox_ts_kpss + anofox_ts_stationarity + AnofoxCombinedStationarityResult (cbindgen, not hand-edited)
- Extension builds and loads; test/sql/ts_diagnostics.test passes with KPSS + stationarity assertions
- examples/diagnostics/stationarity.sql runs end-to-end
- benchmark cross-check confirms ts_kpss parity with statsmodels kpss within documented tolerances; ts_stationarity produces a valid four-way verdict
- docs/api/10-diagnostics.md documents ts_kpss and ts_stationarity including the verbatim four-way truth table
</verification>

<success_criteria>
STAT-02 and STAT-03 are Complete per the Definition of Done: ts_kpss / ts_kpss_by return statistic + p_value + lags per series; ts_stationarity / ts_stationarity_by return a four-way verdict (stationary / trend_stationary / difference_stationary / non_stationary) derived from the documented ADF/KPSS truth table — verified in SQL, documented in docs/api/, and numerically cross-checked against statsmodels kpss. All additions extend the 01-1 scaffolding without new infrastructure.
</success_criteria>

<output>
Create `.planning/phases/01-diagnostics-demand-classification/01-2-SUMMARY.md` when done. The SUMMARY MUST record: the exact STRUCT field order chosen for ts_kpss and ts_stationarity, the CombinedStationarityOut field names, the final verdict label strings used, and the KPSS benchmark tolerances — 01-3 and downstream consumers depend on these.
</output>
