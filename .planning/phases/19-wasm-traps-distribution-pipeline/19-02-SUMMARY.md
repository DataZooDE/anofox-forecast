---
phase: 19-wasm-traps-distribution-pipeline
plan: 02
subsystem: infra
tags: [wasm, webassembly, s3, cloudfront, duckdb-extension-distribution, duckdb-wasm, github-actions]

# Dependency graph
requires:
  - phase: 19-01
    provides: "ts_fill_forward_operator fix verified in the same DuckDB-Wasm Node harness (test/wasm/run.mjs); shared scratch-p19 build artifacts reused here"
provides:
  - "scripts/wasm_signature_sections.py: stdlib-only, read-only LEB128 WASM custom-section walker with count/check/--self-test"
  - "scripts/extension-upload.sh: WASM signature replaced in place (never appended); pre/post guards; upload manifest ($ext.uploaded)"
  - "scripts/verify-wasm-artifact.sh + scripts/extension-verify-upload.sh: post-upload download/decompress/section-count/conditional-LOAD check wired into the deploy job"
  - "test/wasm/post_upload_smoke.test: minimal ts_forecast_by LOAD proof for the post-upload check"
  - "docs/distribution/get-erpl-io.md: owner handover (CORS rule, stale paths, redeploy steps) backed by a 134-line read-only probe log"
affects: [19-04, future get.erpl.io owner bucket changes, any future extension-ci-tools WASM artifact shape change]

# Actuals (#2632)
actuals:
  tokens: 17528
  tasks: 3
  commits: 3
plan_head_before: 8360ffd86be761e75c62802a20fb2299ec817d4d

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Read-only stdlib-only Python parser with a self-contained --self-test synthetic-module suite, mirroring the existing audit_no_stats_in_cpp.py convention"
    - "Upload manifest file ($name.duckdb_extension.wasm.uploaded) as the hand-off between the upload script and the post-upload verifier, avoiding a second copy of the S3 key-formula logic"
    - "Post-upload verification pulls the just-uploaded bytes from S3 directly (via the manifest), not over public HTTPS through the CDN domain -- independent of whether CORS is fixed"

key-files:
  created:
    - scripts/wasm_signature_sections.py
    - scripts/verify-wasm-artifact.sh
    - scripts/extension-verify-upload.sh
    - test/wasm/post_upload_smoke.test
    - docs/distribution/get-erpl-io.md
    - .planning/phases/19-wasm-traps-distribution-pipeline/19-02-get-erpl-io-probe.tsv
  modified:
    - scripts/extension-upload.sh
    - Makefile
    - .github/workflows/audit.yml
    - .github/workflows/_extension_deploy.yml

key-decisions:
  - "Followed CONTEXT's locked unconditional in-place-replacement design (never append a section header) rather than porting anofox-statistics PR #193's conditional variant -- today's CI artifact always already carries the section (19-RESEARCH.md Pitfall 5), and a future shape change now fails loudly via the pre-guard instead of silently falling back."
  - "Post-upload check downloads from S3 via the upload script's own manifest, not over public HTTPS through get.erpl.io -- verification does not depend on CORS being fixed first, and avoids CDN cache staleness entirely."
  - "verify-wasm-artifact.sh treats a missing test/wasm/node_modules as a hard failure (with an install hint) rather than a silent LOAD-check skip, so a misconfigured CI job cannot quietly downgrade to section-count-only coverage."
  - "Local end-to-end testing used a from-scratch fake 'aws' CLI shim (s3 cp only) plus a scratch-only FAKE_S3 directory -- no real aws command was ever run against the real bucket, and no repository variable or secret was read."
  - "The owner-handover probe documents a 404 on the guessed versioned-tree key for the newest release tag as an open gap (EXT_VERSION may be a commit hash rather than the human tag name for a given deploy run) instead of asserting staleness without evidence."

requirements-completed: [DIST-01, DIST-02, DIST-04]

coverage:
  - id: D1
    description: "extension-upload.sh turns a WASM artifact into one with exactly one duckdb_signature section via in-place 256-byte replacement, never a second section header"
    requirement: "DIST-01"
    verification:
      - kind: other
        ref: "env EXTENSION_UPLOAD_DIR=... ./scripts/extension-upload.sh ... wasm_eh ... (dry run on real CI v1.5.6 wasm_eh artifact from run 39846248910) + python3 scripts/wasm_signature_sections.py check on both input and processed bytes"
        status: pass
    human_judgment: false
  - id: D2
    description: "extension-upload.sh aborts (non-zero exit, nothing compressed/uploaded) on a WASM artifact that does not already carry exactly one duckdb_signature section"
    requirement: "DIST-01"
    verification:
      - kind: other
        ref: "double-signed fixture (bytes reproducing the pre-fix script's exact append) run through extension-upload.sh: exit 1, no .compressed produced"
        status: pass
    human_judgment: false
  - id: D3
    description: "Processed WASM artifact is byte-size-identical to the input and LOADs + runs correctly in the DuckDB-Wasm Node harness"
    requirement: "DIST-01"
    verification:
      - kind: e2e
        ref: "node test/wasm/run.mjs --ext processed.wasm --file test/sql/ts_forecast_by.test -- 90 passed, 0 failed; byte-size match confirmed via stat"
        status: pass
    human_judgment: false
  - id: D4
    description: "Post-upload check downloads every uploaded WASM object, asserts section count 1, LOADs v1.5.6 wasm_eh in the Node harness, and records section-count-only for v2.0.0/other archs"
    requirement: "DIST-02"
    verification:
      - kind: e2e
        ref: "fake-S3 end-to-end run (extension-upload.sh with 2 copy flags true, then extension-verify-upload.sh): 2 uploaded objects, 2x 'duckdb_signature section (ok)', 2x post_upload_smoke.test pass; v2.0.0 path prints the documented skip-record message"
        status: pass
    human_judgment: false
  - id: D5
    description: "Post-upload check fails on a deliberately double-signed uploaded object"
    requirement: "DIST-02"
    verification:
      - kind: e2e
        ref: "fake-S3 object swapped for the double-signed fixture: extension-verify-upload.sh and verify-wasm-artifact.sh both exit non-zero"
        status: pass
    human_judgment: false
  - id: D6
    description: "docs/distribution/get-erpl-io.md gives the owner the exact CORS rule, stale old-version paths with live evidence, and redeploy steps, with no bucket change made by this phase"
    requirement: "DIST-04"
    verification:
      - kind: other
        ref: "python3 scripts/check_doc_snippets.py docs/distribution/get-erpl-io.md -- 0 FAIL, 0 STALE; 5 required headings present; exact CORS AllowedOrigins/AllowedMethods JSON lines present; no credential-pattern matches; 134-line probe TSV backing every claim"
        status: pass
    human_judgment: false

duration: ~20min
completed: 2026-10-23
status: complete
---

# Phase 19 Plan 02: Fix WASM distribution pipeline (#272) Summary

**`extension-upload.sh` now replaces a WASM artifact's existing `duckdb_signature` section in place instead of appending a second one, a post-upload check in `_extension_deploy.yml` downloads and LOAD-verifies every published WASM object (fails the deploy job on any defect), and `docs/distribution/get-erpl-io.md` hands the bucket owner an evidence-backed CORS rule and stale-path cleanup list.**

## Performance

- **Duration:** ~20 min
- **Started:** 2026-10-22T23:40:00Z (approx., right after 19-01)
- **Completed:** 2026-10-22T23:58:00Z
- **Tasks:** 3
- **Files modified:** 10

## Accomplishments

- Created `scripts/wasm_signature_sections.py`: a stdlib-only, read-only LEB128 WASM custom-section parser (ported from anofox-visualization's `strip_wasm_signature.py` pattern) with `count`/`check`/`--self-test`, wired into `make audit` and `.github/workflows/audit.yml`.
- Rewrote `scripts/extension-upload.sh`'s WASM handling per the locked CONTEXT #272 decision: the 256-byte signature placeholder is now truncated and replaced in place for every architecture (removing the native-only gate), the legacy section-header-append block is deleted entirely, and a pre-guard (on the input) and post-guard (on the processed bytes) both call `wasm_signature_sections.py check` so the script aborts rather than ever producing a double-signed artifact. Added `EXTENSION_UPLOAD_DIR` (default `/tmp/extension`, CI-compatible) and an upload manifest (`$ext.uploaded`) recording every `s3://` URI this invocation actually published.
- Verified the fix against the real pre-fix CI artifact (v1.5.6 `wasm_eh`, downloaded from main run `39846248910`): the processed artifact is byte-size-identical to the input, passes the section-count guard, and LOADs + passes `ts_forecast_by.test` (90/0) in the DuckDB-Wasm Node harness. A deliberately double-signed fixture (reproducing the pre-fix script's exact appended bytes) makes both the parser check and the upload script fail, producing no `.compressed` output.
- Created `scripts/verify-wasm-artifact.sh` (decompress-if-needed, section check, conditional LOAD via the Node harness for the `v1.5.6`/`wasm_eh` combination the pinned `@duckdb/duckdb-wasm` build can actually run; section-count-only record for everything else) and `scripts/extension-verify-upload.sh` (manifest-driven over every uploaded object for a WASM arch; falls back to local `.compressed` bytes when nothing was uploaded; no-op for native archs). Added `test/wasm/post_upload_smoke.test` (require `anofox_forecast` only, 28-row series, asserts 3 forecast rows) as the harness's LOAD proof.
- Wired `_extension_deploy.yml`: WASM-gated `setup-node` + `npm --prefix test/wasm ci --ignore-scripts` (lockfile-pinned, no install scripts in the credentialed job) ahead of Deploy; the Deploy step calls `extension-verify-upload.sh` immediately after the deploy script in the same shell; `timeout-minutes` raised 20 -> 30. Verified end-to-end with a scratch-only fake `aws` CLI shim against the real CI artifact: 2 uploaded objects (versioned + latest) each verify with one section and a LOAD pass; swapping one fake-S3 object for the double-signed fixture makes the check fail; `v2.0.0`'s skip-record message and the no-upload local-bytes fallback both print as documented.
- Ran a 134-row read-only HTTP probe (`curl` GET/HEAD/OPTIONS only, no bucket writes) against `https://get.erpl.io` and wrote `docs/distribution/get-erpl-io.md`: confirmed all 9 current-line WASM objects are live-double-signed today (will self-correct on the next redeploy), no CORS headers exist anywhere and OPTIONS preflight returns 403, CloudFront fronts the S3 bucket, `v1.5.4`/`v1.5.5` are fully frozen (7 platforms each, since 2026-07-17/2026-09-26), `v1.4.5`'s `windows_amd64`/`osx_amd64`/`osx_arm64` lag the rest of that line because the current CI matrix excludes them (not a staleness bug), and anonymous S3 listing is not actually exposed through the custom domain (200 responses are the marketing landing page, not a `ListBucketResult`).

## Task Commits

Each task was committed atomically:

1. **Task 1: Tracer -- extension-upload.sh overwrites the signature in place** - `6c8f959` (fix)
2. **Task 2: Post-upload check in the deploy job** - `4c4462a` (feat)
3. **Task 3: Owner handover -- get.erpl.io CORS rule, stale paths, redeploy steps** - `393fbef` (docs)

## Files Created/Modified

- `scripts/wasm_signature_sections.py` - New. Read-only LEB128 WASM custom-section walker; `count`/`check`/`--self-test` CLI.
- `scripts/extension-upload.sh` - WASM signature replaced in place (no more section-header append); `EXTENSION_UPLOAD_DIR`; pre/post guards; upload manifest.
- `Makefile`, `.github/workflows/audit.yml` - Wire the parser's `--self-test` into `make audit`.
- `scripts/verify-wasm-artifact.sh` - New. Decompress-if-needed + section check + conditional Node-harness LOAD for one artifact.
- `scripts/extension-verify-upload.sh` - New. Manifest-driven post-upload check over every uploaded object for a WASM arch; local-bytes fallback.
- `test/wasm/post_upload_smoke.test` - New. Minimal `ts_forecast_by` LOAD proof used by the post-upload check.
- `.github/workflows/_extension_deploy.yml` - WASM-gated Node setup + harness install ahead of Deploy; post-upload check call right after the deploy script; `timeout-minutes` 20 -> 30.
- `docs/distribution/get-erpl-io.md` - New. Owner handover: Summary / Live artifact check / CORS rule / Stale paths / Redeploy steps.
- `.planning/phases/19-wasm-traps-distribution-pipeline/19-02-get-erpl-io-probe.tsv` - New. 134-line read-only probe evidence log.

## Decisions Made

See `key-decisions` in frontmatter. In short: followed CONTEXT's locked unconditional in-place-replacement design rather than anofox-statistics' conditional PR #193 variant; verification pulls from S3 via the upload manifest rather than over public HTTPS (independent of the CORS fix); a missing `test/wasm/node_modules` is a hard failure, not a silent LOAD-check skip; all local verification used a scratch-only fake `aws` CLI and FAKE_S3 directory, never the real bucket or a real `aws` credential/variable read.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] The plan's literal native-unittest invocation (`$PWD`-absolute path) does not match this build's Catch2 test-name registration**
- **Found during:** Task 2, step 7 verification (`./build/release/test/unittest "$PWD/test/wasm/post_upload_smoke.test"`)
- **Issue:** Running the exact command text from the plan's verify block (`HOME=$(mktemp -d) ./build/release/test/unittest "$PWD/test/wasm/post_upload_smoke.test" ...`) returned `No test cases matched` -- the sqllogictest files in this build are registered under their repo-root-relative path (`test/wasm/post_upload_smoke.test`), and the Catch2 filter requires an exact name match, not a substring/absolute-path match.
- **Fix:** Ran the equivalent command with the relative path instead (`HOME=$(mktemp -d) ./build/release/test/unittest "test/wasm/post_upload_smoke.test" ...`, executed from the repo root) -- confirmed via `--list-test-names-only` that the registered name is exactly the relative path. The test file's content is unchanged; only the verification command's path style needed correction.
- **Files modified:** None (verification-invocation-only; no source change).
- **Verification:** `All tests passed (3 assertions in 1 test case)` -- re-run multiple times during Task 2, including the final plan-level re-check.
- **Committed in:** N/A (no code change; documented here per the deviation-tracking rule).

---

**Total deviations:** 1 auto-fixed (1 blocking/verification-invocation). **Impact on plan:** None on the delivered code or tests -- the smoke test and all scripts behave exactly as specified; only the literal shell invocation style in one verify command needed a path-format correction to actually run on this build.

## Issues Encountered

None beyond the documented deviation above.

## User Setup Required

None - no external service configuration required. `docs/distribution/get-erpl-io.md` lists owner-only bucket actions (CORS rule, stale-path deletion) that are explicitly out of this phase's scope per CONTEXT.md -- they are handover instructions, not setup this plan performed.

## Next Phase Readiness

- DIST-01 and DIST-02 are closed: the upload script can no longer publish a double-signed WASM artifact, and the deploy job now proves what it published actually LOADs (for the engine/arch the Node harness supports) or at minimum has the correct section count.
- DIST-04's core (CORS rule + stale paths + redeploy steps, documented for the owner) is closed; the `#272` comment and `wasm_mvp` finalization are explicitly deferred to plan 19-04.
- Live evidence confirms every current-line WASM object on `get.erpl.io` is still double-signed as of this probe -- the next `main`/tag push after this plan merges is what actually fixes production; 19-04's own verification task should re-probe post-merge to confirm the fix landed live, not just locally.
- No `aws` command was run against the real bucket; no repository variable or secret was read or printed at any point in this plan's execution.
- Ready for 19-03 (ts_features catalog WASM trap) and 19-04 (wasm_mvp decision, CHANGELOG entries, handover finalization, #272 comment, live-deploy re-verification).

---
*Phase: 19-wasm-traps-distribution-pipeline*
*Completed: 2026-10-23*

## Self-Check: PASSED
