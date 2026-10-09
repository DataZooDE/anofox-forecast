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
- **Status:** open — not fixed in this plan.
