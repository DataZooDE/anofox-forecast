---
phase: 09-reference-fixtures-safe-dependency-bumps
plan: 03
subsystem: testing
tags: [cargo, rust-version, msrv, anofox-forecast, anofox-regression, dependency-bump, duckdb-extension, sqllogictest]

# Dependency graph
requires:
  - phase: 09-01
    provides: "validation/reference/ R-side fixture harness, CARGO_MANIFEST_DIR-relative Rust loader (crates/anofox-fcst-core/tests/common/mod.rs), fixture_provenance_contract test, human-approved package-legitimacy verdict for anofox-regression 0.5.15 / anofox-forecast 0.15.10"
  - phase: 09-02
    provides: "multiscale_ols/{cases.json,exog_typical.csv} (DEP-01 reference), panel_nan_guard/cases.json (DEP-02 reference), 4 Rust self-consistency tests"
provides:
  - "forecast_snapshot fixture family + crates/anofox-fcst-core/tests/forecast_snapshot.rs: 125-entry pre-bump point-forecast snapshot (anofox-forecast 0.15.3) with version-aware compare mode (ALLOWED-CHANGE for MFLES/AutoMFLES/Laplace only)"
  - "Workspace on anofox-regression =0.5.15, anofox-forecast 0.15.10 — both compile unchanged, Cargo.lock diff is 2 version/checksum lines, 0 new/removed crates"
  - "Inline DEP-01 test fit_ols_regression_matches_numpy_lstsq_fixture (forecast.rs), driven by a new crate-root #[cfg(test)] #[path] test_fixtures module in lib.rs — RED on 0.5.3, GREEN on 0.5.15"
  - "crates/anofox-fcst-core/tests/exog_ols_memory.rs: 15,330-row tiled exogenous-fit peak-heap guard (own global-allocator test binary)"
  - "crates/anofox-fcst-core/tests/dependency_bump_regressions.rs: MFLES/AutoMFLES issue-219 bounded-forecast guard"
  - "anofox-fcst-ffi: #[path]'d test_fixtures loader + panel_nan_guard_fixture_outcomes test (DEP-02 panel NaN/Inf guard proof)"
  - "rust-version = \"1.89\" declared in [workspace.package], inherited by both crates; new CI job rust-msrv pinned to dtolnay/rust-toolchain@1.89.0; README/CONTRIBUTING/.claude/CLAUDE.md/.planning docs updated from 1.86 to 1.89"
  - "validation/reference/sql/check_dep01_exog.sql: SQL-level DEP-01 check (ts_forecast_exog_by/OptimizedTheta) against the built extension, both 365-row and 15,330-row tiled designs"
  - "validation/reference/README.md: forecast_snapshot family row, SQL checks section, anofox-fcst-ffi loader note"
affects: ["Phase 10 (forecast wrapper fixes build on anofox-forecast 0.15.10/anofox-regression 0.5.15; FCST-04 AutoARIMA exog stand-in; FCST-05 rank-deficient NaN-aliasing)", "Phase 11", "Phase 15 (DEP-03 fdars-core 0.47 — the anofox-regression 0.4.0 duplicate stays until then)", "Phase 17"]

# Actuals (#2632)
actuals:
  tokens: 36692
  tasks: 3
  commits: 5
plan_head_before: 03ffa4e555e7be6754d89b4c463c2b57c6d9e371

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Point-forecast snapshot fixture (forecast_snapshot family): captured BEFORE a dependency bump, compared AFTER, with a fixture-declared allowed_to_change list — any model outside that list that moves blocks the bump instead of silently widening the list"
    - "Own global-allocator integration-test binary (exog_ols_memory.rs) for a peak-heap regression guard — each cargo integration test file is a separate process/crate, so #[global_allocator] in one never affects sibling test binaries"
    - "#[cfg(test)] #[path] re-pathing of the shared tests/common/mod.rs loader into BOTH crates' own cfg(test) builds (anofox-fcst-core via lib.rs, anofox-fcst-ffi via its own #[path] one directory further up) — lets inline tests of private functions (fit_ols_regression) load validation/ fixtures without a visibility change"
    - "MSRV declared as rust-version in [workspace.package] and inherited via rust-version.workspace = true — cargo itself now refuses an old toolchain with a one-line error instead of a confusing downstream compile failure in a transitive dependency"

key-files:
  created:
    - crates/anofox-fcst-core/tests/forecast_snapshot.rs
    - validation/reference/fixtures/forecast_snapshot/point_forecasts.json
    - crates/anofox-fcst-core/tests/exog_ols_memory.rs
    - crates/anofox-fcst-core/tests/dependency_bump_regressions.rs
    - validation/reference/sql/check_dep01_exog.sql
  modified:
    - crates/anofox-fcst-core/src/lib.rs
    - crates/anofox-fcst-core/src/forecast.rs
    - Cargo.toml
    - Cargo.lock
    - crates/anofox-fcst-ffi/Cargo.toml
    - crates/anofox-fcst-ffi/src/lib.rs
    - crates/anofox-fcst-core/Cargo.toml
    - .github/workflows/MainDistributionPipeline.yml
    - README.md
    - CONTRIBUTING.md
    - .claude/CLAUDE.md
    - .planning/PROJECT.md
    - .planning/codebase/STACK.md
    - .planning/codebase/CONCERNS.md
    - validation/reference/README.md

key-decisions:
  - "Captured the apply_regression RED evidence manually (temporary eprintln, reverted before commit) rather than restructuring the test, because the coef assertion panics before apply_regression runs for exog_typical. On 0.5.3: coeffs=[-19.613, 5.074, 0.298, -1.986, 9.925] (scrambled vs want [2.851, -1.986, 5.074, 0.298, 9.925]), apply_regression step1 = -24.89. This differs from 09-RESEARCH.md's quoted \"~-79\" — that figure came from the full ts_forecast_exog_by SQL path (regression effect + ThetaX residual forecast combined), not from calling apply_regression alone on the raw fit_ols_regression coefficients. Both are genuine 0.5.3 scrambling evidence; recorded here for anyone re-deriving the comparison."
  - "Added explicit INSTALL json; LOAD json; at the top of check_dep01_exog.sql (Rule 3 — blocking: json_extract is not in the catalog by default and the plan's action step only described checking availability, not guaranteeing it). The json extension was already locally cached for this DuckDB build (v1.5.6) from a prior session, so no network call was made this run."
  - "forecast_snapshot.rs's bless-mode run on the unbumped tree needed only ONE pass — all 125 entries came back outcome \"ok\" with no process aborts, so parameters.nondeterministic / parameters.excluded (the plan's fallback paths for a flaky or crashing model) were never needed and are omitted from the fixture."

patterns-established:
  - "A model whose point forecasts are expected to change across a dependency bump must be named in the snapshot fixture's parameters.allowed_to_change (with the exact upstream fix/version that explains the change) BEFORE the bump lands — the compare-mode test treats any other model's drift as a blocking violation, never something to silently re-bless."
  - "cargo +<toolchain> build/check against an explicitly-named rustup toolchain is the MSRV falsification pattern for this repo — both positive (1.89.0 builds) and negative (1.88.0 refused by cargo's own rust-version gate) runs are part of the same proof, not just the positive one."

requirements-completed: [DEP-01, DEP-02, DEP-04, TEST-01]

coverage:
  - id: D1
    description: "Pre-bump point-forecast snapshot (125 entries: 41 labels x 3 series + 2 issue-219 labels) captured on anofox-forecast 0.15.3/anofox-regression 0.5.3 before any version change, with a fixture-declared allowed_to_change list and compare mode that blocks on any non-listed drift"
    requirement: "TEST-01"
    verification:
      - kind: integration
        ref: "cargo test -p anofox-fcst-core --test forecast_snapshot#point_forecasts_match_snapshot"
        status: pass
    human_judgment: false
  - id: D2
    description: "anofox-regression bumped =0.5.3 -> =0.5.15, anofox-forecast bumped 0.15.3 -> 0.15.10; both compile unchanged; Cargo.lock diff is exactly 2 version/checksum line-pairs, 0 new/removed crates, git-source count stays 2 (argmin fork)"
    requirement: "DEP-01"
    verification:
      - kind: other
        ref: "git diff 43b6eef..6ceb115 -- Cargo.lock (this session: only anofox-forecast/anofox-regression version+checksum changed); cargo test --workspace --all-features (241+1+6+25+38+12 passed, 0 failed)"
        status: pass
    human_judgment: false
  - id: D3
    description: "Inline DEP-01 test fit_ols_regression_matches_numpy_lstsq_fixture: RED on 0.5.3 (exog_typical coef[0] got -19.613 want 2.851), GREEN on 0.5.15, driven by a new crate-root #[path] test_fixtures module"
    requirement: "DEP-01"
    verification:
      - kind: unit
        ref: "cargo test -p anofox-fcst-core --lib forecast::tests::fit_ols_regression_matches_numpy_lstsq_fixture"
        status: pass
    human_judgment: false
  - id: D4
    description: "Post-bump guards: 15,330-row tiled exogenous fit peaks at 69.69 MiB (limit 256 MiB, process MAXRSS 15,772 KB < 300,000 KB bound); MFLES/AutoMFLES on the issue-219 reproducer stay finite, < 2.5x median, > 0; every panel_nan_guard case/method gets exactly its fixture outcome"
    requirement: "DEP-01"
    verification:
      - kind: integration
        ref: "cargo test -p anofox-fcst-core --test exog_ols_memory#exog_fit_15k_rows_has_bounded_peak_heap; cargo test -p anofox-fcst-core --test dependency_bump_regressions#mfles_issue_219_forecasts_stay_bounded; cargo test -p anofox-fcst-ffi --lib panel_ffi_tests::panel_nan_guard_fixture_outcomes"
        status: pass
    human_judgment: false
  - id: D5
    description: "MFLES/AutoMFLES/Laplace are the only entries that changed across the full 125-entry forecast_snapshot comparison post-bump — issue_219/{MFLES,AutoMFLES} moved by max_abs_diff=12279.87 (the #219 runaway fix), 15 other ALLOWED-CHANGE lines are sub-1e-9-to-0.108 floating point drift in MFLES/AutoMFLES/Laplace; every other model is byte-for-byte unchanged"
    requirement: "DEP-02"
    verification:
      - kind: integration
        ref: "cargo test -p anofox-fcst-core --test forecast_snapshot -- --nocapture (this session: 17 ALLOWED-CHANGE lines, all naming MFLES/AutoMFLES/Laplace; 0 violations)"
        status: pass
    human_judgment: false
  - id: D6
    description: "MSRV 1.89 declared as rust-version (workspace + both crates), stated consistently in README/CONTRIBUTING/.claude/CLAUDE.md/.planning docs, pinned by a new repo-owned CI job (rust-msrv, dtolnay/rust-toolchain@1.89.0), and positively proven: cargo +1.89.0 build/test succeed, cargo +1.88.0 check is refused by cargo's own rust-version gate"
    requirement: "DEP-04"
    verification:
      - kind: other
        ref: "cargo +1.89.0 build --workspace --all-targets --locked (Finished); cargo +1.89.0 test --workspace --locked (all passed); cargo +1.88.0 check -p anofox-fcst-core (refused: \"requires rustc 1.89\")"
        status: pass
    human_judgment: false
  - id: D7
    description: "Release extension built once on the MSRV toolchain (RUSTUP_TOOLCHAIN=1.89.0 GEN=ninja make release; CMakeCache.txt confirms Rust_VERSION=1.89.0), the full native sqllogictest suite passes against it (make test_release), and check_dep01_exog.sql reproduces the numpy lstsq regression effect through ts_forecast_exog_by on both the 365-row and 15,330-row tiled designs"
    requirement: "DEP-01"
    verification:
      - kind: integration
        ref: "make test_release (this session: All tests passed, 50 skipped tests [require json], 1357 assertions in 17 test cases); validation/reference/sql/check_dep01_exog.sql via ./build/release/duckdb -unsigned -bail (DEP01_SQL_PASS typical, DEP01_SQL_PASS 15k, MAXRSS_KB 175692)"
        status: pass
    human_judgment: false

# Metrics
duration: ~50min
completed: 2026-10-09
status: complete
---

# Phase 9 Plan 3: Safe Dependency Bumps + Honest MSRV Summary

**anofox-regression 0.5.3 -> 0.5.15 and anofox-forecast 0.15.3 -> 0.15.10, proven against a 125-entry pre-bump point-forecast snapshot and three post-bump guard tests; MSRV corrected from a stated-but-false "1.86+" to the verified-true 1.89, enforced by cargo and a dedicated CI job; the exogenous-regression fix proven through the actual built extension via a new SQL-level check.**

## Performance

- **Duration:** ~50 min
- **Started:** ~2026-10-09T08:25:00+02:00 (approx, first commit 08:29:53+02:00)
- **Completed:** 2026-10-09T09:14:25+02:00 (last task commit)
- **Tasks:** 3 completed
- **Files modified:** 20 (5 created + 15 modified; matches the plan's `files_modified` list exactly)

## Accomplishments
- Captured a 125-entry point-forecast snapshot of every `ts_forecast_by` model (plus AutoEnsemble, 3 ETS-spec variants, 2 Laplace variants) on AirPassengers/random-walk/intermittent series, plus MFLES/AutoMFLES on the upstream issue-219 reproducer — on anofox-forecast 0.15.3, committed *before* the bump
- Bumped `anofox-regression` `=0.5.3` -> `=0.5.15` and `anofox-forecast` `0.15.3` -> `0.15.10` — both compile unchanged; `Cargo.lock` diff is exactly the two version/checksum pairs, no new or removed crates
- Proved the DEP-01 fix inline (private `fit_ols_regression` vs numpy `lstsq`): RED on 0.5.3 (scrambled coefficients), GREEN on 0.5.15 — via a new crate-root `#[path]`'d `test_fixtures` module that lets both crates' own `cfg(test)` builds reuse the `tests/common/mod.rs` loader
- Added three post-bump regression guards: a 15,330-row tiled exogenous-fit peak-heap test (own `#[global_allocator]` test binary, 69.69 MiB peak vs a 256 MiB limit), an MFLES/AutoMFLES issue-219 bounded-forecast test, and a panel NaN/Inf outcome-class test in `anofox-fcst-ffi`
- Confirmed via the full 125-entry snapshot comparison that only MFLES/AutoMFLES/Laplace moved post-bump (the issue-219 reproducer's `max_abs_diff=12279.87` is the #219 runaway fix itself) — every other model's point forecasts are unchanged
- Declared `rust-version = "1.89"` across the workspace, pinned a new CI job to that exact toolchain, rewrote every stale "Rust 1.86+" claim in the docs, and proved the floor both ways: `cargo +1.89.0` builds/tests clean, `cargo +1.88.0 check` is refused by cargo itself
- Built the release extension once on the MSRV toolchain, ran the full native sqllogictest suite against it (1357 assertions, 0 failures), and added `validation/reference/sql/check_dep01_exog.sql`, which reproduces the numpy `lstsq` regression effect through `ts_forecast_exog_by` on both the 365-row and a 15,330-row tiled design

## Task Commits

Each task was committed atomically (Task 1 split into two commits per its own explicit two-step ordering: snapshot-before-bump, then bump; Task 3 split into two commits per its own explicit grouping):

1. **Task 1a: End-to-end bump tracer — pre-bump point-forecast snapshot** - `43b6eef` (test)
2. **Task 1b: Version bump + inline DEP-01 test (RED->GREEN)** - `6ceb115` (build)
3. **Task 2: Post-bump regression guards (exog memory, MFLES #219, panel NaN guards)** - `89120dd` (test)
4. **Task 3a: MSRV 1.89 manifests, docs, CI job** - `26e9b73` (build)
5. **Task 3b: SQL-level DEP-01 check against the built extension** - `cd12e65` (test)

**Plan metadata:** (final docs commit — hash recorded after this SUMMARY is written)

## Files Created/Modified
- `crates/anofox-fcst-core/tests/forecast_snapshot.rs` - point-forecast snapshot generator (bless mode) + version-aware comparison test
- `validation/reference/fixtures/forecast_snapshot/point_forecasts.json` - anofox-forecast 0.15.3 baseline, 125 entries, allowed_to_change={MFLES,AutoMFLES,Laplace}
- `crates/anofox-fcst-core/src/lib.rs` - new `#[cfg(test)] #[path = "../tests/common/mod.rs"] mod test_fixtures;`
- `crates/anofox-fcst-core/src/forecast.rs` - inline `fit_ols_regression_matches_numpy_lstsq_fixture` test
- `Cargo.toml` - `anofox-regression = "=0.5.15"`, `anofox-forecast = "0.15.10"`, `rust-version = "1.89"`, argmin comment refreshed
- `Cargo.lock` - resolved bump (2 version/checksum pairs) + FFI serde_json dev-dependency edge
- `crates/anofox-fcst-core/tests/exog_ols_memory.rs` - 15k-row peak-heap guard, own `#[global_allocator]` test binary
- `crates/anofox-fcst-core/tests/dependency_bump_regressions.rs` - MFLES #219 bounded-forecast guard
- `crates/anofox-fcst-ffi/Cargo.toml` - `serde_json = "1"` dev-dependency
- `crates/anofox-fcst-ffi/src/lib.rs` - `#[path]`'d `test_fixtures` loader + `panel_nan_guard_fixture_outcomes` test
- `crates/anofox-fcst-core/Cargo.toml` - `rust-version.workspace = true`
- `.github/workflows/MainDistributionPipeline.yml` - new `rust-msrv` job pinned to `dtolnay/rust-toolchain@1.89.0`
- `README.md`, `CONTRIBUTING.md`, `.claude/CLAUDE.md`, `.planning/PROJECT.md`, `.planning/codebase/STACK.md`, `.planning/codebase/CONCERNS.md` - MSRV text 1.86 -> 1.89
- `validation/reference/sql/check_dep01_exog.sql` - SQL-level DEP-01 check against the built extension
- `validation/reference/README.md` - `forecast_snapshot` family row, concrete test consumers, SQL checks section

## Decisions Made
- See `key-decisions` in frontmatter: the apply_regression RED-evidence discrepancy with 09-RESEARCH.md's "~-79" figure (explained: that number came from the full SQL-level residual+regression path, not raw `apply_regression`), the `INSTALL json; LOAD json;` addition to the SQL check script (Rule 3), and the one-pass bless run meaning `nondeterministic`/`excluded` fixture fallbacks were never needed.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Added explicit `INSTALL json; LOAD json;` to check_dep01_exog.sql**
- **Found during:** Task 3 (SQL-level DEP-01 check)
- **Issue:** `json_extract()` (needed to read `tolerance.sql_point_abs` from `cases.json`) is not in the catalog by default — the plan's action step described checking json availability but the script itself needed the explicit install/load to be reproducible in a clean environment.
- **Fix:** Added `INSTALL json; LOAD json;` right after `.bail on`.
- **Files modified:** `validation/reference/sql/check_dep01_exog.sql`
- **Verification:** Script runs clean end-to-end (`DEP01_SQL_PASS typical`, `DEP01_SQL_PASS 15k`, exit 0); the `json` extension was already locally cached for this DuckDB build (v1.5.6) from a prior session, so no network call was made this run.
- **Committed in:** `cd12e65` (Task 3b commit)

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** Necessary for the SQL check to run reproducibly; no scope creep — confined to the one new script.

## Issues Encountered
None beyond the Rule 3 deviation above. The `forecast_snapshot.rs` bless-mode run needed exactly one pass (no process aborts, no nondeterminism between runs) — the plan's fallback instrumentation for a flaky/crashing model (`parameters.nondeterministic` / `parameters.excluded`) was never triggered.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Phase 9 is now fully complete: all 3 plans (fixture harness, Python fixtures, dependency bumps + MSRV) executed, all ROADMAP Phase 9 success criteria (DEP-01, DEP-02, DEP-04, TEST-01) proven with committed evidence.
- Phase 10 (forecast wrapper fixes) can build directly on anofox-forecast 0.15.10 / anofox-regression 0.5.15: the `forecast_snapshot` fixture is the regression baseline for every future point-forecast change, `multiscale_ols`/`panel_nan_guard` fixtures are ready for FCST-04 (AutoARIMA exog stand-in) and FCST-05 (rank-deficient NaN-aliasing) work, and the SQL-level check pattern (`check_dep01_exog.sql`) is directly reusable for future `ts_forecast_exog_by` proofs.
- The duplicate `anofox-regression 0.4.0` entry (from `fdars-core`'s own `^0.4` edge) is unchanged and remains Phase 15's problem (DEP-03), as scoped.
- No blockers identified for Phase 10.

## Self-Check: PASSED

All 5 created files verified present on disk; all 5 task commits (`43b6eef`, `6ceb115`, `89120dd`, `26e9b73`, `cd12e65`) verified present in `git log --oneline`; all 20 `files_modified` paths from the plan frontmatter verified present on disk; re-ran every plan-level `<verification>` command this session (SNAPSHOT_PREBUMP, the forecast_snapshot python acceptance check, all cargo test invocations, `cargo +1.89.0`/`+1.88.0`, the SQL check with MAXRSS, `make test_release`) — all passed. No missing items.

---
*Phase: 09-reference-fixtures-safe-dependency-bumps*
*Completed: 2026-10-09*
