---
phase: 16-conformal-bootstrap-correctness
reviewed: 2026-10-14T00:00:00Z
depth: standard
files_reviewed: 5
files_reviewed_list:
  - crates/anofox-fcst-ffi/src/lib.rs
  - src/include/anofox_fcst_ffi.h
  - src/macros/ts_macros.cpp
  - src/scalar_functions/conformal.cpp
  - test/sql/ts_conformal_reference.test
findings:
  critical: 0
  warning: 0
  info: 2
  total: 2
status: clean
---

# Phase 16: Code Review Report (Iteration 2 — Fix Verification)

**Reviewed:** 2026-10-14
**Depth:** standard
**Files Reviewed:** 5 (fix commits `3ecb81f` `9ccb373` `e77b753` `e61a0e5` `7a27c9f` `9c92b4d`)
**Status:** clean

## Summary

Re-reviewed all four iteration-1 warnings (WR-01..WR-04) against the fix commits, cross-checked
every claim in `16-REVIEW-FIX.iter1.md` against the actual diffs (`git show`), and reproduced
each scenario live against the rebuilt extension (`build/release/duckdb -unsigned`, loading
`build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension`, confirmed via
`strings` that the binary actually contains the qualified `cal.group_col` fix and the new error
message, not a stale artifact). Also re-ran the full `ts_conformal_reference.test` suite
(132/132 assertions pass) plus `ts_conformal.test`, `ts_conformal_coverage.test` (both skip
cleanly on the pre-existing `require json` gate, unrelated to Phase 16), and
`ts_conformal_null_policy.test` (17/17 assertions pass).

All four warnings are genuinely resolved, with no regressions:

- **WR-01** (empty-but-non-NULL forecasts list rejected as "Null pointer"): fixed in
  `anofox_ts_conformal_apply_score` (3ecb81f) and, after the fixer's own full-suite run
  surfaced two sibling instances of the identical bug, also in `anofox_ts_conformal_intervals`
  (the 2-arg backing function), the now-incorrect `out_lower == nullptr` C++ check, and the
  missing `FunctionNullHandling::SPECIAL_HANDLING` on the 4-arg overload (7a27c9f). Reproduced
  live: `ts_conformal_intervals([]::DOUBLE[], 1.0)` and the 4-arg form with a literal
  `NULL::DOUBLE[]` difficulty both now return `{'lower': [], 'upper': []}`, not SQL `NULL`.
  Checked that `SPECIAL_HANDLING` does not loosen validation on the other three arguments —
  `forecasts`/`score`/`method` NULLs are still explicitly checked in
  `TsConformalApplyScoreFunction` and still short-circuit to SQL `NULL`; only the previously-
  mishandled `difficulty` argument is now allowed through as NULL by design.
- **WR-02** (dead `anofox_ts_conformal_predict_adaptive` FFI export): confirmed fully removed
  from `crates/anofox-fcst-ffi/src/lib.rs` and `src/include/anofox_fcst_ffi.h` (grep for
  `predict_adaptive` across `src/`, `crates/`, `test/` now only matches the core (non-FFI)
  `conformal_predict_adaptive` function and its own Rust unit/integration tests, which were
  never in scope). `cargo build -p anofox-fcst-ffi` is clean with no warnings about the removed
  `build_values` helper's remaining call sites.
- **WR-03** (pooled fallback matching any `group_col IS NULL` row): fixed by gating the pooled
  join on `cal_pooled_eligible` (`COUNT(*) = 1 FROM cal`). Reproduced all three documented
  scenarios live: a sole-row pooled table still pools correctly; a per-group table with an extra
  genuine-NULL-key row no longer leaks that row's score into an unrelated unmatched group (NULL
  bounds instead); a group missing entirely from a per-group-only table still gets NULL bounds.
  Also checked an un-tested edge case: a single-row calibration table whose sole row has a
  **non-NULL** `group_col` (e.g. only group `A` calibrated) — correctly does *not* pool into
  other groups, because the pooled join still requires `pooled.group_col IS NULL` in addition to
  `eligible._eligible`; eligibility alone is necessary but not sufficient.
- **WR-04** (unvalidated duplicate-row fan-out): fixed via `cal_dupe_check`, which raises before
  any row is ever returned. Reproduced duplicate per-group rows and duplicate pooled (`NULL`)
  rows both raising the documented error. Also checked an edge case not in the test suite: a
  forecast table with **zero rows** (`per_group` empty, so the `CROSS JOIN` with `cal_dupe_check`
  would naively seem to produce zero rows and never evaluate the check) still raises the error —
  DuckDB evaluates the aggregate-without-`GROUP BY` subquery eagerly regardless of the outer
  join's cardinality, so the validation is not silently skipped for an empty-forecast edge case.
  The genuine follow-up bug the fixer found and fixed (9c92b4d) — the unqualified `group_col`
  inside `cal_dupe_check`'s subquery colliding with the macro's own `group_col` *parameter* name,
  which DuckDB's macro substitution resolves by literal token match for single-part identifiers
  (not for qualified refs like `cal.group_col`/`direct.group_col`) — is correctly fixed by
  qualifying with `cal.group_col`.

**Cross-file check for the same collision pattern elsewhere:** audited every macro in
`ts_macros.cpp` that reads a *second*, externally-produced table via `query_table(...)`
(`ts_cv_hydrate_by`'s `cv_folds` parameter, `ts_cv_forecast_by`'s `ml_folds` parameter, and
`ts_conformal_apply_by`'s `calibration` parameter — the only three "two-table" macros in the
file). The WR-04-style collision requires both (a) the second table's schema to use a *fixed,
literal* column name regardless of the caller's own column names, and (b) that literal name to
also be a macro parameter name referenced *unqualified* in the body. Verified empirically
(`DESCRIBE cv_folds` after `ts_cv_folds_by`) that `ts_cv_folds_by`'s output preserves the
caller's original column names (e.g. `pid`, `dt`, `y`), not literal `group_col`/`date_col` —
so `ts_cv_hydrate_by`'s `group_col AS __cv_grp` reference is safe: the macro parameter and the
column it resolves to are the same identifier by construction, unlike `ts_conformal_apply_by`
where `ts_conformal_calibrate`'s output schema is a fixed contract (`group_col`, `method`,
`conformity_score`) independent of the caller's own grouping column name. No other instance of
the collision pattern was found.

No new defects were introduced by any of the six fix commits. Full verification was performed
against a freshly rebuilt extension binary (confirmed newer than all touched source files, and
confirmed via `strings` to contain the post-fix macro text), not assumed from the fixer's report.

## Info

### IN-01 (carried from iteration 1, unchanged): `ConformalStrategyFFI::JackknifePlus` removal has no regression test for the raw discriminant value

No action required; out of scope for this iteration. See `16-REVIEW.iter1.md` IN-01 for full detail.

### IN-02 (carried from iteration 1, unchanged): `conformal_apply_score` vs. `conformal_from_pairs` use structurally different "difficulty absent" conventions

No action required; both conventions still resolve identically at every current call site. See
`16-REVIEW.iter1.md` IN-02 for full detail.

---

_Reviewed: 2026-10-14_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
