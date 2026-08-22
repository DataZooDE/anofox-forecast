---
phase: 02-global-panel-models
fixed_at: 2026-08-21T00:00:00Z
review_path: .planning/phases/02-global-panel-models/02-REVIEW.md
iteration: 2
findings_in_scope: 3
fixed: 3
skipped: 0
status: all_fixed
---

# Phase 02: Code Review Fix Report (Iteration 2)

**Fixed at:** 2026-08-21
**Source review:** `.planning/phases/02-global-panel-models/02-REVIEW.md`
**Iteration:** 2

**Summary:**
- Findings in scope: 3
- Fixed: 3
- Skipped: 0

## Fixed Issues

### CR-01: n_kept < 3 deferred-error path silently swallowed the error

**Files modified:** `src/table_functions/ts_forecast_panel_native.cpp`
**Commits:** `54c3d80`, `faa64f8`
**Applied fix:**

Added `deferred_error_message` (std::string) to `TsForecastPanelNativeGlobalState`.

In the `n_kept < 3` branch, set it with `StringUtil::Format(...)` (instead of
conditionally throwing) and fell through to the output-batching block so any
queued DROPPED sentinel rows are emitted first.

Added the deferred throw check at **both** FINISHED return sites in the
output-batching block:
1. The `remaining == 0` early-return path (reached when no DROPPED rows were
   ever queued, or on a second Finalize call after a multi-batch flush).
2. The post-batch path after `output_offset >= results.size()` (reached when
   all DROPPED rows fit within a single STANDARD_VECTOR_SIZE batch).

The initial commit missed the second site; verified via a live DuckDB session
that the first commit failed (DROPPED rows returned silently), then the second
commit fixed it. Both commits are separate, atomic, and correctly labelled.

**Verification:** Ran in the main checkout (`build/release/`):
- `n_kept == 2` + 1 DROPPED series → `InvalidInputException` raised with
  "fewer than 3 usable series" message after DROPPED rows were flushed.
- `n_kept == 0` (all series DROPPED) → DROPPED rows returned, no error.
- `n_kept >= 3` (normal panel) → forecasts produced, no error.
- Full `examples/forecasting/global_panel_forecasting_examples.sql` ran clean
  across all 6 sections.

---

### WR-01: checked_mul overflow for output allocation used unwrap_or(0)

**Files modified:** `crates/anofox-fcst-ffi/src/lib.rs`
**Commit:** `9e92b27`
**Applied fix:**

Replaced `n_series.checked_mul(horizon).unwrap_or(0)` with an explicit
`match` that sets `out_error` and returns `false` on overflow — matching the
established FFI error contract and mirroring the companion guard at line 7013.

Also added an explicit `horizon == 0` guard (returns `InvalidInput` error
rather than silently producing 0 output rows).

The `total > 0` / `null_mut()` branch structure is preserved for the
(impossible after the guards) zero-total case.

**Verification:** Ran in the main checkout:
- `cargo test -p anofox-fcst-ffi` → 56 tests pass (18 unit + 38 integration
  + 0 doc-tests), no regressions. Build: `cargo build --release` clean.

---

### WR-02: Panel benchmark date re-computation hardcoded days=forecast_step

**Files modified:** `benchmark/src/common/anofox_runner.py`
**Commit:** `2db52ce`
**Applied fix:**

Replaced the `apply(lambda row: row['last_ds'] + pd.Timedelta(days=int(row['forecast_step'])))` 
call with a `_FREQ_DELTA` dict keyed by `freq` (`'D'` → `pd.Timedelta(days=1)`,
`'h'` → `pd.Timedelta(hours=1)`, `'W'` → `pd.Timedelta(weeks=1)`,
`'M'` → `pd.DateOffset(months=1)`). Defaults to `days=1` for unknown freq
strings to preserve existing Daily benchmark behaviour.

The vectorised `fcst_df['last_ds'] + step_delta * fcst_df['forecast_step'].astype(int)` 
replaces the row-wise `apply()` call.

**Verification:** Python `ast.parse` syntax check passed. Daily benchmark
behaviour is unchanged (default `pd.Timedelta(days=1)`).

## Skipped Issues

None.

---

## Build and Verify Summary

All verification ran in the **main checkout** (not an isolated worktree;
`workflow.use_worktrees` is `false`).

| Step | Result |
|------|--------|
| `cargo test -p anofox-fcst-ffi` | 56/56 pass |
| `make rust` (release build) | clean |
| `make header` | header regenerated |
| `make -j$(nproc)` (full extension) | clean (pre-existing SFINAE warnings only) |
| `global_panel_forecasting_examples.sql` (all 6 sections) | pass |
| CR-01 deferred-error scenario (n_kept=2) | raises `InvalidInputException` |
| n_kept=0 all-dropped scenario | returns DROPPED rows, no error |
| n_kept>=3 normal panel | forecasts produced |

---

_Fixed: 2026-08-21_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 2_
