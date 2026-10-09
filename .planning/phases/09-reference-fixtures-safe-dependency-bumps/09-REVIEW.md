---
phase: 09-reference-fixtures-safe-dependency-bumps
reviewed: 2026-10-09T00:00:00Z
depth: standard
files_reviewed: 25
files_reviewed_list:
  - .claude/CLAUDE.md
  - .github/workflows/MainDistributionPipeline.yml
  - CONTRIBUTING.md
  - Cargo.toml
  - README.md
  - crates/anofox-fcst-core/Cargo.toml
  - crates/anofox-fcst-core/src/forecast.rs
  - crates/anofox-fcst-core/src/lib.rs
  - crates/anofox-fcst-core/tests/common/mod.rs
  - crates/anofox-fcst-core/tests/dependency_bump_regressions.rs
  - crates/anofox-fcst-core/tests/exog_ols_memory.rs
  - crates/anofox-fcst-core/tests/forecast_snapshot.rs
  - crates/anofox-fcst-core/tests/reference_fixtures.rs
  - crates/anofox-fcst-ffi/Cargo.toml
  - crates/anofox-fcst-ffi/src/lib.rs
  - validation/reference/README.md
  - validation/reference/python/fixture_io.py
  - validation/reference/python/gen_moments.py
  - validation/reference/python/gen_multiscale_ols.py
  - validation/reference/python/gen_panel_nan_guard.py
  - validation/reference/python/pyproject.toml
  - validation/reference/r/common.R
  - validation/reference/r/gen_diagnostics_reference.R
  - validation/reference/r/gen_forecast_reference.R
  - validation/reference/sql/check_dep01_exog.sql
findings:
  critical: 0
  warning: 3
  info: 2
  total: 5
status: issues_found
---

# Phase 09: Code Review Report

**Reviewed:** 2026-10-09T00:00:00Z
**Depth:** standard
**Files Reviewed:** 25
**Status:** issues_found

## Summary

This phase adds a reference-fixture harness (R + Python generators, a std+serde_json-only
Rust fixture loader shared via `#[path]` tricks into both the core crate's `cfg(test)`
build and the FFI crate's `cfg(test)` build), four new/expanded Rust test files that
pin DEP-01 (anofox-regression OLS memory blow-up) and DEP-02 (MFLES #219 runaway,
panel NaN/Inf guards, full point-forecast snapshot parity) regressions, a dependency
bump (`anofox-forecast` 0.15.3→0.15.10, `anofox-regression` 0.5.3→0.5.15), an MSRV
declaration (1.89) with a new gating CI job, and an SQL-level DEP-01 check script.

Verification performed beyond reading:
- `cargo fmt --all -- --check` — clean.
- `cargo clippy --all-targets --all-features -p anofox-fcst-core -p anofox-fcst-ffi -- -D warnings` — clean.
- `cargo test` for all four new/changed test binaries (`reference_fixtures`,
  `forecast_snapshot`, `exog_ols_memory`, `dependency_bump_regressions`) plus the two
  inline `#[cfg(test)]` additions (`fit_ols_regression_matches_numpy_lstsq_fixture` in
  `forecast.rs`, `panel_nan_guard_fixture_outcomes` in `anofox-fcst-ffi::lib.rs`) — all pass.
- Independently recomputed the SAS bimodality coefficient / adjusted skewness / adjusted
  excess-kurtosis formulas used in both `gen_moments.py` and
  `reference_fixtures.rs::population_moments_g1_g2_bc` — they match the standard
  Joanes & Gill (1998) bias-corrected definitions scipy's `bias=False` variants implement.
- Hand-traced the `check_dep01_exog.sql` 15k-row tiling arithmetic (date↔index mapping)
  against `exog_ols_memory.rs`'s `cycle().take()` tiling — both produce the same
  (history-index → regressor-value) correspondence.
- Confirmed `trueno` (the stated MSRV-1.89 floor dependency) is actually resolved in
  `Cargo.lock`, and that the new `rust-msrv` CI job's YAML parses.

No BLOCKER-class defects were found: the dependency bump, the fixture harness, and the
regression tests are internally consistent and all pass. The findings below are
robustness/coverage gaps in the test-generation and test-assertion code itself — the
kind of gap that would let a *future* fixture or generator edit silently lose test
coverage without failing loudly, which matters more than usual for a harness whose
entire purpose is to be the statistical-correctness safety net for later phases.

## Warnings

### WR-01: `gen_panel_nan_guard.py`'s non-zero-count precondition is checked before the NaN injection it's supposed to survive

**File:** `validation/reference/python/gen_panel_nan_guard.py:82-91`
**Issue:** The `intermittent_interior_gaps` case asserts `(non_zero_counts >= 3).all()`
(line 86-89) *before* `vals[0, 7] = np.nan` and `vals[2, 13] = np.nan` are applied
(lines 90-91). The assertion's own message claims "every series must keep >= 3
non-zero observations" — but it verifies that invariant pre-injection, not post-injection.
With the current seed (`20261009`), `vals[0, 7]` and `vals[2, 13]` both happen to already
be `0.0`, so the injected NaNs don't change the non-zero count and the fixture is
correct today — but only by coincidence. If the seed, the injected indices, or the
Poisson rate ever change, this check would pass while silently violating its own stated
invariant, and the downstream Rust consumer
(`anofox-fcst-ffi::panel_ffi_tests::panel_nan_guard_fixture_outcomes`) would then be
asserting `GlobalCroston` behaviour on a case with fewer non-zero observations than the
generator claims to guarantee.
**Fix:** Move the assertion after the NaN injections, and compute it over non-missing,
non-zero values:
```python
vals[0, 7] = np.nan
vals[2, 13] = np.nan
non_zero_counts = np.nansum((vals != 0) & ~np.isnan(vals), axis=1)
assert (non_zero_counts >= 3).all(), (
    f"intermittent_interior_gaps: every series must keep >= 3 non-missing, non-zero "
    f"observations after gap injection, got {non_zero_counts.tolist()}"
)
```

### WR-02: `fit_ols_regression_matches_numpy_lstsq_fixture` only proves one of the seven `dep01_assert` cases actually ran

**File:** `crates/anofox-fcst-core/src/forecast.rs:3568-3680` (loop at ~3583, `ran_exog_typical` check at ~3674)
**Issue:** The loop over `multiscale_ols/cases.json`'s `cases` object `continue`s whenever
`case["dep01_assert"]` is missing or not `true` (`.unwrap_or(false)`, line ~3584). The
fixture currently has seven cases with `dep01_assert: true` (`well`, `multiscale`,
`multiscale3`, `collinear`, `collinear_big`, `constcol`, `exog_typical`) and one with
`false` (`n_lt_p`). The test only asserts that the *`exog_typical`* case specifically ran
(`assert!(ran_exog_typical, ...)` at the end) — there is no assertion on the total number
of cases that actually executed their `coef`/`fitted` comparisons. A future regeneration
of `cases.json` that accidentally drops `dep01_assert` on, say, `collinear_big` (typo,
serialization bug, or a generator edit) would silently reduce this test's coverage to
zero evidence for that case, and `cargo test` would still report green.
**Fix:** Track and assert the total count, in addition to the `exog_typical`-specific flag:
```rust
let mut asserted_count = 0usize;
for (name, case) in cases {
    let dep01_assert = case["dep01_assert"].as_bool().unwrap_or(false);
    if !dep01_assert { continue; }
    asserted_count += 1;
    // ... existing body ...
}
assert_eq!(
    asserted_count, 7,
    "multiscale_ols/cases.json: expected 7 dep01_assert cases to run, ran {asserted_count}"
);
```

### WR-03: Unsigned-sample-size variance divisor (`n - 1`) is unguarded for `n <= 1` in two independent test files

**File:** `crates/anofox-fcst-core/src/forecast.rs:~3617` and `crates/anofox-fcst-core/tests/reference_fixtures.rs:350`
**Issue:** Both `fit_tol` computations divide by `(y.len() as f64 - 1.0)` with no
guard. For `n == 1` this is division by zero (`var_y` → `+inf`, since the numerator
sum-of-squared-deviations is also `0.0`, giving `0.0/0.0 = NaN` — actually worse than
`+inf`: `fit_tol` becomes `NaN`, and every `assert_close` call against a `NaN` bound
silently *passes* because `diff <= bound` is `false` for `NaN`, which is caught... but
only when `got` and `want` differ; when `got == want` exactly, the comparison masks the
fact that the bound itself is meaningless). For `n == 0` this is also division by zero.
Today every case in `multiscale_ols/cases.json` has `n >= 3`, so this is not currently
triggered, but there is no assertion anywhere guarding it, and both this inline test and
the integration-test copy of the same formula would independently need fixing if a future
`n <= 1` case is ever added (e.g. a minimum-data edge case for a later phase's OLS
regressor coverage).
**Fix:** Assert `n > 1` before computing `var_y`, e.g.:
```rust
assert!(y.len() > 1, "{name}: need at least 2 observations to compute var_y");
```

## Info

### IN-01: `ref_script_dir()`-equivalent bootstrap is duplicated verbatim in two R generators

**File:** `validation/reference/r/gen_diagnostics_reference.R:12-16`, `validation/reference/r/gen_forecast_reference.R:11-15`
**Issue:** Both scripts inline the identical 5-line `commandArgs()`/`--file=` resolution
block (with the comment "common.R is not loaded yet, so the script-dir resolution is
inlined here") because `common.R` itself needs to be located before it can be sourced.
This is a reasonable chicken-and-egg workaround, but the duplicated logic can drift if
one copy is edited and the other isn't.
**Fix:** Extract the five lines into a tiny `validation/reference/r/_bootstrap.R` with a
single `source(normalizePath(file.path(dirname(sys.frame(1)$ofile), "_bootstrap.R")))`-free
mechanism is awkward in R without the same bootstrap problem; a lower-risk fix is simply a
code comment cross-referencing the sibling script so a future edit to one is prompted to
check the other, or accept the duplication as intentional (it is small and stable).

### IN-02: New `rust-msrv` CI job proves MSRV compilation only, not MSRV test execution

**File:** `.github/workflows/MainDistributionPipeline.yml:178-205`
**Issue:** The job runs `cargo build --workspace --all-targets --locked` (which does
compile test binaries, since `--all-targets` includes `tests`) but never executes
`cargo test`. This means a test that compiles under 1.89 but panics or behaves
differently at runtime on exactly that toolchain (e.g. a floating-point/SIMD dispatch
difference) would not be caught by this job — only by `build-and-test-rust`, which runs
on `stable` (not pinned to 1.89).
**Fix:** Consider adding `cargo test --workspace --locked` to the MSRV job if runtime
parity at the MSRV floor matters as much as compile-time parity; otherwise document (e.g.
in `CONTRIBUTING.md`'s MSRV section) that the job is build-only by design.

---

_Reviewed: 2026-10-09T00:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
