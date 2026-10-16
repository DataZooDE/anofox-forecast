---
phase: 12-statistics-marshalling-moves-to-rust
plan: 05
subsystem: forecasting
tags: [duckdb-extension, rust-ffi, chrono, calendar-arithmetic, null-handling, ts_forecast_agg, ts_forecast_by]

# Dependency graph
requires:
  - phase: 12-04
    provides: prior statistics-marshalling-to-Rust groundwork in this phase
provides:
  - "crates/anofox-fcst-core/src/calendar.rs: CalendarStep, detect_step, future_dates (D-12 month-end/clamp rule)"
  - "deterministic detect_frequency (tie-break by smaller interval, no HashMap iteration-order dependence)"
  - "anofox_ts_future_dates FFI + src/include/ts_future_dates.hpp (StepFromFrequency, ComputeFutureDates)"
  - "ts_forecast_agg: validity-passing NULL policy (D-11) + Rust calendar forecast dates (D-12)"
  - "ts_forecast_by scalar path: same Rust calendar rule as ts_forecast_agg (D-11 date parity)"
  - "test/sql/ts_forecast_calendar.test: non-json-gated native SQL proof (21 assertions)"
affects: [12-06, 13]

actuals:
  tokens: 21023
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Calendar stepping computed once in Rust (chrono checked_add_months + last-day-of-month clamp), consumed by C++ via one FFI call instead of C++ doing month arithmetic per surface"
    - "Validity vector threaded alongside values/timestamps through Update/Combine/Finalize in an aggregate state, mirroring the Phase 10 forecast NULL policy instead of splicing rows pre-fit"

key-files:
  created:
    - crates/anofox-fcst-core/src/calendar.rs
    - src/include/ts_future_dates.hpp
    - crates/anofox-fcst-ffi/tests/calendar_ffi.rs
    - test/sql/ts_forecast_calendar.test
    - .planning/phases/12-statistics-marshalling-moves-to-rust/snippets/pass-list-12-05.tsv
  modified:
    - crates/anofox-fcst-core/src/gaps.rs
    - crates/anofox-fcst-core/src/lib.rs
    - crates/anofox-fcst-ffi/src/lib.rs
    - src/include/anofox_fcst_ffi.h
    - src/aggregate_functions/ts_forecast_agg.cpp
    - src/scalar_functions/ts_forecast_scalar.cpp
    - docs/api/07-forecasting.md
    - docs/api/22-supported-frequencies.md
    - plugins/anofox-forecast/skills/anofox-forecast-models/SKILL.md
    - CHANGELOG.md
    - examples/data_preparation/null_handling_examples.sql

key-decisions:
  - "Calendar rule (D-12) lives entirely in Rust (calendar.rs); C++ only maps a frequency spec to (step_months, step_micros) via StepFromFrequency and copies the returned dates — no month arithmetic left in ts_forecast_scalar.cpp or ts_forecast_agg.cpp"
  - "detect_frequency's tie-break changed from HashMap::into_iter().max_by_key (nondeterministic) to an explicit (count, Reverse(diff)) ordering that always picks the smaller interval on a tie"
  - "ts_forecast_agg only drops a row for a NULL timestamp now; a NULL value is kept as 0.0/valid=false and handed to the same Phase 10 forecasting NULL policy (trim/interpolate/anchor) ts_forecast_by already uses — no more pre-fit splicing"
  - "ts_forecast_by's scalar path absorbs the same D-12 rule; reversibility rated costly (published forecast dates change for month-end series ending in a short month) but accepted as implementing the user's own D-12 decision for D-11 parity"

patterns-established:
  - "Calendar-aware date stepping: month-end preservation if every observed date is a month-end, otherwise clamp the last observed day-of-month to each target month's length; quarterly/yearly reuse the same function with 3-/12-month steps; each step computed fresh from the last observation, never cumulatively"

requirements-completed: [RUST-08]

coverage:
  - id: D1
    description: "ts_forecast_agg on month-end monthly data (2023-01-31..2023-11-30) emits forecast_timestamp 2023-12-31, 2024-01-31, 2024-02-29 with no day drift (D-12)"
    requirement: "RUST-08"
    verification:
      - kind: integration
        ref: "test/sql/ts_forecast_calendar.test (me fixture, lines 93-104)"
        status: pass
      - kind: integration
        ref: "examples/data_preparation/null_handling_examples.sql SECTION 5 (calendar_month_end_series_agg/by_forecasts_month_ends_no_drift)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Calendar stepping rule (D-12): non-month-end series keep the last observed day-of-month clamped to the target month's length; quarterly and yearly use the same rule with 3-/12-month steps"
    requirement: "RUST-08"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/calendar.rs future_dates tests (d30 clamp, quarter-end, year-end, mid-month cases)"
        status: pass
      - kind: integration
        ref: "test/sql/ts_forecast_calendar.test (d30, qe, ye, mid fixtures, lines 106-156)"
        status: pass
    human_judgment: false
  - id: D3
    description: "detect_frequency returns the same answer on tied step data across runs, breaking ties by the smaller interval"
    requirement: "RUST-08"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/src/gaps.rs tie-break tests (50-repeat determinism)"
        status: pass
    human_judgment: false
  - id: D4
    description: "ts_forecast_agg passes validity instead of dropping NULL-valued rows; trailing_null_demo forecasts from 2020-02-02 with yhat=32.0 exactly like ts_forecast_by, and interior-NULL series match ts_forecast_by's point forecasts"
    requirement: "RUST-08"
    verification:
      - kind: integration
        ref: "test/sql/ts_forecast_calendar.test (trailing_null_demo lines 164-188, inull lines 196-228)"
        status: pass
      - kind: integration
        ref: "examples/data_preparation/null_handling_examples.sql SECTION 5 (calendar_agg_trailing_null_matches_by_forecast_origin_and_value)"
        status: pass
    human_judgment: false
  - id: D5
    description: "ts_forecast_by uses the same Rust calendar rule as ts_forecast_agg, so both agree on forecast dates (not just values) for the same series, including a realistic 12-step AutoETS AirPassengers fixture"
    requirement: "RUST-08"
    verification:
      - kind: integration
        ref: "test/sql/ts_forecast_calendar.test (air fixture, lines 230-250)"
        status: pass
    human_judgment: false
  - id: D6
    description: "Docs (07-forecasting.md, 22-supported-frequencies.md), the anofox-forecast-models skill, and CHANGELOG.md are updated to replace the stale Phase 12 TODO notes with the now-true aligned behavior"
    requirement: "RUST-08"
    verification:
      - kind: other
        ref: "grep -cE 'tracked for Phase 12|planned for v0.10.0, Phase 12' docs/api/07-forecasting.md plugins/anofox-forecast/skills/anofox-forecast-models/SKILL.md CHANGELOG.md -> 0"
        status: pass
      - kind: integration
        ref: ".planning/phases/12-statistics-marshalling-moves-to-rust/snippets/pass-list-12-05.tsv (snippet harness, 1 PASS / 0 FAIL)"
        status: pass
    human_judgment: false

duration: ~45min (Task 3 session; full plan spanned prior Task 1-2 commits earlier the same day)
completed: 2026-10-10
status: complete
---

# Phase 12 Plan 05: Rust Calendar Rule + Validity-Passing ts_forecast_agg Summary

**`ts_forecast_agg` now uses Rust frequency detection with calendar-aware month/quarter/year stepping and shares `ts_forecast_by`'s NULL policy, so both surfaces agree on dates and values for month-end/NULL-bearing series (D-11, D-12, RUST-08).**

## Performance

- **Duration:** Task 3 session ~45 min (review WIP, run DoD, commit, write SUMMARY); full plan (Tasks 1-3) spanned commits from 00:35 to 18:18 on 2026-10-10
- **Started:** 2026-10-10T00:35:54+02:00 (Task 1 commit)
- **Completed:** 2026-10-10T18:18:15+02:00 (Task 3 commit)
- **Tasks:** 3/3
- **Files modified:** 16 unique files across all three task commits

## Accomplishments

- New `crates/anofox-fcst-core/src/calendar.rs`: `CalendarStep::{Months, Fixed}`, `detect_step`, `future_dates` — the D-12 month-end-preserving / day-of-month-clamp rule, reusable later by Phase 13's month-end gap fill.
- `gaps.rs::detect_frequency`'s tied-mode selection is now deterministic (smaller interval wins), independent of hash-map iteration order.
- New FFI `anofox_ts_future_dates` (caller-allocated output buffer, `catch_unwind`, null/overflow checks) and header-only `src/include/ts_future_dates.hpp` (`StepFromFrequency`, `ComputeFutureDates`) replace all C++ month arithmetic in `ts_forecast_agg.cpp` and `ts_forecast_scalar.cpp`.
- `ts_forecast_agg` no longer splices NULL-valued rows out before fitting — it threads a `valid` bitmask through Update/Combine/Finalize, so the Phase 10 forecasting NULL policy (trim/interpolate/anchor) now applies to the aggregate path exactly as it already does for `ts_forecast_by`.
- `ts_forecast_by`'s scalar path adopted the same Rust calendar rule, giving `ts_forecast_agg`/`ts_forecast_by` date parity (not just value parity) on month-end, quarter-end, year-end, and NULL-bearing series.
- New `test/sql/ts_forecast_calendar.test` (no `require json`, 21 assertions): month-end (`me`), day-of-month clamp (`d30`), quarterly (`qe`), yearly (`ye`), mid-month (`mid`), trailing-NULL (`trailing_null_demo`), interior-NULL (`inull`, two model families), and a realistic 12-step AirPassengers (`air`) agg==by parity check.
- SECTION 5 appended to `examples/data_preparation/null_handling_examples.sql` reproduces the headline month-end and trailing-NULL parity cases as a runnable PASS/FAIL proof against the built extension.
- `docs/api/07-forecasting.md`, `docs/api/22-supported-frequencies.md`, the `anofox-forecast-models` skill, and `CHANGELOG.md` all replaced their stale "(tracked for Phase 12)" notes with the now-true aligned behavior.

## Task Commits

Each task was committed atomically:

1. **Task 1: Tracer — Rust calendar rule exposed as `anofox_ts_future_dates`, consumed by `ts_forecast_agg`** - `289b8d5` (feat)
2. **Task 2: `ts_forecast_agg` validity passing (D-11); `ts_forecast_by` shares the Rust calendar rule** - `42a46ea` (feat)
3. **Task 3: Build once, non-gated SQL proof, example SECTION 5, docs/skill/CHANGELOG updates** - `c1d06ac` (test)

_Note: Tasks 1-2 were committed in an earlier session the same day; this session reviewed their already-committed state, then completed and committed Task 3's working-tree WIP._

**Plan metadata:** pending (this SUMMARY + STATE/ROADMAP update commit follows)

## Files Created/Modified

- `crates/anofox-fcst-core/src/calendar.rs` - D-12 calendar rule: `CalendarStep`, `detect_step`, `future_dates`
- `crates/anofox-fcst-core/src/gaps.rs` - deterministic `detect_frequency` tie-break
- `crates/anofox-fcst-core/src/lib.rs` - `pub mod calendar` + re-exports
- `crates/anofox-fcst-ffi/src/lib.rs` - `anofox_ts_future_dates` FFI export
- `crates/anofox-fcst-ffi/tests/calendar_ffi.rs` - FFI-level auto-detect/explicit-step/error tests
- `src/include/anofox_fcst_ffi.h` - regenerated header (via `cargo test -p anofox-fcst-ffi`)
- `src/include/ts_future_dates.hpp` - `StepFromFrequency`, `ComputeFutureDates` (C++ marshalling)
- `src/aggregate_functions/ts_forecast_agg.cpp` - validity vector in Update/Combine/Finalize; `ComputeFutureDates` replaces the median-step block
- `src/scalar_functions/ts_forecast_scalar.cpp` - `ComputeForecastDate` month arithmetic deleted; `StepFromFrequency` + `ComputeFutureDates` used instead
- `test/sql/ts_forecast_calendar.test` - new non-json-gated native SQL value assertions
- `examples/data_preparation/null_handling_examples.sql` - SECTION 5 appended
- `docs/api/07-forecasting.md` - input-handling exception replaced; "Calendar stepping (D-12)" paragraph added
- `docs/api/22-supported-frequencies.md` - "Calendar Stepping for Forecast Dates (D-12)" section + runnable example
- `plugins/anofox-forecast/skills/anofox-forecast-models/SKILL.md` - Phase 12 TODO note replaced
- `CHANGELOG.md` - NULL-exception sentence removed; two Breaking-changes bullets + one Fixed bullet added
- `.planning/phases/12-statistics-marshalling-moves-to-rust/snippets/pass-list-12-05.tsv` - snippet harness pass-list (1 PASS, 0 FAIL)

## Decisions Made

- Calendar rule lives entirely in Rust; C++ is pure marshalling (`StepFromFrequency` maps a frequency spec to `(step_months, step_micros)`, `ComputeFutureDates` copies the FFI's output buffer) — no month arithmetic remains in either `.cpp` file.
- `detect_frequency` ties now break toward the smaller interval via an explicit `(count, Reverse(diff))` ordering, removing `HashMap` iteration-order dependence.
- `ts_forecast_agg` validity policy: only a NULL **timestamp** drops a row; a NULL **value** is kept (0.0, `valid=false`) and handed to the shared Phase 10 forecasting NULL policy — eliminating the old pre-fit splice that caused agg/by divergence on NULL-bearing series.
- `ts_forecast_by`'s date change was accepted as "costly but correct": published forecast dates change for month-end series whose last date falls in a short month, but this implements the user's own D-12 decision and is required for D-11 date parity between agg and by.

## Deviations from Plan

None - plan executed exactly as written. The only differences found when reviewing the Task 3 working tree were cosmetic `cargo fmt` reformatting of Task 1's already-committed Rust code (line-wrapping of `.ok_or_else` closures and multi-line `assert_eq!` calls in `calendar.rs`, `lib.rs` re-export ordering, and import-sort in `calendar_ffi.rs`) — no logic changes, confirmed by `cargo test --workspace` passing identically before and after.

## Issues Encountered

None. All DoD gates passed on the first run: `cargo fmt --all -- --check` (clean), `cargo clippy --all-targets --all-features -- -D warnings` (clean), `cargo test --workspace` (all suites green, including the 23 FFI `calendar_ffi` tests and `anofox-fcst-core` `calendar`/`gaps` unit tests), `RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release` (clean incremental rebuild), `ts_forecast_calendar.test`/`ts_forecast_crate_delegation.test`/`ts_list_null_policy.test` (all "All tests passed" natively), `null_handling_examples.sql` (ALL SECTIONS PASSED, 0 FAIL), the snippet harness (1 PASS, 0 FAIL across the three touched docs/skill files), and `make test_release` (All tests passed: 1531 assertions, 23 test cases, 50 require-json tests skipped as expected).

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- `src/include/ts_future_dates.hpp` and the D-12 calendar rule are ready for plan 12-06 to apply to the remaining C++ date steppers (ensemble/panel/var/native) and `ts_forecast_exog_by`.
- `calendar.rs::future_dates` is explicitly documented as reusable by Phase 13's month-end gap fill.
- No blockers. `ts_forecast_crate_delegation.test`'s pre-existing agg parity block (535-720) remains intact and green.

---
*Phase: 12-statistics-marshalling-moves-to-rust*
*Completed: 2026-10-10*

## Self-Check: PASSED

All claimed files found on disk (`calendar.rs`, `ts_future_dates.hpp`, `ts_forecast_calendar.test`, `pass-list-12-05.tsv`, this SUMMARY) and all three task commits (`289b8d5`, `42a46ea`, `c1d06ac`) confirmed in `git log --oneline --all`.
