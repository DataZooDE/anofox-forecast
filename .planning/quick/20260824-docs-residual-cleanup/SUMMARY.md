---
type: quick-summary
slug: docs-residual-cleanup
status: complete
date: 2026-08-30
commits:
  - 1acbfb7
  - d81ea5c
  - 133c0ca
---

# Quick Task Summary: Documentation Residual Cleanup (post-v0.7.0)

Three residual documentation items from MILESTONE-DOCS-ACCURACY.md closed in three
atomic commits, pushed to origin/main.

## Task 1 — Sweep stale ts_backtest_auto prose

**Commit:** 1acbfb7

Replaced all prose/reference mentions of the removed ts_backtest_auto[_by] in:
- docs/dev/memory-patterns.md: reframed as historical context
- examples/backtesting/README.md: rewrote all 8 patterns to shipped APIs
- examples/conformal_prediction/README.md: two references updated
- examples/forecasting/README.md: Related Functions reference updated
- examples/metrics/README.md: Related Functions reference updated

Acceptance: grep returns only intentional "removed/renamed" notes, no executable SQL.

## Task 2 — Fix backtesting regression-API example sections

**Commit:** d81ea5c

Removed APIs confirmed: ts_prepare_regression_input_by, ts_hydrate_features_by,
ts_hydrate_split_by / _full_by / _strict_by (all absent from duckdb_functions()).

synthetic_backtest_examples.sql:
- Pattern 2: ts_prepare_regression_input_by -> ts_cv_folds_by + ts_cv_hydrate_by + CASE masked_target
- Pattern 5: ts_hydrate_features_by -> ts_cv_hydrate_by; added ::DOUBLE cast (hydrate outputs VARCHAR)
- Pattern 7: ts_hydrate_split_full_by -> plain JOIN; fixed large_sales int-division bug (i/100 was float)
- Pattern 8: all three ts_hydrate_split_*_by -> ts_cv_hydrate_by with ::DOUBLE casts
- Pattern 9: cv_prepared rewritten; qualified ambiguous column refs
- Added SET autoinstall/autoload_known_extensions for json (needed by ts_cv_hydrate_by in piped mode)

m5_backtest_examples.sql Section 6:
- ts_prepare_regression_input_by -> ts_cv_folds_by + plain JOIN (all features calendar-based)
- Column refs updated from group_col/date_col to item_id/ds throughout

Non-network SQL blocks verified green against build/release/duckdb -unsigned.

## Task 3 — Update README test-count badge

**Commit:** 133c0ca

cargo test --all-features: 221 + 24 + 38 + 12 = 295 passed.
Badge updated: Tests-1274%20assertions%20passed -> Tests-295%20Rust%20tests%20passed.

## Acceptance

- [x] Task 1: no executable ts_backtest_auto SQL remains
- [x] Task 2: no removed regression APIs; non-network blocks run green
- [x] Task 3: badge shows 295 (not 1274)
- [x] Three atomic commits pushed to origin/main (1acbfb7, d81ea5c, 133c0ca)
- [x] STATE.md Quick Tasks Completed table updated
- [x] SUMMARY.md written with status: complete
