---
phase: 10-forecast-wrapper-delegates-to-crate-models
plan: 02
subsystem: forecasting
tags: [rust, cpp, duckdb, anofox-forecast, arima, sarima, ffi, cbindgen, r-fixtures]

requires:
  - phase: 10-01
    provides: forecast()/forecast_with_exog() crate-passthrough foundation (D-01/D-02/D-03/D-11/D-12); documented ForecastResult n_fitted/residuals length-sharing gap, explicitly reserved for this plan
provides:
  - "Real crate ARIMA::new(p,d,q)/SARIMA::new(p,d,q,P,D,Q,s) fit replacing the fixed-coefficient AR(1) stand-in; arima_order/seasonal_order parameters end-to-end from SQL MAP params through C++/FFI/core"
  - "Strict FFI option-string parsing (c_field_str, parse_order_triplet, parse_seasonal_periods) — invalid UTF-8 or malformed option values are errors, never a silent 'auto'/dropped-entry fallback"
  - "ForecastResult.n_residuals — fixes the pre-existing n_fitted/residuals length-sharing bug found during 10-01 (GARCH-class models' real residuals no longer silently truncated to empty)"
  - "NULL-safe prediction-interval bounds in ts_cv_forecast_native.cpp and ts_forecast_agg.cpp (D-02) — a model with no native intervals emits SQL NULL instead of dereferencing a null pointer"
  - "R Arima() parity fixtures for the seasonal airline model and two new non-seasonal (d=0) orders (simulated_series.json#series.ar1_l10)"
  - "Natively-run (no `require json`) sqllogictest proving FCST-01/02/03/04 against the built extension"
affects: ["10-03", "10-04", "10-05"]

actuals:
  tokens: 31740
  tasks: 3
  commits: 3
  plan_head_before: 8ab4b1326a0e249bb5d83bc37876e20eab164228

tech-stack:
  added: []
  patterns:
    - "c_field_str(buf, field) — bounded CStr::from_bytes_until_nul read of every FFI option-string field; replaces CStr::from_ptr + unwrap_or(\"auto\")/filter(!empty) everywhere in anofox-fcst-ffi/src/lib.rs"
    - "build_core_options / build_core_options_exog — single shared FFI-to-core option-struct builder used by anofox_ts_forecast, anofox_ts_forecast_exog, anofox_ts_forecast_inspect, anofox_ts_forecast_explain"
    - "parse_order_triplet(param, s) / parse_seasonal_periods(s) — strict core-owned parsers (bracket-optional, comma-split, bounds-checked); FFI never re-implements parsing, only reads+delegates"
    - "ForecastResult.n_residuals as its OWN length field, never aliased to n_fitted — fitted and residuals can legitimately have different presence/length (GARCH: fitted=None, residuals=Some)"

key-files:
  created:
    - crates/anofox-fcst-core/tests/forecast_arima.rs
    - test/sql/ts_forecast_crate_delegation.test
  modified:
    - crates/anofox-fcst-core/src/forecast.rs
    - crates/anofox-fcst-core/src/lib.rs
    - crates/anofox-fcst-ffi/src/types.rs
    - crates/anofox-fcst-ffi/src/lib.rs
    - crates/anofox-fcst-ffi/tests/core_ffi_parity.rs
    - src/include/anofox_fcst_ffi.h
    - src/table_functions/ts_forecast.cpp
    - src/scalar_functions/ts_forecast_scalar.cpp
    - src/table_functions/ts_forecast_native.cpp
    - src/table_functions/ts_cv_forecast_native.cpp
    - src/aggregate_functions/ts_forecast_agg.cpp
    - validation/reference/fixtures/forecast_snapshot/point_forecasts.json
    - validation/reference/fixtures/r_forecast/simulated_series.json
    - validation/reference/r/gen_forecast_reference.R

key-decisions:
  - "ARIMA order encoding: NUL-terminated strings in [c_char; 32] fields (arima_order_str, seasonal_order_str), mirroring the seasonal_periods_str precedent — empty string is the unset sentinel, keeping ARIMA(0,0,0) expressible and the FFI struct additive (fields appended after ensemble_method)."
  - "seasonal_order=[0,0,0] is explicitly non-seasonal (D-08): forecast_arima runs a plain ARIMA::new even when the caller passes an all-zero seasonal_order, so only a GENUINELY non-zero seasonal order engages SARIMA or requires seasonal_period > 1."
  - "model_name reports ' with drift' (d+D==1, nonzero AICc-selected intercept) or ' with non-zero mean' (d+D==0, nonzero intercept) — read directly from ARIMA::intercept()/SARIMA::intercept(), never guessed from the order alone."
  - "forecast_arima_with_exog (D-10, out of scope for this plan) now calls forecast_auto_arima on the OLS residuals instead of the deleted fixed-coefficient stand-in — unblocks compilation without reintroducing a fake model; full native-exog routing is a later plan's scope."
  - "TsForecastAggOperation::Finalize (member-template dead code, never wired to the registered aggregate — confirmed via RegisterTsForecastAggFunction's actual callback list) is intentionally left untouched; only the live free-function TsForecastAggFinalize gained ARIMA/null-safety support."
  - "The additional n_residuals fix (discovered during 10-01, explicitly delegated to this plan): ForecastResult gets its own n_residuals field, independent of n_fitted. ts_forecast.cpp's three residuals reads were migrated from n_fitted to n_residuals; core_ffi_parity.rs's parity_garch/assert_full_parity now assert the FIXED behavior directly instead of documenting the old gap."

patterns-established:
  - "Verified-coincidence deviations (same class as 10-01's ADIDA/IMAPA/SES documentation): when a literal plan assertion turns out empirically false for an identified, non-regression, crate-internal reason, confirm by direct value/array inspection (not just assumption), adjust the assertion, and document the root cause inline in the test — never silently loosen a tolerance without the underlying reason on record."

requirements-completed: [FCST-04, FCST-06, FCST-01, FCST-02, FCST-03]

coverage:
  - id: D1
    description: "ARIMA requires arima_order=[p,d,q] and runs a real crate ARIMA/SARIMA fit (never the old fixed-coefficient AR(1) stand-in); seasonal_order engages SARIMA only when non-zero and seasonal_period > 1 is explicit; model_name always reports the fitted order (+drift/non-zero-mean suffix when the crate's own AICc comparison keeps a constant)"
    requirement: FCST-04
    verification:
      - kind: integration
        ref: "crates/anofox-fcst-ffi/tests/core_ffi_parity.rs#parity_arima,arima_airline_matches_r_through_ffi,parity_auto_arima"
        status: pass
      - kind: integration
        ref: "crates/anofox-fcst-core/tests/forecast_arima.rs#arima_airline_matches_r_fixture,arima_100_and_101_match_r_fixture,arima_without_order_errors_naming_auto_arima,seasonal_order_without_arima_order_errors,seasonal_order_nonzero_without_seasonal_period_errors,seasonal_period_without_seasonal_order_errors,arima_order_on_non_arima_model_is_invalid_parameter"
        status: pass
      - kind: integration
        ref: "test/sql/ts_forecast_crate_delegation.test (FCST-04 section, 7 assertions)"
        status: pass
    human_judgment: false
  - id: D2
    description: "FFI option-string parsing is strict for every entry point (anofox_ts_forecast, anofox_ts_forecast_exog, inspect, explain): invalid UTF-8 and malformed/unparsable values are InvalidInput errors, never a silent 'auto' fallback or a silently-dropped list entry"
    requirement: FCST-06
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-ffi/src/lib.rs#strict_option_parsing_ffi_tests::invalid_utf8_model_name_is_invalid_input_not_auto_fallback,malformed_seasonal_periods_entry_is_invalid_input"
        status: pass
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/forecast_arima.rs#parse_order_triplet_valid_inputs,parse_order_triplet_invalid_inputs,parse_seasonal_periods_valid_inputs,parse_seasonal_periods_invalid_inputs"
        status: pass
    human_judgment: false
  - id: D3
    description: "ts_forecast_by Naive 95% step-1 half-width is within the crate's documented z-approximation bound of R's 66.07; the 0.80/0.95 half-width ratio is within 1e-3 relative of R's exact ratio; _ts_forecast MSE differs across SES/Holt/AutoARIMA/Theta/CrostonClassic except two verified, documented crate-internal coincidences (Theta/CrostonClassic both reduce to SES(alpha=0.1) in-sample on this series; SES's own real MSE happens to be ~2090.284) — asserted by a natively-run sqllogictest against the built extension"
    requirement: "FCST-01, FCST-02, FCST-03"
    verification:
      - kind: e2e
        ref: "test/sql/ts_forecast_crate_delegation.test (FCST-01/02/03 sections, 7 assertions) — run via ./build/release/test/unittest against a RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release build"
        status: pass
    human_judgment: false
  - id: D4
    description: "ts_forecast_agg and ts_cv_forecast_by emit SQL NULL (no crash) when a model returns no bounds; fitted-values NaN entries (crate warmup/trimmed positions) are marked SQL NULL rather than surfaced as a literal NaN double"
    verification:
      - kind: manual_procedural
        ref: "Manual duckdb CLI verification: anofox_fcst_ts_forecast_agg with arima_order/seasonal_order/seasonal_period reproduces the same seasonal ARIMA fit and model_name as ts_forecast_by end to end. The null-pointer-guard code path itself (ts_forecast_agg.cpp lower/upper list writers) is defensive — per 10-01's research, no model among the 36 currently returns null bounds, so this guards a future crate regression, not a path exercised by today's models. No automated SQL test covers this directly because every ts_forecast_agg.test-family file requires `json` (unavailable in this sandbox)."
    human_judgment: true
    rationale: "The agg-specific null-safety code path has no model today that actually triggers it (confirmed in 10-01's research), and the only automated harness for ts_forecast_agg.cpp (test/sql/ts_forecast_agg.test and siblings) requires the json extension, which this sandbox cannot load — so this deliverable is proven by direct code inspection + a manual CLI smoke test, not an automated regression test. A human (or a later plan with json available) should confirm the agg null-safety path once a model that returns NULL bounds exists, or once `make test_release` can run with json."
duration: ~2h (single session)
completed: 2026-10-09
status: complete
---

# Phase 10 Plan 02: ARIMA/SARIMA Crate Delegation + Strict FFI Parsing + SQL Proof Summary

**ARIMA/SARIMA now runs a real crate fit (never the old AR(1)-coefficient-0.5 stand-in) driven by new `arima_order`/`seasonal_order` SQL parameters, every FFI option string is strictly parsed (no UTF-8-to-"auto" or silently-dropped seasonal periods), a pre-existing `ForecastResult.n_fitted`/`residuals` length-sharing bug is fixed with an additive `n_residuals` field, and a natively-run sqllogictest pins FCST-01/02/03/04 against the built extension.**

## Performance

- **Duration:** ~2h (single session)
- **Tasks:** 3 (all completed as planned, no checkpoints)
- **Files modified:** 14 (11 modified, 2 created, 1 fixture re-blessed)

## Accomplishments

- Replaced `forecast_arima`'s fixed-coefficient AR(1)-on-differenced-series stand-in with a real `ARIMA::new(p,d,q)` / `SARIMA::new(p,d,q,P,D,Q,s)` fit; `ARIMA` without an explicit `arima_order` is now a hard SQL error naming `AutoARIMA` — never a silent fallback to Naive or a mislabeled different model.
- Added `arima_order`/`seasonal_order` as new strict-parsed `Option<[usize; 3]>` fields on `ForecastOptions`/`ForecastOptionsExog` (core), `[c_char; 32]` string fields on the FFI structs (additive ABI), and SQL `MAP` keys on every C++ forecast entry point (`ts_forecast_by`/`_ts_forecast_native`, `ts_cv_forecast_by`, `ts_forecast_agg`).
- `parse_order_triplet`/`parse_seasonal_periods` (core) and a new `c_field_str` (FFI) make every option-string parse strict: malformed `arima_order`, out-of-range orders, non-numeric `seasonal_periods` entries, and invalid-UTF-8 model names are all `InvalidInput`/`InvalidParameter` errors — the FFI's old silent `"auto"` fallback and silently-dropped `seasonal_periods` entries are gone (`build_core_options`/`build_core_options_exog` are now the single shared parsing path for all four FFI forecast entry points).
- Fixed a pre-existing FFI correctness gap discovered during 10-01 (explicitly reserved for this plan): `ForecastResult` shared one `n_fitted` length field between `fitted_values` and `residuals`, silently truncating residuals to empty whenever a crate model's `fitted_values()` was `None` but `residuals()` was `Some` (GARCH, always, by crate design). Added an additive `n_residuals` field, migrated `ts_forecast.cpp`'s three residuals reads, and flipped `core_ffi_parity.rs`'s `parity_garch`/`assert_full_parity` from documenting the gap to asserting the fix.
- Added new R Arima() reference fixtures: the existing seasonal airline model (0,1,1)(0,1,1)[12] plus two new non-seasonal (d=0) orders on a deterministic level-10 AR(1) series (`simulated_series.json#series.ar1_l10.models.{arima_100,arima_101}`) — `air_passengers.json` stays byte-identical, `rw`/`inter` subtrees untouched.
- `ts_cv_forecast_native.cpp` and `ts_forecast_agg.cpp` no longer dereference null `lower_bounds`/`upper_bounds` pointers (D-02): both now emit SQL NULL for a model with no native intervals, matching the NaN-sentinel convention already established in `ts_forecast_native.cpp`.
- New `test/sql/ts_forecast_crate_delegation.test` — runs natively (no `require json`) against the built extension and proves FCST-01 (Naive 95% half-width matches R within the crate's documented z-approximation bound), FCST-02 (0.80/0.95 half-width ratio matches R's exact ratio within 1e-3 relative), FCST-03 (MSE differs across model families, with two verified exceptions documented), and FCST-04 (ARIMA airline through `ts_forecast_by` matches R within 1%, exact `model_name`, plus four `statement error` cases for ARIMA misuse).

## Task Commits

Each task was committed atomically:

1. **Task 1: Tracer — arima_order/seasonal_order through FFI struct → strict parser → real crate SARIMA, matched to R airline through anofox_ts_forecast** — `e9478f2` (feat)
2. **Task 2: R Arima() references for d=0 orders, ARIMA validation tests, strict FFI option parsing, snapshot re-bless** — `d2ae461` (feat)
3. **Task 3: arima_order/seasonal_order in every C++ forecast entry point, NULL-safe bounds in agg/CV, build, natively-run SQL assertions for FCST-01..04** — `c8ab3f8` (feat)

**Plan metadata:** (this commit, immediately following)

## Files Created/Modified

- `crates/anofox-fcst-core/src/forecast.rs` — real ARIMA/SARIMA fit, `parse_order_triplet`/`parse_seasonal_periods`, ARIMA-only validation for `arima_order`/`seasonal_order`, `forecast_arima_with_exog`'s OLS-residual model switched to `forecast_auto_arima`.
- `crates/anofox-fcst-core/src/lib.rs` — re-exports the two new parsers.
- `crates/anofox-fcst-ffi/src/types.rs` — `ForecastOptions`/`ForecastOptionsExog.{arima_order_str,seasonal_order_str}`; `ForecastResult.n_residuals`.
- `crates/anofox-fcst-ffi/src/lib.rs` — `c_field_str`, `build_core_options`/`build_core_options_exog` shared builders, `n_residuals` writers, `anofox_ts_detrend`'s UTF-8 fix, new `strict_option_parsing_ffi_tests` module.
- `crates/anofox-fcst-ffi/tests/core_ffi_parity.rs` — `parity_arima` rewritten against the real crate model; new `arima_airline_matches_r_through_ffi`; `assert_full_parity`/`parity_garch` assert the fixed `n_residuals` behavior and the (separate, pre-existing, intentional) T-10-02 length-mismatch null-export for differenced ARIMA/AutoARIMA.
- `crates/anofox-fcst-core/tests/forecast_arima.rs` (new) — 11 tests: R parity (airline + two d=0 orders), every ARIMA misuse error, parser table tests.
- `src/include/anofox_fcst_ffi.h` — cbindgen-regenerated, fully additive (39 insertions, 0 deletions).
- `src/table_functions/ts_forecast.cpp` — 3 residuals reads migrated from `n_fitted` to `n_residuals`.
- `src/scalar_functions/ts_forecast_scalar.cpp`, `src/table_functions/ts_forecast_native.cpp`, `src/table_functions/ts_cv_forecast_native.cpp` — `arima_order`/`seasonal_order` MAP keys, bind data, opts fill.
- `src/table_functions/ts_cv_forecast_native.cpp` — null-safe bounds read + SQL NULL emission.
- `src/aggregate_functions/ts_forecast_agg.cpp` — `arima_order`/`seasonal_order`/`seasonal_period` state fields, null-safe lower/upper list writers, NaN-to-invalid fitted-list marking.
- `validation/reference/r/gen_forecast_reference.R`, `validation/reference/fixtures/r_forecast/simulated_series.json` — new `ar1_l10` series + `arima_100`/`arima_101` models.
- `validation/reference/fixtures/forecast_snapshot/point_forecasts.json` — re-blessed (air/rw/inter ARIMA flip ok→error; pre-existing Laplace/MFLES/AutoMFLES drift from the 0.15.3→0.15.10 bump also recaptured).
- `test/sql/ts_forecast_crate_delegation.test` (new) — 14 assertions, runs natively.

## Decisions Made

See frontmatter `key-decisions`. Most significant: the `n_residuals` fix (an "additional finding" explicitly delegated to this plan by 10-01) is scoped to the two live FFI writers (`anofox_ts_forecast`, `anofox_ts_forecast_exog`) plus the one live C++ consumer that actually read `n_fitted` for residuals (`ts_forecast.cpp`'s three call sites); `ts_forecast_agg.cpp` never read residuals via `n_fitted` (it doesn't expose residuals at all today), so no change was needed there for that specific bug — the agg file's changes in this plan are solely the new ARIMA params and the D-02 null-safety guard.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] `forecast_arima_with_exog` no longer compiled after `forecast_arima`'s signature changed**
- **Found during:** Task 1, first `cargo check` after rewriting `forecast_arima`
- **Issue:** The old hand-rolled exog path called `forecast_arima(&residuals, horizon)` (2 args) to fit a model on OLS residuals; `forecast_arima` now requires 6 args including a mandatory `arima_order`, which this call site has no way to supply.
- **Fix:** Switched the call to `forecast_auto_arima(&residuals, horizon, 0, 0.95)` — AutoARIMA's own order search needs no hand-picked order, and this function's output never surfaces the residual model's own level/interval fields (hardcoded empty `lower`/`upper`), so the fixed `level`/`period` arguments are inert for output purposes. Native crate exog routing (D-10) replaces this whole hack in a later plan.
- **Files modified:** `crates/anofox-fcst-core/src/forecast.rs`
- **Committed in:** `e9478f2` (Task 1 commit)

**2. [Rule 1 - Bug/test-design, verified non-regression] `test_forecast_arima` and `test_auto_arima_different_from_simple_arima` asserted the old fake behavior**
- **Found during:** Task 1, `cargo test -p anofox-fcst-core --lib`
- **Issue:** Two inline tests in `forecast.rs` called `ARIMA` without an order (now a hard error) or asserted the exact bare model_name `"ARIMA"` (now `"ARIMA(p,d,q)"` plus a possible drift/non-zero-mean suffix).
- **Fix:** Added an explicit `arima_order: Some([1,1,1])`; relaxed the exact-name assertions to `starts_with`; added a new `test_forecast_arima_without_order_errors` test covering the no-order error path; excluded `ModelType::ARIMA` from `test_all_model_names_match_enum`'s exact-match case list with an inline comment (it's covered separately).
- **Files modified:** `crates/anofox-fcst-core/src/forecast.rs`
- **Verification:** `cargo test -p anofox-fcst-core --lib` 241/241 pass.
- **Committed in:** `e9478f2` (Task 1 commit)

**3. [Rule 1 - Test-design, verified non-regression] FCST-03's literal "5 distinct MSE" and "no mse within 0.01 of 2090.28" assertions were empirically false for two identified, non-regression reasons**
- **Found during:** Task 3, first run of `test/sql/ts_forecast_crate_delegation.test` against the built extension
- **Issue (a):** `Theta` and `CrostonClassic` produce bit-identical MSE (and, confirmed by direct residual-array inspection, bit-identical residuals) on AirPassengers: `anofox-forecast`'s `Theta::fit` computes in-sample fitted/residuals from the plain SES(alpha=0.1) level component only (the theta-drift term is applied solely to the h-step-ahead point forecast, never to in-sample fitted), and Croston's default alpha is also 0.1; on AirPassengers (strictly positive, no zero-demand gaps) Croston's demand-interval component degenerates to interval=1 always, making it plain SES(0.1) too — a genuine crate-internal design coincidence for this specific series.
  **Issue (b):** SES's own real MSE (~2090.284) is coincidentally within 0.01 of the OLD universal SES(alpha=0.3)-stand-in constant 2090.28 — SES genuinely IS `SimpleExponentialSmoothing::new(0.3)`, so 2090.28 was never a fabrication unique to SES (same root-cause class as 10-01's documented deviation).
- **Fix:** Adjusted the distinctness assertion from 5 to 4 (with Theta/CrostonClassic's coincidence documented inline); excluded SES from the "no mse within 0.01" check (with the coincidence documented inline), matching 10-01's established "verified, non-regression, documented" deviation pattern exactly.
- **Files modified:** `test/sql/ts_forecast_crate_delegation.test`
- **Verification:** All 14 assertions pass; each deviation is independently reproducible (residual arrays and raw MSE values inspected directly via `./build/release/duckdb`).
- **Committed in:** `c8ab3f8` (Task 3 commit)

**4. [Rule 1 - Bug/test-design, verified non-regression] FCST-01/02's literal "< 1e-6"/"< 1e-9" tolerances were unrealistic given the crate's documented z-approximation (10-01 finding)**
- **Found during:** Task 3, drafting the SQL test before running it (cross-checked against 10-01's own `naive_intervals_match_r_naive_through_ffi` tolerance of 0.1 absolute and `z_is_exact_inverse_normal`'s 1e-3 relative tolerance)
- **Issue:** The plan's literal `abs(... - 66.07118560930098) < 1e-6` and ratio-within-`1e-9` would almost certainly fail: `anofox-forecast` 0.15.10's `Naive::predict_with_intervals` z comes from a private Abramowitz & Stegun 26.2.23 approximation, not an exact inverse-normal CDF — the same documented root cause 10-01 already bounded at ≤0.1 absolute / ~1e-3 relative for this exact model/series.
- **Fix:** Used the same tolerances 10-01 already established and verified for this exact quantity (0.1 absolute for the half-width, 1e-3 relative for the ratio) rather than the plan's literal, untested values — both are still two to three orders of magnitude tighter than the old ~234/~168 fabrication would have allowed, so the test still meaningfully proves FCST-01/02.
- **Files modified:** `test/sql/ts_forecast_crate_delegation.test`
- **Verification:** Both assertions pass against the built extension.
- **Committed in:** `c8ab3f8` (Task 3 commit)

---

**Total deviations:** 4 auto-fixed (1 Rule 3 blocking compile error, 3 Rule 1 test-design adjustments — all verified non-regression against direct value inspection, matching 10-01's established documentation pattern)
**Impact on plan:** The Rule 3 fix was necessary to unblock compilation without reintroducing a fixed-coefficient model. All three Rule 1 test-design deviations are documented, reproducible corrections to literal numeric claims that turned out to be either based on a since-superseded assumption (D-07's unused `confidence_level` column reasoning doesn't apply here) or empirically inaccurate at plan-authoring time — no scope creep, no silent tolerance-loosening without the underlying cause on record.

## Issues Encountered

- **`TsForecastAggOperation::Finalize`/`Combine` (member-template dead code) exist in `ts_forecast_agg.cpp` but are never wired to the registered aggregate** — confirmed by reading `RegisterTsForecastAggFunction`'s actual callback arguments (`TsForecastAggUpdate`/`TsForecastAggCombine`/`TsForecastAggFinalize`, all separate free functions). The plan's read_first pointed at "both Finalize paths ~130-190 and ~330-560"; only the second (the live free function) needed ARIMA/null-safety support. Documented as a decision, not fixed (out of scope — it's pre-existing dead code, unrelated to this plan's goal).
- **No automated SQL regression coverage for `ts_forecast_agg`'s null-safety path** — every `test/sql/ts_forecast_agg*.test`-family file requires the `json` extension, which this sandbox cannot load. Verified manually via the `./build/release/duckdb` CLI instead (see coverage D4); flagged for `human_judgment: true` in the coverage block so a future verifier with `json` available (or a human) can confirm.

## Known Stubs

None — all deliverables are fully wired; no placeholder/mock data paths were introduced.

## Threat Flags

None beyond what the plan's own `<threat_model>` already covers (T-10-04 through T-10-07, all mitigated per the plan's design: bounds-checked `parse_order_triplet`, bounded `c_field_str` reads, `catch_unwind` already wrapping every FFI entry point, null-checked bounds writers).

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- Plan 10-03/10-04 can proceed: `arima_order`/`seasonal_order` are available on every forecast entry point (core, FFI, and all four C++ surfaces), and the strict `c_field_str`/`parse_order_triplet`/`parse_seasonal_periods` parsing pattern is established for any new option fields those plans add.
- `cargo test --workspace`, `cargo fmt --all -- --check`, and `cargo clippy --all-targets --all-features -- -D warnings` are all green on `HEAD` (`c8ab3f8`) — clippy/fmt verified against the **default/stable toolchain** (matching the "Rust Tests" CI job in `.github/workflows/MainDistributionPipeline.yml`, which uses `dtolnay/rust-toolchain@stable`, not the 1.89 MSRV pin that only applies to the extension build itself; the 1.89 toolchain's clippy rejects two lint names — `clippy::manual_is_multiple_of` — that only exist in newer clippy versions, a pre-existing, unrelated environment mismatch confirmed present at `HEAD~3` before this plan's changes).
- `RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release` succeeds; `./build/release/test/unittest "test/sql/ts_forecast_crate_delegation.test"` passes (14/14 assertions).
- Plan 10-05 (migrating the 26 other `test/sql/ts_forecast*.test`-family files' stale ARIMA/interval expectations) has a confirmed, isolated starting point: `test/sql/ts_native_model_names.test` fails exactly once, at the `_ts_forecast_native(..., 'ARIMA', MAP{})` call expecting the bare model_name `"ARIMA"` — the single, expected, deliberate consequence of D-09.

---
*Phase: 10-forecast-wrapper-delegates-to-crate-models*
*Completed: 2026-10-09*

## Self-Check: PASSED

All claimed files found on disk (`forecast.rs`, `types.rs`, `lib.rs`, `core_ffi_parity.rs`, `forecast_arima.rs`, `anofox_fcst_ffi.h`, `ts_forecast.cpp`, `ts_forecast_scalar.cpp`, `ts_forecast_native.cpp`, `ts_cv_forecast_native.cpp`, `ts_forecast_agg.cpp`, `gen_forecast_reference.R`, `simulated_series.json`, `point_forecasts.json`, `ts_forecast_crate_delegation.test`, this SUMMARY) and all claimed commits (`e9478f2`, `d2ae461`, `c8ab3f8`) found in `git log --oneline --all`.
