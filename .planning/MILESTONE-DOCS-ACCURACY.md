# Milestone Brief: Documentation Accuracy (post-v0.7.0)

**Type:** GSD milestone candidate — seed for `/gsd-new-milestone` (or `/gsd-quick` per defect class)
**Created:** 2026-08-24
**Status:** OPEN — a fix pass was dispatched (background agent `ad7f7300`) covering all classes below. **Before starting, run `git log --oneline -15` and re-run the doc verification** to see what is already fixed vs. still open; do not redo committed work.

## Goal

Every SQL snippet in `docs/**` and `examples/**` runs green against the built extension (the PR #230 rule), and every internal markdown link resolves. No stale/renamed functions, no signature/column drift, no parser errors.

## Context

v0.7.0 shipped (diagnostics + global/panel + GARCH/Kalman/VAR) and is released. A full documentation verification (README + 42 doc files, ~380 SQL blocks, 20 example files, run against `build/release/duckdb -unsigned`) found the **v0.7.0 additions are all correct**, but there is **pre-existing drift** in older surfaces. This brief captures that drift.

**How to verify any fix:** `build/release/duckdb -unsigned` (extension auto-loads; no `LOAD`). Some diagnostics macros need `SET order_by_non_integer_literal=true;`. Do NOT touch network-dependent snippets (`httpfs` / `read_parquet('https://…')` / m5-benchmarks S3).

## Defects (grouped; each is a discrete work item)

### A — Removed `ts_backtest_auto` / `ts_backtest_auto_by` still referenced  ⟶ **largest**
The CV/backtest API is now the two-step **`ts_cv_folds_by` + `ts_cv_forecast_by`** (see `docs/api/08-cross-validation.md`, which is correct — mirror it).
Executable blocks that error with `Catalog Error: … does not exist`:
- `docs/api/01-table-macros.md` (~L38, L42)
- `docs/api/00-api-design.md` (~L123, L127, L237; stale prose L157, L277)
- `docs/api/07-forecasting.md` (~L66)
- `docs/api/11-conformal-prediction.md` (~L208)
- `docs/api/22-supported-frequencies.md` (~L87; stale prose L15)
- `docs/API_REFERENCE.md` (~L72, L76; stale prose L120)
- `docs/guides/02-model-selection.md` (block ~8, ~L153)
- `docs/guides/03-cross-validation.md` — **whole guide is built around the removed macro; needs a real rewrite** around the two-step CV API (stale prose L21)
- `examples/backtesting/synthetic_backtest_examples.sql` (+ follow-on `ts_cv_split_by` arg mismatch)
- `examples/backtesting/m5_backtest_examples.sql` (network data, but the `ts_backtest_auto_by` call itself must be replaced)

### B — Bare `{}` must be `MAP{}` (Parser Error)  ⟶ **trivial**
Multi-line `ts_forecast_by(...)` example ends params with bare `{}` (~L50-52); same files use `MAP{}` elsewhere:
- `docs/reference/models/baseline/{naive,sma,random_walk_drift}.md`
- `docs/reference/models/exponential-smoothing/{ses,ses_optimized,holt}.md`

### C — Output column-name drift in example `.sql` (Binder Error)
Functions echo the GROUP column under its ORIGINAL name (`product_id`/`series_id`), not `id`; some placeholders renamed. Discover real emitted columns by running the inner call; fix `SELECT`/`ORDER BY`/`GROUP BY`/join names:
- `examples/forecasting/synthetic_forecasting_examples.sql` (`id` → `product_id`)
- `examples/decomposition/synthetic_decomposition_examples.sql` (`id` → `series_id`)
- `examples/feature_extraction/synthetic_feature_examples.sql` (`id` not found)
- `examples/peak_detection/synthetic_peak_examples.sql` (`id` → `product_id`)
- `examples/period_detection/synthetic_period_examples.sql` (`id` → `series_id`)
- `examples/seasonality_classification/synthetic_seasonality_examples.sql` (`id` → `series_id`)
- `examples/data_preparation/synthetic_data_prep_examples.sql` (`value_col` → `value`/`filled_value`)
- `examples/gap_filling/synthetic_gap_examples.sql` (`group_col` → `series_id`)

### D — Signature drift in example `.sql`
- `examples/metrics/synthetic_metrics_examples.sql` — `ts_mae_by('t', product_id, date, actual, predicted)` (5 args) → shipped is `ts_mae_by(source, date_col, actual_col, forecast_col)` (4 args, no group). Same for `ts_rmse_by`/`ts_mape_by`. Mirror the per-series pattern in `docs/api/09-evaluation-metrics.md`.
- `examples/multi_key_hierarchy/synthetic_multi_key_examples.sql` — `ts_validate_separator('t', region_id, store_id, item_id)` → shipped is `ts_validate_separator(TABLE, separator := VARCHAR)`. See `docs/api/02-hierarchical.md`.

### Broken internal markdown links  ⟶ **trivial**
- `docs/api/00-api-design.md` — `[Evaluation Metrics](07-evaluation-metrics.md)` → `09-evaluation-metrics.md`
- `docs/guides/03-cross-validation.md` — `[Evaluation Metrics](../api/07-evaluation-metrics.md)` → `../api/09-evaluation-metrics.md`
- `docs/reference/models/exponential-smoothing/global_ets.md` — `../../api/07-forecasting.md` (+ `#anchor`) → `../../../api/07-forecasting.md` (wrong depth)
- `docs/reference/models/intermittent/global_croston.md` — same `../../api` → `../../../api`
- `docs/reference/models/theta/global_theta.md` — same `../../api` → `../../../api`

### Non-defects (do not "fix")
- `examples/changepoint_detection/m5_changepoint_examples.sql`, `examples/peak_detection/m5_peak_examples.sql` — network / `json` autoload environment gap in the bare piped CLI (works in the real dev CLI). Not catalog drift.

## Definition of Done
- [ ] Every executable ```sql block in the files above runs green against `build/release/duckdb` (no Catalog/Binder/Parser errors).
- [ ] All 5 broken links resolve to existing files.
- [ ] `docs/guides/03-cross-validation.md` rewritten around `ts_cv_folds_by` + `ts_cv_forecast_by`, narrative intact.
- [ ] Changes committed (`docs(...)` / `fix(examples): …`) and pushed; the distribution pipeline's LTS-1.4.5-Windows red is the known unrelated vcpkg/msys2 issue (ignore).

## Notes / related
- Matches the standing "API drift stale refs" memory (backtest → CV two-step; stale refs remain in tests/comments/docs).
- Separately outstanding (NOT docs): **LTS-1.4.5 Windows CI** fails on an upstream vcpkg/msys2 `msys2-runtime-3.5.4-2` 404. A naive `vcpkg_commit` bump was tried and reverted (it broke the whole LTS matrix). Real fix = drop `windows_amd64` from the LTS lane's `exclude_archs`, wait for upstream `extension-ci-tools@v1.4-andium` to bump its vcpkg default, or a surgical OpenSSL-port overlay. Own ticket.
- README badge "1274 assertions passed" is a stale marketing number — update to the real current count when convenient (left untouched pending owner confirmation).
