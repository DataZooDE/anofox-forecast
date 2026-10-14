---
phase: 15-detection-decomposition-on-fdars-0-47
fixed_at: 2026-10-14T09:31:33Z
review_path: .planning/phases/15-detection-decomposition-on-fdars-0-47/15-REVIEW.md
iteration: 4
iteration_note: >-
  User-authorised one-off beyond the standard 3-iteration cap. Scope was
  explicitly restricted to the three iteration-3 findings (CR-06, CR-07,
  WR-04) discovered during the final re-review of the --auto fix loop;
  no other findings from any prior iteration were revisited.
findings_in_scope: 3
fixed: 3
skipped: 0
status: all_fixed
---

# Phase 15: Code Review Fix Report (iteration 4 — user-authorised, beyond the 3-iteration cap)

**Fixed at:** 2026-10-14T09:31:33Z
**Source review:** .planning/phases/15-detection-decomposition-on-fdars-0-47/15-REVIEW.md
**Iteration:** 4 (user-authorised one-off beyond the standard 3-iteration cap; scope strictly limited to CR-06, CR-07, WR-04)

**Summary:**
- Findings in scope: 3 (CR-06, CR-07, WR-04)
- Fixed: 3
- Skipped: 0

Verification ran in the main working tree on branch `fix/statistical-correctness`
(`workflow.use_worktrees` is `false` for this project per `.planning/config.json`
— no isolated worktree was created; all edits, builds, and tests below are
directly reproducible from the current checkout).

## Fixed Issues

### CR-06: `ts_classify_seasonality` silently returned `NULL` instead of raising for ordinary non-positive (but finite) periods — 0.0 and -5.0

**Files modified:** `src/table_functions/ts_seasonality.cpp`
**Commit:** `aed6c01` — fix(15): CR-06/WR-04 ts_classify_seasonality delegate period validation to core
**Applied fix:** Per the task's required approach ("validate solely in Rust
and propagate INVALID_INPUT, whichever matches the existing sibling
pattern"), deleted the local `std::isfinite(period)` pre-check and the
`period <= 0 || values.size() < static_cast<size_t>(2 * period)` guard
entirely from `TsClassifySeasonalityFunction`. The function now passes
`period` straight through to `anofox_ts_classify_seasonality` and, on
failure, checks `error.code == INVALID_INPUT` before throwing
`InvalidInputException("ts_classify_seasonality: %s", error.message)` —
the exact pattern `ts_seasonal_strength` and its 5 other siblings in
`ts_detrend.cpp` already use. The core-level check
(`crates/anofox-fcst-core/src/seasonality.rs::classify_seasonality`,
`!period.is_finite() || period <= 0.0`) is the single source of truth;
its `InvalidParameter` error is already mapped to `ErrorCode::InvalidInput`
at the FFI boundary (`ffi_error_code`, fixed in `86802cc`). Insufficient-data
failures (`error.code != INVALID_INPUT`) still fall through to
`FlatVector::SetNull`, preserving the documented "too-short series returns
NULL" behavior. No `static_cast<size_t>` of a period-derived value remains
on this path.

### WR-04: `ts_classify_seasonality`'s NaN/Infinity error message format diverged from its 8 siblings

**Files modified:** `src/table_functions/ts_seasonality.cpp` (same commit as CR-06)
**Commit:** `aed6c01`
**Applied fix:** Resolved automatically as a side effect of CR-06's fix, per
the review's own guidance: deleting the local pre-check means every
invalid-period failure (finite or not) now surfaces the unified
`Invalid parameter 'period' = '<value>': Period must be a positive finite
number` message produced by the Rust `ForecastError::InvalidParameter`
`Display` impl, identical to `ts_seasonal_strength` and the other 7
period-validating functions. No separate code change was needed beyond
CR-06's.

### CR-07: `ts_classify_seasonality_by` / `ts_classify_seasonality_agg` had a second, untouched copy of the same defect

**Files modified:** `src/aggregate_functions/ts_classify_seasonality_agg.cpp`
**Commit:** `063d81b` — fix(15): CR-07 ts_classify_seasonality_agg delegate period validation to core
**Applied fix:** Deleted the inline `period <= 0 || data.values.size() <
static_cast<size_t>(2 * period)` pre-check from
`TsClassifySeasonalityAggFinalize` entirely (this backend had no
`isfinite` guard at all, so a NaN period previously reached the
`static_cast<size_t>(2 * period)` UB cast directly). Mirrored CR-06:
`period` now passes straight through to `anofox_ts_classify_seasonality`,
and on failure the function checks `error.code == INVALID_INPUT` before
throwing `InvalidInputException("ts_classify_seasonality_by: %s",
error.message)`. This eliminates the second, independent copy of the
defect class rather than re-adding a duplicated `isfinite` check, matching
the task's required single-source-of-truth approach. Insufficient-data
failures keep returning a NULL row (unchanged for the already-working
too-short-series case).

**Note on "naming the group":** the review's fix suggestion mentioned
"naming the group" in the thrown message, mirroring CR-04's table-function
fix. `ts_classify_seasonality_agg` is a standard DuckDB `AggregateFunction`
(not the custom streaming table-function machinery CR-04 used) — its
`Finalize` callback has no access to the GROUP BY key at all, and no
other aggregate function in this codebase (checked `ts_forecast_agg.cpp`,
the only other aggregate with an `InvalidInputException` throw) names the
group either. The thrown message instead names the macro
(`ts_classify_seasonality_by: ...`), consistent with the only precedent
available in this codebase.

## Skipped Issues

None — all 3 in-scope findings (CR-06, CR-07, WR-04) were fixed. WR-04
required no code change beyond CR-06's fix, per the review's own guidance.

## Verification

- `cargo fmt --all --check`: clean (no Rust files were modified by this
  iteration — both fixes are C++-only, deleting local validation in favor
  of the already-fixed Rust/FFI path).
- `RUSTUP_TOOLCHAIN=1.89.0 cargo test --workspace`: all passed (same
  workspace test counts as iteration 2/3's baseline — no Rust source
  changed, so no test behavior could regress — 0 failures across unit,
  integration, and doc tests).
- `RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release`: succeeded (only
  pre-existing SFINAE compiler warnings from DuckDB headers, same as every
  prior iteration's baseline; no new warnings from either changed file).
- `make test_release`: **all tests passed — 2082 assertions in 32 test
  cases** (50 json-gated tests skipped, as expected), up from iteration 3's
  implicit baseline of 2070 assertions carried over from iteration 2.
  `test/sql/ts_detection_reference.test` (non-json-gated — confirmed the
  `ts_classify_seasonality_by` macro itself requires no `json` extension,
  so both the scalar and `_by` assertions run natively) carries the new
  iteration-4 regression block: a `foreach period nan inf -inf -5.0 0.0`
  loop asserting the unified "Period must be a positive finite number"
  message on both `ts_classify_seasonality` and `ts_classify_seasonality_by`,
  plus one valid-period (`12.0` on the existing `sine120` fixture) case on
  each confirming no regression.
- Live repro of the review's exact failing queries, re-run against the
  rebuilt extension (`build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension`,
  rebuilt at this iteration's HEAD, commit `063d81b`):
  - `SELECT ts_classify_seasonality(LIST(value), 0.0) FROM t` and
    `... -5.0 ...` now raise `Invalid Input Error: ts_classify_seasonality:
    Invalid parameter 'period' = '0': Period must be a positive finite
    number` (and `'-5'` respectively) instead of returning NULL with no
    error — confirmed via the new `make test_release` assertions above
    (equivalent repro, run through the test harness rather than an
    interactive `duckdb -unsigned` session in this pass).
  - `ts_classify_seasonality_by('sales', product_id, ds, v, 'nan'::DOUBLE)`
    / `0.0` / `-5.0` now raise instead of returning an all-NULL row —
    confirmed via the new `ts_classify_seasonality_by` foreach assertions
    against the `sine120` fixture.
- `src/include/anofox_fcst_ffi.h` was **not** regenerated/committed: `git
  diff --stat` against it is empty — no FFI function signature changed in
  either fix (only C++-side deletion of local validation and the error
  handling branch after an unchanged FFI call).
- No `static_cast<size_t>` remains on a period-derived value in either
  modified file — confirmed via `grep -n "static_cast<size_t>"` on both
  files (the only remaining matches are inside explanatory code comments,
  not live casts).

## Commits

- `aed6c01` — fix(15): CR-06/WR-04 ts_classify_seasonality delegate period validation to core
- `063d81b` — fix(15): CR-07 ts_classify_seasonality_agg delegate period validation to core
- `a253d88` — test(15): add CR-06/CR-07/WR-04 regression assertions

---

_Fixed: 2026-10-14T09:31:33Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 4 (user-authorised beyond the 3-iteration cap)_

## Loop outcome (orchestrator)

Iterations 1–3 ran under the --auto cap. Iteration 3's re-review confirmed CR-04/CR-05/WR-03 and raised CR-06/CR-07/WR-04. On 2026-10-14 the user authorised one targeted iteration 4 beyond the cap, limited to those three. The orchestrator verified it directly against the built extension, with no further review loop: NaN/0/-5 periods raise the unified InvalidInput error on `ts_classify_seasonality`, a valid period still classifies, no period-derived `static_cast<size_t>` remains, and ts_detection_reference.test has 147 assertions passing. Remaining open items are info only (IN-01, IN-02). Iteration backups are kept as 15-REVIEW.iter1/iter2.md and 15-REVIEW-FIX.iter1/iter2.md.
