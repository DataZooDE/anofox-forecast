# get.erpl.io distribution handover (#272)

**Date:** 2026-10-23
**Probe evidence:** `.planning/phases/19-wasm-traps-distribution-pipeline/19-02-get-erpl-io-probe.tsv` (131 read-only HTTP probe rows, all GET/HEAD/OPTIONS, no bucket writes)
**Bucket changes made by this phase:** none. Everything below is a handover for the bucket owner to apply; the owner holds the AWS credentials for the `get.erpl.io` bucket (the repository variable `DEPLOY_S3_BUCKET`, not read or printed by this document or by this phase's code).

## Summary

Issue #272 reported three independent problems with the `get.erpl.io` distribution pipeline. This phase (19-02) fixed the two that are controlled by this repository's code, and documents the one that is owner/bucket-controlled:

1. **Double-signed WASM objects that fail to LOAD (DIST-01, fixed in code).** `scripts/extension-upload.sh` used to unconditionally append a second `duckdb_signature` custom section to every WASM artifact. It now replaces the last 256 bytes of the artifact's existing trailing section in place -- exactly like the native path -- and aborts (via `scripts/wasm_signature_sections.py check`) unless the input already ends with exactly one such section. See `scripts/wasm_signature_sections.py` and `scripts/extension-upload.sh`.
2. **No check after upload caught a broken artifact (DIST-02, fixed in code).** The deploy job (`.github/workflows/_extension_deploy.yml`) now runs `scripts/extension-verify-upload.sh` immediately after `extension-upload.sh`: it downloads every object the upload script just published, decompresses it, re-asserts the one-section invariant, and -- on the `v1.5.6`/`wasm_eh` line the pinned `@duckdb/duckdb-wasm` build can actually LOAD -- runs the real Node DuckDB-Wasm harness against it. Any failure fails the deploy job.
3. **No CORS rule, and stale old-version paths (DIST-04, this document).** `get.erpl.io` currently returns no `Access-Control-Allow-Origin` header at all and rejects CORS preflight requests with `403`, so browser-based DuckDB-Wasm cannot fetch extensions from it cross-origin. Several old-version paths are also frozen well before the current three-line pipeline (`v2.0.0` / `v1.5.6` / `v1.4.5`). Both require bucket-level changes this phase does not make -- see "CORS rule" and "Stale paths" below.

**Live evidence at probe time (2026-10-22/23, see the TSV for every row):** every currently-shipped WASM object on all three current lines (`v2.0.0`, `v1.5.6`, `v1.4.5` x `wasm_mvp`/`wasm_eh`/`wasm_threads` -- 9 objects) is live-double-signed (`duckdb_signature` section count = 2) today, confirming DIST-01 is still an active production bug until the next deploy picks up this phase's fix. `get.erpl.io` is fronted by CloudFront in front of an S3 bucket (`server: AmazonS3`, `via: ... (CloudFront)` on every response, including 404s, which return a real S3 `NoSuchKey` XML body -- not a generic error page).

## Live artifact check

### duckdb_signature section count, current-line WASM objects (probed 2026-10-22T23:54Z)

| Version | Arch | URL | Section count |
|---------|------|-----|----------------|
| v2.0.0 | wasm_mvp | `https://get.erpl.io/v2.0.0/wasm_mvp/anofox_forecast.duckdb_extension.wasm` | 2 (broken) |
| v2.0.0 | wasm_eh | `https://get.erpl.io/v2.0.0/wasm_eh/anofox_forecast.duckdb_extension.wasm` | 2 (broken) |
| v2.0.0 | wasm_threads | `https://get.erpl.io/v2.0.0/wasm_threads/anofox_forecast.duckdb_extension.wasm` | 2 (broken) |
| v1.5.6 | wasm_mvp | `https://get.erpl.io/v1.5.6/wasm_mvp/anofox_forecast.duckdb_extension.wasm` | 2 (broken) |
| v1.5.6 | wasm_eh | `https://get.erpl.io/v1.5.6/wasm_eh/anofox_forecast.duckdb_extension.wasm` | 2 (broken) |
| v1.5.6 | wasm_threads | `https://get.erpl.io/v1.5.6/wasm_threads/anofox_forecast.duckdb_extension.wasm` | 2 (broken) |
| v1.4.5 | wasm_mvp | `https://get.erpl.io/v1.4.5/wasm_mvp/anofox_forecast.duckdb_extension.wasm` | 2 (broken) |
| v1.4.5 | wasm_eh | `https://get.erpl.io/v1.4.5/wasm_eh/anofox_forecast.duckdb_extension.wasm` | 2 (broken) |
| v1.4.5 | wasm_threads | `https://get.erpl.io/v1.4.5/wasm_threads/anofox_forecast.duckdb_extension.wasm` | 2 (broken) |

All 9 are double-signed today. The next `main` push (or tag push) after this plan merges redeploys all three lines with the fixed `extension-upload.sh`, which will publish single-section objects and the deploy job's own post-upload check will verify that before the job reports success (see "Redeploy steps").

### CORS / preflight (probed against `v1.5.6/wasm_eh`, representative of the whole domain)

| Check | Result |
|-------|--------|
| `GET` with `Origin: https://example.org` | `200`, no `Access-Control-Allow-Origin` header in the response |
| `HEAD` with `Origin: https://example.org` | `200`, no `Access-Control-Allow-Origin` header |
| `OPTIONS` preflight (`Access-Control-Request-Method: GET`) | `403` |
| Fronting infrastructure | `server: AmazonS3`, `via: ... (CloudFront)`, `x-cache: Miss from cloudfront` on a hit, `Error from cloudfront` on a 404 -- confirms CloudFront sits in front of the S3 bucket |

No CORS configuration is present anywhere in the response path today. A browser-based DuckDB-Wasm client (as opposed to this repo's Node harness, which serves the artifact itself over `localhost` and so never hits this restriction) cannot `fetch()` an extension from `get.erpl.io` cross-origin.

### Anonymous listing

A `GET /?list-type=2&delimiter=/` (and prefixed variants) returns HTTP `200`, but the response body is the `ERPL.io` marketing landing page HTML (`<title>Download Extensions - ERPL.io</title>`), not an S3 `ListBucketResult` XML document. Anonymous `ListBucket` is **not** actually exposed through this custom domain -- the `200` is the domain's own default-document behavior for an unmatched/query-string path, not a real listing. All path discovery in this document therefore comes from `HEAD`-probing the fixed key layout `scripts/extension-upload.sh` writes (`<version>/<arch>/<file>` for the latest tree, `<name>/<ext_version>/<duckdb_version>/<arch>/<file>` for the versioned tree), not from a directory listing.

## CORS rule

Apply this CORS configuration to the bucket (`BUCKET` below is the value of the `DEPLOY_S3_BUCKET` repository variable -- the owner has that value; it is intentionally not printed in this document):

```json
{
  "CORSRules": [
    {
      "AllowedOrigins": ["*"],
      "AllowedMethods": ["GET", "HEAD"],
      "AllowedHeaders": ["*"],
      "ExposeHeaders": ["Content-Length", "Content-Type", "Content-Encoding", "ETag", "Last-Modified"],
      "MaxAgeSeconds": 86400
    }
  ]
}
```

```bash
# Owner action -- NOT run by this phase. Save the JSON above as cors.json, then:
aws s3api put-bucket-cors --bucket "$BUCKET" --cors-configuration file://cors.json

# Confirm it took effect:
aws s3api get-bucket-cors --bucket "$BUCKET"
```

Because CloudFront fronts the bucket (confirmed above), the S3-level CORS rule alone is not sufficient -- CloudFront must also be told to forward the `Origin` request header to the origin and to pass through (or itself attach) the CORS response headers, otherwise CloudFront's cache will serve a cached response with no CORS headers to every origin regardless of the bucket's own configuration:

```bash
# Owner action -- NOT run by this phase.
# 1. Attach (or create) a CloudFront response-headers policy that mirrors the
#    rule above (Access-Control-Allow-Origin: *, Allow GET/HEAD) to the
#    distribution's default cache behavior.
# 2. Ensure the cache behavior's cache policy includes "Origin" in its
#    Headers allow-list (or use CORS-S3Origin / CORS-CustomOrigin managed
#    cache policies), so responses vary correctly by Origin.
# 3. Invalidate the WASM paths so clients stop getting cached pre-CORS
#    responses:
aws cloudfront create-invalidation --distribution-id "$DISTRIBUTION_ID" --paths "/*/wasm_*/*"
```

Verification from a workstation after both the S3 rule and the CloudFront policy are applied:

```bash
curl -s -D - -o /dev/null -H "Origin: https://example.org" \
  https://get.erpl.io/v1.5.6/wasm_eh/anofox_forecast.duckdb_extension.wasm | grep -i access-control

curl -s -D - -o /dev/null -X OPTIONS -H "Origin: https://example.org" \
  -H "Access-Control-Request-Method: GET" \
  https://get.erpl.io/v1.5.6/wasm_eh/anofox_forecast.duckdb_extension.wasm
# Expected: a 2xx/204 response (not 403) and an access-control-allow-origin header on both requests.
```

## Stale paths

### Fully orphaned old-version latest-tree objects

| Path prefix | Last-Modified (probed) | Reason |
|-------------|--------------------------|--------|
| `v1.5.4/*/anofox_forecast.duckdb_extension.*` (7 platforms: linux_amd64, linux_arm64, osx_amd64, osx_arm64, windows_amd64, wasm_mvp, wasm_eh, wasm_threads) | `2026-07-17` (every probed object, identical timestamp) | `v1.5.4` is not one of the current pipeline's three lines (`v2.0.0`/`v1.5.6`/`v1.4.5`); nothing has rebuilt or re-uploaded to this path since 2026-07-17 |
| `v1.5.5/*/anofox_forecast.duckdb_extension.*` (same 7 platforms) | `2026-09-26` (every probed object, identical timestamp) | Same as above; `v1.5.5` is also not a current line |

`v1.4.3`, `v1.4.4`, `v1.5.0`, and `v1.5.1` -- the other DuckDB versions that appeared in `MainDistributionPipeline.yml` history -- all returned `404 NoSuchKey` for every probed arch at probe time: no objects exist at those paths today (either never uploaded under the current key layout, or already cleaned up). No action needed for these four.

Owner cleanup commands (prefix-scoped; run only after confirming nothing still depends on these, since `aws s3 rm` is destructive and irreversible without versioning):

```bash
# Owner action -- NOT run by this phase.
aws s3 rm "s3://$BUCKET/v1.5.4/" --recursive
aws s3 rm "s3://$BUCKET/v1.5.5/" --recursive
```

### Stale-within-a-current-line objects (arch excluded by the current build matrix)

The `v1.4.5` (LTS) line excludes `windows_amd64`, `windows_amd64_rtools`, `windows_amd64_mingw`, `osx_arm64`, and `osx_amd64` from its CI matrix today (see `.github/workflows/MainDistributionPipeline.yml`'s `duckdb-lts-build` comments: a dead vcpkg OpenSSL baseline for `windows_amd64`, and a `macos-26` runner SDK/linker break for the `osx_*` archs, both first hit 2026-10-16). Objects for those archs under the `v1.4.5` latest-tree path are therefore **not refreshed by the current pipeline**, even though `v1.4.5` itself is a current line:

| Path | Last-Modified (probed) | Reason |
|------|--------------------------|--------|
| `v1.4.5/windows_amd64/anofox_forecast.duckdb_extension.gz` | `2026-08-09` | `windows_amd64` excluded on the LTS line (vcpkg OpenSSL baseline 404s) |
| `v1.4.5/osx_amd64/anofox_forecast.duckdb_extension.gz` | `2026-10-16` | `osx_amd64` excluded on the LTS line (macOS runner SDK linker break, first hit 2026-10-16) |
| `v1.4.5/osx_arm64/anofox_forecast.duckdb_extension.gz` | `2026-10-08` | `osx_arm64` excluded on the LTS line (same macOS SDK issue) |

These are not candidates for deletion (a user still targeting `v1.4.5` on those platforms may be relying on the last good build) -- they are flagged here so the owner understands why their `Last-Modified` dates lag the rest of the `v1.4.5` tree, not because `v1.4.5` itself is stale. They will start refreshing again automatically once the underlying CI exclusion (tracked separately, not in this phase's scope) is lifted.

### Versioned-tree objects: not stale, retained by design

Every object uploaded under the versioned-tree key layout (`<name>/<ext_version>/<duckdb_version>/<arch>/<file>`, written whenever `copy_to_versioned` is true -- every `main` push and every `v*` tag push) is retained intentionally and is **not** a candidate for the cleanup above; that is the entire point of the versioned path (pinning a specific extension version against a specific DuckDB version indefinitely). A probe against the versioned tree for the newest release tag (`v2026.10.22`, which did have a successful tag-triggered pipeline run) returned `404` for the exact key format this script guessed (`anofox_forecast/v2026.10.22/v2.0.0/linux_amd64/...` and the `wasm_eh` equivalent) -- this is recorded as an open probe gap, not a staleness finding: either that specific tag run did not populate every arch at the versioned path, or the exact `EXT_VERSION` string the deploy script used for that run differs from the human-readable tag name (`extension-upload.sh`'s `EXT_VERSION` is `git tag --points-at HEAD`, falling back to a short commit hash when HEAD is untagged at deploy time -- the two are not always interchangeable). This does not block DIST-04; it is flagged for the owner in case they want to confirm versioned-tree coverage independently (e.g. via their own S3 console access, which this phase's read-only HTTP probe cannot substitute for).

## Redeploy steps

1. **Merge this plan's branch to `main`.** `MainDistributionPipeline.yml`'s three lines (`duckdb-stable-deploy` / `duckdb-v15-deploy` / `duckdb-lts-deploy`) each run `scripts/extension-upload.sh` (now fixed) followed by `scripts/extension-verify-upload.sh` (new) inside the same deploy-job shell, for every arch in that line's build matrix. Any post-upload verification failure fails that arch's deploy-job matrix entry.
2. **Confirm the redeploy's WASM objects are now single-signed.** Either re-run this plan's probe script pattern against the live objects, or trust the deploy job's own post-upload check (it is the stronger proof -- it LOADs `v1.5.6`/`wasm_eh` in the real Node harness against the actual uploaded bytes, not just a byte count).
3. **Owner applies the CORS rule** (see "CORS rule" above) to the S3 bucket and, since CloudFront fronts it, to the CloudFront distribution's response-headers/cache policy, then invalidates the WASM paths.
4. **Owner deletes the stale `v1.5.4`/`v1.5.5` paths** (see "Stale paths" above) once satisfied nothing still depends on them.
5. **Verify from a workstation** (not CI, to confirm the public-facing bytes and headers, independent of this repo's own tooling):
   ```bash
   curl --compressed -o /tmp/eh.wasm https://get.erpl.io/v1.5.6/wasm_eh/anofox_forecast.duckdb_extension.wasm
   python3 scripts/wasm_signature_sections.py check /tmp/eh.wasm   # expect: ok, 1 section
   scripts/verify-wasm-artifact.sh /tmp/eh.wasm wasm_eh v1.5.6     # expect: LOAD + post_upload_smoke.test pass
   ```
   Then, in a browser or DuckDB-Wasm Node session: `FORCE INSTALL anofox_forecast FROM 'https://get.erpl.io';` and confirm it loads and `ts_forecast_by` runs.

## wasm_mvp status (DIST-03)

**Date:** 2026-10-23
**Artifact provenance:** both `wasm_mvp` and `wasm_eh` artifacts are the official build-job outputs of branch CI run [`39879822323`](https://github.com/DataZooDE/anofox-forecast/actions/runs/39879822323) (commit `611b1fd9e88d9a15ceda19b8c0d789fc1b0270b3`, conclusion `success`), downloaded via `gh run download 39879822323 -n anofox_forecast-v1.5.6-extension-wasm_mvp` / `-wasm_eh`. All measurements below were additionally cross-checked against a `wasm_mvp` artifact built locally from commit `3d2cd48` (one commit behind HEAD; `git diff 3d2cd48 611b1fd` touches only `.planning/`, `test/wasm/run.mjs`'s skip-list wording, and `test/wasm/README.md` -- no `src/`, `crates/`, or `CMakeLists.txt` changes) -- both sources produced byte-identical totals (482/0/0 curated, 3604/49/0 `--all`, 188 `_setThrew` occurrences), ruling out a build-environment artifact as the explanation for any of the findings below. Harness: `node test/wasm/run.mjs --bundle mvp`, `@duckdb/duckdb-wasm` 1.33.1-dev65.0 (engine v1.5.6).

### LOAD verdict

`FORCE INSTALL anofox_forecast FROM <local server>; LOAD anofox_forecast;` succeeds on `wasm_mvp` -- confirmed via `DuckDB-Wasm bundle: mvp` followed by `LOAD anofox_forecast succeeded` in every run below. The `#272` issue comment's "fails to LOAD" framing does not hold against a correctly-signed artifact (the originally-reported LOAD failure was most likely confounded by the separate DIST-01 double-signature bug, fixed in 19-02 -- see "mvp vs. the pre-DIST-01-fix production artifact" below).

### Curated subset (10 files) vs. `--all` (94 files)

| Run | Files | Passed | Failed | Skipped | `_setThrew` console occurrences |
|-----|-------|--------|--------|---------|----------------------------------|
| `mvp`, curated | 10 | 482 | 0 | 0 | 10 |
| `mvp`, `--all` | 94 | 3604 | 49 | 0 | 188 (187 with a captured stack; 3 distinct stack signatures) |
| `eh`, `--all` | 94 | 3604 | 49 | 0 | 0 |

### mvp-only failures

**None.** `mvp --all` and `eh --all` produce byte-identical per-file pass/fail/skip counts and the identical 12-file failing-file list (`diff` of the two per-file result listings is empty). The 49 failures on both bundles are the pre-existing, already-tracked WASM test-suite debt documented in `test/wasm/README.md`'s "Known failures" section (stale API references, cascade failures, `DATE + BIGINT` test bugs) plus the pre-existing calendar-frequency `_ts_fill_forward_native` WASM bug (19-RESEARCH.md Pitfall 1) -- none are new, and none are caused by the `_setThrew` reference.

### `_setThrew` count and triggering statements

188 occurrences across 40 of the 94 `--all` files (`ts_diagnostics.test`, `ts_conformal_*.test`, `ts_cv_*.test`, `ts_forecast_*.test`, and others spanning most of the extension's functional breadth -- not confined to one function family). Running any one of these files **in isolation** (e.g. `--file test/sql/ts_diagnostics.test`, `--file test/sql/ts_conformal.test`) reproduces **zero** occurrences -- the trigger depends on accumulated runtime/call-table state built up over the course of the `--all` run, not on a single, isolatable SQL statement. Only 3 distinct JS stack signatures occur across all 188 occurrences (same `invoke_ii`/`invoke_vii`/`invoke_viiii`/`invoke_viii` chains and the same `wasm-function[N]` indices every time), e.g.:

```
ReferenceError: _setThrew is not defined
    at invoke_ii (.../duckdb-node-mvp.worker.cjs:38:101785)
    at wasm://wasm/09e0d7fa:wasm-function[34349]:0x1a65350
    at invoke_vii (.../duckdb-node-mvp.worker.cjs:38:101870)
    at wasm://wasm/09e0d7fa:wasm-function[34342]:0x1a64feb
    at invoke_vii (.../duckdb-node-mvp.worker.cjs:38:101870)
    at wasm://wasm/09e0d7fa:wasm-function[8694]:0xc59844
    at invoke_vii (.../duckdb-node-mvp.worker.cjs:38:101870)
    at wasm://wasm/09e0d7fa:wasm-function[28269]:0x179ba36
    at invoke_viiii (.../duckdb-node-mvp.worker.cjs:38:102151)
    at wasm://wasm/09e0d7fa:wasm-function[28305]:0x17a2187
```
Both the release build of DuckDB core (the main module) and of this extension (the side module) are compiled with symbols stripped, so no frame in any of the 3 signatures resolves to a named DuckDB-core, this-extension-C++, or this-extension-Rust symbol -- the stack trace alone cannot attribute the call to one side with certainty. The console error is emitted from inside the Worker thread's own exception-reporting path and is asynchronous with respect to the query call that was in flight: the main-thread `await conn.query(...)` promise that was running at the time still resolves with the correct result in every one of the 188 cases (confirmed by the 0-failure totals above), consistent with the error firing on a trampoline invocation whose result was never actually needed by the SQL statement that happened to be executing, or with DuckDB's own exception handling recovering at a layer above the broken JS shim before the result reaches the caller.

### Four-check root-cause investigation (time-boxed per CONTEXT #272 decision 3)

1. **`WebAssembly.Module.imports` of the `mvp` and `eh` extension artifacts**, filtered to `invoke_*`/`setThrew`/`__cxa_*`/`getTempRet0`/`setTempRet0`/`longjmp`/`setjmp`: both artifacts import the same large `invoke_*` family (120+ signatures), `__cxa_allocate_exception`/`__cxa_throw`/`__cxa_begin_catch`/`__cxa_end_catch`/`__cxa_find_matching_catch_2`/`_4`, `getTempRet0`/`setTempRet0`, and `GOT.func.__cxa_pure_virtual`. **Neither artifact imports `setThrew`/`_setThrew` directly** -- the extension itself never references it; the import is resolved transitively through the host's own `invoke_*` wrapper implementations, which are JS, not WASM, functions.
2. **`WebAssembly.Module.exports` of `duckdb-mvp.wasm`/`duckdb-eh.wasm`** (the `@duckdb/duckdb-wasm` npm package's own compiled main modules), filtered to `setThrew`: **both** main modules export a WASM function named `setThrew` (plus `getTempRet0`/`setTempRet0`). Grepping both `duckdb-node-mvp.worker.cjs` and `duckdb-node-eh.worker.cjs` for the `invoke_*` trampoline bodies confirms every one of them calls a bare `_setThrew(1,0)` (346 occurrences of the literal `_setThrew` in each file) -- but **neither file contains an assignment, `var` declaration, or `function` declaration for `_setThrew` anywhere** (`grep -o '_setThrew=[^,;)]*'` and `grep -o 'function _setThrew'` both return nothing in both files). The compiled main module exports a WASM function literally named `setThrew`, but the JS glue that ships in the `@duckdb/duckdb-wasm` npm package never binds that export to the global `_setThrew` identifier its own generated `invoke_*` wrappers call. **This gap exists identically in both bundles** -- it is latent/unreachable in `eh` because `eh`'s own compiled code uses native Wasm exception handling (`-fwasm-exceptions`) for the call sites a normal SQL error path exercises, so the broken JS `invoke_*`/`_setThrew` path is never actually invoked; `mvp` has no native Wasm EH at all, so any call that needs this path hits the undefined global.
3. **JS stack for one occurrence, and which side it is on:** captured above. Inconclusive on attribution (symbols stripped on both sides, as noted), but the 3-signature invariance across 40 functionally unrelated test files, and the fact that isolating any single triggering file to a standalone run makes the error disappear, both point away from anything specific to this extension's own ts_* functions and toward a structural, generic code path inside DuckDB-Wasm's own cross-module call/exception-dispatch machinery.
4. **`rustc +1.86.0 --print cfg --target wasm32-unknown-emscripten`** (panic strategy): `panic="unwind"` (the Rust toolchain default for this target; this repository does not override it in `Cargo.toml`). **This repository's own `CMakeLists.txt` and `extension_config.cmake` set no exception-handling-related C++ flags for WASM** (`grep -i 'exception\|panic\|-fwasm-exceptions'` over both returns nothing) -- the `mvp`/`eh` asymmetry (`WASM_CXX_MVP_FLAGS=` empty vs. `WASM_CXX_EH_FLAGS=-fwasm-exceptions -DWEBDB_FAST_EXCEPTIONS=1`) is set entirely by `duckdb/extension-ci-tools`' own `makefiles/duckdb_extension.Makefile`, not by anything in this repository.

Classification: upstream

The undefined `_setThrew` global is a defect in the `@duckdb/duckdb-wasm` npm package's own compiled JS glue (both `duckdb-node-mvp.worker.cjs` and `duckdb-node-eh.worker.cjs` reference it without ever binding it to the main module's own `setThrew` WASM export), not in anything this repository's `CMakeLists.txt`, `extension_config.cmake`, or Rust/C++ source controls. No change available in this repository removes the gap -- the `mvp`/`eh` compile-flag asymmetry that determines whether the gap is reachable is itself owned entirely by `duckdb/extension-ci-tools`, and even a hypothetical Rust `panic="abort"` override (which would weaken this crate's own `catch_unwind`-based panic safety at the FFI boundary -- a correctness regression, not a fix) would not touch the DuckDB-core-side call paths that trigger it, since DuckDB core's own C++ exception handling is unaffected by this repository's Rust panic strategy.

### Drafted upstream issue (for the owner to file against `duckdb/duckdb-wasm`; not filed by this phase)

**Title:** `mvp bundle: invoke_* trampolines call an unbound global _setThrew when a dynamically-loaded extension crosses the exception boundary`

**Body:**
```
Environment: @duckdb/duckdb-wasm 1.33.1-dev65.0 (bundles DuckDB engine v1.5.6),
Node.js v24+, mvp bundle (duckdb-mvp.wasm + duckdb-node-mvp.worker.cjs).

Reproduction: LOAD any dynamically-loaded (side-module) DuckDB extension that is
itself compiled without native Wasm exception handling (the standard
extension-ci-tools `wasm_mvp` target: WASM_CXX_MVP_FLAGS is empty, no
-fwasm-exceptions). Run any SQL statement sequence long/varied enough to
exercise an indirect call that can throw across the main-module/side-module
boundary (reliably reproduced via sqllogictest files exercising
diagnostics/conformal/CV/forecast error paths; not reproducible by running any
single triggering file in isolation -- requires accumulated call-table state
from a longer run).

Observed: `ReferenceError: _setThrew is not defined` thrown from inside the
package's own generated invoke_* wrapper functions in
dist/duckdb-node-mvp.worker.cjs, e.g.:

  function invoke_vii(index,a1,a2){var sp=stackSave();try{getWasmTableEntry(index)(a1,a2)}
  catch(e){stackRestore(sp);if(e!==e+0)throw e;_setThrew(1,0)}}

_setThrew is referenced ~346 times in this file (and identically in
duckdb-node-eh.worker.cjs) but is never declared (no `var _setThrew`, no
`function _setThrew`) or bound to the compiled module's own `setThrew` WASM
export anywhere in the bundled JS.

Evidence the export exists but is never wired up:
  node -e "console.log(WebAssembly.Module.exports(new WebAssembly.Module(
    require('fs').readFileSync('dist/duckdb-mvp.wasm'))).filter(e=>e.name==='setThrew'))"
  -> [ { name: 'setThrew', kind: 'function' } ]   (the export exists)

  grep -o '_setThrew=[^,;)]*' dist/duckdb-node-mvp.worker.cjs
  grep -o 'function _setThrew' dist/duckdb-node-mvp.worker.cjs
  -> both empty (the export is never assigned to the JS global the invoke_*
     wrappers call)

Impact: in the eh bundle this is latent/unreachable because the eh bundle's
own compiled code uses native Wasm exception handling for the call sites a
normal SQL error path exercises. The mvp bundle has no native Wasm EH, so any
call needing this JS-level path hits the undefined global. In our testing
this was non-fatal and asynchronous with respect to the in-flight query (the
query's own result was unaffected in every observed case), but it is
user-visible console noise and may be fatal in other call orderings we have
not characterized.
```

Plan 19-04 adds this `wasm_mvp` decision-record section (DIST-03) and files the #272 comment linking to this document; neither is in this plan's scope.
