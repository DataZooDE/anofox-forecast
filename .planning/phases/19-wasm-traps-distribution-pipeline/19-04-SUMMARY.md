---
phase: 19-wasm-traps-distribution-pipeline
plan: 04
subsystem: infra
tags: [wasm, webassembly, duckdb-wasm, duckdb-extension-distribution, github-actions, s3, cloudfront]

# Dependency graph
requires:
  - phase: 19-02
    provides: "scripts/verify-wasm-artifact.sh, scripts/extension-verify-upload.sh, test/wasm/post_upload_smoke.test, the owner handover doc skeleton (docs/distribution/get-erpl-io.md), and the shared scratch-p19 CI artifact/evidence environment"
  - phase: 19-03
    provides: "ts_features catalog WASM trap fix (curated subset grew to 10 files, 482 assertions) -- the baseline this plan's wasm_mvp measurements run against"
provides:
  - "test/wasm/run.mjs --bundle eh|mvp (also ANOFOX_WASM_BUNDLE), default eh, unrecognized value exits 1 before booting"
  - "Measured, classified wasm_mvp `_setThrew is not defined` root cause (upstream, in @duckdb/duckdb-wasm's own JS glue) in docs/distribution/get-erpl-io.md's new \"wasm_mvp status (DIST-03)\" section"
  - "User-decided keep-mvp Decision record in docs/distribution/get-erpl-io.md, with the rationale for deviating from the locked upstream-drop default"
  - "scripts/verify-wasm-artifact.sh LOAD_ARCHS=\"wasm_eh wasm_mvp\": the post-upload deploy check now LOADs wasm_mvp (via --bundle mvp) on every future v1.5.6 deploy, not just wasm_eh"
  - "CHANGELOG.md #272 entries (Changed: wasm_mvp decision; Fixed: double-signed WASM artifacts + post-upload check)"
  - "Drafted #272 issue comment at /home/simonm/projects/duckdb/scratch-p19/issue-272/comment.md, NOT posted (orchestrator posts after user approval)"
affects: [future duckdb-wasm engine-version bumps (test/wasm/README.md DEP-01), any future get.erpl.io owner bucket changes, Phase 25 (#272 closure)]

# Actuals (#2632)
actuals:
  tokens: 7166
  tasks: 3
  commits: 2
plan_head_before: 611b1fd9e88d9a15ceda19b8c0d789fc1b0270b3

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Harness bundle promoted to an explicit CLI/env parameter (--bundle / ANOFOX_WASM_BUNDLE) rather than an mvp-only side path, per the plan's assumption-delta decision"
    - "Four-check, time-boxed root-cause protocol for a WASM-toolchain-level defect (imports, exports, JS stack, compiler/panic-strategy flags) when source-level debugging is unavailable (symbols stripped on both sides)"

key-files:
  created: []
  modified:
    - test/wasm/run.mjs
    - test/wasm/README.md
    - docs/distribution/get-erpl-io.md
    - scripts/verify-wasm-artifact.sh
    - CHANGELOG.md

key-decisions:
  - "wasm_mvp: keep-mvp (user decision at the Task 2 checkpoint). Measured zero wasm_mvp-only failures across the full 94-file suite versus wasm_eh, and a confirmed LOAD success in every run against a correctly-signed artifact; the only symptom of the upstream _setThrew defect is non-fatal console noise. The locked CONTEXT #272 rule (upstream -> drop) was written against the #272 comment's \"fails to LOAD\" framing, which this measurement does not confirm -- fix-ours was foreclosed because Classification is upstream (the gap is in @duckdb/duckdb-wasm's own JS glue, not anything this repository controls)."
  - "The #272 issue comment is drafted to a scratch file but intentionally NOT posted in this plan execution -- the orchestrator posts it after explicit user approval, per the sequential-execution instructions for this run."
  - "The production wasm_mvp artifact (still double-signed as of this probe, DIST-01 fix not yet redeployed) crashes the harness worker outright on the same _setThrew reference that is merely non-fatal console noise against a correctly-signed artifact -- recorded as likely explaining the #272 report's original severity, confounded by the separate DIST-01 bug fixed in 19-02."

requirements-completed: [DIST-03, DIST-04]

coverage:
  - id: D1
    description: "test/wasm/run.mjs gains a --bundle eh|mvp / ANOFOX_WASM_BUNDLE option (default eh unchanged) that can boot the mvp DuckDB-Wasm bundle; an unrecognized value exits 1 before booting anything"
    verification:
      - kind: other
        ref: "node test/wasm/run.mjs --bundle bogus --ext <artifact> exits 1 with a HARNESS ERROR before any boot output; default (no --bundle) run against the branch-built wasm_eh artifact reproduces the pre-existing 482/0/0 curated total unchanged"
        status: pass
    human_judgment: false
  - id: D2
    description: "wasm_mvp LOAD/suite-parity/_setThrew measurement and four-check root-cause classification (upstream), written up in docs/distribution/get-erpl-io.md's \"wasm_mvp status (DIST-03)\" section, backed by the official branch CI build artifacts (run 39879822323, commit 611b1fd, conclusion success) cross-checked against a local build"
    requirement: "DIST-03"
    verification:
      - kind: other
        ref: "mvp curated: 482 passed/0 failed/0 skipped (10 files); mvp --all: 3604 passed/49 failed/0 skipped (94 files), byte-identical per-file results to eh --all (zero mvp-only failures); 188 _setThrew console occurrences (3 distinct stack signatures) on mvp, 0 on eh -- all captured in logs-19-04-mvp-curated.log / logs-19-04-mvp-all.log / logs-19-04-eh-all.log under scratch-p19"
        status: pass
    human_judgment: false
  - id: D3
    description: "Branch CI run is green on all three lines (v2.0.0, v1.5.6, v1.4.5) with the 19-02 post-upload check visible on every WASM deploy entry (9/9 no-upload local-bytes fallback, v1.5.6/wasm_eh post_upload_smoke.test pass, v2.0.0/wasm_eh LOAD-check-skipped record)"
    verification:
      - kind: other
        ref: "gh run view 39879822323 --json conclusion -> success; grep -c 'nothing was uploaded' logs-19-04-ci.log -> 9; grep post_upload_smoke.test logs-19-04-ci.log -> 3 passed, 0 failed; grep 'LOAD check skipped for wasm_eh v2.0.0' logs-19-04-ci.log -> present"
        status: pass
    human_judgment: false
  - id: D4
    description: "keep-mvp decision implemented: scripts/verify-wasm-artifact.sh's LOAD_ARCHS now includes wasm_mvp, and the script passes --bundle mvp to the harness when verifying a wasm_mvp object; no build/deploy matrix excludes wasm_mvp"
    requirement: "DIST-03"
    verification:
      - kind: other
        ref: "bash -n scripts/verify-wasm-artifact.sh exits 0; scripts/verify-wasm-artifact.sh <official CI wasm_mvp artifact> wasm_mvp v1.5.6 exits 0 (LOAD + post_upload_smoke.test 3/0/0 pass); the Task 3 Python consistency check (parses the Decision line and the workflow YAML's exclude_archs, asserts keep-mvp's own LOAD_ARCHS invariant) prints 'decision keep-mvp consistent'"
        status: pass
    human_judgment: false
  - id: D5
    description: "docs/distribution/get-erpl-io.md gains a \"Decision record\" section (Decision: keep-mvp, date, decided-by, rationale, what changed) and CHANGELOG.md [Unreleased] records #272 under both Changed and Fixed"
    requirement: "DIST-04"
    verification:
      - kind: other
        ref: "grep -cE '^Decision: (fix-ours|drop-mvp|keep-mvp)' docs/distribution/get-erpl-io.md -> 1, value keep-mvp; sed -n '/^## \\[Unreleased\\]/,/^## \\[2026/p' CHANGELOG.md | grep -c '#272' -> 2; python3 scripts/check_doc_snippets.py docs/distribution/get-erpl-io.md -> 0 FAIL, 0 STALE"
        status: pass
    human_judgment: false
  - id: D6
    description: "#272 issue comment drafted (what was fixed with commit SHAs, the keep-mvp decision, owner actions, the handover doc link on the feature branch, and an explicit no-bucket-change statement; no credential material) to a scratch file for the orchestrator to post after user approval"
    requirement: "DIST-04"
    verification: []
    human_judgment: true
    rationale: "The comment is deliberately not posted by this plan execution -- posting to the real GitHub issue is the orchestrator's responsibility after explicit user approval (sequential-execution instruction for this run). Content correctness (credential-pattern grep, doc-link presence) was verified mechanically, but the decision to actually post it requires human sign-off."

duration: 5h 13min
completed: 2026-10-23
status: complete
---

# Phase 19 Plan 04: wasm_mvp decision and #272 finalization Summary

**Measured wasm_mvp against the official branch CI build (zero mvp-only failures across 94 files, LOAD succeeds every time, `_setThrew` is non-fatal console noise caused by an upstream `@duckdb/duckdb-wasm` JS-glue gap), the user decided keep-mvp over the locked drop-on-upstream default, and the post-upload deploy check now LOAD-gates wasm_mvp for real.**

## Performance

- **Duration:** 5h 13min (dominated by waiting on the full three-line GitHub Actions distribution-pipeline build, plus a ~25min self-inflicted GitHub API rate-limit lockout from over-eager `gh run watch` polling -- see Issues Encountered)
- **Started:** 2026-10-23T00:43:37Z
- **Completed:** 2026-10-23T05:56:35Z
- **Tasks:** 3 (1 tracer/auto, 1 checkpoint:decision, 1 auto)
- **Files modified:** 5

## Accomplishments

- Added `test/wasm/run.mjs --bundle eh|mvp` (also `ANOFOX_WASM_BUNDLE`, default `eh`, unrecognized value exits 1 before booting) -- promoted the harness bundle from an implicit constant to an explicit parameter per the plan's assumption-delta decision, with the default (`eh`) path's behavior unchanged.
- Pushed the branch and dispatched `MainDistributionPipeline.yml` on `milestone/v0.11.0-issue-sweep` (run [`39879822323`](https://github.com/DataZooDE/anofox-forecast/actions/runs/39879822323)); it ran green on all three lines (v2.0.0, v1.5.6, v1.4.5), confirming the 19-02 DIST-01/DIST-02 fixes hold end-to-end on CI: all 9 WASM deploy-job matrix entries show the no-upload local-bytes fallback with the post-upload check running, `v1.5.6`/`wasm_eh` passes `post_upload_smoke.test`, and `v2.0.0`/`wasm_eh` correctly records the LOAD-check-skipped case.
- Measured `wasm_mvp` against the official CI build artifacts (cross-checked byte-for-byte against a local build from one commit behind HEAD): LOAD succeeds every time; curated subset 482/0/0 (10 files); `--all` 3604/49/0 (94 files) with **byte-identical per-file pass/fail/skip results to `wasm_eh`** -- zero `wasm_mvp`-only failures, the 49 shared failures are pre-existing, already-tracked WASM test-suite debt. 188 `_setThrew` console `ReferenceError` occurrences across 40 of 94 files (only 3 distinct JS stack signatures), none of which ever failed a query result.
- Ran a time-boxed, four-check root-cause investigation (`WebAssembly.Module` imports/exports of both bundles, a captured JS stack, `rustc --print cfg` panic strategy plus this repository's own CMake/extension-config WASM flags) and classified the defect as **upstream**: both `duckdb-node-mvp.worker.cjs` and `duckdb-node-eh.worker.cjs` (the `@duckdb/duckdb-wasm` npm package's own compiled JS glue) call a bare `_setThrew(1,0)` from every `invoke_*` trampoline but never bind that identifier to the main module's own exported `setThrew` WASM function -- latent/unreachable in `eh` (native Wasm exception handling avoids the JS path for a normal SQL error path) and reachable in `mvp` (no native EH). Drafted a `duckdb/duckdb-wasm` upstream issue for the owner to file, kept in the handover doc.
- Returned a `checkpoint:decision` with the full measured evidence and a recommendation (`keep-mvp`); the user/orchestrator decided **keep-mvp**.
- Implemented `keep-mvp`: `scripts/verify-wasm-artifact.sh`'s `LOAD_ARCHS` now includes `wasm_mvp` alongside `wasm_eh` for the `v1.5.6` line, passing `--bundle mvp` to the harness when verifying a `wasm_mvp` object -- verified end-to-end against the official CI `wasm_mvp` artifact (LOAD + `post_upload_smoke.test` 3/0/0 pass). No build/deploy matrix YAML changed; `wasm_mvp` is excluded from nothing.
- Added `docs/distribution/get-erpl-io.md`'s "Decision record" section (`Decision: keep-mvp`, rationale, what changed in the repository) and updated "Redeploy steps" to verify both bundles post-upload; fixed a dangling forward-reference discovered while writing the record.
- Added `CHANGELOG.md [Unreleased]` entries for `#272` under both `### Changed` (the `wasm_mvp` decision) and `### Fixed` (the double-signed-artifact fix and the post-upload check).
- Drafted the `#272` issue comment (code-fix commit SHAs, the decision, owner actions, the handover-doc link, an explicit no-bucket-change statement, no credential material) to `/home/simonm/projects/duckdb/scratch-p19/issue-272/comment.md` -- **not posted**, per this run's explicit instruction that the orchestrator posts it after user approval.
- Re-ran the phase-level verification on the final branch head: `HOME=$(mktemp -d) ./build/release/test/unittest "$PWD/test/*"` -> "All tests passed (5687 assertions in 94 test cases)"; `make audit` exits 0; `cargo fmt --all -- --check` exits 0; `make check-docs` -> "345 PASS, 0 FAIL, 8 EXPECTED-ERROR, 154 SYNOPSIS, 4 SKIP-NETWORK, 0 STALE".

## Task Commits

Each task was committed atomically:

1. **Task 1: Tracer — branch CI run proves the phase on all three lines; run.mjs boots the mvp bundle; wasm_mvp LOAD, suite parity and the `_setThrew` root cause are measured and written up** - `634bb35` (fix)
2. **Task 2: Decide the wasm_mvp branch — fix-ours, drop-mvp or keep-mvp** - checkpoint:decision, no commit (user decided `keep-mvp`)
3. **Task 3: Apply the wasm_mvp decision, finalise the handover, record #272 in the CHANGELOG and comment on #272** - `7354f11` (fix)

**Plan metadata:** committed below (this SUMMARY + STATE/ROADMAP/REQUIREMENTS).

## Files Created/Modified

- `test/wasm/run.mjs` - `--bundle eh|mvp` / `ANOFOX_WASM_BUNDLE` option; `DuckDB-Wasm bundle: <bundle>` banner line; unrecognized value exits 1 before booting.
- `test/wasm/README.md` - documents `--bundle` in the running-modes table and `scripts/verify-wasm-artifact.sh`'s `WASM_HARNESS_ENGINE` as another place to bump with the duckdb-wasm pin.
- `docs/distribution/get-erpl-io.md` - new "wasm_mvp status (DIST-03)" section (measurements, four-check root-cause investigation, drafted upstream issue) and "Decision record" section (`Decision: keep-mvp`); updated "Redeploy steps" to verify both bundles.
- `scripts/verify-wasm-artifact.sh` - `LOAD_ARCHS="wasm_eh wasm_mvp"`; passes `--bundle mvp` to the harness for `wasm_mvp` objects.
- `CHANGELOG.md` - `[Unreleased]` `#272` entries under `### Changed` and `### Fixed`.

## Decisions Made

See `key-decisions` in frontmatter. In short: `keep-mvp` was decided by the user at the Task 2 checkpoint, deviating from the locked CONTEXT #272 upstream-drop default because the measured evidence (LOAD succeeds, zero mvp-only failures) does not match the `#272` report's "fails to LOAD" framing that rule was written against; `fix-ours` was never on the table because the Classification is upstream; the `#272` comment is intentionally left unposted for this run.

## Deviations from Plan

None — plan executed exactly as written, including the Task 2 checkpoint protocol (measured evidence presented, recommendation given, decision deferred to the user) and the orchestrator's explicit override of Task 3's literal "post the #272 comment" step (draft-only, scratch file, no post).

## Issues Encountered

- **GitHub API rate limit lockout (~25 min lost).** Early in Task 1, four separate `gh run watch` background processes (3-second poll interval each) were started concurrently while waiting for the long-running CI matrix, exhausting the account's primary `core` REST rate limit (5000/hr) for the Actions API specifically. Every `gh run view`/`gh run list` call returned HTTP 403 until the limit reset (`X-Ratelimit-Reset`). Recovered by backing off until the reset timestamp, then using a single `gh run watch --interval 30` instance for the remainder of the wait. No workaround was needed beyond patience; no destructive or credential-related action was taken during the lockout.
- **Reproducing the exact #272-reported severity required testing against the wrong artifact first.** An initial repro attempt used the currently-live, still-double-signed production `wasm_mvp` object (left over from the pre-19-02-fix pipeline) and reproducibly crashed the whole harness worker -- which looked like it confirmed the #272 report's "fails to LOAD" framing. Re-running against the correctly-signed branch CI build artifact showed LOAD succeeding and the same reference being merely non-fatal console noise. This discrepancy is recorded explicitly in the handover doc as likely explaining the original issue's severity (confounded by the separate, now-fixed DIST-01 double-signing bug) rather than silently trusting the first (misleading) result.

## User Setup Required

None - no external service configuration required. The owner-only bucket actions (CORS rule, stale-path deletion) remain documented in `docs/distribution/get-erpl-io.md`'s "CORS rule" / "Stale paths" / "Redeploy steps" sections from 19-02, unchanged in scope by this plan.

## Next Phase Readiness

- Phase 19 is complete: BUG-01, BUG-02 (19-01, 19-03), DIST-01, DIST-02, DIST-03, DIST-04 (19-02, 19-04) are all closed. ROADMAP Phase 19 success criteria 1-5 are met (criterion 4's "wasm_mvp LOADs or the root cause is written up with a recorded decision" is satisfied by `keep-mvp`; criterion 5's owner handover now also covers `wasm_mvp`).
- **Follow-up required outside this plan's scope:** the `#272` issue comment at `/home/simonm/projects/duckdb/scratch-p19/issue-272/comment.md` is drafted but not posted. The orchestrator should post it (via `gh issue comment 272 -R DataZooDE/anofox-forecast --body-file <path>`) after explicit user approval, per this run's instructions. Issue `#272` stays open until then and until Phase 25's REL-01 closes it per the milestone's close-out plan.
- The drafted upstream `duckdb/duckdb-wasm` issue (in the handover doc's "wasm_mvp status (DIST-03)" section) has not been filed -- filing it is at the owner's discretion and is not required for DIST-03 to be considered resolved this milestone (the defect is measured, classified, and now gated against regression).
- No `aws` command was run against the real bucket during this plan; no repository variable or secret was read or printed.
- Ready for `/gsd-verify-work 19` and the next milestone phase.

---
*Phase: 19-wasm-traps-distribution-pipeline*
*Completed: 2026-10-23*

## Self-Check: PASSED
