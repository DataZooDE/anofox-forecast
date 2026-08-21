---
phase: 01-diagnostics-demand-classification
plan: 3
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
  - examples/diagnostics/residuals.sql
  - docs/api/10-diagnostics.md
  - benchmark/diagnostics/reference_values.py
  - benchmark/diagnostics/run_anofox.py
  - benchmark/diagnostics/README.md
  - test/sql/ts_diagnostics.test
autonomous: true
requirements: [RESID-01, RESID-02, RESID-03, RESID-04]
estimate:
  tokens: 95000
  raw_tokens: 95000
  tasks: 3
  confidence: low
must_haves:
  truths:
    - "A user can call ts_ljung_box(LIST(r ORDER BY ds)) and ts_ljung_box_by('tbl', grp, ds, r) and receive a STRUCT with statistic, p_value, lags, df per series; lags default min(10, n/5) with an override param (RESID-01)"
    - "A user can call ts_durbin_watson(...) and ts_durbin_watson_by(...) and receive a STRUCT with statistic and an interpretation VARCHAR (RESID-02)"
    - "A user can call ts_jarque_bera(...) and ts_jarque_bera_by(...) and receive a STRUCT with statistic, p_value, skewness, excess_kurtosis (RESID-03)"
    - "A user can call ts_residual_diagnostics(...) and ts_residual_diagnostics_by(...) and receive ONE STRUCT with all three tests' stats/p-values plus an adequate BOOLEAN = (ljung_box.p_value > alpha), alpha default 0.05 (RESID-04)"
    - "ts_ljung_box, ts_durbin_watson, ts_jarque_bera numerically cross-check against statsmodels acorr_ljungbox / durbin_watson / jarque_bera within documented tolerances"
    - "examples/diagnostics/residuals.sql runs end-to-end and prints all four residual diagnostics"
  artifacts:
    - crates/anofox-fcst-core/src/validation.rs
    - src/scalar_functions/diagnostics.cpp
    - examples/diagnostics/residuals.sql
    - docs/api/10-diagnostics.md
    - benchmark/diagnostics/run_anofox.py
    - test/sql/ts_diagnostics.test
  key_links:
    - "cbindgen build.rs regenerates src/include/anofox_fcst_ffi.h with AnofoxLjungBoxResult, AnofoxDurbinWatsonResult, AnofoxJarqueBeraResult, AnofoxResidualDiagnosticsResult and their four anofox_ts_* exports (never hand-edited)"
    - "RegisterTsLjungBoxFunction / RegisterTsDurbinWatsonFunction / RegisterTsJarqueBeraFunction / RegisterTsResidualDiagnosticsFunction are declared in the hpp and called in LoadInternal after the stationarity registrations"
    - "the adequate verdict in ts_residual_diagnostics is computed as ljung_box.p_value > alpha (RESID-04 adequacy gate per CONTEXT); JB and DW are advisory fields"
---

<objective>
Add the residual-diagnostics family to the diagnostics scaffolding established by the 01-1 tracer: Ljung-Box (`ts_ljung_box` / `_by`, RESID-01), Durbin-Watson (`ts_durbin_watson` / `_by`, RESID-02), Jarque-Bera (`ts_jarque_bera` / `_by`, RESID-03), and a combined residual-adequacy report (`ts_residual_diagnostics` / `_by`, RESID-04). All four ADD to the existing 5-layer scaffolding from 01-1 (core `validation` module, FFI struct pattern, `diagnostics.cpp` scalar file, the diagnostics registration block, the `diagnostics` macro category) and to the shared `docs/api/10-diagnostics.md` + `benchmark/diagnostics/` assets. No new infrastructure is introduced (only a new `examples/diagnostics/residuals.sql` file, per the phase asset plan).

Purpose: Give SQL users a one-call residual-adequacy verdict for model diagnostics, with individual tests available, cross-checked against statsmodels.
Output: Working ts_ljung_box / ts_durbin_watson / ts_jarque_bera / ts_residual_diagnostics (+ `_by` macros) verified in SQL, documented, and numerically cross-checked, satisfying the Definition of Done for RESID-01..04.
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
Plans 01-2 and 01-3 run in the same wave and BOTH edit these shared files: `crates/anofox-fcst-core/src/validation.rs`, `crates/anofox-fcst-core/src/lib.rs`, `crates/anofox-fcst-ffi/src/types.rs`, `crates/anofox-fcst-ffi/src/lib.rs`, `src/scalar_functions/diagnostics.cpp`, `src/include/anofox_forecast_extension.hpp`, `src/anofox_forecast_extension.cpp`, `src/macros/ts_macros.cpp`, `docs/api/10-diagnostics.md`, `benchmark/diagnostics/*`, and `test/sql/ts_diagnostics.test`. This plan uses a SEPARATE example file (`examples/diagnostics/residuals.sql`) to avoid colliding with 01-2's `stationarity.sql`. Apply ONLY additive edits — append new functions/structs/registration calls/macro entries next to the existing ADF (01-1) and KPSS/stationarity (01-2) ones. Do NOT rewrite or reorder existing content. The residual symbol names (ljung_box, durbin_watson, jarque_bera, residual_diagnostics) do not collide with the stationarity names in 01-2. If a merge collision is detected on a shared file, re-read it and re-apply your addition below the current tail of the relevant block.
</coordination_note>

<artifacts_this_phase_produces>
This plan ADDS the following NEW symbols to the 01-1 scaffolding (only new file: examples/diagnostics/residuals.sql):

- Rust core (`crates/anofox-fcst-core/src/validation.rs`): `LjungBoxOut`, `DurbinWatsonOut`, `JarqueBeraOut`, `ResidualDiagnosticsOut` flat owned types + `pub fn ljung_box(series, lags: Option<usize>, fitted_params: usize) -> LjungBoxOut`, `pub fn durbin_watson(series) -> DurbinWatsonOut`, `pub fn jarque_bera(series) -> JarqueBeraOut`, `pub fn residual_diagnostics(series, fitted_params: usize, alpha: f64) -> ResidualDiagnosticsOut`. Re-exported via the existing `pub use validation::*;`.
- FFI (`crates/anofox-fcst-ffi/src/types.rs`): `AnofoxLjungBoxResult`, `AnofoxDurbinWatsonResult`, `AnofoxJarqueBeraResult`, `AnofoxResidualDiagnosticsResult` (#[repr(C)] + Default + From). Exports `anofox_ts_ljung_box`, `anofox_ts_durbin_watson`, `anofox_ts_jarque_bera`, `anofox_ts_residual_diagnostics` in `lib.rs`. cbindgen regenerates all into `src/include/anofox_fcst_ffi.h`.
- C++ scalar (`src/scalar_functions/diagnostics.cpp`): `TsLjungBoxFunction`/`RegisterTsLjungBoxFunction`, `TsDurbinWatsonFunction`/`RegisterTsDurbinWatsonFunction`, `TsJarqueBeraFunction`/`RegisterTsJarqueBeraFunction`, `TsResidualDiagnosticsFunction`/`RegisterTsResidualDiagnosticsFunction`. Declarations in the hpp; calls in LoadInternal after the stationarity registrations.
- SQL macros (`src/macros/ts_macros.cpp`): `ts_ljung_box_by`, `ts_durbin_watson_by`, `ts_jarque_bera_by`, `ts_residual_diagnostics_by` under the existing `"diagnostics"` category.
- Assets: `examples/diagnostics/residuals.sql` (new file); residual sections filling the stubs in `docs/api/10-diagnostics.md`; residual cross-checks in `benchmark/diagnostics/*`; residual assertions in `test/sql/ts_diagnostics.test`.
</artifacts_this_phase_produces>

<adequacy_rule>
RESID-04 adequacy verdict (from CONTEXT, locked): `adequate = (ljung_box.p_value > alpha)` — Ljung-Box p > alpha means NO residual autocorrelation, which is the PASS gate. Jarque-Bera (normality) and Durbin-Watson (≈2) are ADVISORY fields carried in the STRUCT but NOT part of the pass/fail decision. `alpha` default 0.05, configurable via a named macro parameter. State this explicitly in docs and the SQL description.
</adequacy_rule>

<tasks>

<task type="auto" tdd="true">
  <name>Task 1: Add four residual diagnostics through core → FFI (RESID-01..04 layers 0-1)</name>
  <files>crates/anofox-fcst-core/src/validation.rs, crates/anofox-fcst-core/src/lib.rs, crates/anofox-fcst-ffi/src/types.rs, crates/anofox-fcst-ffi/src/lib.rs</files>
  <read_first>
    - .planning/phases/01-diagnostics-demand-classification/01-1-SUMMARY.md — the core wrapper fn/type conventions and AnofoxStationarityResult layout 01-1 chose (mirror the style for the four new result types)
    - crates/anofox-fcst-core/src/validation.rs — the existing adf (01-1) wrapper; add the four residual wrappers beside it in the same shape
    - crates/anofox-fcst-ffi/src/lib.rs — the anofox_ts_adf export from 01-1 (template) and copy_string_to_buffer (for the durbin_watson interpretation char[])
    - crates/anofox-fcst-ffi/src/types.rs — the AnofoxStationarityResult struct + Default + From from 01-1; add the four residual structs beside it
    - RESEARCH.md Section 1.2 (ljung_box, durbin_watson, jarque_bera, diagnose_residuals signatures + result structs; AutocorrelationType enum → VARCHAR mapping), Section 2 Layer 1 (all four FFI struct layouts), Pitfall 5 (enum→VARCHAR), Pitfall 8 (fitted_params)
    - crate result structs: LjungBoxResult{statistic,p_value,lags,df}, DurbinWatsonResult{statistic,interpretation:AutocorrelationType}, JarqueBeraResult{statistic,p_value,skewness,excess_kurtosis}, ResidualDiagnostics{ljung_box,durbin_watson,jarque_bera,mean,variance,n} with is_adequate(alpha)=ljung_box.p_value>alpha
  </read_first>
  <behavior>
    Tests written first (RED), then implementation until GREEN:
    - Rust core unit test: `ljung_box` on white-noise residuals returns a high p_value (fail to reject → white noise); on strongly autocorrelated residuals (e.g. r[i]=r[i-1]*0.9+noise) returns a low p_value. lags=None yields the min(10,n/5) heuristic; passing lags=Some(5) uses 5 lags.
    - Rust core unit test: `durbin_watson` on independent residuals returns statistic near 2.0 with interpretation "none"; on positively autocorrelated residuals returns statistic < 1.5 with a "positive_*" interpretation string. Interpretation is always one of {"positive_strong","positive_weak","none","negative_weak","negative_strong"}.
    - Rust core unit test: `jarque_bera` on approximately-normal residuals returns a high p_value; on a strongly skewed series returns a low p_value; skewness/excess_kurtosis are finite.
    - Rust core unit test: `residual_diagnostics` sets adequate=true when ljung_box.p_value>alpha and false otherwise; the individual sub-fields match the standalone ljung_box/durbin_watson/jarque_bera outputs on the same series; alpha is respected (adequate flips when alpha crosses ljung_box.p_value).
    - Rust core unit test: short series (n<3 for LB/JB, n<2 for DW) yield NaN statistics without panicking.
    - FFI parity test: each anofox_ts_* statistic equals its core counterpart within 1e-9; durbin_watson interpretation char[] and residual_diagnostics dw_interpretation char[] decode to the same string as core.
  </behavior>
  <action>
    Add the four residual diagnostics to core and FFI, ADDITIVE only (do not touch ADF from 01-1 or the stationarity code from 01-2).

    Layer 0 (core, crates/anofox-fcst-core/src/validation.rs): Define flat owned types — `LjungBoxOut { statistic, p_value, lags, df }`, `DurbinWatsonOut { statistic, interpretation: String }`, `JarqueBeraOut { statistic, p_value, skewness, excess_kurtosis }`, `ResidualDiagnosticsOut { lb_statistic, lb_p_value, lb_lags, lb_df, dw_statistic, dw_interpretation: String, jb_statistic, jb_p_value, jb_skewness, jb_excess_kurtosis, mean, variance, n, adequate: bool, alpha }`. Add wrappers: `pub fn ljung_box(series: &[f64], lags: Option<usize>, fitted_params: usize) -> LjungBoxOut` calling `anofox_forecast::validation::ljung_box(series, lags, fitted_params)`; `pub fn durbin_watson(series: &[f64]) -> DurbinWatsonOut` calling the crate fn and converting AutocorrelationType via a private `fn autocorr_label(t: &AutocorrelationType) -> &'static str` → {"positive_strong","positive_weak","none","negative_weak","negative_strong"}; `pub fn jarque_bera(series: &[f64]) -> JarqueBeraOut`; `pub fn residual_diagnostics(series: &[f64], fitted_params: usize, alpha: f64) -> ResidualDiagnosticsOut` calling `anofox_forecast::validation::diagnose_residuals(series, fitted_params)`, copying the sub-fields, reusing autocorr_label for dw_interpretation, and setting `adequate = d.is_adequate(alpha)` (== d.ljung_box.p_value > alpha) — the RESID-04 gate. Confirm the existing `pub use validation::*;` re-exports the new names. Add the `#[cfg(test)] mod tests` cases from the behavior block.

    Layer 1 (FFI): In crates/anofox-fcst-ffi/src/types.rs add `#[repr(C)]` structs: `AnofoxLjungBoxResult { statistic: c_double, p_value: c_double, lags: size_t, df: size_t }`; `AnofoxDurbinWatsonResult { statistic: c_double, interpretation: [c_char; 24] }`; `AnofoxJarqueBeraResult { statistic: c_double, p_value: c_double, skewness: c_double, excess_kurtosis: c_double }`; `AnofoxResidualDiagnosticsResult { lb_statistic: c_double, lb_p_value: c_double, lb_lags: size_t, lb_df: size_t, dw_statistic: c_double, dw_interpretation: [c_char; 24], jb_statistic: c_double, jb_p_value: c_double, jb_skewness: c_double, jb_excess_kurtosis: c_double, mean: c_double, variance: c_double, n: size_t, adequate: bool, alpha: c_double }`. Each gets a `Default` impl (NaN doubles, zeroed integers/buffers, adequate=false) and a `From<...Out>` impl (copy_string_to_buffer for the char[] interpretation fields). In crates/anofox-fcst-ffi/src/lib.rs add four exports mirroring anofox_ts_adf: `anofox_ts_ljung_box(values, validity, length, lags: c_int, fitted_params: c_int, out_result: *mut AnofoxLjungBoxResult, out_error) -> bool` (lags<0 → None, fitted_params<0 → 0); `anofox_ts_durbin_watson(values, validity, length, out_result: *mut AnofoxDurbinWatsonResult, out_error) -> bool`; `anofox_ts_jarque_bera(values, validity, length, out_result: *mut AnofoxJarqueBeraResult, out_error) -> bool`; `anofox_ts_residual_diagnostics(values, validity, length, fitted_params: c_int, alpha: c_double, out_result: *mut AnofoxResidualDiagnosticsResult, out_error) -> bool`. Use build_values (NaN-for-NULL), length==0 early-returns Default, catch_unwind(AssertUnwindSafe) on the crate call, r.into() on Ok. Do NOT hand-edit src/include/anofox_fcst_ffi.h — cbindgen regenerates it (verified in Task 2).
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast && cargo test -p anofox-fcst-core validation::tests -- --nocapture && cargo test -p anofox-fcst-ffi</automated>
  </verify>
  <acceptance_criteria>
    - Core ljung_box / durbin_watson / jarque_bera / residual_diagnostics exist and pass unit tests including the adequacy-gate flip test.
    - residual_diagnostics.adequate == (lb_p_value > alpha) for all tested alpha values.
    - DurbinWatson interpretation and residual dw_interpretation are always one of the five allowed labels.
    - All four FFI exports pass parity tests; no hand-edit of the cbindgen header.
  </acceptance_criteria>
  <done>The four core residual wrappers and their FFI exports exist; unit + parity + adequacy-gate tests pass; edits are additive to 01-1/01-2.</done>
  <reversibility rating="reversible">Additive functions/structs; no existing behavior changed.</reversibility>
</task>

<task type="auto">
  <name>Task 2: C++ scalars + registration + macros, build, and prove all four residual functions in SQL (RESID-01..04 layers 2-4)</name>
  <files>src/scalar_functions/diagnostics.cpp, src/include/anofox_forecast_extension.hpp, src/anofox_forecast_extension.cpp, src/macros/ts_macros.cpp, test/sql/ts_diagnostics.test</files>
  <read_first>
    - src/scalar_functions/diagnostics.cpp — TsAdfFunction/RegisterTsAdfFunction from 01-1 (copy the STRUCT-build + StructVector::GetEntries + ExtractListAsDouble + null-guard + length==0 pattern); for VARCHAR fields (dw interpretation) follow FlatVector::GetData<string_t> + StringVector::AddString
    - src/anofox_forecast_extension.cpp — the diagnostics registration block; add the four residual Register* calls after the stationarity ones (or after RegisterTsAdfFunction if 01-2 has not landed yet — additive, order-independent)
    - src/include/anofox_forecast_extension.hpp — RegisterTsAdfFunction declaration; add the four residual declarations beside it
    - src/macros/ts_macros.cpp — ts_adf_by entry (macro shape, named_params slot, category "diagnostics"); RESEARCH Section 6 per-requirement notes for each _by signature and named params
    - test/sql/ts_diagnostics.test — 01-1 ADF assertions; append residual assertions on a deterministic residual fixture
    - crates/anofox-fcst-ffi/build.rs — cbindgen writes the header during `make rust`
  </read_first>
  <precondition>The extension build toolchain (make + duckdb extension-ci-tools) is available and 01-1 previously produced ./build/debug — CI is green per recent commits, so this holds.</precondition>
  <action>
    Wire C++ / SQL, build, and test. ADDITIVE only.

    Layer 2 (C++ scalar, src/scalar_functions/diagnostics.cpp): Add four scalar functions + their Register* functions, each building the matching STRUCT return type and reading LIST(DOUBLE) (+ optional integer/double params handled by overloads), null-guarding each list and length==0:
    - `TsLjungBoxFunction` / `RegisterTsLjungBoxFunction`: STRUCT(statistic DOUBLE, p_value DOUBLE, lags BIGINT, df BIGINT). ScalarFunctionSet "ts_ljung_box" overloads {LIST(DOUBLE)}, {LIST(DOUBLE), INTEGER (lags)}, {LIST(DOUBLE), INTEGER (lags), INTEGER (fitted_params)}; call anofox_ts_ljung_box(..., lags_or_-1, fitted_or_0, &r, &err). Alias anofox_fcst_ts_ljung_box, category "diagnostics".
    - `TsDurbinWatsonFunction` / `RegisterTsDurbinWatsonFunction`: STRUCT(statistic DOUBLE, interpretation VARCHAR). Single {LIST(DOUBLE)} overload; write interpretation via StringVector::AddString. Alias, category "diagnostics".
    - `TsJarqueBeraFunction` / `RegisterTsJarqueBeraFunction`: STRUCT(statistic DOUBLE, p_value DOUBLE, skewness DOUBLE, excess_kurtosis DOUBLE). Single {LIST(DOUBLE)} overload. Alias, category "diagnostics".
    - `TsResidualDiagnosticsFunction` / `RegisterTsResidualDiagnosticsFunction`: STRUCT(lb_statistic DOUBLE, lb_p_value DOUBLE, lb_lags BIGINT, lb_df BIGINT, dw_statistic DOUBLE, dw_interpretation VARCHAR, jb_statistic DOUBLE, jb_p_value DOUBLE, jb_skewness DOUBLE, jb_excess_kurtosis DOUBLE, mean DOUBLE, variance DOUBLE, n BIGINT, adequate BOOLEAN, alpha DOUBLE). Overloads {LIST(DOUBLE)}, {LIST(DOUBLE), INTEGER (fitted_params)}, {LIST(DOUBLE), INTEGER (fitted_params), DOUBLE (alpha)}; call anofox_ts_residual_diagnostics(..., fitted_or_0, alpha_or_0.05, &r, &err); write dw_interpretation via StringVector::AddString and adequate as BOOLEAN. Alias anofox_fcst_ts_residual_diagnostics, category "diagnostics".

    Layer 3 (registration): Add the four `void RegisterTs*Function(ExtensionLoader &loader);` declarations to src/include/anofox_forecast_extension.hpp beside RegisterTsAdfFunction. In src/anofox_forecast_extension.cpp LoadInternal, after the existing diagnostics registration calls, add RegisterTsLjungBoxFunction(loader); RegisterTsDurbinWatsonFunction(loader); RegisterTsJarqueBeraFunction(loader); RegisterTsResidualDiagnosticsFunction(loader);.

    Layer 4 (macros, src/macros/ts_macros.cpp): Add four `_by` entries under category "diagnostics", each `SELECT group_col, ts_<name>(LIST(value_col::DOUBLE ORDER BY date_col), <named params>) AS <alias> FROM query_table(source::VARCHAR) GROUP BY group_col`:
    - `ts_ljung_box_by`: named_params {"lags","-1"} and {"fitted_params","0"}; alias `ljung_box`.
    - `ts_durbin_watson_by`: no named_params; alias `durbin_watson`.
    - `ts_jarque_bera_by`: no named_params; alias `jarque_bera`.
    - `ts_residual_diagnostics_by`: named_params {"fitted_params","0"} and {"alpha","0.05"}; alias `residual_diagnostics`; description states the adequacy gate is ljung_box.p_value > alpha; example projects `(residual_diagnostics).adequate` and `(residual_diagnostics).lb_p_value`.

    Build: `make rust` (confirm anofox_ts_ljung_box, anofox_ts_durbin_watson, anofox_ts_jarque_bera, anofox_ts_residual_diagnostics and their four structs now appear in src/include/anofox_fcst_ffi.h — cbindgen, do NOT hand-edit), then `make debug`.

    Test (test/sql/ts_diagnostics.test): append a deterministic residual fixture (e.g. two groups of ~40 pseudo-random residuals) and assert: (a) each of ts_ljung_box / ts_durbin_watson / ts_jarque_bera returns a non-null STRUCT with correct field types; ljung_box/jarque_bera p_value BETWEEN 0 AND 1; durbin_watson statistic BETWEEN 0 AND 4; interpretation IN the five labels; (b) each `_by` variant returns one row per group; (c) ts_residual_diagnostics returns a STRUCT whose adequate BOOLEAN equals `(residual_diagnostics).lb_p_value > 0.05`; (d) ts_residual_diagnostics_by(..., alpha:=0.5) flips adequate consistently with the lb_p_value on the fixture (assert the gate relationship, not a hardcoded value).
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast && make rust && grep -q "anofox_ts_ljung_box" src/include/anofox_fcst_ffi.h && grep -q "anofox_ts_residual_diagnostics" src/include/anofox_fcst_ffi.h && make debug && (make test_debug ARGS="test/sql/ts_diagnostics.test" 2>/dev/null || ./build/debug/test/unittest test/sql/ts_diagnostics.test)</automated>
  </verify>
  <acceptance_criteria>
    - cbindgen header contains all four new anofox_ts_* exports and their structs.
    - Extension builds and loads; all four scalar functions and their _by macros return correct per-series STRUCTs.
    - ts_residual_diagnostics.adequate always equals (lb_p_value > alpha) in SQL, verified for two alpha values.
  </acceptance_criteria>
  <done>Extension builds; ts_diagnostics.test passes with all four residual diagnostics and the adequacy-gate assertion; edits additive.</done>
</task>

<task type="auto">
  <name>Task 3: Example, docs, and statsmodels cross-check for the four residual diagnostics (Definition of Done, RESID-01..04)</name>
  <files>examples/diagnostics/residuals.sql, docs/api/10-diagnostics.md, benchmark/diagnostics/reference_values.py, benchmark/diagnostics/run_anofox.py, benchmark/diagnostics/README.md</files>
  <read_first>
    - examples/diagnostics/stationarity.sql (from 01-1) — the example header/run-comment/LOAD style to mirror in the new residuals.sql
    - docs/api/10-diagnostics.md — the residual-diagnostics STUBS 01-1 left; fill them
    - benchmark/diagnostics/reference_values.py and run_anofox.py (from 01-1) — extend both to add the three residual reference values + parity checks
    - benchmark/diagnostics/README.md — the statsmodels function map; fill the acorr_ljungbox / durbin_watson / jarque_bera rows
    - RESEARCH Section 4 (statsmodels reference functions: acorr_ljungbox, durbin_watson, jarque_bera) and Section 6 (per-requirement STRUCT fields)
  </read_first>
  <action>
    Satisfy the Definition of Done for RESID-01..04. New example file + additive docs/benchmark edits.

    examples/diagnostics/residuals.sql: new file under examples/diagnostics/. Header comment with the run command (`./build/release/duckdb < examples/diagnostics/residuals.sql`), LOAD anofox_forecast, create a small synthetic multi-series RESIDUAL table (two groups), and demonstrate all four: `ts_ljung_box(LIST(r ORDER BY ds))` + `ts_ljung_box_by(...)` projecting statistic/p_value/lags/df; `ts_durbin_watson(...)` + `_by` projecting statistic/interpretation; `ts_jarque_bera(...)` + `_by` projecting statistic/p_value/skewness/excess_kurtosis; `ts_residual_diagnostics(...)` + `_by` projecting `(residual_diagnostics).adequate`, `(residual_diagnostics).lb_p_value`, and the advisory dw_statistic/jb_p_value. Show the adequacy verdict prominently. Verify it runs end-to-end.

    docs/api/10-diagnostics.md: fill the residual-diagnostics stubs. Document ts_ljung_box / _by (signature, LIST(DOUBLE) + optional lags INTEGER default min(10,n/5) + optional fitted_params INTEGER default 0, returned STRUCT, and the fitted_params note from RESEARCH Pitfall 8 — pass 0 for raw residuals, p+q for an ARIMA fit). Document ts_durbin_watson / _by (statistic in [0,4], interpretation VARCHAR labels, no p-value — DW has no closed-form p-value in the crate). Document ts_jarque_bera / _by (statistic, p_value, skewness, excess_kurtosis). Document ts_residual_diagnostics / _by (the full combined STRUCT, the alpha param default 0.05, and the adequacy rule stated VERBATIM: `adequate = (ljung_box.p_value > alpha)`; Jarque-Bera and Durbin-Watson are advisory only). State that residual p-values are approximate (chi-squared / table approximations).

    benchmark/diagnostics/: extend reference_values.py to emit statsmodels references — `statsmodels.stats.diagnostic.acorr_ljungbox(residuals, lags=[k])`, `statsmodels.stats.stattools.durbin_watson(residuals)`, `statsmodels.stats.stattools.jarque_bera(residuals)` — for the deterministic residual fixture. Extend run_anofox.py to run ts_ljung_box (statistic rtol=0.02, p_value rtol=0.10), ts_durbin_watson (statistic rtol=0.001 — DW is a closed-form ratio, should match closely), ts_jarque_bera (statistic rtol=0.02, p_value rtol=0.10), and a smoke check that ts_residual_diagnostics.adequate == (lb_p_value > alpha). Ensure the Ljung-Box lags used in the reference match the crate's default so the comparison is apples-to-apples (compute min(10,n/5) in the reference script). Update README.md: fill the three residual rows in the statsmodels map and document each tolerance and WHY. If statsmodels is unavailable, fail loudly with an install hint.
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast && ./build/debug/duckdb < examples/diagnostics/residuals.sql && grep -q "ts_residual_diagnostics" docs/api/10-diagnostics.md && grep -q "ts_ljung_box" docs/api/10-diagnostics.md && python3 benchmark/diagnostics/reference_values.py && python3 benchmark/diagnostics/run_anofox.py</automated>
  </verify>
  <acceptance_criteria>
    - residuals.sql runs clean and shows all four residual diagnostics including the adequacy verdict.
    - docs/api/10-diagnostics.md documents all four functions; the RESID-04 adequacy rule appears verbatim as adequate = (ljung_box.p_value > alpha) with JB/DW noted as advisory.
    - The cross-check confirms ts_ljung_box / ts_durbin_watson / ts_jarque_bera match statsmodels within documented tolerances.
  </acceptance_criteria>
  <done>DoD satisfied for RESID-01..04: runnable example, docs entries, and numeric cross-check all pass.</done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| SQL query → C++ scalar | User-supplied LIST(DOUBLE), lags/fitted_params INTEGER, alpha DOUBLE cross into the extension |
| C++ scalar → Rust FFI | Raw pointers + length cross the FFI boundary; interpretation char[] copied back |

## STRIDE Threat Register

| Threat ID | Category | Component | Severity | Disposition | Mitigation Plan |
|-----------|----------|-----------|----------|-------------|-----------------|
| T-01-09 | Tampering | four anofox_ts_* residual FFI entries | high | mitigate | init_error + check_null_pointers on {values, out_result} at entry (reuse anofox_ts_adf pattern from 01-1) |
| T-01-10 | Denial of Service | Rust residual computations | high | mitigate | catch_unwind(AssertUnwindSafe) wraps each crate call; panic → set_error + return false |
| T-01-11 | Tampering | dw_interpretation char[24] buffer copy | medium | mitigate | copy_string_to_buffer truncates to buffer size; labels are fixed known strings ≤ 15 chars, well under 24 |
| T-01-12 | Tampering | lags / fitted_params c_int and alpha c_double casts | medium | mitigate | lags<0 → None, fitted_params<0 → 0 clamp; alpha passed through, crate handles range; NaN/negative alpha simply yields adequate per comparison semantics |
| T-01-13 | Denial of Service | Empty/short residual series | medium | mitigate | length==0 early-returns Default; crate returns NaN (not panic) for n below each test's minimum |
| T-01-SC | Tampering | python statsmodels install for benchmark | low | accept | statsmodels is an established scientific package; benchmark-only, not shipped in the extension; no new runtime dependency (already accepted in 01-1) |
</threat_model>

<verification>
- `cargo test -p anofox-fcst-core validation::` (four residual wrappers + adequacy-gate tests) and `cargo test -p anofox-fcst-ffi` pass
- `make rust` regenerates src/include/anofox_fcst_ffi.h containing the four residual exports + structs (cbindgen, not hand-edited)
- Extension builds and loads; test/sql/ts_diagnostics.test passes with all four residual diagnostics + adequacy-gate assertions
- examples/diagnostics/residuals.sql runs end-to-end
- benchmark cross-check confirms ts_ljung_box / ts_durbin_watson / ts_jarque_bera parity with statsmodels within documented tolerances
- docs/api/10-diagnostics.md documents all four functions; RESID-04 adequacy rule stated verbatim
</verification>

<success_criteria>
RESID-01, RESID-02, RESID-03, RESID-04 are Complete per the Definition of Done: ts_ljung_box / ts_durbin_watson / ts_jarque_bera return their per-series STRUCTs; ts_residual_diagnostics returns one combined STRUCT with all three tests plus an adequate verdict computed as (ljung_box.p_value > alpha) with JB/DW advisory — all verified in SQL, documented in docs/api/, and numerically cross-checked against statsmodels. All additions extend the 01-1 scaffolding without new infrastructure (one new example file only).
</success_criteria>

<output>
Create `.planning/phases/01-diagnostics-demand-classification/01-3-SUMMARY.md` when done. The SUMMARY MUST record: the exact STRUCT field order for each of the four functions, the interpretation label strings, the combined ResidualDiagnosticsOut field names, the alpha default and adequacy gate, and the residual benchmark tolerances.
</output>
