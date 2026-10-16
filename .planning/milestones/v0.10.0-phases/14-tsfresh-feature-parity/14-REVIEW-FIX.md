---
phase: 14-tsfresh-feature-parity
fixed_at: 2026-10-11T22:54:47Z
review_path: .planning/phases/14-tsfresh-feature-parity/14-REVIEW.md
iteration: 1
findings_in_scope: 1
fixed: 1
skipped: 0
status: all_fixed
---

# Phase 14: Code Review Fix Report

**Fixed at:** 2026-10-11T22:54:47Z
**Source review:** .planning/phases/14-tsfresh-feature-parity/14-REVIEW.md
**Iteration:** 1

**Summary:**
- Findings in scope: 1 (WR-01; no CR/BL findings in REVIEW.md; IN-01/IN-02 out of scope under `critical_warning`)
- Fixed: 1
- Skipped: 0

**Worktree:** Not used — run in the main checkout on `fix/statistical-correctness` per the
project-specific instructions in the task config (no `workflow.use_worktrees` isolation for
this repo; see memory note "GSD worktree isolation leaks into main tree"). All verification
(cargo fmt/test, `make release`, `make test_release`) ran directly in the main working tree —
the numbers below are reproducible from this tree as-is.

## Fixed Issues

### WR-01: `linear_trend_slope`/`linear_trend_intercept`/`linear_trend_r_squared` silently wrong (not NaN) for n=1 series

**Files modified:** `crates/anofox-fcst-core/src/features.rs`, `test/sql/ts_features_reference.test`
**Commit:** `59a3e82`
**Applied fix:**
- `linear_trend()`'s `values.len() < 2` short-circuit (features.rs:694) now returns
  `(f64::NAN, f64::NAN, f64::NAN)` instead of the previous `(0.0, values[0], 0.0)`,
  matching `scipy.stats.linregress`'s actual n=1 behavior (slope/intercept/rvalue all NaN —
  a single point cannot determine a line's intercept either, so it is NaN, not `values[0]`).
- Checked the other trend-derived consumer the review flagged: `aggregated_linear_trend`
  (the `agg_linear_trend_*` chunk path) calls its own `scipy_linregress` helper, not
  `linear_trend()`, and was already verified algebraically to produce NaN for a
  single-chunk-mean input (`ss_xx`/`ss_yy`/`ss_xy` all `0.0`, giving `0.0/0.0 = NaN` for
  slope, and `NaN * 0.0 = NaN` for intercept) — no code change needed there, and its guard
  (`chunk_len >= n -> NaN`) means a true single-chunk case can't reach that path anyway
  (`chunk_len < n` always yields >= 2 chunk means). No other caller of `linear_trend()`
  exists besides `extract_features`'s `linear_trend_slope`/`linear_trend_intercept`/
  `linear_trend_r_squared` insertions.
- Added `test_linear_trend_nan_for_n_lt_2` (Rust unit test, `features.rs`): asserts
  `linear_trend(&[5.0])` and `linear_trend(&[])` both return all-NaN, and additionally
  asserts the fix end-to-end through `extract_features(&[5.0])` (the actual FFI/SQL-reachable
  path) for all three feature keys.
- Did not add a Python-generator fixture for n=1/n=2 (the "if cheap" fallback was taken
  instead): the 9-series fixture harness (`tsfresh_features_reference.rs`) asserts the full
  117-feature mapping per series, and adding an n=1 series would require either a new fixture
  file + `mapping.json` entries for every one of the other 116 features' n=1 behavior
  (out of scope for this fix) or asserting only a subset (inconsistent with the harness's
  "exactly 117 assertions, no silent skips" invariant). The direct scipy-referenced Rust unit
  test plus the SQL assertion below give equivalent coverage for this specific feature without
  destabilizing the 117/117 gate's existing contract.
- Added a SQL assertion in the non-json-gated `test/sql/ts_features_reference.test`: a
  one-row `ts_features_by` group now asserts `isnan(linear_trend_slope)`,
  `isnan(linear_trend_intercept)`, and `isnan(linear_trend_r_squared)` all return `true`,
  reproducing the exact SQL repro from the review end-to-end against the built extension.

**Verification performed:**
- `cargo fmt --all --check`: clean (after `cargo fmt --all` normalized the new test block).
- `cargo test -p anofox-fcst-core`: 356 lib tests + 12 reference/parity tests + 14 doctests,
  all green (including the new `test_linear_trend_nan_for_n_lt_2` and the unchanged
  117/117 `tsfresh_parity_*` suite — confirming the fix does not regress any of the 9
  fixture series, none of which has n=1).
- `RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release`: succeeded; `src/include/anofox_fcst_ffi.h`
  md5sum unchanged (`6ac65ee57445a99a7c665f380c60cb55`) before and after — byte-identical, as
  expected since this fix touches only Rust-internal logic, no FFI signatures.
  Commit hash: `59a3e82`.
- `GEN=ninja make test_release`: **All tests passed (50 skipped tests, 1935 assertions in 31
  test cases)** — above the 1931-assertion floor, and includes the new
  `ts_features_reference.test` assertions passing against the built extension.

**Logic-bug flag:** This is a one-line value-swap (hardcoded placeholders -> `f64::NAN`), not
an algorithmic change, and is directly verified against a live scipy reference, a Rust unit
test, and a SQL assertion against the built extension — not flagged as
`requires human verification`.

## Skipped Issues

None — the sole in-scope finding (WR-01) was fixed. IN-01 and IN-02 are Info-severity and
out of scope for `fix_scope: critical_warning` (per REVIEW.md frontmatter: `critical: 0,
warning: 1, info: 2`); they were not attempted.

---

_Fixed: 2026-10-11T22:54:47Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
