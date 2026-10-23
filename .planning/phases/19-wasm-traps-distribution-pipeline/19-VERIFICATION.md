---
phase: 19-wasm-traps-distribution-pipeline
verified: 2026-10-23T06:09:38Z
status: passed
score: 7/7 must-haves verified
covered_files: [".github/workflows/MainDistributionPipeline.yml", ".github/workflows/_extension_deploy.yml", ".github/workflows/audit.yml", ".planning/REQUIREMENTS.md", ".planning/phases/19-wasm-traps-distribution-pipeline/19-01-PLAN.md", ".planning/phases/19-wasm-traps-distribution-pipeline/19-01-SUMMARY.md", ".planning/phases/19-wasm-traps-distribution-pipeline/19-02-PLAN.md", ".planning/phases/19-wasm-traps-distribution-pipeline/19-02-SUMMARY.md", ".planning/phases/19-wasm-traps-distribution-pipeline/19-03-PLAN.md", ".planning/phases/19-wasm-traps-distribution-pipeline/19-03-SUMMARY.md", ".planning/phases/19-wasm-traps-distribution-pipeline/19-04-PLAN.md", ".planning/phases/19-wasm-traps-distribution-pipeline/19-04-SUMMARY.md", ".planning/phases/19-wasm-traps-distribution-pipeline/19-CONTEXT.md", ".planning/phases/19-wasm-traps-distribution-pipeline/19-RESEARCH.md", ".planning/phases/19-wasm-traps-distribution-pipeline/19-REVIEW.md", ".planning/phases/19-wasm-traps-distribution-pipeline/COVERAGE.md", ".planning/phases/19-wasm-traps-distribution-pipeline/deferred-items.md", ".planning/phases/19-wasm-traps-distribution-pipeline/repro/ts_features_catalog_repro.test", "CHANGELOG.md", "Makefile", "crates/anofox-fcst-ffi/src/lib.rs", "crates/anofox-fcst-ffi/tests/features_list_ffi.rs", "docs/api/04-data-preparation.md", "docs/distribution/get-erpl-io.md", "examples/gap_filling/synthetic_gap_examples.sql", "plugins/anofox-forecast/skills/anofox-forecast-data-prep/SKILL.md", "scripts/extension-upload.sh", "scripts/extension-verify-upload.sh", "scripts/verify-wasm-artifact.sh", "scripts/wasm_signature_sections.py", "src/include/anofox_fcst_ffi.h", "src/macros/ts_macros.cpp", "src/table_functions/ts_features.cpp", "test/sql/ts_features.test", "test/sql/ts_fill_forward_native.test", "test/sql/ts_fill_forward_operator.test", "test/wasm/README.md", "test/wasm/post_upload_smoke.test", "test/wasm/run.mjs"]
covered_digest: "v1:sha256:4392815433c0b5c886717ccfd72b477e941c3f8569558aef15694c330749671a"
behavior_unverified: 0
overrides_applied: 0
gaps:
  - truth: "Issue #272 has a comment linking the handover document (docs/distribution/get-erpl-io.md), giving the owner the CORS rule, stale paths and redeploy steps"
    status: failed
    reason: "19-04-PLAN.md's own must_haves.truths requires this comment to exist on issue #272. It does not: `gh issue view 272 -R DataZooDE/anofox-forecast --json comments` finds zero comments referencing docs/distribution/get-erpl-io.md. The 19-04-SUMMARY.md documents this as an intentional deviation (comment drafted to a scratch file, posting deferred to the orchestrator pending explicit user approval), but that approval/posting step has not happened yet."
    artifacts:
      - path: "/home/simonm/projects/duckdb/scratch-p19/issue-272/comment.md"
        issue: "Fully drafted (verified: commit SHAs, keep-mvp decision, owner actions, handover-doc link, explicit no-bucket-change statement, no credential material) but still sitting in scratch space, never posted to GitHub"
    missing:
      - "Post the drafted comment: gh issue comment 272 -R DataZooDE/anofox-forecast --body-file /home/simonm/projects/duckdb/scratch-p19/issue-272/comment.md (after confirming its content still matches the final branch state, e.g. commit SHAs and the doc URL)"
---

# Phase 19: WASM Traps & Distribution Pipeline Verification Report

**Phase Goal:** The extension runs in DuckDB-Wasm without the #289 traps, and the release pipeline can no longer publish a WASM artifact that fails to LOAD (#272).
**Verified:** 2026-10-23T06:09:38Z
**Status:** gaps_found
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

All five are ROADMAP Phase 19 success criteria (the authoritative contract); truth 6 is an additional must-have the plans themselves added (19-04-PLAN.md frontmatter), which does not reduce scope but does add an obligation that must be checked.

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | `ts_fill_forward_operator` no longer hangs/traps; if kept, quoted identifiers + non-default frequency work natively and in DuckDB-Wasm; `ts_fill_forward_by` output unchanged and pinned | ✓ VERIFIED | `src/table_functions/ts_fill_forward_operator.cpp` deleted (confirmed on disk); `src/macros/ts_macros.cpp:470` registers it as a `COLUMNS([param])` table macro delegating to `ts_fill_forward_by`; live run against the built v2.0.0 extension: `ts_fill_forward_operator('raw_sales', ..., TIMESTAMP '2024-01-10', '1d')` → `15` (was a 20s+ hang); `duckdb_functions()` lists `ts_fill_forward_operator:table_macro` and `anofox_fcst_ts_fill_forward_operator:table_macro`; `test/sql/ts_fill_forward_operator.test` (30 assertions) passes natively (full suite run) and in the DuckDB-Wasm Node harness against the branch-built v1.5.6 `wasm_eh` artifact (`ts_fill_forward_operator.test — 30 passed, 0 failed, 0 skipped`, independently re-run this session) |
| 2 | `ts_features_list()` / `ts_features_config_template()` return 117 rows natively and in DuckDB-Wasm (wasm_eh); curated WASM subset gates CI | ✓ VERIFIED | Live run against the built extension: `PREPARE p AS SELECT count(*) FROM ts_features_list(); EXECUTE p; EXECUTE p; ... ts_features_config_template() ...` → `117 117 117 117` (prepared re-execution no longer returns 0 the second time); `src/table_functions/ts_features.cpp` has 4 `GlobalTableFunctionState` occurrences and zero raw `free(` calls, releasing via `anofox_free_ts_features_list` (present in `crates/anofox-fcst-ffi/src/lib.rs:3619` and `src/include/anofox_fcst_ffi.h:2789`); `test/sql/ts_features.test` is in `test/wasm/run.mjs` CURATED and independently re-run this session in the harness passes 37/0/0 |
| 3 | A WASM artifact from `extension-upload.sh` has exactly one `duckdb_signature` section; post-upload check downloads, asserts the count, LOADs v1.5.6 `wasm_eh`, and fails on a deliberately double-signed artifact | ✓ VERIFIED | Independently re-ran the dry-run path against the real pre-fix main-branch CI artifact (run 39846248910): output has 1 section, byte-size-identical to input; independently built a double-signed fixture and confirmed both `wasm_signature_sections.py check` and `extension-upload.sh` itself abort (exit 1, no `.compressed` produced) on it; independently ran `scripts/verify-wasm-artifact.sh` against both the double-signed fixture (fails, exit 1) and a correctly-signed artifact (passes, LOADs, `post_upload_smoke.test — 3 passed, 0 failed`); confirmed live in real CI logs for run 39879822323 (`gh run view --job ... --log`): `v1.5.6/wasm_eh` deploy job shows `1 duckdb_signature section (ok)` + `post_upload_smoke.test — 3 passed, 0 failed`, `v2.0.0/wasm_eh` shows the documented `LOAD check skipped for wasm_eh v2.0.0` count-only record |
| 4 | `wasm_mvp` LOADs in DuckDB-Wasm, or the `_setThrew` root cause is written up with a recorded decision, and the build/upload matrix matches it | ✓ VERIFIED | `docs/distribution/get-erpl-io.md` has a "wasm_mvp status (DIST-03)" section and a "Decision record" section with `Decision: keep-mvp`; `scripts/verify-wasm-artifact.sh` has `LOAD_ARCHS="wasm_eh wasm_mvp"`; independently ran `scripts/verify-wasm-artifact.sh` against the official CI-built `wasm_mvp` artifact (run 39879822323) for `wasm_mvp`/`v1.5.6` — LOADs successfully, `post_upload_smoke.test — 3 passed, 0 failed, 0 skipped`, confirming the keep-mvp LOAD gate is real, not just documented |
| 5 | A handover note gives the owner the exact get.erpl.io CORS rule and the stale old-version paths, ready to apply | ✓ VERIFIED | `docs/distribution/get-erpl-io.md` has the 7 required sections (Summary, Live artifact check, CORS rule, Stale paths, Redeploy steps, wasm_mvp status, Decision record); CORS JSON contains the exact `"AllowedOrigins": ["*"]` / `"AllowedMethods": ["GET", "HEAD"]` lines; `python3 scripts/check_doc_snippets.py docs/distribution/get-erpl-io.md` → `0 FAIL, 0 STALE`; no credential-pattern matches |
| 6 | Issue #272 has a comment linking the handover document (19-04-PLAN.md must-have, additional to the 5 ROADMAP criteria above) | ✗ FAILED | `gh issue view 272 -R DataZooDE/anofox-forecast --json comments --jq '... select(contains("docs/distribution/get-erpl-io.md")) ...'` → `0`. Comment is drafted (`/home/simonm/projects/duckdb/scratch-p19/issue-272/comment.md`, content verified sound) but not posted — see Gaps |

**Score:** 6/7 must-haves verified (5/5 ROADMAP success criteria; 1/2 plan-added must-haves; the 1 failure is a ready-to-execute human/orchestrator action, not a code defect)

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/macros/ts_macros.cpp` | `ts_fill_forward_operator` table macro entry | ✓ VERIFIED | Entry present at line 470; body matches the planned `COLUMNS([...])` delegation pattern exactly |
| `src/table_functions/ts_fill_forward_operator.cpp` | Deleted | ✓ VERIFIED | File does not exist on disk; no `FillForwardOperator` symbol anywhere in `src`/`CMakeLists.txt` |
| `test/sql/ts_fill_forward_operator.test` | Operator + `ts_fill_forward_by` pins, in curated WASM subset | ✓ VERIFIED | 30 assertions, passes natively and in the harness (re-run this session) |
| `crates/anofox-fcst-ffi/src/lib.rs` | `anofox_free_ts_features_list` paired free function | ✓ VERIFIED | Present at line 3619 |
| `src/include/anofox_fcst_ffi.h` | cbindgen-regenerated declaration | ✓ VERIFIED | Present at line 2789 |
| `src/table_functions/ts_features.cpp` | Paired release, `GlobalTableFunctionState` | ✓ VERIFIED | 0 raw `free(`, 4 `GlobalTableFunctionState` occurrences, 2 `anofox_free_ts_features_list(` call sites |
| `scripts/wasm_signature_sections.py` | Read-only LEB128 section walker, count/check/--self-test | ✓ VERIFIED | `--self-test` passes (5 synthetic verdicts); `check`/`count` behave correctly against real and synthetic double-signed fixtures built independently this session |
| `scripts/extension-upload.sh` | In-place signature replacement, pre/post guards, manifest | ✓ VERIFIED | `bash -n` clean; 2 `wasm_signature_sections.py" check` call sites; no `echo -n -e` section-header append remains; aborts on a double-signed input with no `.compressed` produced (independently reproduced) |
| `scripts/verify-wasm-artifact.sh` / `scripts/extension-verify-upload.sh` | Post-upload download/decompress/section-count/LOAD check | ✓ VERIFIED | Both executable (mode 100755), `bash -n` clean, `LOAD_ARCHS="wasm_eh wasm_mvp"`; independently exercised against real artifacts (pass) and a double-signed fixture (fail) |
| `docs/distribution/get-erpl-io.md` | Owner handover: CORS rule, stale paths, live evidence, wasm_mvp decision | ✓ VERIFIED | All 7 sections present; `check_doc_snippets.py` clean |
| `.github/workflows/_extension_deploy.yml` | Deploy job runs post-upload check right after upload, WASM-gated node/npm install | ✓ VERIFIED | `extension-verify-upload.sh` invoked in the same shell directly after `${{ inputs.deploy_script }}`; `setup-node`/`npm ci --ignore-scripts` both gated on `startsWith(matrix.duckdb_arch, 'wasm')`; `timeout-minutes: 30` |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `ts_fill_forward_operator` macro | `ts_fill_forward_by` → `_ts_fill_forward_native` | `COLUMNS([param])` AST substitution in the macro body | ✓ WIRED | Confirmed by live execution (returns correct row counts, not the old hang) |
| `extension-upload.sh` | `wasm_signature_sections.py` | `check` calls before truncation and after signature append | ✓ WIRED | Grep + live dry-run + double-signed negative control, all independently reproduced |
| `_extension_deploy.yml` Deploy step | `extension-verify-upload.sh` | same-shell invocation after the deploy script | ✓ WIRED | Confirmed in workflow YAML and in real CI job logs (run 39879822323) |
| `verify-wasm-artifact.sh` | `test/wasm/run.mjs --bundle mvp` / `--bundle eh` | bundle selected from ARCH | ✓ WIRED | Independently ran both; mvp bundle LOADs via `--bundle mvp` exactly as documented |
| `docs/distribution/get-erpl-io.md` Decision line | `scripts/verify-wasm-artifact.sh` LOAD_ARCHS | keep-mvp ⇒ `wasm_mvp` present in LOAD_ARCHS | ✓ WIRED | `Decision: keep-mvp` matches `LOAD_ARCHS="wasm_eh wasm_mvp"` |
| GitHub issue #272 | `docs/distribution/get-erpl-io.md` on the pushed branch | comment linking the blob URL | ✗ NOT WIRED | Draft exists, not posted (see Truth 6 / Gaps) |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| #289 reproducer no longer hangs | `ts_fill_forward_operator('raw_sales', ...)` on built v2.0.0 extension | `15` | ✓ PASS |
| Operator registered as table macro | `duckdb_functions()` filter | 2 rows, both `table_macro` | ✓ PASS |
| Catalog functions return 117, survive prepared re-execution | `PREPARE/EXECUTE` x2 on both functions | `117 117 117 117` | ✓ PASS |
| Full native suite | `HOME=$(mktemp -d) ./build/release/test/unittest "test/*"` | `All tests passed (5690 assertions in 95 test cases)` | ✓ PASS |
| `make audit` | `make audit` | exit 0, WASM parser self-test included | ✓ PASS |
| `cargo fmt --all -- --check` | — | exit 0 | ✓ PASS |
| `make check-docs` | — | `345 PASS, 0 FAIL, ... 0 STALE` | ✓ PASS |
| Curated DuckDB-Wasm subset on branch CI `wasm_eh` artifact | `node test/wasm/run.mjs` (10 files) | `482 passed, 0 failed, 0 skipped` / `All assertions passed on DuckDB-Wasm.` | ✓ PASS |
| Double-signed fixture rejected by parser + upload script | `wasm_signature_sections.py check` / `extension-upload.sh` | both exit 1, no `.compressed` | ✓ PASS |
| Post-upload LOAD gate on a correctly-signed `wasm_eh` artifact | `scripts/verify-wasm-artifact.sh ... wasm_eh v1.5.6` | exit 0, LOAD + smoke test pass | ✓ PASS |
| Post-upload LOAD gate on the keep-mvp `wasm_mvp` artifact (official CI build) | `scripts/verify-wasm-artifact.sh ... wasm_mvp v1.5.6` | exit 0, LOAD + smoke test pass | ✓ PASS |
| Real CI deploy-job evidence (run 39879822323) | `gh run view --job <id> --log` on `v1.5.6/wasm_eh` and `v2.0.0/wasm_eh` deploy jobs | section-count-ok, smoke-test pass, documented v2.0.0 skip-record all present verbatim | ✓ PASS |
| #272 comment posted | `gh issue view 272 --json comments` | 0 matching comments | ✗ FAIL (see Truth 6) |

### Probe Execution

Not applicable — this phase has no `scripts/*/tests/probe-*.sh` convention; verification used the project's own `make audit`, `make check-docs`, native `test/unittest`, and the DuckDB-Wasm Node harness (`test/wasm/run.mjs`) instead, all exercised directly above.

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|--------------|-------------|--------------|--------|----------|
| BUG-01 | 19-01 | `ts_fill_forward_operator` no longer hangs/traps | ✓ SATISFIED | Truth 1 |
| BUG-02 | 19-03 | `ts_features_list()`/`ts_features_config_template()` run in WASM, 117 rows | ✓ SATISFIED | Truth 2 |
| DIST-01 | 19-02 | Upload script produces exactly one `duckdb_signature` section | ✓ SATISFIED | Truth 3 |
| DIST-02 | 19-02 | Post-upload check downloads, counts, LOADs, fails on double-signed | ✓ SATISFIED | Truth 3 |
| DIST-03 | 19-04 | `wasm_mvp` LOADs or root cause + decision recorded, matrix matches | ✓ SATISFIED | Truth 4 |
| DIST-04 | 19-02, 19-04 | CORS rule + stale paths documented and handed to the owner | ⚠️ PARTIALLY SATISFIED | Doc exists and is complete (Truth 5); "handed to the owner" via the #272 comment has not happened yet (Truth 6 gap) |

No orphaned requirements: `.planning/REQUIREMENTS.md` maps exactly BUG-01, BUG-02, DIST-01..04 to Phase 19, and all six are claimed across the four plans' `requirements:` frontmatter.

### Anti-Patterns Found

None. Scanned all touched core files (`src/macros/ts_macros.cpp`, `src/table_functions/ts_features.cpp`, `scripts/wasm_signature_sections.py`, `scripts/extension-upload.sh`, `scripts/verify-wasm-artifact.sh`, `scripts/extension-verify-upload.sh`) for `TBD`/`FIXME`/`XXX`/`TODO`/`HACK`/placeholder language/stub returns — zero matches. A prior `gsd-code-reviewer` pass (`19-REVIEW.md`, 2026-10-23T06:06:40Z, 20 files) found 0 critical/blocker issues and 3 warnings (unquoted variables in `extension-upload.sh`'s older code paths; `_ts_fill_forward_native`'s pre-existing WASM silent-data-loss bug losing WASM CI exposure after the test-file move; a weak self-test boundary case in the signature-section payload-length check). None of these three warnings contradict a ROADMAP success criterion or a must-have truth — they are code-quality follow-ups, already partly pre-existing and partly correctly out-of-scope per the locked CONTEXT.md decision, and are listed here for visibility rather than as phase-blocking gaps.

### Human Verification Required

None. Every must-have in this phase is either directly, mechanically verifiable (and was verified above) or already failed a mechanical check (Truth 6, which is a concrete, actionable gap, not a judgment call).

### Gaps Summary

One gap, low severity and trivially closeable: the 19-04 plan's own must-have requires a comment on issue #272 linking `docs/distribution/get-erpl-io.md`. That comment is fully drafted and verified sound (`/home/simonm/projects/duckdb/scratch-p19/issue-272/comment.md`: correct commit SHAs, the keep-mvp decision, owner actions, the handover-doc URL, an explicit "no bucket change was made" statement, no credential material) but was deliberately not posted during 19-04's execution — the SUMMARY states this was an explicit instruction to defer posting to the orchestrator pending user approval. That approval/posting step has not yet occurred. Every underlying technical deliverable of Phase 19 (BUG-01, BUG-02, DIST-01, DIST-02, DIST-03, and the handover document itself) is independently verified working, including live evidence pulled from a real GitHub Actions run (39879822323) and live exercises of the shipped scripts against real CI-built WASM artifacts (including the correctly-signed `wasm_mvp` artifact under the keep-mvp LOAD gate). Closing this gap requires no code change, no replanning, and no further investigation — only running `gh issue comment 272 -R DataZooDE/anofox-forecast --body-file /home/simonm/projects/duckdb/scratch-p19/issue-272/comment.md` (after a final glance to confirm the commit SHAs it cites are still current) once a human has signed off on posting it.

If the user judges the drafted-but-unposted comment to be an acceptable interim state for this phase (e.g. because posting to a public issue should happen alongside the eventual `main` merge rather than from the feature branch), the appropriate resolution is an override, not a replan:

```yaml
overrides:
  - must_have: "Issue #272 has a comment linking the handover document"
    reason: "Comment is drafted and verified sound; posting deferred until the milestone branch merges to main / until explicit user sign-off, to avoid prematurely pointing #272 at a feature-branch doc URL"
    accepted_by: "<human>"
    accepted_at: "<ISO timestamp>"
```

---

*Verified: 2026-10-23T06:09:38Z*
*Verifier: Claude (gsd-verifier)*


## Gap closure (2026-10-23)

The one gap (the #272 comment linking the handover doc) is closed: the user approved a tightened version, posted at https://github.com/DataZooDE/anofox-forecast/issues/272#issuecomment-6358734104 . It links docs/distribution/get-erpl-io.md and lists the owner bucket actions. All 7 must-haves are now verified.
