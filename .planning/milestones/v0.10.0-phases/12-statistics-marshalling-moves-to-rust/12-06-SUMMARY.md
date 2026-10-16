---
phase: 12-statistics-marshalling-moves-to-rust
plan: 06
subsystem: forecasting
tags: [duckdb-extension, rust-ffi, chrono, calendar-arithmetic, ts_forecast_exog_by, ts_forecast_ensemble_by, ts_forecast_panel_by, ts_forecast_var_by]

# Dependency graph
requires:
  - phase: 12-05
    provides: "crates/anofox-fcst-core/src/calendar.rs (D-12 rule), anofox_ts_future_dates FFI, src/include/ts_future_dates.hpp (StepFromFrequency, ComputeFutureDates)"
provides:
  - "_ts_future_dates internal scalar (ts_forecast_scalar.cpp): dates LIST(TIMESTAMP), horizon INTEGER, step INTERVAL -> LIST(TIMESTAMP), backed by the Rust D-12 calendar rule"
  - "ts_forecast_exog_by future dates via _ts_future_dates (replaces the epoch-bounded generate_series)"
  - "ts_forecast_ensemble_by / ts_forecast_panel_by / ts_forecast_var_by / _ts_forecast_native on ComputeFutureDates — zero remaining C++ month arithmetic"
  - "test/sql/ts_forecast_calendar_surfaces.test: non-json-gated native SQL proof (15 assertions)"
affects: [13]

actuals:
  tokens: 11865
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "A SQL-reachable internal scalar (_ts_future_dates) exposes a Rust-backed calendar helper directly to a macro (ts_forecast_exog_by), the same pattern _ts_data_quality already used for ts_data_quality"
    - "Panel's two stepping sites use different `observed` anchors (per-series own dates for DROPPED rows; the shared date grid for the kept-series forecast) while sharing one (step_months, step_micros) pair computed once per Finalize call"

key-files:
  created:
    - test/sql/ts_forecast_calendar_surfaces.test
    - .planning/phases/12-statistics-marshalling-moves-to-rust/snippets/pass-list-12-06.tsv
  modified:
    - src/scalar_functions/ts_forecast_scalar.cpp
    - src/macros/ts_macros.cpp
    - src/table_functions/ts_forecast_ensemble_native.cpp
    - src/table_functions/ts_forecast_panel_native.cpp
    - src/table_functions/ts_forecast_var_native.cpp
    - src/table_functions/ts_forecast_native.cpp
    - examples/data_preparation/null_handling_examples.sql
    - docs/api/07-forecasting.md
    - CHANGELOG.md

key-decisions:
  - "_ts_future_dates registered inside the existing RegisterTsForecastScalarFunction (internal = true, like _ts_data_quality) instead of a new Register* function, so no extension.cpp/CMakeLists change was needed"
  - "ts_forecast_exog_by carries the observed date list (_dates = LIST(__exog_dt__::TIMESTAMP ORDER BY __exog_dt__)) through grouped_historical/forecast_data instead of a single last_date scalar, because _ts_future_dates needs every observed date (for frequency-agnostic stepping), not just the last one"
  - "Panel's shared-future-grid stepping site computes ComputeFutureDates(shared_grid, ...) ONCE outside the per-series loop (same base/step for every kept series) rather than once per series — a correctness-preserving optimization since the old code's last_grid_date anchor was already identical across all series"
  - "Ensemble now tracks date_is_null per list entry (mirroring ts_forecast_scalar.cpp's existing pattern) so the observed series passed to ComputeFutureDates never includes a NULL date disguised as epoch zero — panel/var/native already skipped NULL dates at row-ingestion time, so only ensemble needed this addition"

patterns-established:
  - "Comments describing removed C++ calendar arithmetic avoid literally spelling the grep-targeted strings (Date::MonthDays, require json) that the plan's own verification commands scan for, so a prose explanation doesn't accidentally fail the 'zero occurrences' check"

requirements-completed: [RUST-09, RUST-08]

coverage:
  - id: D1
    description: "ts_forecast_exog_by future dates are calendar-correct across months of different lengths — exactly len(point) dates, one per forecast_step, following the D-12 month-end/clamp rule, with no EXTRACT(EPOCH...) 30-day-month upper bound"
    requirement: "RUST-09"
    verification:
      - kind: integration
        ref: "examples/data_preparation/null_handling_examples.sql SECTION 6 (exog_by_future_dates_are_calendar_correct_month_ends)"
        status: pass
    human_judgment: false
  - id: D2
    description: "_ts_future_dates(LIST(TIMESTAMP), horizon, INTERVAL) returns Rust calendar dates: month-end input + INTERVAL '1 month' -> month-ends (incl. 2024-02-29 leap case); mixed-step INTERVAL is an error; NULL arguments -> NULL; NULL list entries are skipped"
    requirement: "RUST-09"
    verification:
      - kind: integration
        ref: "test/sql/ts_forecast_calendar_surfaces.test (_ts_future_dates block, lines 10-73)"
        status: pass
    human_judgment: false
  - id: D3
    description: "ts_forecast_ensemble_by, ts_forecast_panel_by, ts_forecast_var_by, and the macro-less _ts_forecast_native all compute monthly forecast dates with the same Rust rule as ts_forecast_by — every public forecast surface agrees on dates for the same input"
    requirement: "RUST-09"
    verification:
      - kind: integration
        ref: "test/sql/ts_forecast_calendar_surfaces.test (ensemble/panel/var blocks, lines 95-140)"
        status: pass
    human_judgment: false
  - id: D4
    description: "No C++ forecast date code computes month arithmetic any more — zero Date::MonthDays occurrences across the four native forecasters and ts_forecast_scalar.cpp"
    requirement: "RUST-09"
    verification:
      - kind: other
        ref: "grep -c Date::MonthDays across ts_forecast_{ensemble,panel,var}_native.cpp, ts_forecast_native.cpp, ts_forecast_scalar.cpp -> 0"
        status: pass
    human_judgment: false

duration: ~75min
completed: 2026-10-10
status: complete
---

# Phase 12 Plan 06: Calendar-Correct Dates on Every Remaining Forecast Surface Summary

**`ts_forecast_exog_by`'s future dates now come from a Rust-backed `_ts_future_dates` scalar instead of an epoch-bounded `generate_series`, and the ensemble/panel/VAR/native C++ forecasters all call the same `ComputeFutureDates` helper — zero C++ month arithmetic remains anywhere in the forecast date path (RUST-09).**

## Performance

- **Duration:** ~75 min (Task 1: 18:14-18:23, Task 2: 18:23-18:28, Task 3: 18:28-18:36, plus earlier reading/context time)
- **Started:** 2026-10-10 (Task 1 commit `e254ea9` at 18:23:35+02:00)
- **Completed:** 2026-10-10T18:36:30+02:00 (Task 3 commit `767be6e`)
- **Tasks:** 3/3
- **Files modified:** 10 unique files across all three task commits

## Accomplishments

- New internal scalar `_ts_future_dates(dates LIST(TIMESTAMP), horizon INTEGER, step INTERVAL) -> LIST(TIMESTAMP)` in `ts_forecast_scalar.cpp`, marshalling straight to `StepFromFrequency`/`ComputeFutureDates` (plan 12-05's Rust D-12 calendar rule) — registered `internal = true` the same way `_ts_data_quality` already is.
- `ts_forecast_exog_by` no longer builds future dates with `generate_series(last_date + interval, last_date + (horizon * EXTRACT(EPOCH FROM interval) || ' seconds')::INTERVAL, interval)` — the macro now carries the full observed date list (`_dates`) through `grouped_historical`/`forecast_data` and zips `UNNEST(_ts_future_dates(_dates, len((fcst).point)::INTEGER, _interval))` with the forecast arrays, guaranteeing exactly `len(point)` dates aligned 1:1 with `forecast_step` (RUST-09).
- `ts_forecast_ensemble_native.cpp`, `ts_forecast_panel_native.cpp` (both stepping sites — per-series DROPPED rows and the shared future grid), `ts_forecast_var_native.cpp`, and `ts_forecast_native.cpp` all replaced their own month/quarter/year arithmetic + `Date::MonthDays` clamp with `StepFromFrequency` + one `ComputeFutureDates` call. Ensemble additionally now tracks which date-list entries were NULL so its `observed` series never includes a NULL date disguised as epoch zero.
- New `test/sql/ts_forecast_calendar_surfaces.test` (no json dependency, 15 assertions): `_ts_future_dates` direct scalar proofs (month-end step including the 2024-02-29 leap case, first-of-month, quarter-end, daily step, NULL-entry skipping, NULL-argument -> NULL, `horizon <= 0` -> empty list, mixed-interval step -> error) plus `ts_forecast_ensemble_by`/`ts_forecast_panel_by`/`ts_forecast_var_by` on a shared month-end monthly fixture ending 2023-11-30 — all three land on 2023-12-31, 2024-01-31, 2024-02-29 for `'1mo'`/h=3.
- SECTION 6 appended to `examples/data_preparation/null_handling_examples.sql`: a 24-row month-end history + 12-row future regressor table through `ts_forecast_exog_by(..., '1mo', 'AutoARIMA', 12)` — proves exactly 12 rows, `forecast_step` 1..12, `date` = the k-th month-end after the last observation for every row, and no NULL `yhat`, run against the built extension (needs `LOAD json`, so this is the CLI proof; the SQL test file above covers the json-free surfaces natively).
- `docs/api/07-forecasting.md` and `CHANGELOG.md` updated to state that `ts_forecast_exog_by`/`ts_forecast_ensemble_by`/`ts_forecast_panel_by`/`ts_forecast_var_by` share the same Rust calendar rule as `ts_forecast_by`/`ts_forecast_agg`.

## Task Commits

Each task was committed atomically:

1. **Task 1: Tracer — `_ts_future_dates` scalar (Rust calendar rule) replaces `ts_forecast_exog_by`'s epoch-bounded `generate_series`** - `e254ea9` (feat)
2. **Task 2: Ensemble, panel, VAR and native forecasters use `ComputeFutureDates` instead of C++ month arithmetic** - `f31a16d` (feat)
3. **Task 3: Build once, SQL proof (non-gated) for `_ts_future_dates` + ensemble/panel/var dates, CLI proof for `ts_forecast_exog_by`, docs + CHANGELOG** - `767be6e` (test)

**Plan metadata:** no STATE.md/ROADMAP.md update in this commit sequence per the executor's explicit instruction (a concurrent Phase-11 executor owns those files this session) — SUMMARY.md committed separately.

## Files Created/Modified

- `src/scalar_functions/ts_forecast_scalar.cpp` - new `_ts_future_dates` internal scalar + registration
- `src/macros/ts_macros.cpp` - `ts_forecast_exog_by`'s `_dates`/`_ts_future_dates(...)` replace `last_date`/`generate_series(...)`
- `src/table_functions/ts_forecast_ensemble_native.cpp` - `date_is_null` tracking + `ComputeFutureDates` replaces the `compute_forecast_date` lambda
- `src/table_functions/ts_forecast_panel_native.cpp` - both stepping sites (DROPPED-row anchor on `grp.dates`; shared-grid anchor on `shared_grid`) on `ComputeFutureDates`
- `src/table_functions/ts_forecast_var_native.cpp` - `sorted_dates` + one `ComputeFutureDates` call replaces the inline month arithmetic
- `src/table_functions/ts_forecast_native.cpp` - `sorted_dates` + one `ComputeFutureDates` call per group replaces the inline month arithmetic
- `test/sql/ts_forecast_calendar_surfaces.test` - new non-json-gated native SQL value assertions
- `examples/data_preparation/null_handling_examples.sql` - SECTION 6 appended
- `docs/api/07-forecasting.md` - calendar-stepping paragraph + `ts_forecast_exog_by` section both updated
- `CHANGELOG.md` - Breaking-changes bullet extended to name all six surfaces; two new Fixed bullets
- `.planning/phases/12-statistics-marshalling-moves-to-rust/snippets/pass-list-12-06.tsv` - empty (0 PASS/0 FAIL — no fenced SQL block in the touched doc changed relative to BASE)

## Decisions Made

- `_ts_future_dates` lives inside the existing `RegisterTsForecastScalarFunction` (not a new `Register*` function) — no `extension.cpp`/`CMakeLists.txt` change needed, matching the plan's explicit instruction.
- `ts_forecast_exog_by` carries the full observed date list through the CTE chain instead of a single `last_date`, since the Rust calendar rule needs every observed date for month-end detection, not just the last one.
- Panel's shared-future-grid stepping site computes `ComputeFutureDates` once (outside the per-series loop) since every kept series shares the same base date and step — a correctness-preserving simplification over the old per-series-redundant-but-identical-result inline arithmetic.
- Comments explaining the removed C++ calendar code deliberately avoid literally spelling `Date::MonthDays` or `require json` as substrings, since the plan's own verification commands grep for exactly those strings across the whole file (including comments) — a prose explanation that happened to contain the target string would have falsely tripped the "zero occurrences" check.

## Deviations from Plan

None - plan executed exactly as written. Two self-corrections during Task 3 authoring (not deviations from the plan's intent, just getting the exact values right on first principles):
- The `_ts_future_dates` 12-monthly-dates assertion's expected "13 months after the first observation" arithmetic was off by one in the first draft (the fixture's last observed date is `2023-12-01`, not `2023-01-01`); corrected to assert `2024-12-01` (12 months after the last observation) and verified against the built extension before finalizing.
- The `Date::MonthDays`/`require json` comment-text self-collision above (not a logic bug — caught by re-running the plan's own grep verification commands, as instructed).

## Issues Encountered

None. All verification commands passed on their final run: `cargo fmt --all -- --check` (clean, no Rust changed), `RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release` (clean full rebuild, 112/112 → 15/15 incremental), `ts_forecast_calendar_surfaces.test`/`ts_forecast_calendar.test`/`ts_forecast_crate_delegation.test` (15/21/55 assertions, all "All tests passed" natively), `null_handling_examples.sql` (ALL SECTIONS PASSED including the new SECTION 6, 0 FAIL), the snippet harness (0 PASS/0 FAIL/exit 0 — no SQL block text changed), and `make test_release` (1546 assertions, 24 test cases, 50 require-json tests skipped as expected, all passed).

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Every public forecast surface (`ts_forecast_by`, `ts_forecast_agg`, `ts_forecast_exog_by`, `ts_forecast_ensemble_by`, `ts_forecast_panel_by`, `ts_forecast_var_by`, `_ts_forecast_native`) now shares the single Rust D-12 calendar rule (`crates/anofox-fcst-core/src/calendar.rs` via `StepFromFrequency`/`ComputeFutureDates`). No C++ forecast date code computes month arithmetic any more.
- `_ts_future_dates` is a reusable Rust-calendar-backed scalar available to any future macro needing the same (dates, horizon, step) -> future-dates shape.
- No blockers for plan 12-07/12-08.

---
*Phase: 12-statistics-marshalling-moves-to-rust*
*Completed: 2026-10-10*
