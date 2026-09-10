---
type: quick
slug: docs-residual-cleanup
created: 2026-08-24
status: in-progress
---

# Quick Task: Documentation residual cleanup (post-v0.7.0)

Close out the three residual items recorded in `.planning/MILESTONE-DOCS-ACCURACY.md` (§ Residual open items + README badge note). All prior doc-SQL drift (classes A–D + links) is already fixed & pushed — do NOT redo it; `git log --oneline -12` shows what landed.

## Verify method
`build/release/duckdb -unsigned` (extension auto-loads; no LOAD). Every SQL block touched must run green (no Catalog/Binder/Parser errors). Don't touch network-dependent snippets (httpfs / read_parquet('https://…') / m5-benchmarks S3) beyond the specific stale-function replacement.

## Tasks

### Task 1 — Sweep stale `ts_backtest_auto` prose
`ts_backtest_auto[_by]` is removed; the CV API is `ts_cv_folds_by` + `ts_cv_forecast_by` (see the already-fixed `docs/api/08-cross-validation.md` and `docs/guides/03-cross-validation.md` for the correct two-step usage). Replace the remaining **prose/reference** mentions in:
- `docs/dev/memory-patterns.md`
- `examples/backtesting/README.md`
- `examples/conformal_prediction/README.md`
- `examples/forecasting/README.md`
- `examples/metrics/README.md`
**Acceptance:** `grep -rn "ts_backtest_auto" docs/ examples/` returns nothing (or only inside a "removed/renamed" note that intentionally names it). No executable SQL left referencing it.

### Task 2 — Fix backtesting regression-API example sections
`examples/backtesting/m5_backtest_examples.sql` (Section 6) and `examples/backtesting/synthetic_backtest_examples.sql` (Patterns 2/5/7/8) use the **removed** `ts_prepare_regression_input_by` / `ts_hydrate_*` APIs.
- First determine the current regression/exogenous surface: `grep -rn "regression\|exog\|hydrate\|prepare_regression" docs/api/ src/macros/ts_macros.cpp` and check what exogenous forecasting is shipped (e.g. `ts_forecast_exog_by`, ARIMAX/ThetaX/MFLESX methods). Confirm real signatures by running against the built extension.
- Rewrite those sections to the shipped API. If a removed capability has NO shipped replacement, replace the section with the nearest supported pattern (e.g. plain `ts_cv_forecast_by` backtest, or exogenous via `ts_forecast_exog_by`) and add a one-line comment noting the change — do NOT leave calls to non-existent functions.
- Keep network m5 data loading as-is (don't fail on network).
**Acceptance:** both files' non-network SQL blocks run green; no calls to `ts_prepare_regression_input_by`/`ts_hydrate_*` (or any non-existent function) remain.

### Task 3 — Update README test-count badge
`README.md` line ~12: `Tests-1274%20assertions%20passed`. Get the real current count and update it.
- Run the suite: `cargo test --all-features 2>&1 | grep "test result:"` and sum the `N passed` across the result lines (Rust side). If a fuller "assertions" number is intended (SQL tests), note in the SUMMARY that the badge now reflects the Rust test count; otherwise use the Rust passed-count.
**Acceptance:** badge shows a real, current number (not 1274); README still renders.

## Done when
- All three tasks' acceptance criteria met and verified.
- Atomic commits (one per task, `docs(...)`/`fix(examples): …`), pushed to `origin/main`.
- `SUMMARY.md` written in this task dir; STATE.md "Quick Tasks Completed" updated.
- End commit bodies with:
  Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
