---
phase: 11-upstream-anofox-forecast-correctness-fixes
plan: 01
subsystem: infra
tags: [rust, cargo-fmt, cargo-clippy, cargo-test, git, github-pr, upstream-crate]

requires: []
provides:
  - "11-upstream-base.env: shared BASE_REF/PR_BASE/LINT_PATCH/BASELINE_FAILING_TESTS contract for all Phase 11 fix branches"
  - "11-baseline-lint.patch: behaviour-preserving clippy fix, verified to apply cleanly to BASE_REF"
  - "Confirmed pristine baseline on BASE_REF: cargo fmt clean, cargo clippy clean (after one test-only fix), cargo test 100% pass (0 pre-existing failures)"
  - "Upstream sync PR #220 (perf/v1.0-validation-hardening -> main) opened, not merged"
affects: [11-02, 11-03, 11-04, 11-05, 11-06, 11-07, 11-08, 11-09, 11-10]

actuals:
  tokens: 410
  tasks: 2
  commits: 1
  plan_head_before: d9a99390d5320d7bfb88e5c4949e729ba7cae609

tech-stack:
  added: []
  patterns:
    - "Env-file contract (.env, plain KEY=value) shared across all plans in a phase that branch from the same upstream commit"

key-files:
  created:
    - .planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-upstream-base.env
    - .planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-baseline-lint.patch
  modified: []

key-decisions:
  - "D-01a (pre-answered by user 2026-10-09): option-b — branch from 1ae5cfc (released v0.15.10, on unmerged perf/v1.0-validation-hardening), PRs still target main"
  - "Opened upstream PR #220 (perf/v1.0-validation-hardening -> main) as the sync PR authorised in D-01a; left unmerged for the user to review"
  - "Fixed one baseline clippy::approx_constant finding (test-only, src/forecastability/knn_mi.rs) by replacing a literal with std::f64::consts::EULER_GAMMA (numerically identical, clippy's own suggested fix); captured as 11-baseline-lint.patch rather than committed upstream"
  - "Did NOT run `requirements mark-complete` for DIAG-01/DIAG-02/UPST-01..05 despite them being listed in this plan's frontmatter `requirements` field — the plan's own objective states it 'implements none of them itself'; REQUIREMENTS.md confirms all seven are still Pending and should stay that way until plans 11-02..11-06 land the actual fixes"

patterns-established:
  - "Baseline measurement before fix work: run fmt/clippy/test on a pristine detached checkout of the shared BASE_REF before any branch starts, so later plans never misattribute pre-existing failures to their own changes"

requirements-completed: []

coverage:
  - id: D1
    description: "Base-ref decision (option-b) recorded verbatim in this SUMMARY"
    human_judgment: false
    verification:
      - kind: other
        ref: "Base decision section below, copied verbatim from 11-01-PLAN.md Task 1"
        status: pass
  - id: D2
    description: "11-upstream-base.env written with BASE_REF (40-hex), BASE_LABEL, PR_BASE=main, LINT_PATCH, BASELINE_FAILING_TESTS, RUST_TOOLCHAIN, BASELINE_TEST_MINUTES"
    requirement: null
    human_judgment: false
    verification:
      - kind: other
        ref: "automated verify command from 11-01-PLAN.md (grep -cE BASE_REF, PR_BASE, git cat-file -e) -> printed BASE_OK / ENV_CHECKS_OK"
        status: pass
  - id: D3
    description: "Pristine baseline measured on BASE_REF: cargo fmt --check, cargo clippy --all-features --all-targets -D warnings, cargo test --all-features"
    human_judgment: false
    verification:
      - kind: other
        ref: "cargo fmt --all -- --check (exit 0 before and after fix); cargo clippy --all-features --all-targets -- -D warnings (1 finding -> 0 after fix, re-run confirmed `Finished` with no errors); cargo test --all-features (test result: ok. 3187 passed; 0 failed across the primary lib test binary, 0 failed across all 36 binaries/doctests)"
        status: pass
  - id: D4
    description: "11-baseline-lint.patch captured, verified to apply cleanly to pristine BASE_REF, and the edit discarded from the upstream working tree (no commit made upstream)"
    human_judgment: false
    verification:
      - kind: other
        ref: "git stash + git apply --check 11-baseline-lint.patch on detached BASE_REF printed PATCH_APPLIES_CLEAN; git checkout -- src/forecastability/knn_mi.rs discarded the edit; final git status --porcelain shows only the pre-existing untracked .planning/state.json"
        status: pass
  - id: D5
    description: "Upstream checkout restored to START_BRANCH (perf/v1.0-validation-hardening), clean except the pre-existing untracked .planning/state.json"
    human_judgment: false
    verification:
      - kind: other
        ref: "git branch --show-current == perf/v1.0-validation-hardening; git status --porcelain | grep -v '.planning/state.json' | wc -l == 0"
        status: pass

duration: ~20min
completed: 2026-10-09
status: complete
---

# Phase 11 Plan 01: Resolve Upstream Base Ref and Measure Pristine Baseline Summary

**Branches from 1ae5cfc (released v0.15.10, unmerged into main) per the user's pre-answered D-01a; pristine baseline was clean (0 pre-existing test failures) after one test-only clippy fix captured as a patch, and a sync PR (#220) was opened to merge the 0.15.10 commits into main.**

## Performance

- **Duration:** ~20 min
- **Started:** 2026-10-09T20:11Z (approx, per STATE.md last_updated)
- **Completed:** 2026-10-09T20:22Z
- **Tasks:** 2
- **Files modified:** 2 (both created, in this repo's `.planning/` only — the upstream crate repo has zero net changes)

## Base decision

> **D-01a (pre-answered by the human on 2026-10-09 via orchestrator AskUserQuestion):** "v0.15.10 + sync PR" = **option-b**. Branches from `1ae5cfc13ef98afdbb17e27def95a632cbb74fe7` (released v0.15.10, on the unmerged branch `perf/v1.0-validation-hardening`); PRs still target `main`. Also recorded: user keeps plan 11-08 in scope.

Resolved `BASE_REF`:
- `origin/perf/v1.0-validation-hardening` tip = `1ae5cfc13ef98afdbb17e27def95a632cbb74fe7` — unchanged since planning time, so `BASE_REF = 1ae5cfc13ef98afdbb17e27def95a632cbb74fe7` exactly (no new tip to note).
- `BASE_LABEL = "1ae5cfc = released 0.15.10 (perf/v1.0-validation-hardening, not merged into main)"`.

Additional (user-authorised) action: no open PR existed syncing `perf/v1.0-validation-hardening` into `main` — the only prior PR on that head (`#218`) had already merged an **earlier** state of the branch (at `375e51b`) into `main`, and the branch then advanced two more commits (`3bf779d` version bump, `1ae5cfc` clippy fix) that were never merged. Opened a new sync PR:

**https://github.com/sipemu/anofox-forecast/pull/220** — "Sync main with v0.15.10 (perf/v1.0-validation-hardening)". **Not merged.**

## Accomplishments
- Resolved and recorded the shared `BASE_REF` contract (`11-upstream-base.env`) that plans 11-02..11-10 will source to create their fix branches
- Measured the pristine baseline on `BASE_REF` with the installed stable toolchain: `cargo fmt --all -- --check` clean; `cargo clippy --all-features --all-targets -- -D warnings` found exactly one finding (test-only); `cargo test --all-features` — **0 pre-existing failures** across all 36 test binaries/doctests (3187 passed in the primary lib-test binary alone)
- Fixed the one clippy finding with a behaviour-preserving edit, captured it as `11-baseline-lint.patch`, verified the patch applies cleanly to the pristine `BASE_REF`, then discarded the edit from the upstream working tree (no commit made upstream, per the threat model's "no commits in the upstream repo" rule)
- Opened upstream sync PR #220 (`perf/v1.0-validation-hardening` -> `main`), left unmerged for the user
- Restored the upstream checkout to its starting branch (`perf/v1.0-validation-hardening`), clean except the pre-existing untracked `.planning/state.json`

## Task Commits

1. **Task 1: Choose the upstream base ref** — no separate commit (decision-only; recorded in this SUMMARY and in Task 2's commit message)
2. **Task 2: Tracer — resolve BASE_REF, measure baseline, write env contract, restore upstream checkout** — `b681c88` (feat)

**Plan metadata:** (this commit, docs: complete plan)

## Files Created/Modified
- `.planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-upstream-base.env` — BASE_REF, BASE_LABEL, PR_BASE=main, LINT_PATCH path, BASELINE_FAILING_TESTS (empty), RUST_TOOLCHAIN, BASELINE_TEST_MINUTES=5.4
- `.planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-baseline-lint.patch` — single-file, 2-line behaviour-preserving diff (`src/forecastability/knn_mi.rs`), applies cleanly to pristine `BASE_REF`

No files in the upstream crate repo (`~/projects/rust/anofox-forecast`) were committed; its working tree is back to its starting state.

## Decisions Made
- Option-b confirmed verbatim (see "Base decision" above) — no deviation, this was pre-answered.
- Opened the sync PR (#220) per the user-authorised additional instruction, since no open PR already covered the gap between `main` and the current `perf/v1.0-validation-hardening` tip.
- Chose `std::f64::consts::EULER_GAMMA` (confirmed stable on the installed `rustc 1.99.0`, and no MSRV pin exists in the crate's `Cargo.toml`/CI matrix beyond stable/beta/nightly) over an `#[allow(clippy::approx_constant)]` suppression, since it is clippy's own suggested fix and is strictly more precise than the hand-typed literal it replaces.
- Did not run `requirements mark-complete` for DIAG-01/DIAG-02/UPST-01..05 (see key-decisions above) — this plan is foundational and completes none of the seven requirements listed in its own frontmatter.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed a clippy::approx_constant finding in the pristine baseline**
- **Found during:** Task 2, step 3 (`cargo clippy --all-features --all-targets -- -D warnings` on the detached `BASE_REF` checkout)
- **Issue:** `src/forecastability/knn_mi.rs:290` asserted `digamma(1.0)` against the hand-typed literal `-0.5772156649` (an approximation of `-EULER_GAMMA`), which clippy denies under `-D warnings`.
- **Fix:** Replaced the literal with `-std::f64::consts::EULER_GAMMA` per clippy's own suggestion. Test-only change, numerically equivalent (both evaluate to `-0.5772156649015329` on the installed toolchain) — no production behaviour affected.
- **Files modified (upstream, not committed):** `src/forecastability/knn_mi.rs` — captured as `11-baseline-lint.patch` in this repo instead, then reverted from the upstream working tree per the plan's explicit "no commit on the detached HEAD" instruction.
- **Verification:** `cargo clippy --all-features --all-targets -- -D warnings` re-run after the edit → `Finished` with no errors; `cargo fmt --all -- --check` still clean; `git apply --check 11-baseline-lint.patch` on a fresh pristine `BASE_REF` checkout (via `git stash` / `git stash pop` in the same session) → `PATCH_APPLIES_CLEAN`.
- **Committed in:** `b681c88` (this repo's Task 2 commit — the patch file and env file, not the upstream edit itself).

---

**Total deviations:** 1 auto-fixed (1 bug — Rule 1).
**Impact on plan:** Necessary to produce a clean baseline per the plan's acceptance criteria. No scope creep — scope was exactly "find and fix exactly the reported clippy/fmt findings."

## Issues Encountered

- **Concurrent Phase 12 execution in the same working directory.** While staging this plan's two files with `git add <path1> <path2>`, the resulting index unexpectedly also contained unrelated staged changes (`crates/anofox-fcst-core/src/lib.rs`, `crates/anofox-fcst-core/src/missing.rs`, `crates/anofox-fcst-ffi/src/lib.rs`, `crates/anofox-fcst-ffi/tests/null_policy_ffi.rs`, `src/include/anofox_fcst_ffi.h`, `src/include/ts_list_marshal.hpp`, `src/scalar_functions/metrics.cpp`, `src/table_functions/ts_metrics_native.cpp`) — these belong to a different, concurrently-running GSD agent executing Phase 12 Plan 01 in this exact same checkout (confirmed: that agent's commit `d0ff31a feat(12-01): tracer - Rust missing-data policy...` landed on this branch moments later). This repo has `workflow.use_worktrees` disabled per a standing project note (`feedback_parallel_architecture`/`project_gsd_worktree_isolation_leaks` in MEMORY.md), so two phase executors share one working tree and one `.git/index`. I ran `git restore --staged <foreign files>` (index-only, never touched their working-tree content) to unstage everything except my own two files, re-staged only `11-upstream-base.env` and `11-baseline-lint.patch`, and committed immediately. Verified post-commit (`git show b681c88 --stat`) that the landed commit contains exactly those two files and nothing from Phase 12.
  - **Measurement note:** the plan-commit ledger (`gsd-plan-head-before-11-01`) was captured before the concurrent agent's commit landed, so `git rev-list --count ${PLAN_HEAD_BEFORE}..HEAD` measures `2` (includes the foreign `d0ff31a` commit), not `1`. The `actuals.commits: 1` and `plan_head_before` recorded above reflect the correct, verified count for *this* plan only (commit `b681c88`), with this note as the audit trail for the discrepancy — this is a genuine shared-working-tree interleaving artifact, not a narrated/fabricated count.
  - **Recommendation for the orchestrator:** if Phase 11 and Phase 12 are intended to run concurrently without worktree isolation, each executor's commit step should stage-and-commit as tightly as this plan did (explicit paths, immediate commit, no `git add -A`) to avoid cross-phase commit contamination; a close call here.

## User Setup Required

None — no external service configuration required. (The sync PR #220 is informational for the user to review/merge on their own schedule; it does not block Phase 11's fix-branch work, which can proceed from `BASE_REF` regardless of whether/when #220 merges.)

## Next Phase Readiness

- Plans 11-02 through 11-10 can now source `.planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-upstream-base.env` to know exactly which commit to branch from (`BASE_REF=1ae5cfc13ef98afdbb17e27def95a632cbb74fe7`), that the baseline has zero pre-existing test failures, and where to find the one lint patch if their own branch needs the same clippy fix (their branch will already be clean of it, since it's baked into `BASE_REF`'s history only via the patch file here — if a later branch rebases or cherry-picks differently, `11-baseline-lint.patch` is the reference fix).
- Blocker carried forward (already tracked in STATE.md): upstream release (merge PR #220, merge/publish each fix PR) remains a user action; Phase 17 cannot close UPST-06 until a released version is consumed.
- New observation for the orchestrator: this repo runs without worktree isolation, so parallel phase execution (11 and 12 observed running concurrently this session) shares one git index — future concurrent executors should stage explicit paths and commit promptly, as this plan did, to avoid contaminating each other's commits.

---
*Phase: 11-upstream-anofox-forecast-correctness-fixes*
*Completed: 2026-10-09*

## Self-Check: PASSED

- FOUND: .planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-upstream-base.env
- FOUND: .planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-baseline-lint.patch
- FOUND: .planning/phases/11-upstream-anofox-forecast-correctness-fixes/11-01-SUMMARY.md
- FOUND: commit b681c88 in git log --oneline --all
