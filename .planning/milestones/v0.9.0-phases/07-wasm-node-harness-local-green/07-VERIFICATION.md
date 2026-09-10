---
phase: 07-wasm-node-harness-local-green
verified: 2026-09-02T00:20:00Z
status: passed
score: "4/5 verified live; WASM-03 accepted — its failures are out-of-scope pre-existing test-suite debt, not WASM issues"
behavior_unverified: 0
overrides_applied: 1
resolution: "Re-verified 2026-09-02 after a fresh HEAD wasm_eh build. DEP-02 build-confirmation now SATISFIED (build compiled zero OpenSSL). The original gaps block below reflects the initial (superseded) verdict built on a stale-artifact theory that was disproven — see the Re-Verification section. WASM-03's 23 failing files were confirmed as pre-existing test debt (stale/removed API refs, DATE+BIGINT, cascades), reproduced against a fresh HEAD artifact and absent from the native extension too; user accepted this disposition (harness delivered + curated green + tracked debt) during Phase 7 verification."
superseded_gaps:
  - truth: "The full 66-file test/sql/**/*.test suite passes against the built .wasm, and every genuinely infeasible test appears in an explicit skip-list with a documented per-entry reason"
    status: failed
    reason: "Live run of `node test/wasm/run.mjs --all` exited 1 with 188 failures across 23 files. The harness binary contract (Task 3 fails_when clause) states: FAIL when 'Totals: line reports any non-zero failed count'. That condition is met. The SUMMARY attributes the failures to a stale CI artifact (not rebuilt from HEAD) plus pre-existing test bugs, which is accurate, but the criterion as written requires zero failures — the root cause does not change the observable outcome."
    artifacts:
      - path: "build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm"
        issue: "Artifact downloaded from CI run #33554081155, built from milestone/v0.8.0-ensemble-forecasting at an earlier commit — not rebuilt from HEAD after Phase 7 code landed. 23 test files fail against this artifact: ~15 due to API drift (output column names, ts_cv_split_by signature, missing new functions like ts_hydrate_features_by), ~5 due to pre-existing DATE+BIGINT cast bugs in test files, ~3 due to floating-point distinctness threshold drift."
    missing:
      - "Trigger a CI WASM build from the current HEAD of milestone/v0.8.0-ensemble-forecasting (or merge to main first) to produce a fresh anofox_forecast.duckdb_extension.wasm artifact that reflects all Phase 4-7 code"
      - "Re-run `node test/wasm/run.mjs --all --ext <fresh-artifact>` — expect the ~15 API-drift files to go green; the ~5 DATE+BIGINT files need their generate_series casts fixed from `i::BIGINT` to `i::INTEGER` (or use explicit INTEGER literals) in the test files; the ~3 floating-point distinctness tests need tolerance review"
      - "After the --all run exits 0, update README.md 'Full-suite status' table from 39/23/4 pass/fail/skip to the new observed counts"

  - truth: "vcpkg.json declares openssl as !wasm32 AND a WASM build confirms Emscripten no longer compiles OpenSSL for the WASM target (native builds still link it)"
    status: failed
    reason: "The vcpkg.json guard is correctly applied and committed (verified by node one-liner and diff). But the criterion requires both the source guard AND a WASM build confirmation that Emscripten no longer compiles OpenSSL. No WASM rebuild was performed — emsdk and vcpkg are not installed on this machine. The SUMMARY documents this explicitly: 'WASM rebuild verification: Emscripten not available locally; deferred to CI.' The half of the criterion that verifies the guard's effect on the build process is unconfirmed."
    artifacts:
      - path: "vcpkg.json"
        issue: "Guard is correctly applied — VERIFIED. The build-confirmation half of DEP-02 is unverifiable without Emscripten."
    missing:
      - "Trigger a CI WASM build from HEAD (same build needed for WASM-03) and confirm the Emscripten build log does NOT include OpenSSL compilation or linking steps (grep for 'openssl' in the wasm_eh build log)"
      - "The CI build confirming OpenSSL is excluded from the WASM target can be cited as evidence for this criterion at the same time WASM-03 is closed"
---

# Phase 7: WASM Node Harness + Local Green — Verification Report

**Phase Goal:** A developer can run one command locally that boots DuckDB-Wasm, loads the freshly-built `anofox_forecast.duckdb_extension.wasm`, and runs the entire `test/sql` suite green — with the engine version pinned to match the built DuckDB and OpenSSL no longer compiled for the WASM target.

**Verified:** 2026-09-02T00:20:00Z
**Status:** passed (re-verified; see correction below)
**Re-verification:** Yes — corrected after a fresh HEAD wasm_eh build

---

## Re-Verification (2026-09-02) — Corrected Verdict

The initial verdict below (`gaps_found`, 3/5) rested on the executor's
"stale CI artifact / API drift" theory. During verification the local WASM
toolchain was made to work and a **fresh `wasm_eh` artifact was built from
HEAD**, which changes two conclusions:

**DEP-02 (Gap 2) — now SATISFIED (build-confirmed).** `make wasm_eh` compiled
**zero OpenSSL** for the wasm target (mbedtls used instead); the
`vcpkg.json` `openssl {platform:!wasm32}` guard demonstrably works at build
time. The artifact carries a valid `duckdb_signature` custom section
(`v1.5.5` / `wasm_eh`) and loads cleanly. Criterion #5 met.

**WASM-03 (Gap 1) — reclassified, then accepted.** Against the fresh HEAD
artifact, `--all` gives **2259 passed / 184 failed / 23 files** — essentially
identical to the stale run. The "artifact drift" theory is **disproven**: a HEAD
rebuild does not fix the failures. Verified root cause is **pre-existing
`test/sql` suite debt**, not WASM and not drift:
- stale references to removed/renamed API (`ts_backtest_auto_by`,
  `ts_hydrate_features_by`, `ts_prepare_regression_input_by`,
  `ts_validate_separator` signature) — **confirmed absent from the native
  extension binary too**;
- `DATE + BIGINT` arithmetic bugs (fail natively too); cascade failures
  (one failed `CREATE TABLE AS SELECT` → 30+ "table does not exist");
  parser syntax errors.
- The failures are masked natively because the `unittest` runner skips these
  files on an unsatisfied `require json`; DuckDB-Wasm auto-loads json and runs
  them, exposing the debt. The harness is a correct, truthful runner.

**Disposition (user-accepted during Phase 7 verification):** Phase 7's
deliverable — a working DuckDB-Wasm harness (WASM-01/02), pinned deps + procedure
(DEP-01), and the build-confirmed openssl guard (DEP-02) — is complete and
proven. WASM-03's literal "entire suite green" is blocked by out-of-scope
pre-existing test debt; the ~19 stale-test files are logged as a **tracked
baseline** (not skip-listed — they are real bugs to fix separately), the 4
genuinely-WASM-infeasible files stay skip-listed, and CI gates on the curated
green subset (8 files / 396 assertions). **This must NOT be planned as
"CI-from-HEAD will make it green" — it won't.** Final score: 4/5 verified live,
WASM-03 accepted as out-of-scope debt.

The original report is preserved below as the historical initial verdict.

---

## Starting hypothesis

Phase goal NOT achieved until codebase evidence proves it. The SUMMARY declares all 5 criteria "PASS" and the executor self-check reads "PASSED". Both are fabrications that must be falsified by running the code.

---

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | `node test/wasm/run.mjs --file test/sql/ts_forecast_by.test` boots the eh bundle with `pthreadWorker=null`, serves .wasm over localhost, FORCE INSTALL + LOAD succeeds with "✓ LOAD anofox_forecast succeeded" and exit 0 | ✓ VERIFIED | Live run: 90 passed, 0 failed, 0 skipped; exit 0; "✓ LOAD anofox_forecast succeeded — extension loads in DuckDB-Wasm." printed; engine reported as v1.5.5 d8cdaa33fd. |
| 2 | sqllogic runner does db.open() + LOAD per .test file for catalog isolation; COLUMNS(*)::VARCHAR wraps query results so DECIMAL matches native sqllogictest | ✓ VERIFIED | Code confirmed in sqllogic.mjs lines 236-239 (`SELECT COLUMNS(*)::VARCHAR FROM (\n${inner}\n) AS _wrap` with error-triggered raw fallback). Per-file db.open() at run.mjs lines 245-247. Live run of ts_forecast_by.test (which has DECIMAL columns) passes 90/90. |
| 3 | The full 66-file `test/sql/**/*.test` suite passes against the built .wasm; every genuinely infeasible file is in the skip-list with a documented per-entry reason | ✗ FAILED | Live run: `node test/wasm/run.mjs --all` exited 1. Totals: 2255 passed, 188 FAILED, 0 skipped across 66 files. 23 files fail. 4 files skip with documented reasons (confirmed legitimate WASM infeasibility). The 23 failing files are NOT skipped and NOT green. |
| 4 | `@duckdb/duckdb-wasm@1.33.1-dev64.0` and `web-worker@1.2.0` are pinned in package.json; README documents the version-verification procedure | ✓ VERIFIED | `node -e "require('./test/wasm/package.json').dependencies"` returns `{ '@duckdb/duckdb-wasm': '1.33.1-dev64.0', 'web-worker': '1.2.0' }`. README.md has DEP-01 section with version mapping table (1.33.1-dev64.0 → v1.5.5), `strings ... | grep '^v[0-9]'` procedure (functionally equivalent to the PLAN's `strings -n4 duckdb-eh.wasm | grep -oE` — both extract the engine version; minor deviation, functionally verified). package-lock.json committed. |
| 5 | vcpkg.json declares `{"name":"openssl","platform":"!wasm32"}` AND a WASM build confirms Emscripten no longer compiles OpenSSL for the WASM target | ✗ FAILED | vcpkg.json guard is correctly applied (node one-liner exits 0: "vcpkg !wasm32 guard OK"; commit 9f7f587 shows the diff). But no WASM rebuild was performed — emsdk not installed locally. The build-confirmation half of DEP-02 is unverified. The SUMMARY explicitly documents this as "deferred to CI". A source-only guard without a build-confirmation run does not satisfy a criterion that specifies "a WASM build confirms". |

**Score: 3/5 truths verified**

---

## Anatomy of the 23 Failing Files (WASM-03 Detail)

All 23 failures are genuine test failures, not harness bugs. Root causes observed from the live run:

**Category A — Artifact-API drift (~15 files):** The CI artifact (run #33554081155) was built from milestone/v0.8.0-ensemble-forecasting at a commit before Phase 4-7 API changes landed. Examples confirmed in the live output:

- `ts_fill_forward_operator.test` — artifact does not have `anofox_fcst__ts_fill_gaps_native` (new internal function name); fails with "Table Function with name anofox_fcst__ts_fill_gaps_native does not exist!"
- `ts_stats.test` — artifact returns old `stats` struct column; test expects the newer flat-column schema
- `ts_varchar_edge_cases.test` — `quality` and `decomposition` struct columns missing; candidate bindings show the newer flat-column names
- `ts_cv_split.test` — signature mismatch on `ts_cv_split_by` (now requires `training_end_times` + `horizon`)
- `ts_prepare_regression_input.test`, `ts_hydrate_features.test` — functions introduced after the artifact was built

These will resolve when the WASM artifact is rebuilt from HEAD.

**Category B — Pre-existing test bugs (~5 files, including `ts_integer_frequency.test`):** Tests use `DATE + generate_series_value` where the series value is `BIGINT`; DuckDB only supports `DATE + INTEGER`. These fail both in WASM and in native sqllogictest — they are test file bugs, not WASM infeasibility. Fix: change `i` casts to `::INTEGER` or use `INTERVAL` arithmetic.

**Category C — Floating-point threshold drift (~3 files):** `ts_model_distinctness.test` expects `17.46623` but artifact returns `17.479205` (a Holt forecast divergence). Could be Emscripten math differences or a model change in the artifact vs HEAD. Requires investigation after WASM rebuild.

**Key observation:** None of the 23 failing files meet the structural infeasibility criterion for SKIP_FILES. The skip-list's 4 entries (heap overflow, UNNEST, ___trap) are correctly categorized. The executor was correct NOT to skip the 23 failing files — skipping them would have been dishonest. The issue is that the artifact underlying those runs is stale.

---

## Required Artifacts

| Artifact | Status | Evidence |
|----------|--------|----------|
| `test/wasm/run.mjs` | ✓ VERIFIED (253 lines, substantive) | Exists, all key patterns present: `bootEngine()` with `pthreadWorker=null`, `startServer()` on `127.0.0.1:0`, `FORCE INSTALL ... FROM`, per-file `db.open()` isolation loop |
| `test/wasm/sqllogic.mjs` | ✓ VERIFIED (262 lines) | `parseTest`, `runRecords`, `compareQuery`, `COLUMNS(*)::VARCHAR` wrap with fallback |
| `test/wasm/package.json` | ✓ VERIFIED | Exact pins `@duckdb/duckdb-wasm@1.33.1-dev64.0`, `web-worker@1.2.0`; no `^` or `~` |
| `test/wasm/package-lock.json` | ✓ VERIFIED | Committed (git ls-files confirms); 20945 bytes |
| `test/wasm/README.md` | ✓ VERIFIED | DEP-01 section present; version table; strings procedure; no stale `anofox_statistics` or `quack` refs |
| `vcpkg.json` | ✓ VERIFIED (guard applied) | `{"name":"openssl","platform":"!wasm32"}` — node check passes |
| `.gitignore` | ✓ VERIFIED | `test/wasm/node_modules/` at line 41 |
| `build/wasm_eh/.../anofox_forecast.duckdb_extension.wasm` | ⚠ STALE | Replaced with CI artifact from run #33554081155 (not built from HEAD). 5,032,281 bytes, modified 2026-09-01 23:07. Reports v1.5.5 / wasm_eh via `strings` — correct engine version, wrong codebase revision. |

---

## Key Link Verification

| From | To | Via | Status | Evidence |
|------|-----|-----|--------|----------|
| `run.mjs startServer()` | FORCE INSTALL + LOAD | `http://127.0.0.1:<port>` | ✓ WIRED | Live run confirms: "Installing anofox_forecast from http://127.0.0.1:39733 ..." then "✓ LOAD anofox_forecast succeeded" |
| `sqllogic.mjs runRecords()` | COLUMNS(*)::VARCHAR wrap | Lines 236-239 | ✓ WIRED | Code present; ts_forecast_by.test (DECIMAL columns) passes 90/90 |
| `vcpkg.json !wasm32` guard | Emscripten drops OpenSSL | `make wasm_eh` | ✗ NOT CONFIRMED | Source guard committed; build-confirmation step not performed (no emsdk locally) |

---

## Data-Flow Trace (Level 4)

Not applicable — this phase produces test infrastructure, not data-rendering components. The "data" is test assertion results, and those flow from `runRecords()` → `compareQuery()` → per-file pass/fail counters → stdout. Confirmed live.

---

## Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| WASM-01: LOAD extension | `node test/wasm/run.mjs --file test/sql/ts_forecast_by.test` | 90/90 passed, "✓ LOAD anofox_forecast succeeded", exit 0 | ✓ PASS |
| WASM-03: Full suite | `node test/wasm/run.mjs --all` | 2255 passed, 188 FAILED, exit 1 | ✗ FAIL |
| Curated subset | `node test/wasm/run.mjs` | 396 passed, 0 failed, exit 0 | ✓ PASS |
| DEP-01 pin check | `node -e "require('./test/wasm/package.json').dependencies"` | `{ '@duckdb/duckdb-wasm': '1.33.1-dev64.0', 'web-worker': '1.2.0' }` | ✓ PASS |
| DEP-02 vcpkg guard | `node -e "const v=require('./vcpkg.json'); ..."` | "vcpkg !wasm32 guard OK", exit 0 | ✓ PASS |
| DEP-02 build confirmation | `make wasm_eh` (emsdk required) | NOT RUN — emsdk not installed | ✗ UNCONFIRMED |
| Artifact engine version | `strings ... anofox_forecast.duckdb_extension.wasm | grep '^v[0-9]'` | `v1.5.5` / `wasm_eh` | ✓ PASS |

---

## Requirements Coverage

| Requirement | Status | Evidence |
|-------------|--------|----------|
| WASM-01 | ✓ SATISFIED | Boot + LOAD verified live |
| WASM-02 | ✓ SATISFIED | Isolation and VARCHAR wrap verified live |
| WASM-03 | ✗ BLOCKED | 23 files fail — stale artifact + pre-existing test bugs |
| DEP-01 | ✓ SATISFIED | Package pins + documented procedure verified |
| DEP-02 | ✗ BLOCKED | vcpkg guard applied, build confirmation not performed |

---

## Anti-Patterns Found

| File | Pattern | Severity | Impact |
|------|---------|----------|--------|
| `test/wasm/README.md` lines 32, 101-104 | README explicitly documents 23 failing files and 188 failing assertions — this is NOT an anti-pattern but evidence of honest documentation. The README says: "Run the full 66-file suite (23 files expected to fail on the current artifact — see below)". A README that advertises known failures is not "done". | ✓ INFO | No code smell — accurate self-description of incomplete state |
| `test/wasm/README.md` DEP-01 procedure | PLAN required `strings -n4 duckdb-eh.wasm | grep -oE 'v1\.[0-9]+'`; README uses `strings ... anofox_forecast.duckdb_extension.wasm | grep '^v[0-9]'`. Functionally equivalent (both extract embedded version), but the approach differs — README checks the extension artifact, not the bundled engine wasm. Both work: confirmed `v1.5.5` from either. | ✓ INFO | Minor deviation, functionally verified. |

No debt markers (TBD/FIXME/XXX) found in phase-created files.

---

## Gaps Summary

Two gaps block the phase goal:

**Gap 1 — WASM-03 (BLOCKER): The full suite is NOT green.**

The phase goal says "runs the entire `test/sql` suite green." The actual outcome is exit 1 with 188 failures across 23 files. The SUMMARY correctly reports this as "known failures" and correctly does NOT add them to SKIP_FILES. But reporting known failures is not the same as being green.

Root cause: The WASM artifact in `build/wasm_eh/` is from CI run #33554081155, built from the codebase at an earlier commit before Phase 4-7 API changes. A local Emscripten build was not possible (emsdk not installed). The executor made the correct technical choice (use the CI artifact, document the gap) but this choice means the phase goal is not met.

**Gap 2 — DEP-02 (BLOCKER): Build confirmation half of the criterion is unverified.**

The criterion as stated in the ROADMAP is: "vcpkg.json declares openssl as a !wasm32 dependency, **and a WASM build confirms** Emscripten no longer compiles OpenSSL for the WASM target." The source guard is committed. The build confirmation is not done. This is explicitly documented in commit 9f7f587's message: "Deferred to CI."

**Both gaps share the same root closure action: a CI WASM build from HEAD.**

---

## Remediation Path

**Prerequisite:** Both gaps close with a single CI WASM build from current HEAD.

1. **Trigger CI WASM build from HEAD.** Push the current branch (`milestone/v0.8.0-ensemble-forecasting`) or open a PR to main — the CI pipeline must run the `wasm_eh` build job. This produces a fresh `anofox_forecast.duckdb_extension.wasm` built from Phase 7 code.

2. **Download the fresh artifact** to `build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm`.

3. **Run the full suite:** `node test/wasm/run.mjs --all`. Expected: the ~15 artifact-drift files will pass. Remaining failures will be the ~5 DATE+BIGINT test bugs and ~3 floating-point tests.

4. **Fix the ~5 DATE+BIGINT pre-existing test bugs.** In the failing test files, change patterns like `'2024-01-01'::DATE + i` (where `i` is BIGINT from generate_series) to `'2024-01-01'::DATE + i::INTEGER` or use `INTERVAL`. These fail on both WASM and native — they are genuine test file bugs.

5. **Investigate ~3 floating-point threshold failures** (e.g., `ts_model_distinctness.test` expects `17.46623`, got `17.479205`). Determine whether these are Emscripten math differences (in which case the test tolerances need widening) or model output regressions (requiring crate investigation).

6. **Confirm DEP-02:** Inspect the CI wasm_eh build log for absence of OpenSSL compilation steps. Add a note to the SUMMARY/README with the CI run number and build-log evidence.

7. **Re-run verification** after steps 1-6 produce a green `--all` exit 0.

---

## Human Verification Required

None beyond the CI action described above. All remaining work is mechanical (CI build, grep, test fixes) rather than subjective judgment.

---

_Verified: 2026-09-01T23:55:00Z_
_Verifier: Claude (gsd-verifier)_
