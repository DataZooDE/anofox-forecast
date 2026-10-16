---
phase: 16-conformal-bootstrap-correctness
reviewed: 2026-10-14T00:00:00Z
depth: standard
files_reviewed: 10
files_reviewed_list:
  - crates/anofox-fcst-core/src/bootstrap.rs
  - crates/anofox-fcst-core/src/conformal.rs
  - crates/anofox-fcst-core/src/lib.rs
  - crates/anofox-fcst-ffi/src/lib.rs
  - crates/anofox-fcst-ffi/src/types.rs
  - crates/anofox-fcst-ffi/tests/null_policy_ffi.rs
  - src/include/anofox_fcst_ffi.h
  - src/anofox_forecast_extension.cpp
  - src/include/anofox_forecast_extension.hpp
  - src/macros/ts_macros.cpp
  - src/scalar_functions/bootstrap.cpp
  - src/scalar_functions/conformal.cpp
findings:
  critical: 0
  warning: 4
  info: 2
  total: 6
status: issues_found
---

# Phase 16: Code Review Report

**Reviewed:** 2026-10-14
**Depth:** standard
**Files Reviewed:** 12 (commits `29219db`..`2ff4925`, i.e. 16-01 through 16-03)
**Status:** issues_found

## Summary

Reviewed the full Phase 16 diff: the Rust `conformal`/`bootstrap` cores, the FFI
boundary (`anofox-fcst-ffi`), the new `_ts_conformal_from_pairs` /
`_ts_conformal_calibrate` C++ scalar functions, and the rewritten
`ts_conformal_by` / `ts_conformal_calibrate` / `ts_conformal_apply_by` /
`ts_interval_width_by` macros.

The core statistical logic (finite-sample level, signed-residual asymmetric
margins, adaptive difficulty scoring, the bootstrap sign fix, jackknife+
removal) is careful and internally consistent, and is backed by a real
reference-fixture + Monte Carlo test suite (verified non-vacuous via the
`conformal_fixture_has_expected_shape` guard test and a tolerance-band
calculation that is tight enough, relative to the `trials`/`n_cal`/`m_test`
sample sizes in `monte_carlo.json`, to catch a width-magnitude bug like the
pre-16-02 "40% too wide" asymmetric bug).

I traced the SQL date-pairing end-to-end for `ts_conformal_by` and
`ts_conformal_apply_by`, including the DuckDB positional-UNNEST padding
semantics for NULL/empty intermediate lists (verified empirically against a
live `duckdb` CLI, not assumed), and found the pairing itself sound: STRUCT
lists keep `(date, point, lower, upper)` bound together through a single
`ORDER BY date_col`, so duplicate dates, NULL actual/forecast rows, and
empty/NULL-list groups all degrade gracefully rather than silently
misaligning columns.

No crash, panic, or memory-safety defect was found reachable from SQL. The
issues below are two FFI-boundary inconsistencies (one NULL-vs-empty-list
contract violation, one dead/untested exported function) and a documented but
unvalidated semantic ambiguity in the new `ts_conformal_apply_by` calibration-table
matching contract.

## Warnings

### WR-01: `anofox_ts_conformal_apply_score` rejects a non-null, zero-length forecasts list as "Null pointer"

**File:** `crates/anofox-fcst-ffi/src/lib.rs:5469` (`anofox_ts_conformal_apply_score`, Phase 16 D-08)

**Issue:** Every other new Phase 16 FFI function in this file (`anofox_ts_conformal_from_pairs`,
`anofox_ts_conformal_calibrate`, and the pre-existing `anofox_ts_conformal_apply`) was written
to explicitly special-case `len == 0` before touching the corresponding data pointer:

```rust
let actuals_series = if cal_actuals_len == 0 {
    Vec::new()
} else {
    build_series(cal_actuals, cal_actuals_validity, cal_actuals_len)
};
```

`anofox_ts_conformal_apply_score` does not follow this pattern. Its entry guard is:

```rust
if forecasts.is_null() || out_lower.is_null() || out_upper.is_null() {
    set_error(out_error, ErrorCode::NullPointer, "Null pointer argument");
    return false;
}
```

A default-constructed, never-reserved `std::vector<double>` in the C++ caller has
`.data() == nullptr` (confirmed empirically with `g++`), so a direct SQL call with an
empty-but-non-NULL forecasts list hits this branch:

```sql
SELECT ts_conformal_intervals([]::DOUBLE[], 1.0, 'symmetric', NULL::DOUBLE[]);
```

This returns SQL `NULL` (via the C++ `error.code != INVALID_INPUT` → `FlatVector::SetNull`
fallback in `TsConformalApplyScoreFunction`), rather than the `STRUCT(lower: [], upper: [])`
an empty input should produce by the same convention the sibling functions document
("An empty `point_forecasts` list is valid" — see `conformal_from_pairs` doc comment).
Not currently reachable through the `ts_conformal_apply_by` macro (which only ever produces a
non-null, non-empty, or NULL `_points` list — verified against `LIST(...) FILTER (...)`
semantics), but it is reachable through a direct call to the public `ts_conformal_intervals`
4-arg overload.

**Fix:** Add the same `n_forecasts == 0` short-circuit used elsewhere:
```rust
let forecast_series = if n_forecasts == 0 {
    Vec::new()
} else {
    if forecasts.is_null() { /* NullPointer error */ }
    build_series(forecasts, forecasts_validity, n_forecasts)
};
```

### WR-02: `anofox_ts_conformal_predict_adaptive` is dead code with zero callers

**File:** `crates/anofox-fcst-ffi/src/lib.rs:5726`

**Issue:** Phase 16 (plan 16-02) changed this function's signature (added `difficulty_cal` /
`difficulty_cal_validity` parameters, changed `difficulty` → `difficulty_pred` +
`difficulty_pred_validity`) to track the new `conformal_predict_adaptive` core signature.
`grep -rn "anofox_ts_conformal_predict_adaptive" src/ crates/ test/` shows it is declared in
`src/include/anofox_fcst_ffi.h` and defined here, but is not called from any C++ scalar
function, any Rust test, or any SQL test. The adaptive path reachable from SQL goes entirely
through `_ts_conformal_from_pairs` / `ts_conformal_learn` / `ts_conformal_apply` instead.

A modified-but-never-exercised FFI entry point is a maintenance hazard: it can silently drift
out of sync with the core API (e.g. a future signature change to `conformal_predict_adaptive`
could leave this wrapper subtly wrong) with no test to catch it, and it inherits the same
null-pointer-vs-zero-length issue as WR-01 (`residuals.is_null() || difficulty_cal.is_null() ||
forecasts.is_null() || difficulty_pred.is_null()` rejects any zero-length calibration set
outright, unlike every other conformal entry point in this phase).

**Fix:** Either wire it up to a C++ caller (it looks like the originally-intended backing for a
`ts_conformal_predict_adaptive` SQL scalar that Phase 16 replaced with `_ts_conformal_from_pairs`
before shipping) and give it the same `len == 0` treatment as its siblings, or remove it along
with its header declaration to stop it rotting unexercised.

### WR-03: `ts_conformal_apply_by`'s pooled-row matching treats *any* `group_col IS NULL` calibration row as a fallback, including a real NULL group key

**File:** `src/macros/ts_macros.cpp` (`ts_conformal_apply_by`, around the `matched` CTE)

**Issue:**
```sql
matched AS (
    SELECT
        pg._grp AS _grp,
        pg._points AS _points,
        COALESCE(direct.conformity_score, pooled.conformity_score) AS _score,
        COALESCE(direct.method, pooled.method) AS _method
    FROM per_group pg
    LEFT JOIN cal direct ON direct.group_col = pg._grp
    LEFT JOIN cal pooled ON pooled.group_col IS NULL
)
```
`D-14`'s contract is "A literal score is applied through a one-row table with `group_col`
NULL", i.e. `group_col IS NULL` is meant to mark an *intentionally pooled* calibration row.
But the `pooled` join condition (`pooled.group_col IS NULL`) does not also require
`pg._grp IS NULL` or any other correlation to the forecast group — it matches *every* `pg`
row whenever the calibration table contains *any* row with `group_col = NULL`, regardless of
whether that row came from `ts_conformal_calibrate(..., params := {'pooled': true})` or from a
genuinely NULL-valued `group_col` in the user's own backtest data (e.g. an ungrouped/unknown
segment, calibrated with `pooled` defaulted to `false`). In the latter case, that group's own
calibration score is silently applied to *every other, unrelated* forecast group that lacks a
direct match — a correctness defect with no error and no documentation warning the caller
about it. This is consistent with the documented comment in the macro ("it must hold at MOST
one row per group_col value, plus optionally ONE pooled row with group_col = NULL") but that
comment describes a caller obligation that is nowhere validated, and the ambiguity between
"intentional pooled row" and "coincidental NULL group key" is not tested anywhere in
`test/sql/ts_conformal*.test` (no test uses a real NULL `group_col` value).

**Fix:** Either reject/ignore calibration rows with NULL `group_col` unless they were produced
via the documented pooled path (e.g. carry an explicit `is_pooled` flag through
`ts_conformal_calibrate`'s output instead of overloading `group_col IS NULL`), or at minimum
document the hazard prominently and add a `test/sql` case with a genuine NULL group key to
pin the (surprising) current behavior.

### WR-04: `ts_conformal_calibrate` / `ts_conformal_apply_by` caller contract ("at most one calibration row per group") is unvalidated, silently fans out on violation

**File:** `src/macros/ts_macros.cpp` (`ts_conformal_apply_by`, `matched` CTE)

**Issue:** `LEFT JOIN cal direct ON direct.group_col = pg._grp` assumes the `calibration` table
has at most one row per `group_col` value (stated in the macro's own comment). If a caller
passes a `calibration` table with duplicate rows for the same group (e.g. by concatenating two
calibration runs, or a hand-built calibration table), the join fans out, producing duplicate
output rows per forecast point with no error — a silent correctness issue rather than a loud
failure. No length/uniqueness check exists anywhere in the pipeline (Rust, C++, or SQL).

**Fix:** Add a `QUALIFY ROW_NUMBER() OVER (PARTITION BY group_col ORDER BY ...) = 1` (or an
explicit `assert`-style check that raises on duplicate `group_col`) in the `cal` CTE, or
document this prominently as a caller contract the macro validates, matching the way `D-06`
validates `alpha` rather than leaving it to silently misbehave.

## Info

### IN-01: `ConformalStrategyFFI::JackknifePlus` removal verified clean, but worth a regression test for the removed FFI enum value

**File:** `crates/anofox-fcst-ffi/src/types.rs`

The `JackknifePlus = 2` discriminant was removed outright from the `#[repr(C)]` enum rather
than kept as a reserved/unreachable slot. `grep`-ing the full tree confirms no remaining
functional reference (only comments) and every string-alias path
(`"jackknife+"|"jackknifeplus"|"jackknife_plus"|"jk+"`) raises the documented explanatory
`InvalidInput` error via `ConformalStrategy::from_str`/`anofox_ts_conformal_parse_strategy`.
No test directly exercises passing the raw former discriminant value `2` as a
`ConformalStrategyFFI` through the C ABI from a hypothetical stale/ABI-mismatched caller (not
a realistic risk given Corrosion builds Rust+C++ together, but worth noting since this is a
`#[repr(C)]` type crossing a language boundary and `from<ConformalStrategyFFI>` is a non-`match`-exhaustive-by-value-cast API surface in principle). No action required; noted for completeness.

### IN-02: `conformal_apply_score`'s "difficulty present" check differs structurally from `conformal_from_pairs`'s, but both resolve consistently

**File:** `crates/anofox-fcst-core/src/conformal.rs` (`conformal_apply_score`) vs.
`conformal_from_pairs`

`conformal_apply_score` treats "difficulty absent" as `difficulty.is_empty()` (list-level), while
`conformal_from_pairs` treats it as "every entry is `None`" (element-level,
`cal_difficulty.is_some_and(|d| d.iter().any(|v| v.is_some()))`). Traced through the C++ call
sites (`TsConformalApplyScoreFunction` normalizes a NULL difficulty list to `Vec::new()`
before the FFI call, and the macro always passes a difficulty list of matching length filled
with NULLs when no `difficulty_col` is given), both conventions currently produce the same
observable behavior for every call site in this codebase. Flagging only because the two
functions silently rely on different definitions of "no difficulty data" and a future caller
that passes a non-empty all-NULL list to `conformal_apply_score` (bypassing the C++ wrapper's
normalization) would get different semantics than it would from `conformal_from_pairs`. No
fix required now; consider unifying the convention if a new caller is added.

---

_Reviewed: 2026-10-14_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
