---
phase: 13-core-statistics-metrics-data-quality
fixed_at: 2026-10-10T23:25:23Z
review_path: .planning/phases/13-core-statistics-metrics-data-quality/13-REVIEW.md
iteration: 2
findings_in_scope: 2
fixed: 2
skipped: 0
status: partial
---

# Phase 13: Code Review Fix Report

**Fixed at:** 2026-10-10T23:25:23Z
**Source review:** .planning/phases/13-core-statistics-metrics-data-quality/13-REVIEW.md
**Iteration:** 2

**Summary:**
- Findings in scope (fix_scope = critical_warning, i.e. CR-* and WR-*; iteration 2
  surfaced only Critical findings, CR-02 and CR-03): 2
- Fixed: 2
- Skipped: 0

**Verification environment:** main checkout (no worktree — `workflow.use_worktrees`
is `false` in `.planning/config.json`, and project memory
(`project_gsd_worktree_isolation_leaks.md`) records that worktree isolation
leaks into the main tree for this repo; all edits, builds, and test runs
happened directly on branch `fix/statistical-correctness`). Both commits below
are reproducible from this same tree.

## Fixed Issues

### CR-02: WR-01's NULL-dates-list fix turns one bad row into a hard failure for `ts_stats()`/`ts_data_quality()`/`ts_data_quality_by()`

**Files modified:** `src/macros/ts_macros.cpp`, `test/sql/ts_data_quality_reference.test`

**Commit:** `147f4d4`

**Status:** fixed

**Applied fix:** Took fix option (b) from the review (filter NULL dates out of
the `LIST(...)` aggregation in the macros themselves) rather than weakening the
C++ `ExtractListTimestamps` throw, per the orchestrator's explicit guidance:
"Prefer fixing at the macro level ... over weakening the C++ check; keep the
C++ throw as a defensive guard for direct callers if lists still contain
NULLs." Added `FILTER (WHERE date_col IS NOT NULL)` to both the value-list and
date-list `LIST(...)` aggregates (paired, so the two lists stay aligned) in
all four macros that build a dates-aware group for a scalar call:
`ts_stats`, `ts_data_quality`, `ts_data_quality_summary`, and
`ts_data_quality_by`. A NULL-dated row is now dropped (value + date together)
before it ever reaches `ExtractListTimestamps`, matching the "drop this row"
semantics `ts_stats_by`'s `TsStatsByInOut`, `_ts_fill_gaps_native`/
`_ts_fill_forward_native`'s `InOut` functions, and `ts_stats_agg`/
`ts_data_quality_agg`'s `Update` functions already use. The C++
`ExtractListTimestamps` throw in `ts_stats.cpp`/`ts_data_quality.cpp` is left
unchanged as a defensive guard for a direct scalar call with a hand-built
list that still contains a NULL (WR-01's original scope).

Added a macro-level regression test (not a hand-built literal array passed
directly to the scalar function, which is what let this slip through
iteration 1) in `test/sql/ts_data_quality_reference.test` (non-json-gated, so
it runs on every native build): a 10-row table with one NULL-dated row (via
`GROUP BY` + `LIST(date_col ...)`) run through `ts_stats_by`, `ts_stats`,
`ts_data_quality`, `ts_data_quality_by`, and `ts_data_quality_summary`.
Verified live against the built extension before writing the test:

```
ts_stats_by:        length=9  n_gaps=1  n_gap_runs=1
ts_stats:            length=9  n_gaps=1  n_gap_runs=1   (previously: throw)
ts_data_quality:                n_gaps=1  n_gap_runs=1   (previously: throw)
ts_data_quality_by:              n_gaps=1  n_gap_runs=1   (previously: throw)
```

All four now agree; none throw.

**Verification:** `cargo fmt --all --check`: clean. `RUSTUP_TOOLCHAIN=1.89.0
GEN=ninja make release`: succeeded. `make test_release`: all tests passed (50
skipped [require json], 1824 assertions in 30 test cases — up from 1795
pre-fix, confirming the new assertions actually executed).

### CR-03: `fill_gaps`/`fill_forward` and `gap_counts`/`resolve_gap_step` disagree about how many periods are missing for the same calendar-frequency series

**Files modified:** `crates/anofox-fcst-core/src/gaps.rs`,
`crates/anofox-fcst-core/src/quality.rs`, `crates/anofox-fcst-core/src/stats.rs`,
`crates/anofox-fcst-core/tests/stats_reference.rs`,
`test/sql/ts_stats_reference.test`

**Commit:** `1d01594`

**Status:** fixed

**Applied fix:** Unified the two algorithms on a single shared grid-walk
function, `calendar_grid_gap_points(prev, curr, step_months_count,
month_end)`, added to `gaps.rs`. It walks forward from `prev` in
`step_months_count`-month increments (via the existing `step_months` helper)
and returns every grid point strictly before `curr`.

- `fill_gaps_calendar` (backs `ts_fill_gaps_by`/`ts_fill_forward_by`) now
  computes `step_months_count = base_months * period_count` and inserts a
  `None` row at each point `calendar_grid_gap_points` returns, instead of its
  old `idx_diff`-via-`month_index`/`quarter_index`/year-difference loop. This
  simplifies the function (removes the three-way `match base_months {1 => ...,
  3 => ..., _ => ...}` index dispatch entirely) as a side effect of sharing
  the walk.
- `step_count`'s `Months(m)` branch (backs `gap_counts`, which backs
  `resolve_gap_step`'s consumers: `ts_stats`/`ts_stats_by`/`ts_data_quality`/
  `ts_data_quality_by`) now calls the same `calendar_grid_gap_points` and
  returns `points.len() + 1`, instead of independently rounding the total
  elapsed months to the nearest whole step (round-half-away-from-zero) — the
  exact algorithm the review identified as the root cause of the disagreement.
- Both consumers now resolve `month_end` the same way, once per whole series
  via the existing `all_month_end` helper (previously only
  `fill_gaps_calendar` did this; `step_count` had no month-end notion at all).
  `gap_counts` now resolves `month_end` once and threads it through to every
  pairwise `step_count` call, matching `fill_gaps_calendar`'s single
  whole-series resolution.
- `gap_counts` and `step_count` now return `Result<...>` (the shared grid walk
  is fallible via `step_months`'s date arithmetic). Updated the three
  pre-existing call sites (`stats.rs` x2, `quality.rs` x1) to propagate with
  `?`, and the two in-crate test call sites to `.unwrap()`.
- Removed the now-dead `month_index`/`quarter_index` helpers (no longer used
  by either consumer after the unification); moved the `chrono::Datelike`
  import into the `#[cfg(test)]` module since it was only used there after the
  refactor (avoids a dev-build unused-import warning).

Verified live against the built extension before writing tests (all four
reproduce the review's exact scenarios and now agree):

```
'1q' over a 5-month gap (q3):    fill_gaps_by inserts 1 NULL row | ts_stats_by n_gaps=1
'3q' over 12 months (tq):        fill_gaps_by inserts 1 NULL row | ts_stats_by n_gaps=1 | ts_data_quality_by n_gaps=1
'2mo' exact+off-grid (bm):       fill_gaps_by inserts 1 NULL row | ts_stats_by n_gaps=1
month-end '1q' (me):             fill_gaps_by inserts 0 NULL rows | ts_stats_by n_gaps=0
```

(Previously: `q3` -> fill_gaps inserted 0, gap_counts said 1; `tq` -> fill_gaps
inserted 1, gap_counts said 0 — the exact review reproductions, now both
sides agree in both directions.)

Added 4 new Rust unit tests in `gaps.rs` via a shared
`assert_fill_gaps_agrees_with_gap_counts` helper (cross-checks `fill_gaps`'s
inserted-row count against `gap_counts(...).n_missing_periods` for the same
series/frequency/period_count): `'1q'` over a 5-month gap, `'3q'` over 12
months, `'2mo'` exact+off-grid, and a month-end `'1q'` series. Added matching
SQL assertions in `test/sql/ts_stats_reference.test` (non-json-gated) for the
same four cases, cross-checking `ts_fill_gaps_by`'s `NULL`-row count against
`ts_stats_by`'s `n_gaps`.

**Fixture regeneration note (required by the task brief, documented here even
though no regeneration was needed):** The `validation/reference/fixtures/
gap_counts/cases.json` fixture (consumed by `gap_counts_fixture_matches_
gap_counts` in `crates/anofox-fcst-core/tests/stats_reference.rs`) was
inspected case-by-case for the `"months"`-kind cases (`month_start`,
`month_end`, `quarter_end`, `year_start`): every one of them has observed
dates spaced at an EXACT multiple of the declared step (e.g. `month_start`
skips exactly 2 months between Feb and Apr, and between Jul and Sep). On an
exact multiple, the old rounding rule and the new grid walk compute
identical results by construction (there is no remainder to round).
Confirmed this by running the full test suite unchanged — the fixture test
passed with zero fixture edits, and `cargo test -p anofox-fcst-core`'s
`gap_counts_fixture_matches_gap_counts` specifically passed. No case's
expected value changed, so no Python-reference-generator regeneration was
required.

**Verification:** `cargo fmt --all --check`: clean. `cargo test --workspace`:
all passed (`anofox-fcst-core` lib: 318 passed, up from 314 pre-fix — the 4
new CR-03 unit tests; all 9 other `anofox-fcst-core` integration test binaries
and both `anofox-fcst-ffi` test binaries: all green, 0 failed).
`RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release`: succeeded (no FFI signature
changes — `gap_counts`/`step_count`/`calendar_grid_gap_points` are internal
to `anofox-fcst-core`, not exposed across the FFI boundary — so
`src/include/anofox_fcst_ffi.h` required no regeneration; confirmed via `git
status` showing it untouched). `make test_release`: all tests passed (50
skipped [require json], 1824 assertions in 30 test cases).

## Skipped Issues

None — both in-scope findings were fixed.

---

_Fixed: 2026-10-10T23:25:23Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 2_

## Loop outcome (orchestrator, after iteration 3 re-review)

Reached the 3-iteration cap. Iteration-3 re-review confirmed CR-02 and CR-03 resolved and raised one new WARNING:

- **WR-05 (open):** `ts_data_quality_summary` (src/macros/ts_macros.cpp:170-197): when every row of a group has a NULL date, the filtered `LIST()` yields NULL instead of an empty list, so that group's quality struct is NULL and is silently excluded from `n_good`/`n_fair`/`n_poor`/`avg_score` while still counted in `n_total`. Repro: 2 groups (one all-NULL-dated, one clean) gives `n_total=2, n_good=1, n_fair=0, n_poor=0, avg_score=1.0`. Fix sketch: COALESCE the filtered lists to empty lists, or add an explicit "unassessed" bucket. Logged in deferred-items.md.

Iteration backups 13-REVIEW.iter1.md / iter2.md and 13-REVIEW-FIX.iter1.md / iter2.md are kept for post-mortem.
