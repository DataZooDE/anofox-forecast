---
phase: 01-diagnostics-demand-classification
plan: 3
subsystem: diagnostics
status: complete
requirements: [RESID-01, RESID-02, RESID-03, RESID-04]
completed: 2026-08-21
---

# Phase 01 Plan 3: Residual Diagnostics

Delivered the four residual-diagnostic functions across all five layers,
extending the shared diagnostics scaffolding.

## What was built
- **Core** (`validation.rs`): `ljung_box()`, `durbin_watson()` (with a stable
  `interpretation` string label), `jarque_bera()`, and `residual_diagnostics()`
  → `ResidualDiagnosticsOut`. 5 new unit tests.
- **FFI**: `AnofoxLjungBoxResult`, `AnofoxDurbinWatsonResult` (`interpretation: [c_char;32]`),
  `AnofoxJarqueBeraResult`, `AnofoxResidualDiagnosticsResult`; four exports.
- **C++** (`diagnostics.cpp`): `TsLjungBoxFunction`, `TsDurbinWatsonFunction`,
  `TsJarqueBeraFunction`, `TsResidualDiagnosticsFunction` (+ registrations, aliases).
- **Macros**: `ts_ljung_box_by`, `ts_durbin_watson_by`, `ts_jarque_bera_by`,
  `ts_residual_diagnostics_by`.
- **Docs / example / cross-check**: residual sections in `docs/api/10-diagnostics.md`;
  `examples/diagnostics/residuals.sql`; `benchmark/diagnostics/crosscheck_residuals.py`.

## Key decisions
- Adequacy verdict (RESID-04) gates on Ljung-Box p-value > alpha (default 0.05);
  Durbin-Watson and Jarque-Bera are advisory fields (per locked CONTEXT decision).
- `ts_ljung_box` uses `fitted_params = 0` (caller supplies raw residuals), so `df == lags`.
- `AutocorrelationType` enum mapped to string labels
  (positive_strong / positive_weak / none / negative_weak / negative_strong).

## Verification
- 14/14 core cargo tests, 51/51 SQL assertions, `residuals.sql` runs clean,
  9/9 statsmodels cross-checks (Jarque-Bera EXACT parity; Durbin-Watson within 1e-6).

## Commits
- `44bfdbb` feat(01-3): expose residual diagnostics (RESID-01..04)
