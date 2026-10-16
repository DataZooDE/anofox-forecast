---
phase: 16-conformal-bootstrap-correctness
fixed_at: 2026-10-14T00:00:00Z
review_path: .planning/phases/16-conformal-bootstrap-correctness/16-REVIEW.md
iteration: 1
findings_in_scope: 4
fixed: 4
skipped: 0
status: all_fixed
---

# Phase 16: Code Review Fix Report

**Fixed at:** 2026-10-14
**Source review:** .planning/phases/16-conformal-bootstrap-correctness/16-REVIEW.md
**Iteration:** 1

**Summary:**
- Findings in scope: 4 (WR-01..WR-04; `fix_scope: critical_warning`, 0 critical findings in this review, IN-01/IN-02 out of scope)
- Fixed: 4
- Skipped: 0

## Fixed Issues

### WR-01: `anofox_ts_conformal_apply_score` rejects a non-null, zero-length forecasts list as "Null pointer"

**Files modified:** `crates/anofox-fcst-ffi/src/lib.rs`, `test/sql/ts_conformal_reference.test`
**Commit:** `3ecb81f`
**Applied fix:** Added the same `n_forecasts == 0` short-circuit used by every sibling conformal
FFI entry point (`anofox_ts_conformal_from_pairs`, `anofox_ts_conformal_calibrate`,
`anofox_ts_conformal_apply`) to `anofox_ts_conformal_apply_score`, so a non-null, zero-length
forecasts pointer now produces an empty result instead of a `NullPointer` error. Added a SQL
test asserting the 4-arg `ts_conformal_intervals` overload returns `STRUCT(lower: [], upper: [])`
(not NULL) for `[]::DOUBLE[]`.

**Follow-up (same finding, found during full-suite verification):**
**Files modified:** `crates/anofox-fcst-ffi/src/lib.rs`, `src/scalar_functions/conformal.cpp`
**Commit:** `7a27c9f`
**Applied fix:** Running the full `make test_release` suite surfaced two more instances of the
exact same contract violation that WR-01 names, both reachable from plain SQL and not covered by
the review's own repro:
1. `anofox_ts_conformal_intervals` (the 2-arg `ts_conformal_intervals` overload's backing FFI)
   had the identical null-pointer-vs-zero-length bug; fixed the same way.
2. `TsConformalIntervalsFunction` (C++) treated a *successful* empty-forecasts call as a failure
   because `out_lower`/`out_upper` legitimately come back null for a zero-length allocation;
   removed the incorrect `out_lower == nullptr || out_upper == nullptr` check (the FFI allocator
   never mallocs for a zero-length result by design, and `WriteDoubleListNanAsNull` already
   handles a null pointer paired with a zero length safely, producing an empty, not NULL, list).
3. The 4-arg `ts_conformal_intervals` overload was missing
   `FunctionNullHandling::SPECIAL_HANDLING`, so DuckDB's default null-propagation nullified the
   *entire* result whenever `difficulty` was a literal SQL `NULL` (the review's own documented
   "no difficulty_col at all" call) -- before the function body, which already distinguishes
   forecasts/score/method nulls from an absent difficulty, ever ran. This is exactly the repro
   SQL the review itself proposed (`ts_conformal_intervals([]::DOUBLE[], 1.0, 'symmetric',
   NULL::DOUBLE[])`); without this fix that call still returned NULL even after fix #1 above.

### WR-02: `anofox_ts_conformal_predict_adaptive` is dead code with zero callers

**Files modified:** `crates/anofox-fcst-ffi/src/lib.rs`, `src/include/anofox_fcst_ffi.h`
**Commit:** `9ccb373`
**Applied fix:** Confirmed via `grep -rn "anofox_ts_conformal_predict_adaptive" src/ crates/
test/ docs/` that it has zero callers anywhere in C++, Rust tests, SQL tests, or docs. Removed
the function body from `crates/anofox-fcst-ffi/src/lib.rs` and regenerated
`src/include/anofox_fcst_ffi.h` via `cargo build -p anofox-fcst-ffi` (the crate's `build.rs`
re-runs `cbindgen` on any `src/lib.rs` change), confirmed the header diff contains only the
removed declaration (no unrelated drift). `build_values` (the helper this function used) remains
used by 9 other call sites, so no further dead code was introduced.

### WR-03: `ts_conformal_apply_by`'s pooled-row matching treats *any* `group_col IS NULL` row as a fallback, including a real NULL group key

**Files modified:** `src/macros/ts_macros.cpp`, `test/sql/ts_conformal_reference.test`
**Commit:** `e61a0e5`
**Applied fix:** Chose the "single clear rule" option offered by the review's own fix text: a
`NULL group_col` row is now the documented pooled fallback *only* when it is the SOLE row of the
calibration table (the exact shape `ts_conformal_calibrate(params := {'pooled': true})` and the
literal-score one-row wrapper (D-14) both produce). Added a `cal_pooled_eligible` CTE
(`COUNT(*) = 1 FROM cal`) and gated the pooled join on it. Narrowed the macro's own doc comment
accordingly (the previous "plus optionally ONE pooled row with group_col = NULL" caller contract
allowing per-group rows + a pooled row together is no longer supported; a per-group table with an
extra genuine-NULL-key row no longer silently pools). Added SQL tests for all three requested
scenarios: a one-row pooled table (still works), a per-group table with an extra NULL-group row
(group B, with no direct match, now gets NULL bounds instead of the stray row's score), and a
group missing from a per-group-only calibration table (NULL bounds, unchanged).

### WR-04: `ts_conformal_apply_by` caller contract ("at most one calibration row per group") is unvalidated, silently fans out on violation

**Files modified:** `src/macros/ts_macros.cpp`, `test/sql/ts_conformal_reference.test`
**Commit:** `e77b753`
**Applied fix:** Added a `cal_dupe_check` CTE that raises a clear `error(...)` SQL error whenever
the calibration table has more than one row for the same `group_col` value (including more than
one row with `group_col = NULL`, i.e. duplicate pooled rows), wired into the output via
`COALESCE(..., dupe_check._err)` so the check is forced to evaluate on every call (verified
empirically against the live `duckdb` CLI that this pattern raises on duplicates and stays silent
otherwise, before editing the macro). Added SQL tests for duplicate per-group rows and duplicate
pooled (`NULL` group_col) rows, both raising "more than one row for the same group_col value".

**Follow-up (same finding, found during full-suite verification):**
**Files modified:** `src/macros/ts_macros.cpp`
**Commit:** `9c92b4d`
**Applied fix:** `make test_release` (not `cargo test`, which never exercises macro SQL text)
caught a self-inflicted bug: the `cal_dupe_check` CTE's subquery used a *bare* `group_col`
reference, which is textually identical to this macro's own `group_col` *parameter* name --
DuckDB's macro substitution replaced it with the caller's argument (e.g. plain column `g`)
instead of resolving it against the calibration table's own `group_col` schema column, breaking
every `ts_conformal_apply_by` call with `Referenced column "g" not found in FROM clause!`.
Qualified both references as `cal.group_col`, matching the pattern every other reference to the
calibration table's own column already uses in this macro (`direct.group_col`,
`pooled.group_col`).

## Verification

- `cargo fmt --all --check`: clean.
- `cargo test --workspace`: all green (469 + 39 + 15 + ... unit/integration/doc tests across both
  crates, 0 failed).
- `RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release`: clean build (only pre-existing DuckDB-header
  SFINAE warnings, unrelated to this phase).
- `make test_release`: **all tests passed**, 2215 assertions (up from the 2193 pre-fix baseline;
  50 skipped tests are the pre-existing `require json` gate, unrelated), 50 skipped for the
  documented json-extension reason only.
- Verification ran in the **main working tree** (not an isolated worktree):
  `workflow.use_worktrees` is `false` in `.planning/config.json` for this project (per project
  memory: GSD worktree isolation surfaces edits into the main tree for this repo, so it is
  disabled for autonomous runs here), so all reads/edits/builds/tests above are reproducible
  directly from this checkout at the current `HEAD`.

## Skipped Issues

None -- all four in-scope findings (WR-01..WR-04) were fixed, verified, and committed.

---

_Fixed: 2026-10-14_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
