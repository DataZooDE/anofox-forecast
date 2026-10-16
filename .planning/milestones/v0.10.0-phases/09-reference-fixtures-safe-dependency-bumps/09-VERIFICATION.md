---
phase: 09-reference-fixtures-safe-dependency-bumps
verified: 2026-10-09T00:00:00Z
status: passed
score: 4/4 must-haves verified (ROADMAP success criteria); 0 gaps
covered_files:
  - .claude/CLAUDE.md
  - .github/workflows/MainDistributionPipeline.yml
  - .planning/PROJECT.md
  - .planning/REQUIREMENTS.md
  - .planning/codebase/CONCERNS.md
  - .planning/codebase/STACK.md
  - .planning/phases/09-reference-fixtures-safe-dependency-bumps/09-01-PLAN.md
  - .planning/phases/09-reference-fixtures-safe-dependency-bumps/09-01-SUMMARY.md
  - .planning/phases/09-reference-fixtures-safe-dependency-bumps/09-02-PLAN.md
  - .planning/phases/09-reference-fixtures-safe-dependency-bumps/09-02-SUMMARY.md
  - .planning/phases/09-reference-fixtures-safe-dependency-bumps/09-03-PLAN.md
  - .planning/phases/09-reference-fixtures-safe-dependency-bumps/09-03-SUMMARY.md
  - .planning/phases/09-reference-fixtures-safe-dependency-bumps/09-CONTEXT.md
  - .planning/phases/09-reference-fixtures-safe-dependency-bumps/09-RESEARCH.md
  - .planning/phases/09-reference-fixtures-safe-dependency-bumps/09-REVIEW-FIX.md
  - .planning/phases/09-reference-fixtures-safe-dependency-bumps/09-REVIEW.md
  - CONTRIBUTING.md
  - Cargo.lock
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
  - validation/reference/python/uv.lock
  - validation/reference/r/common.R
  - validation/reference/r/gen_diagnostics_reference.R
  - validation/reference/r/gen_forecast_reference.R
  - validation/reference/sql/check_dep01_exog.sql
covered_digest: "v1:sha256:e9f8bec5fba51c5908c3c94383477e7d1674191dd3978fc7632f90b3cf90caf8"
behavior_unverified: 0
overrides_applied: 0
---

# Phase 9: Reference Fixtures & Safe Dependency Bumps Verification Report

**Phase Goal:** Every later fix can be proven against checked-in R/Python reference values, and the extension runs on the low-risk dependency bumps — with exogenous regression already producing correct coefficients and the stated MSRV matching reality.
**Verified:** 2026-10-09
**Status:** passed
**Re-verification:** No — initial verification

All checks below were run directly against the working tree/branch `fix/statistical-correctness` (not read from SUMMARY claims) — fixtures were regenerated, every relevant `cargo test` invocation was executed, both MSRV toolchains were invoked, the release extension (already built on this branch at `build/release/duckdb`, confirmed via `CMakeCache.txt` to have used `rustc 1.89.0`) was re-run against the SQL check script, and `git log`/`git show`/`git diff` were used to confirm every claimed commit and diff actually exists.

## Goal Achievement

### Observable Truths (ROADMAP Success Criteria)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Checked-in R+Python generators regenerate fixtures byte-identically; a Rust test loads a fixture and asserts values to a stated tolerance; README documents regeneration/tool-versions/tolerances (TEST-01) | ✓ VERIFIED | `Rscript validation/reference/r/gen_forecast_reference.R && Rscript validation/reference/r/gen_diagnostics_reference.R && git diff --exit-code -- validation/reference/fixtures/` → exit 0 (no diff). `uv run --locked --project validation/reference/python python validation/reference/python/gen_{moments,multiscale_ols,panel_nan_guard}.py && git diff --exit-code -- validation/reference/fixtures/` → exit 0. `cargo test -p anofox-fcst-core --test reference_fixtures` → `6 passed; 0 failed`. README contains `forecast_snapshot`, `ANOFOX_BLESS_FORECAST_SNAPSHOT`, `check_dep01_exog.sql`, `point_rel`, `allowed_to_change`, `anofox-fcst-ffi`, `panel_nan_guard_fixture_outcomes` (all present, grep-confirmed) |
| 2 | With anofox-regression `=0.5.15`, `ts_forecast_exog_by` on the multi-scale-regressor fixture matches numpy `lstsq` (≈ −8.8 at step 1, was ≈ −79/−25 on 0.5.3); 15k-row exogenous fit has no O(n²) memory blow-up (DEP-01) | ✓ VERIFIED | `grep -A1 'name = "anofox-regression"' Cargo.lock` shows `0.4.0` (fdars-core, Phase 15) and `0.5.15`. `cargo test -p anofox-fcst-core --lib fit_ols_regression_matches_numpy_lstsq_fixture` → `1 passed`. SQL check re-run live: `./build/release/duckdb -unsigned -bail < validation/reference/sql/check_dep01_exog.sql` printed `DEP01_SQL_PASS typical` and `DEP01_SQL_PASS 15k`, MAXRSS_KB=169840 (well under the 1,000,000 bound). `cargo test -p anofox-fcst-core --test exog_ols_memory -- --nocapture` → peak heap delta 73,072,492 bytes (69.69 MiB) vs 256 MiB limit, `1 passed` |
| 3 | With anofox-forecast `≥0.15.10`, MFLES #219 is bounded; panel NaN-guard fixture gives no NaN; every other `ts_forecast_by` model is unchanged vs the 0.15.3 snapshot outside documented MFLES/panel/Laplace (DEP-02) | ✓ VERIFIED | `grep -A1 'name = "anofox-forecast"' Cargo.lock` → `0.15.10`. `cargo test -p anofox-fcst-core --test dependency_bump_regressions` → `1 passed` (MFLES/AutoMFLES bounded). `cargo test -p anofox-fcst-ffi --lib panel_nan_guard_fixture_outcomes` → `1 passed`. `cargo test -p anofox-fcst-core --test forecast_snapshot -- --nocapture` → `1 passed`, 17 `ALLOWED-CHANGE` lines, every one naming `MFLES`/`AutoMFLES`/`Laplace`/`Laplace[auto_aid]`/`Laplace[skaters]`, zero violations against any other of the 125 entries |
| 4 | Documented MSRV (README/CLAUDE.md/docs) and CI toolchain pin equal the true minimum the dependency tree requires; a build on exactly that toolchain succeeds (DEP-04) | ✓ VERIFIED | `Cargo.toml` has `rust-version = "1.89"`; `crates/anofox-fcst-core/Cargo.toml` and `crates/anofox-fcst-ffi/Cargo.toml` both have `rust-version.workspace = true`. `git grep '1\.86' -- Cargo.toml README.md .claude/CLAUDE.md .planning/PROJECT.md .planning/codebase/STACK.md .planning/codebase/CONCERNS.md` → no matches. All 6 of README/CONTRIBUTING/.claude/CLAUDE.md/.planning/PROJECT.md/.planning/codebase/{STACK,CONCERNS}.md contain `1.89`. `.github/workflows/MainDistributionPipeline.yml` `rust-msrv` job pins `dtolnay/rust-toolchain@1.89.0` and runs both `cargo build --workspace --all-targets --locked` and `cargo test --workspace --locked`. Live re-run: `cargo +1.89.0 build --workspace --all-targets --locked` → `Finished`; `cargo +1.89.0 test --workspace --locked` → all 9 test-result lines `ok`, 0 failed; `cargo +1.88.0 check -p anofox-fcst-core` → refused with `error: rustc 1.88.0 is not supported ... requires rustc 1.89`. Release extension was built on exactly 1.89.0 (`build/release/CMakeCache.txt`: `Rust_VERSION:INTERNAL=1.89.0`), and `make test_release`'s saved log shows `All tests passed (50 skipped tests [require json], 1357 assertions in 17 test cases)` |

**Score:** 4/4 ROADMAP success criteria verified; 0 present-but-behavior-unverified; 0 gaps

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `validation/reference/r/common.R`, `gen_forecast_reference.R`, `gen_diagnostics_reference.R` | R generators + provenance helpers | ✓ VERIFIED | Present, regenerate byte-identically |
| `validation/reference/fixtures/r_forecast/*.json`, `r_diagnostics/*.json` | R fixture families | ✓ VERIFIED | Present, provenance-contract-passing |
| `crates/anofox-fcst-core/tests/common/mod.rs` | CARGO_MANIFEST_DIR-relative loader | ✓ VERIFIED | Present; `grep -c 'env!('` ≥ 1; `grep -cE 'anofox_fcst_(core|ffi)'` = 0 (crate-agnostic, as required) |
| `crates/anofox-fcst-core/tests/reference_fixtures.rs` | Provenance contract + self-consistency tests | ✓ VERIFIED | 6 tests, all pass |
| `validation/reference/python/{pyproject.toml,uv.lock,fixture_io.py,gen_moments.py,gen_multiscale_ols.py,gen_panel_nan_guard.py}` | uv-pinned Python toolchain + generators | ✓ VERIFIED | Present; exact pins confirmed; regenerate byte-identically; `.venv` untracked |
| `validation/reference/fixtures/{moments,multiscale_ols,panel_nan_guard}/*` | Python fixture families | ✓ VERIFIED | Present; exog_typical coefficients match audit-pinned values (confirmed via inline Rust test, not re-derived independently in this verification pass, but the Rust assertion against the fixture passed) |
| `crates/anofox-fcst-core/tests/forecast_snapshot.rs` + `validation/reference/fixtures/forecast_snapshot/point_forecasts.json` | Pre-bump snapshot + compare-mode test | ✓ VERIFIED | 125-entry snapshot predates the bump (`S^` Cargo.lock holds 0.15.3; confirmed via `git show "$S:Cargo.lock"` in the plan's own acceptance check, re-derivable); compare test passes live |
| `crates/anofox-fcst-core/tests/exog_ols_memory.rs`, `tests/dependency_bump_regressions.rs` | Post-bump memory/MFLES guards | ✓ VERIFIED | Both pass live with recorded numbers matching SUMMARY |
| `crates/anofox-fcst-ffi/src/lib.rs` (`panel_nan_guard_fixture_outcomes`) | Panel NaN-guard FFI test | ✓ VERIFIED | Passes live |
| `.github/workflows/MainDistributionPipeline.yml` (`rust-msrv` job) | CI MSRV pin | ✓ VERIFIED | Job present, pins 1.89.0, builds + tests, additions-only diff |
| `validation/reference/sql/check_dep01_exog.sql` | SQL-level DEP-01 check | ✓ VERIFIED | Re-run live against the already-built extension: both PASS lines, exit 0 |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| `crates/anofox-fcst-core/src/lib.rs` | `crates/anofox-fcst-core/tests/common/mod.rs` | `#[cfg(test)] #[path = "../tests/common/mod.rs"] mod test_fixtures;` | ✓ WIRED | `grep -c` = 1; `fit_ols_regression_matches_numpy_lstsq_fixture` compiles and runs through it |
| `crates/anofox-fcst-ffi/src/lib.rs` | `crates/anofox-fcst-core/tests/common/mod.rs` | `#[path = "../../anofox-fcst-core/tests/common/mod.rs"]` | ✓ WIRED | `panel_nan_guard_fixture_outcomes` passes, proving the cross-crate `#[path]` resolves |
| `validation/reference/sql/check_dep01_exog.sql` | `validation/reference/fixtures/multiscale_ols/{cases.json,exog_typical.csv}` | `read_csv` + `read_text`/`json_extract` for tolerance | ✓ WIRED | Live run reads the fixture-declared tolerance and produces both PASS verdicts |
| `crates/anofox-fcst-core/tests/forecast_snapshot.rs` | `Cargo.lock` | locked `anofox-forecast` version decides strict vs. allowed-change comparison | ✓ WIRED | `same_version=false` printed in the live run (fixture 0.15.3, current 0.15.10), correctly triggering allowed-change mode |

### Requirements Coverage

| Requirement | Source Plan(s) | Description | Status | Evidence |
|---|---|---|---|---|
| TEST-01 | 09-01, 09-02, 09-03 | Reference-fixture harness (R+Python generators, Rust loader, provenance contract, README) | ✓ SATISFIED | All fixture families regenerate byte-identically; `reference_fixtures` test suite (6 tests) + `forecast_snapshot` pass; README documents layout/contract/regeneration/tolerances |
| DEP-01 | 09-01 (gate), 09-02 (fixture), 09-03 | anofox-regression `=0.5.3` → `=0.5.15`; OLS matches numpy lstsq; no O(n²) memory blow-up | ✓ SATISFIED | Cargo.lock at 0.5.15; inline test passes; SQL check passes; memory test passes at 69.69 MiB peak |
| DEP-02 | 09-01 (gate), 09-02 (fixture), 09-03 | anofox-forecast → ≥0.15.10; MFLES/panel/Laplace fixes; other models unchanged | ✓ SATISFIED | Cargo.lock at 0.15.10; MFLES bounded test passes; panel NaN-guard test passes; snapshot shows only MFLES/AutoMFLES/Laplace moved |
| DEP-04 | 09-03 | Documented/CI MSRV matches true dependency-tree requirement | ✓ SATISFIED | rust-version=1.89 everywhere; CI job pinned; 1.89.0 build+test succeed; 1.88.0 refused; release built on 1.89.0 |

No orphaned requirements: `.planning/REQUIREMENTS.md` traceability table maps exactly TEST-01/DEP-01/DEP-02/DEP-04 to Phase 9, and the union of `requirements:` fields across 09-01/09-02/09-03 PLAN frontmatter is exactly these four IDs.

### Anti-Patterns Found

Scanned every file touched by this phase's commits (diff from before the phase to `2f2900f`) for `TBD|FIXME|XXX|TODO|HACK|PLACEHOLDER`. No matches were introduced by this phase. One pre-existing `TODO ENS-03` comment in `crates/anofox-fcst-core/src/forecast.rs` (line ~2709, introduced in Phase 05 commit `bbfce39`, unrelated to this phase's diff, and references a formal follow-up ID) is not a Phase 9 debt marker and is excluded from the gate.

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| (none introduced by Phase 9) | — | — | — | — |

### Behavioral Spot-Checks / Live Re-Execution

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| R fixture regeneration determinism | `Rscript gen_forecast_reference.R && gen_diagnostics_reference.R && git diff --exit-code` | exit 0, no diff | ✓ PASS |
| Python fixture regeneration determinism | `uv run --locked ... gen_{moments,multiscale_ols,panel_nan_guard}.py && git diff --exit-code` | exit 0, no diff | ✓ PASS |
| Rust fixture self-consistency | `cargo test -p anofox-fcst-core --test reference_fixtures` | 6 passed; 0 failed | ✓ PASS |
| Inline DEP-01 OLS test | `cargo test -p anofox-fcst-core --lib fit_ols_regression_matches_numpy_lstsq_fixture` | 1 passed | ✓ PASS |
| Pre/post-bump point-forecast snapshot | `cargo test -p anofox-fcst-core --test forecast_snapshot -- --nocapture` | 1 passed; 17 ALLOWED-CHANGE lines, all MFLES/AutoMFLES/Laplace | ✓ PASS |
| 15k-row exogenous peak-heap guard | `cargo test -p anofox-fcst-core --test exog_ols_memory -- --nocapture` | 1 passed; peak 69.69 MiB vs 256 MiB limit | ✓ PASS |
| MFLES #219 bound | `cargo test -p anofox-fcst-core --test dependency_bump_regressions` | 1 passed | ✓ PASS |
| Panel NaN-guard outcomes | `cargo test -p anofox-fcst-ffi --lib panel_nan_guard_fixture_outcomes` | 1 passed | ✓ PASS |
| Full workspace suite | `cargo test --workspace --all-features` | 241+1+1+1+6+25+38+12 passed; 0 failed | ✓ PASS |
| fmt / clippy | `cargo fmt --all -- --check`; `cargo clippy --all-targets --all-features -- -D warnings` | both clean, exit 0 | ✓ PASS |
| MSRV build on 1.89.0 | `cargo +1.89.0 build --workspace --all-targets --locked` | `Finished` | ✓ PASS |
| MSRV test on 1.89.0 | `cargo +1.89.0 test --workspace --locked` | all 9 suites ok, 0 failed | ✓ PASS |
| MSRV negative control | `cargo +1.88.0 check -p anofox-fcst-core` | refused: `requires rustc 1.89` | ✓ PASS |
| SQL-level DEP-01 check against built extension | `./build/release/duckdb -unsigned -bail < check_dep01_exog.sql` (RSS-wrapped) | `DEP01_SQL_PASS typical`, `DEP01_SQL_PASS 15k`, MAXRSS_KB=169840 | ✓ PASS |
| Release toolchain confirmation | `grep Rust_VERSION build/release/CMakeCache.txt` | `1.89.0` | ✓ PASS |
| Native sqllogictest suite | saved `build/09-03-test-release.log` tail | `All tests passed (50 skipped [require json], 1357 assertions in 17 test cases)` | ✓ PASS |
| Commit existence (fabrication check) | `git show --no-patch` on all 15 commits named across 09-01/02/03 SUMMARYs + review-fix | all resolve to real commits with matching subjects | ✓ PASS |
| Review findings closed | `09-REVIEW.md` (3 warnings + 2 info) vs `09-REVIEW-FIX.md` | WR-01/WR-02/WR-03 + IN-02 fixed with commits `e3d485a`/`45e1ee7`/`018b1a3`/`d999290`, all exist and match claimed diffs; IN-01 explicitly skipped (not required) | ✓ PASS |

### Human Verification Required

None. Every must-have in this phase is mechanically checkable (fixture regeneration, cargo test results, CI YAML content, MSRV toolchain behavior, SQL script output against the built extension) and was independently re-executed in this verification pass rather than taken from the SUMMARY.

### Gaps Summary

No gaps. All four ROADMAP success criteria, all four requirement IDs (TEST-01, DEP-01, DEP-02, DEP-04), and every PLAN-frontmatter must-have across 09-01/09-02/09-03 were independently re-verified against the live codebase (not the SUMMARY text) by regenerating fixtures, running the relevant `cargo test` targets, invoking both the 1.89.0 and 1.88.0 toolchains, and re-running the SQL check against the already-built release extension. All code-review findings (09-REVIEW.md) were confirmed fixed in 09-REVIEW-FIX.md with real, inspectable commits.

---

*Verified: 2026-10-09*
*Verifier: Claude (gsd-verifier)*
