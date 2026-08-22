---
phase: 01-diagnostics-demand-classification
plan: 2
subsystem: diagnostics
status: complete
requirements: [STAT-02, STAT-03]
completed: 2026-08-21
---

# Phase 01 Plan 2: KPSS + Combined Stationarity Verdict

Delivered `ts_kpss` / `ts_kpss_by` (STAT-02) and `ts_stationarity` /
`ts_stationarity_by` (STAT-03) across all five layers, extending the 01-1
diagnostics scaffolding (no new infrastructure).

## What was built
- **Core** (`validation.rs`): `kpss()` (reuses `StationarityOut`), `classify_stationarity()`,
  `stationarity()` → `CombinedStationarityOut`. 5 new unit tests.
- **FFI**: `AnofoxCombinedStationarityResult` (`verdict: [c_char;32]`); `anofox_ts_kpss`
  (reuses `AnofoxStationarityResult`) and `anofox_ts_stationarity` exports.
- **C++** (`diagnostics.cpp`): `TsKpssFunction`, `TsStationarityFunction` (+ registrations,
  aliases). VARCHAR verdict via `StringVector::AddString`.
- **Macros**: `ts_kpss_by`, `ts_stationarity_by`.
- **Docs / example / cross-check**: `docs/api/10-diagnostics.md` KPSS + stationarity sections;
  example sections in `stationarity.sql`; `benchmark/diagnostics/crosscheck_kpss.py`.

## Key decision — four-way verdict truth table (corrected)
The 01-2 plan's draft table swapped the trend/difference labels. Implemented the
standard ADF+KPSS interpretation instead (both flags mean "this test says stationary"):

| adf_is_stationary | kpss_is_stationary | verdict |
|---|---|---|
| true  | true  | stationary |
| true  | false | trend_stationary |
| false | false | difference_stationary |
| false | true  | non_stationary |

## Verification
- 9/9 core cargo tests, 39/39 SQL assertions, example runs clean,
  7/7 statsmodels KPSS/stationarity cross-checks.

## Deviation
- Corrected the plan's verdict truth table (see above) — a labeling bug the
  plan-checker had passed. Recorded in the commit message and here.

## Commits
- `23ce90e` feat(01-2): expose ts_kpss + ts_stationarity (STAT-02, STAT-03)
