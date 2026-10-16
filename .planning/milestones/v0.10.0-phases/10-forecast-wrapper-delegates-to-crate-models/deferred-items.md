# Deferred Items — Phase 10 (forecast-wrapper-delegates-to-crate-models)

Out-of-scope discoveries logged per the executor's scope-boundary rule
("only auto-fix issues DIRECTLY caused by the current task's changes").

## 1. `test/sql/ts_model_distinctness.test` SeasonalES/SeasonalESOptimized expected values are stale (pre-existing, from Plan 10-03)

- **Discovered during:** Plan 10-04 Task 3, running the full `test/sql/*.test` suite as part of the build DoD.
- **Symptom:** The "Exponential smoothing: Optimized variants" section asserts
  `seasonal_es = 14.451866` and `seasonal_es_opt = 18.803254` on the 24-point
  `distinctness_ts` fixture; the built extension now returns `12.304672` and
  `16.998999`.
- **Root cause (not this plan):** Plan 10-03 Task 2 (commit `e01e87b`,
  "every silent fallback becomes an error or reported identity") changed
  `resolve_period`/`requires_seasonal_period` so `SeasonalES`/
  `SeasonalESOptimized` now auto-detect their seasonal period via
  `detect_seasonality` REGARDLESS of `auto_detect_seasonality` — previously
  `_ts_forecast(y, horizon, 'SeasonalES')`'s hard-coded
  `auto_detect_seasonality = false` meant no period was ever detected for
  these two models through this entry point. `test/sql/
  ts_model_distinctness.test` was not in Plan 10-03's `files_modified` list
  and was never re-run/re-blessed after that change landed — this is the
  first full-suite run since.
- **Why deferred, not fixed here:** Plan 10-04's scope is the exogenous
  forecasting path (D-10/D-12, FCST-05/08); this file exercises the
  non-exog `_ts_forecast(values, horizon, model)` 3-arg overload, which
  Plan 10-04 does not modify (only its `fitted`/`residuals` list emission
  helper changed from `SetListFromArray` to `SetListFromArrayNanSafe` —
  verified NOT the cause: `point[1]` comes from `SetListFromArray`, field 0,
  untouched logic). The regression is traceable to Plan 10-03, a prior,
  already-committed plan.
- **Verification that it predates this plan:** `git log --oneline -- test/sql/ts_model_distinctness.test`
  shows the file's last edit (`fd2203d`) predates Plan 10-03's `resolve_period`
  change (`e01e87b`); `_ts_forecast(y, 3, 'SeasonalES')` with the explicit
  `auto_detect_seasonality=false` hard-coded in `TsForecastWithModelFunction`
  (unchanged by any Plan 10-04 commit) now still auto-detects because
  `resolve_period` forces it for `requires_seasonal_period` models since
  10-03 — the new values are the CORRECT consequence of 10-03's intentional
  change, just never re-blessed in this specific fixture.
- **Suggested fix (for whichever plan owns this):** Re-bless the two
  expected values in `test/sql/ts_model_distinctness.test`'s "Exponential
  smoothing: Optimized variants" section to `12.304672`/`16.998999` (or
  pass an explicit `seasonal_period` to pin the test to a period
  independent of `detect_seasonality`'s result). The `all_distinct`
  assertion logic itself still holds (`1`).
- **Status:** acknowledged

## 2. Theta and CrostonClassic produce identical in-sample fitted arrays and MSE on non-seasonal, no-zero series

- **Discovered during:** 10-VERIFICATION.md (coincidental_reliance item),
  confirmed/pinned in Plan 10-10 Task 2.
- **Root cause:** `anofox-forecast` 0.15.10 fixes `Theta`'s STM alpha at
  0.1 (`Theta::new()`, never optimised) and `Croston::new()`'s alpha at
  0.1 too. On a non-seasonal, no-zero-demand series, Theta's in-sample
  `fit()` loop computes `fitted[i] = level` (the SES level BEFORE the
  drift term `predict_internal`/`extract_forecast` adds for OUT-of-sample
  forecasts), and Croston-classic's `compute_fitted()` reduces to the same
  SES(0.1) recursion because every point is a demand (`interval_level`
  stays exactly 1.0, so `demand_level / interval_level == demand_level`).
  The two models' forecasts still diverge (Theta adds the drift term;
  Croston does not) — only the IN-SAMPLE fitted path coincides.
- **Evidence:** `crates/anofox-fcst-core/tests/theta_croston_fitted.rs` —
  `theta_and_croston_classic_fitted_are_both_ses_alpha_0_1_on_positive_series`
  (bitwise-equal fitted arrays + equal MSE despite diverging point
  forecasts, matched against an independent SES(0.1) recursion),
  `wrapper_fitted_is_the_crate_models_own_fitted_values` (proves
  `forecast.rs`'s wrapper output is exactly each model's own crate
  `fitted_values()` — no shared helper, no aliasing bug), and
  `croston_fitted_diverges_from_theta_on_intermittent_series` (proves the
  coincidence is specific to no-zero series, not a universal aliasing).
  All three pass against crate 0.15.10 (confirmed genuine crate behaviour,
  not a wrapper dispatch bug).
- **Why deferred:** This is upstream crate calibration behaviour
  (`~/projects/rust/anofox-forecast`), not a wrapper defect this phase can
  fix. Routed to Phase 11 UPST-04 ("Theta (STM) estimates alpha by SES
  optimisation") — once Theta's alpha is no longer fixed at 0.1, the first
  test above will need updating (not silent deletion) to reflect the new
  behaviour. Phase 11 should also confirm the in-sample fitted convention
  against R `thetaf` / statsforecast before Theta's fitted/residuals are
  relied on for anything calibration-sensitive (e.g. conformal intervals).
- **Suggested fix:** Phase 11 — once Theta's alpha is optimised rather
  than fixed, update `theta_and_croston_classic_fitted_are_both_ses_alpha_
  0_1_on_positive_series` (and its independent-recursion comparison) to
  match the new behaviour.
- **Status:** acknowledged

## 3. Unparsable numeric params values silently fall back to defaults on every MAP-param surface

- **Discovered during:** Plan 10-10 Task 1, while consolidating the
  per-key parse helpers into `src/include/ts_forecast_params.hpp`.
- **Symptom:** A params MAP value like `garch_p: 'two'`,
  `laplace_seasonal_batch_init: 'true'`, or `confidence_level: 'abc'` that
  fails `std::stoll`/`std::stod` silently falls back to the parameter's
  default instead of raising an SQL error — on every surface that reads
  the params MAP (`ts_forecast_by` via `_ts_forecast_scalar`,
  `ts_forecast_agg`, `_ts_forecast_native`, `ts_cv_forecast_by`, and the
  exog path).
- **Root cause:** `ForecastParamInt64`/`ForecastParamDouble` (and their
  pre-10-10 private equivalents on every surface) wrap `std::stoll`/
  `std::stod` in `catch (...) { return default_val; }` — this predates
  this phase and was never part of FCST-06's enumerated silent-fallback
  list (that list is about unrecognized MODEL/parameter VALUES like a bad
  `kalman_model`, not malformed numeric strings).
- **Why deferred:** Out of FCST-06's enumerated scope; Plan 10-10 kept
  this leniency unchanged on both `ts_forecast_by` and `ts_forecast_agg`
  specifically so the two surfaces stay provably identical (parity is
  10-10's contract) rather than fixing it on only one surface.
- **Suggested fix:** Make `ForecastParamInt64`/`ForecastParamDouble` in
  `src/include/ts_forecast_params.hpp` raise `InvalidInputException` on a
  parse failure instead of silently defaulting, then mirror the fix in the
  three remaining private copies (`ts_forecast_native.cpp`,
  `ts_cv_forecast_native.cpp`, `ts_forecast.cpp`'s exog path). Needs a
  decision on accepted boolean spellings for
  `laplace_seasonal_batch_init` (currently any non-empty, non-zero-parsing
  string is truthy via the `!= 0` int64 coercion).
- **Status:** acknowledged

## 4. docs/api/00-api-design.md lists stale ts_forecast_agg STRUCT field names

- **Discovered during:** Plan 10-10 Task 3 doc-accuracy pass.
- **Symptom:** The "Aggregate Functions" block in
  `docs/api/00-api-design.md` lists `ts_forecast_agg`'s STRUCT fields as
  `yhat[]`/`yhat_lower[]`/`yhat_upper[]`; the real fields are
  `point_forecast` / `lower_<pct>` / `upper_<pct>` (see
  `GetForecastAggResultType` in `src/aggregate_functions/ts_forecast_agg.cpp`).
- **Why deferred:** Pre-existing doc inaccuracy outside this plan's
  `files_modified` list (`docs/api/00-api-design.md` is not one of 10-10's
  touched files); 10-10 Task 3 corrects the `ts_forecast_agg`-specific
  claims in `docs/api/07-forecasting.md` and the skills/reference pages,
  but a full audit of `00-api-design.md` is out of scope here.
- **Suggested fix:** Update `docs/api/00-api-design.md`'s Aggregate
  Functions block to the real field names/signature.
- **Status:** acknowledged

## 5. json-gated agg test files expect pre-Phase-10 (and now pre-10-10) behaviour

- **Discovered during:** Plan 10-10 Task 1/3 full-suite run.
- **Symptom:** `test/sql/ts_forecast_agg.test` and the other `require
  json` forecast test files still expect pre-Phase-10 agg model names
  (e.g. `'ARIMA'` without `arima_order` succeeding instead of erroring) and
  now, after 10-10, also the pre-10-10 `error_message`-only behaviour for
  invalid model/parameter values. They never run in this environment
  because the `json` extension is not built here (confirmed pre-existing —
  see entry below and 10-VERIFICATION.md's Build/Test Verification
  section), so this is latent test debt, not a live regression.
- **Why deferred:** Fixing requires either building with `json` enabled
  (an environment change outside this plan's scope) or rewriting these
  files' expectations blind, without being able to run them to confirm the
  fix — both out of scope for a gap-closure plan.
- **Suggested fix:** When `json` is next enabled in this environment (or
  in CI), re-run these files, re-bless the `ARIMA`/invalid-parameter
  expectations to match 10-02's and 10-10's error-raising behaviour, and
  remove the `require json` gate if it is no longer actually needed by the
  file's own assertions.
- **Status:** acknowledged
