---
phase: 03-classical-multivariate-models
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - crates/anofox-fcst-ffi/src/types.rs
  - crates/anofox-fcst-core/src/forecast.rs
  - crates/anofox-fcst-ffi/src/lib.rs
  - src/include/anofox_fcst_ffi.h
  - src/table_functions/ts_forecast_native.cpp
  - examples/forecasting/classical_forecasting_examples.sql
autonomous: true
requirements: [CLAS-01, CLAS-02]
estimate:
  tokens: 78000
  raw_tokens: 52000
  tasks: 3
  confidence: med
must_haves:
  truths:
    - "ts_forecast_by(..., 'Kalman', h, freq) returns h forecast rows per group with model_name='Kalman' (CLAS-02)"
    - "ts_forecast_by(..., 'GARCH', h, freq) returns h conditional-volatility (std-dev) rows per group with model_name='GARCH(1,1)' (CLAS-01)"
    - "GARCH forecast_value is sqrt(forecast_variance(h)) — volatility, not variance"
    - "params MAP{'garch_p':'1','garch_q':'1'} and params MAP{'kalman_model':'local_linear_trend'} are accepted and change model behavior"
    - "src/include/anofox_fcst_ffi.h contains garch_p, garch_q, kalman_model fields after make header (ABI-aligned, additive)"
  artifacts:
    - crates/anofox-fcst-core/src/forecast.rs
    - crates/anofox-fcst-ffi/src/types.rs
    - crates/anofox-fcst-ffi/src/lib.rs
    - src/table_functions/ts_forecast_native.cpp
    - examples/forecasting/classical_forecasting_examples.sql
  key_links:
    - "ForecastOptions (types.rs) → make header → anofox_fcst_ffi.h → C++ opts population → Rust FFI reads opts → core ForecastOptions → forecast() dispatch"
    - "ModelType::GARCH/Kalman FromStr arm ← method string 'GARCH'/'Kalman' from SQL"
  prohibitions:
    - "MUST NOT use GARCH::predict() / extract_forecast for GARCH output — it returns seeded simulated innovations, not the analytical variance forecast; use forecast_variance(h) + sqrt"
    - "MUST NOT hand-edit src/include/anofox_fcst_ffi.h — regenerate via make header (cbindgen)"
    - "MUST NOT remove or reorder existing ForecastOptions fields — append new fields only (additive ABI, keep defaults)"
    - "MUST NOT emit prediction intervals for GARCH/Kalman (deferred to v2)"
---

<objective>
Add GARCH and Kalman as new `ts_forecast_by` method arms, reusing the entire existing univariate forecast pipeline (collect → FFI → long-format emit). This is the phase tracer: it proves the ForecastOptions ABI extension end-to-end through all layers (Rust core → FFI struct → cbindgen header → C++ Bind/Finalize param plumbing → SQL) before the novel VAR I/O shape is built in 03-2.

Kalman is done first (simplest — it is a `Forecaster`-trait drop-in via `extract_forecast`, needs only the `kalman_model` string field). GARCH follows (needs `garch_p`/`garch_q` int fields plus the sqrt-of-variance output rule).

Purpose: Deliver CLAS-01 (GARCH) and CLAS-02 (Kalman) and de-risk the ForecastOptions ABI change on the best early-context tokens.
Output: Extended ForecastOptions (Rust core + FFI + regenerated header), two new ModelType arms, param plumbing in the C++ native table function, and a verified runnable example.
</objective>

<execution_context>
@$HOME/.claude/gsd-core/workflows/execute-plan.md
@$HOME/.claude/gsd-core/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/ROADMAP.md
@.planning/STATE.md
@.planning/phases/03-classical-multivariate-models/03-RESEARCH.md
@.planning/phases/03-classical-multivariate-models/03-PATTERNS.md
@.planning/phases/03-classical-multivariate-models/03-CONTEXT.md
</context>

<artifacts_produced>
## Artifacts this plan produces (NEW symbols)

- `ModelType::GARCH` and `ModelType::Kalman` enum variants (crates/anofox-fcst-core/src/forecast.rs)
- Method strings `'GARCH'` and `'Kalman'` (FromStr exact-match + lowercase arms; `ModelType::name()` arms)
- `forecast_garch(values, horizon, p, q)` and `forecast_kalman(values, horizon, spec)` core functions
- New core `ForecastOptions` fields: `garch_p: usize`, `garch_q: usize`, `kalman_model: Option<String>`
- New FFI `ForecastOptions` C-struct fields: `garch_p: c_int`, `garch_q: c_int`, `kalman_model: [c_char; 32]` (mirrored in ForecastOptionsExog if that struct also feeds anofox_ts_forecast)
- Regenerated `src/include/anofox_fcst_ffi.h` (cbindgen) carrying the three new fields
- C++ `TsForecastNativeBindData` fields `garch_p`, `garch_q`, `kalman_model`; new valid param keys `"garch_p"`, `"garch_q"`, `"kalman_model"`
- `examples/forecasting/classical_forecasting_examples.sql` (GARCH + Kalman sections; VAR section appended in 03-2)
</artifacts_produced>

<tasks>

<task type="tracer" tdd="true">
  <name>Task 1: Kalman end-to-end tracer — extend ForecastOptions, wire ModelType::Kalman through all layers</name>
  <read_first>
    - .planning/phases/03-classical-multivariate-models/03-PATTERNS.md (forecast.rs section lines 34-142; types.rs section lines 147-162; lib.rs GARCH/Kalman wiring section lines 189-202; ts_forecast_native.cpp param-plumbing section lines 471-504)
    - crates/anofox-fcst-core/src/forecast.rs (ModelType enum ~92-146, FromStr ~152-255, name() ~259-306, ForecastOptions struct ~309-347, Default ~349-367, forecast() dispatch ~570-681, make_timeseries + extract_forecast helpers, forecast_laplace as the shape analog)
    - crates/anofox-fcst-ffi/src/types.rs (ForecastOptions C struct ~373-406 and its Default impl; ForecastOptionsExog if present)
    - crates/anofox-fcst-ffi/src/lib.rs (anofox_ts_forecast export, the block ~3400-3450 where laplace_variant is read from opts and converted to Option)
    - src/table_functions/ts_forecast_native.cpp (ValidateParamKeys ~270-306, TsForecastNativeBindData struct def, TsForecastNativeBind param parsing ~342-354, Finalize opts population ~617-651)
    - Makefile (the `header` target — cbindgen invocation)
  </read_first>
  <behavior>
    - Rust core unit test: forecast_kalman(&[non-trivial series], horizon=5, None) returns a ForecastOutput whose point vec has length 5 and model_name == "Kalman".
    - Rust core unit test: forecast_kalman(&series, 5, Some("local_linear_trend")) succeeds and differs from local_level output.
    - Rust FFI unit test: calling anofox_ts_forecast with opts.model="Kalman", opts.kalman_model="" produces horizon point values (local_level default); with opts.kalman_model="local_linear_trend" it uses the trend spec.
  </behavior>
  <action>
Extend ForecastOptions in BOTH the core (crates/anofox-fcst-core/src/forecast.rs) and the FFI struct (crates/anofox-fcst-ffi/src/types.rs), then wire Kalman through every layer. Do NOT touch GARCH yet — Kalman is the thinnest path (Forecaster-trait drop-in, one string field) and proves the ABI change first.

1. Core forecast.rs: append `Kalman` to the ModelType enum (after Laplace). Add FromStr exact-match arm `"Kalman" => return Ok(ModelType::Kalman)` and lowercase-fallback arm `"kalman" => Ok(ModelType::Kalman)`. Add `ModelType::Kalman => "Kalman"` to name(). Append to the ForecastOptions struct: `garch_p: usize`, `garch_q: usize`, `kalman_model: Option<String>` (add all three now so the struct layout is settled; GARCH dispatch is added in Task 2). Extend the Default impl with `garch_p: 0, garch_q: 0, kalman_model: None`. Add the Kalman dispatch arm to the forecast() match: `ModelType::Kalman => forecast_kalman(&clean_values, options.horizon, options.kalman_model.as_deref())`. Implement forecast_kalman per 03-PATTERNS.md lines 128-143: build TimeSeries via make_timeseries, select KalmanForecaster::local_linear_trend() when spec=="local_linear_trend" else local_level(), fit, then return via the existing extract_forecast(&model, horizon, "Kalman") helper (KalmanForecaster implements Forecaster). Import `use anofox_forecast::models::kalman_forecaster::KalmanForecaster`.

2. FFI types.rs: append to the ForecastOptions C struct `pub garch_p: c_int`, `pub garch_q: c_int`, `pub kalman_model: [c_char; 32]` after laplace_seasonal_batch_init. Extend its Default impl with garch_p: 0, garch_q: 0, kalman_model: [0; 32]. If ForecastOptionsExog exists and also feeds anofox_ts_forecast, mirror the same three fields there. Append fields only — do not reorder existing fields (additive backward-compatible ABI).

3. Run `make header` (cbindgen). This regenerates src/include/anofox_fcst_ffi.h. Verify garch_p, garch_q, kalman_model appear in the regenerated header struct BEFORE compiling any C++. This is the mandatory step after any types.rs edit.

4. FFI lib.rs: in anofox_ts_forecast, after the laplace_seasonal_batch_init read (~line 3431), read the three new opts fields into the core ForecastOptions per 03-PATTERNS.md lines 194-202: options.garch_p = opts.garch_p as usize; options.garch_q = opts.garch_q as usize; options.kalman_model = CStr::from_ptr(opts.kalman_model.as_ptr()).to_str().ok().filter(|s| !s.is_empty()).map(str::to_owned).

5. C++ ts_forecast_native.cpp: add "garch_p", "garch_q", "kalman_model" to the ValidateParamKeys valid_keys set. Add int64_t garch_p = 0; int64_t garch_q = 0; string kalman_model = ""; to TsForecastNativeBindData. In TsForecastNativeBind parse them via the existing ParseInt64FromParams / ParseStringFromParams helpers. In TsForecastNativeFinalize populate opts.garch_p/garch_q (static_cast<int>) and strncpy kalman_model into opts.kalman_model[32] with explicit null-termination, per 03-PATTERNS.md lines 495-503.

Add the Rust unit tests described in <behavior> to the forecast.rs test module and the FFI test module.
  </action>
  <verify>
    <automated>make header && grep -q 'kalman_model' src/include/anofox_fcst_ffi.h && grep -q 'garch_p' src/include/anofox_fcst_ffi.h && cargo test -p anofox-fcst-core forecast_kalman && cargo test -p anofox-fcst-ffi kalman</automated>
  </verify>
  <acceptance_criteria>
    - `make header` regenerates src/include/anofox_fcst_ffi.h and `grep -c 'garch_p\|garch_q\|kalman_model' src/include/anofox_fcst_ffi.h` returns ≥ 3.
    - `cargo test -p anofox-fcst-core forecast_kalman` passes; `cargo test -p anofox-fcst-ffi kalman` passes.
    - No existing ForecastOptions field was reordered or removed (git diff shows only appended fields).
  </acceptance_criteria>
  <reversibility rating="reversible">ModelType arm + additive ForecastOptions fields; greenfield/additive, no existing behavior changed.</reversibility>
  <done>Kalman is dispatchable through the full Rust stack; the ABI header carries all three new fields; core + FFI Kalman unit tests pass.</done>
</task>

<task type="auto" tdd="true">
  <name>Task 2: GARCH dispatch — forecast_variance + sqrt, garch_p/garch_q plumbing</name>
  <read_first>
    - .planning/phases/03-classical-multivariate-models/03-RESEARCH.md (Critical Finding 1, lines 196-289 — GARCH API, min-obs = p+q+10, forecast_variance vs predict, sqrt rule; Pitfall 1 lines 652-656; Pitfall 8/9 lines 682-688)
    - .planning/phases/03-classical-multivariate-models/03-PATTERNS.md (forecast_garch helper lines 104-126; forecast() dispatch arm lines 88-102)
    - crates/anofox-fcst-core/src/forecast.rs (the arms/struct edited in Task 1; forecast_laplace as shape analog; ForecastOutput struct fields)
  </read_first>
  <behavior>
    - Rust core unit test: forecast_garch(&returns_like_series (len ≥ 12), horizon=5, 1, 1) returns ForecastOutput with point.len()==5, all point values ≥ 0 (volatility is non-negative), and model_name=="GARCH(1,1)".
    - Rust core unit test: each forecast_garch point value equals the sqrt of the corresponding GARCH::forecast_variance element (spot-check element 0 within 1e-9).
    - Rust core unit test: a series shorter than p+q+10 (e.g. len 8 for GARCH(1,1)) returns Err (InsufficientData surfaced as ComputationError), NOT a silent empty vec.
  </behavior>
  <action>
Add the GARCH dispatch to forecast.rs. The Kalman task already added garch_p/garch_q to ForecastOptions and the enum slot for GARCH is present from Task 1's enum edit only if you added it — if not, add `GARCH` to the ModelType enum now (after Kalman), plus FromStr exact arm `"GARCH" => return Ok(ModelType::GARCH)`, lowercase arm `"garch" => Ok(ModelType::GARCH)`, and `ModelType::GARCH => "GARCH"` in name().

Add the dispatch arm to forecast(): `ModelType::GARCH => forecast_garch(&clean_values, options.horizon, if options.garch_p == 0 { 1 } else { options.garch_p }, if options.garch_q == 0 { 1 } else { options.garch_q })`.

Implement forecast_garch per 03-PATTERNS.md lines 104-126 / 03-RESEARCH.md Pattern 2: build TimeSeries via make_timeseries; construct GARCH::new(p, q); fit; call model.forecast_variance(horizon) — NOT predict() and NOT extract_forecast (predict returns seeded simulated innovations per Pitfall 1). Take the element-wise sqrt to convert variance → volatility (std-dev). Return ForecastOutput { point: volatility, lower: vec![], upper: vec![], fitted: None, residuals: None, model_name: format!("GARCH({},{})", p, q), aic: None, bic: None, mse: None }. Map both fit and forecast_variance errors to ForecastError::ComputationError with a descriptive message (propagate — do not swallow into an empty vec). Import `use anofox_forecast::models::garch::GARCH`.

Add the three Rust unit tests from <behavior> to the forecast.rs test module.
  </action>
  <verify>
    <automated>cargo test -p anofox-fcst-core forecast_garch</automated>
  </verify>
  <acceptance_criteria>
    - `cargo test -p anofox-fcst-core forecast_garch` passes all three GARCH tests.
    - `grep -n 'forecast_variance' crates/anofox-fcst-core/src/forecast.rs` shows forecast_garch calls forecast_variance (not predict).
    - `grep -n 'sqrt' crates/anofox-fcst-core/src/forecast.rs` confirms the variance→volatility conversion inside forecast_garch.
  </acceptance_criteria>
  <reversibility rating="reversible">Additive ModelType arm + new core function; no existing model touched.</reversibility>
  <done>GARCH is dispatchable, outputs sqrt-of-variance volatility, propagates the min-obs error, and unit tests pass.</done>
</task>

<task type="auto">
  <name>Task 3: Build + load extension, verify GARCH & Kalman end-to-end via runnable example</name>
  <read_first>
    - examples/forecasting/global_panel_forecasting_examples.sql (structure/layout analog for the new file)
    - .planning/phases/03-classical-multivariate-models/03-PATTERNS.md (examples section lines 542-550)
    - .planning/phases/02-global-panel-models/02-1-SUMMARY.md (CLI subprocess verification pattern: build/release/duckdb -unsigned avoids venv duckdb version mismatch)
    - .planning/phases/03-classical-multivariate-models/03-CONTEXT.md (GARCH output is volatility not variance — document in example comments)
  </read_first>
  <action>
Build the extension so the new ForecastOptions plumbing compiles into a loadable binary, then create and verify the runnable example. Build via the project's standard extension build (make / cmake as configured — the same target that produces build/release/duckdb and the loadable extension used in Phase 2). Fix any C++ compile errors from the Task 1 param plumbing.

Create examples/forecasting/classical_forecasting_examples.sql with two sections (VAR section appended in 03-2):
- Section 1 — GARCH: a returns-like series (≥ 12 rows), call ts_forecast_by(source, group_col, date_col, value_col, 'GARCH', horizon, frequency). Add a second call passing params := MAP{'garch_p':'1','garch_q':'1'}. A SQL comment MUST state that forecast_value is conditional volatility (standard deviation) = sqrt(forecast_variance), NOT variance (D-Area1 documentation must-have).
- Section 2 — Kalman: call ts_forecast_by(..., 'Kalman', horizon, frequency) for the default local_level, and a second call with params := MAP{'kalman_model':'local_linear_trend'}.

Run the example end-to-end against the BUILT extension using the Phase-2 CLI subprocess pattern (build/release/duckdb -unsigned, LOAD the built extension, run the example SQL). Confirm each call returns exactly `horizon` rows per group with the correct model_name. This is the PR #230 rule — no eyeballing; the SQL must actually execute against the built binary.
  </action>
  <verify>
    <automated>build/release/duckdb -unsigned -c "LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension'; CREATE TABLE r AS SELECT 'A' AS g, (DATE '2020-01-01' + INTERVAL (i) DAY) AS ds, (0.5*sin(i*0.7)+0.3*cos(i*0.3)) AS y FROM range(40) t(i); SELECT count(*) AS n, any_value(model_name) FROM ts_forecast_by('r','g','ds','y','GARCH',7,'1d'); SELECT count(*) FROM ts_forecast_by('r','g','ds','y','Kalman',7,'1d', params := MAP{'kalman_model':'local_linear_trend'});"</automated>
  </verify>
  <acceptance_criteria>
    - The extension builds and loads cleanly (no missing-symbol / ABI errors).
    - The GARCH query returns n=7 rows and model_name='GARCH(1,1)'.
    - The Kalman query (local_linear_trend) returns 7 rows.
    - Running the full examples/forecasting/classical_forecasting_examples.sql against build/release/duckdb produces forecast rows for every GARCH and Kalman call (exit 0, no error).
  </acceptance_criteria>
  <done>GARCH and Kalman are callable from SQL against the built extension; the example is verified end-to-end (CLAS-01, CLAS-02 satisfied for the example+delivery-pattern DoD; docs/benchmark DoD closed in 03-3).</done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| SQL params MAP → C++ Bind | User-supplied garch_p/garch_q/kalman_model strings cross into C++ |
| C++ opts struct → Rust FFI | Fixed-size C arrays (kalman_model[32]) and ints cross the FFI ABI |
| Rust FFI → anofox-forecast crate | Series values cross into GARCH/Kalman fit |

## STRIDE Threat Register

| Threat ID | Category | Component | Severity | Disposition | Mitigation Plan |
|-----------|----------|-----------|----------|-------------|-----------------|
| T-03-01 | Tampering | kalman_model[32] C string | medium | mitigate | strncpy with explicit null-termination at index 31 (Task 1 step 5); Rust reads via CStr::from_ptr with to_str().ok() fallback to None on invalid UTF-8 |
| T-03-02 | Denial of Service | GARCH fit on short/degenerate series | low | mitigate | forecast_garch propagates InsufficientData (p+q+10 min) as ComputationError instead of panic/hang; catch_unwind at FFI boundary contains any panic |
| T-03-03 | Tampering | ForecastOptions ABI mismatch after struct edit | high | mitigate | mandatory `make header` (cbindgen) regenerates anofox_fcst_ffi.h; verify grep before C++ compile (Pitfall 2); fields appended only (additive, no reorder) |
| T-03-SC | Tampering | npm/pip/cargo installs | low | accept | No new packages installed in this plan (arch dependency is added in 03-3, gated there). Stay on anofox-forecast 0.15.3. |
</threat_model>

<verification>
- `make header` shows garch_p/garch_q/kalman_model in src/include/anofox_fcst_ffi.h.
- `cargo test -p anofox-fcst-core` and `cargo test -p anofox-fcst-ffi` (GARCH + Kalman) pass.
- Extension builds and loads; GARCH returns model_name='GARCH(1,1)' and horizon rows; Kalman returns horizon rows for both specs.
- examples/forecasting/classical_forecasting_examples.sql runs clean end-to-end against build/release/duckdb.
</verification>

<success_criteria>
- CLAS-01: ts_forecast_by with method 'GARCH' returns conditional-volatility forecasts, verified against the built extension (roadmap success criterion 1).
- CLAS-02: ts_forecast_by with method 'Kalman' returns smoothed/forecasted values, verified end-to-end (roadmap success criterion 2).
- ForecastOptions ABI extension proven end-to-end (tracer goal) — 03-2 VAR and 03-3 docs/benchmark build on this.
</success_criteria>

<output>
Create `.planning/phases/03-classical-multivariate-models/03-01-SUMMARY.md` when done.
</output>
