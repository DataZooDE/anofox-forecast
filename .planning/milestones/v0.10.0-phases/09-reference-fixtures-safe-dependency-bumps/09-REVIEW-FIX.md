---
phase: 09-reference-fixtures-safe-dependency-bumps
fixed_at: 2026-10-09T07:31:21Z
review_path: .planning/phases/09-reference-fixtures-safe-dependency-bumps/09-REVIEW.md
iteration: 1
findings_in_scope: 4
fixed: 4
skipped: 0
status: all_fixed
---

# Phase 09: Code Review Fix Report

**Fixed at:** 2026-10-09T07:31:21Z
**Source review:** .planning/phases/09-reference-fixtures-safe-dependency-bumps/09-REVIEW.md
**Iteration:** 1

**Summary:**
- Findings in scope: 4 (WR-01, WR-02, WR-03 required; IN-02 included per explicit scope — "if cheap"; IN-01 explicitly skipped per scope)
- Fixed: 4
- Skipped: 0

Worked directly on the main tree, branch `fix/statistical-correctness` (no worktree created,
per the orchestrating agent's explicit instruction and this project's standing memory note
that worktree isolation leaks into the main tree here).

## Fixed Issues

### WR-01: `gen_panel_nan_guard.py`'s non-zero-count precondition is checked before the NaN injection it's supposed to survive

**Files modified:** `validation/reference/python/gen_panel_nan_guard.py`
**Commit:** e3d485a
**Applied fix:** Moved the `non_zero_counts >= 3` assertion to run *after* the two NaN
injections (`vals[0, 7] = np.nan`, `vals[2, 13] = np.nan`), and changed the count to
`np.sum((vals != 0) & ~np.isnan(vals), axis=1)` so it actually measures non-missing,
non-zero observations post-injection rather than the pre-injection count. Updated the
assertion message to match.

**Fixture regeneration:** Ran
`uv run --locked --project validation/reference/python python validation/reference/python/gen_panel_nan_guard.py`
and compared `validation/reference/fixtures/panel_nan_guard/cases.json` before/after via
`md5sum` — both are `283b3124d7dfd7a3ce02e97850201f15` (byte-identical). `git diff` on the
fixture directory was empty. This confirms the review's prediction: with the current seed
(`20261009`) the injected NaN positions already held `0.0`, so the fix changes only the
correctness of the invariant check, not the generated fixture data.

**Verification:** `cargo test -p anofox-fcst-ffi panel_nan_guard_fixture_outcomes` — 1 passed.

### WR-02: `fit_ols_regression_matches_numpy_lstsq_fixture` only proves one of the seven `dep01_assert` cases actually ran

**Files modified:** `crates/anofox-fcst-core/src/forecast.rs`
**Commit:** 45e1ee7
**Applied fix:** Added an `asserted_count` counter incremented once per case that passes
the `dep01_assert` filter, and a final `assert_eq!(asserted_count, 7, ...)` alongside the
existing `exog_typical`-specific assertion, applied exactly as suggested in the review.

**Verification:** `cargo test -p anofox-fcst-core fit_ols_regression_matches_numpy_lstsq_fixture`
— 1 passed (241 total passed in the lib's unit-test binary).

### WR-03: Unsigned-sample-size variance divisor (`n - 1`) is unguarded for `n <= 1` in two independent test files

**Files modified:** `crates/anofox-fcst-core/src/forecast.rs`, `crates/anofox-fcst-core/tests/reference_fixtures.rs`
**Commit:** 018b1a3
**Applied fix:** Added `assert!(y.len() > 1, "{name}: need at least 2 observations to compute var_y")`
immediately before each independent `var_y` computation, in both the inline
`fit_ols_regression_matches_numpy_lstsq_fixture` test (forecast.rs) and the
`multiscale_ols_fixture_is_self_consistent` integration test (reference_fixtures.rs), as
suggested in the review. No current case in `multiscale_ols/cases.json` has `n <= 1`, so
this is a defensive guard against a future fixture edit, not a live bug fix.

**Verification:** `cargo test -p anofox-fcst-core fit_ols_regression_matches_numpy_lstsq_fixture`
and `cargo test -p anofox-fcst-core --test reference_fixtures multiscale_ols_fixture_is_self_consistent`
— both pass.

### IN-02: New `rust-msrv` CI job proves MSRV compilation only, not MSRV test execution

**Files modified:** `.github/workflows/MainDistributionPipeline.yml`
**Commit:** d999290
**Applied fix:** Added a `cargo test --workspace --locked` step to the `rust-msrv` job,
running after the existing `cargo build --workspace --all-targets --locked` step, on the
same pinned `1.89.0` toolchain. Included per the orchestrating agent's explicit scope
("optionally add `cargo +1.89.0 test` to the rust-msrv CI job if cheap") — this was a
single-line, zero-risk addition reusing the already-installed MSRV toolchain and existing
cache key.

**Verification:** `python3 -c "import yaml; yaml.safe_load(...)"` confirms the YAML still
parses. Could not execute the new CI job itself (GitHub Actions runs remotely on push),
but `cargo test --workspace --locked` was run locally (on the ambient stable toolchain,
not 1.89.0) and passed, confirming the command and flags are valid for this workspace.

## Skipped Issues

None. IN-01 (duplicated R bootstrap block in two generator scripts) was explicitly
excluded from scope by the orchestrating agent's instructions ("skip IN-01").

## Final Verification

Run on the full workspace after all four commits, on the main tree (no worktree):
- `cargo fmt --all -- --check` — clean.
- `cargo clippy --all-targets --all-features -- -D warnings` — clean.
- `cargo test --workspace` — 241 + 1 + 1 + 1 + 6 + 25 + 38 + 12 (doc) passed, 0 failed,
  across all lib/integration/doc-test binaries in both crates.

---

_Fixed: 2026-10-09T07:31:21Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
