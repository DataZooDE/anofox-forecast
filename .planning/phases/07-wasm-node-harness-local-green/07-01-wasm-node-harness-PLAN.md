---
phase: 07-wasm-node-harness-local-green
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - vcpkg.json
  - .gitignore
  - test/wasm/run.mjs
  - test/wasm/sqllogic.mjs
  - test/wasm/package.json
  - test/wasm/package-lock.json
  - test/wasm/README.md
autonomous: true
requirements: [WASM-01, WASM-02, WASM-03, DEP-01, DEP-02]
user_setup: []

estimate:
  tokens: 70000
  raw_tokens: 47000
  tasks: 4
  confidence: med

must_haves:
  truths:
    - "Running `node test/wasm/run.mjs --file test/sql/ts_forecast_by.test` boots DuckDB-Wasm on the eh bundle with pthreadWorker=null and web-worker@1.2.0, serves the built .wasm over a version-agnostic localhost path, and FORCE INSTALL + LOAD succeeds with a '✓ LOAD anofox_forecast succeeded' line and exit 0"
    - "The sqllogic runner re-opens the DB (db.open) and re-LOADs the extension per .test file for catalog isolation, and wraps query results in SELECT COLUMNS(*)::VARCHAR so DECIMAL-bearing queries match native sqllogictest output (no unscaled-integer mismatch)"
    - "`node test/wasm/run.mjs --all` runs all 66 test/sql/**/*.test files against the built .wasm and reports zero failures; any genuinely infeasible file is in the SKIP_FILES map with a documented per-entry reason, and the empty (or populated) skip-list is confirmed by the --all run"
    - "test/wasm/package.json pins @duckdb/duckdb-wasm@1.33.1-dev64.0 (engine v1.5.5) and web-worker@1.2.0, and test/wasm/README.md documents the strings/grep procedure to verify/update the npm-version-to-engine-ABI match"
    - "vcpkg.json declares openssl as {\"name\":\"openssl\",\"platform\":\"!wasm32\"}, a make wasm_eh rebuild confirms Emscripten no longer compiles OpenSSL for the WASM target, and the harness runs green against that freshly-rebuilt artifact"
  artifacts:
    - vcpkg.json
    - test/wasm/run.mjs
    - test/wasm/sqllogic.mjs
    - test/wasm/package.json
    - test/wasm/package-lock.json
    - test/wasm/README.md
    - .gitignore
  key_links:
    - "run.mjs startServer → FORCE INSTALL FROM 'http://127.0.0.1:<port>' → LOAD anofox_forecast (the load path CI compile+link cannot verify)"
    - "sqllogic.mjs SELECT COLUMNS(*)::VARCHAR wrap → correct DECIMAL rendering vs native sqllogictest"
    - "vcpkg.json !wasm32 guard → make wasm_eh drops OpenSSL → fresh .wasm the harness loads"
---

<objective>
Port the CI-verified DuckDB-Wasm Node test harness from anofox-statistics PR #131 into anofox-forecast (`test/wasm/run.mjs`, `test/wasm/sqllogic.mjs`, `test/wasm/package.json`, `test/wasm/README.md`), substituting `anofox_statistics` → `anofox_forecast` throughout, and apply the `openssl` `!wasm32` vcpkg guard (DEP-02). The harness boots DuckDB-Wasm, `FORCE INSTALL` + `LOAD`s the freshly-built `.wasm`, and replays the full 66-file `test/sql` suite green locally.

Purpose: Prove the built `.wasm` actually loads and runs in DuckDB-Wasm — the gap a compile+link CI leg cannot close (issue #255) — and pin the engine version to the built DuckDB ABI (v1.5.5).
Output: `test/wasm/` harness (4 files + committed lockfile), `.gitignore` entry for `test/wasm/node_modules/`, and the `vcpkg.json` `!wasm32` guard.
</objective>

<execution_context>
@~/.claude/gsd-core/workflows/execute-plan.md
@~/.claude/gsd-core/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/ROADMAP.md
@.planning/STATE.md
@.planning/phases/07-wasm-node-harness-local-green/07-RESEARCH.md
@.planning/phases/07-wasm-node-harness-local-green/07-CONTEXT.md

# Reference implementation — source of truth for the port (read verbatim):
@/home/simonm/projects/duckdb/anofox-statistics/test/wasm/run.mjs
@/home/simonm/projects/duckdb/anofox-statistics/test/wasm/sqllogic.mjs
@/home/simonm/projects/duckdb/anofox-statistics/test/wasm/package.json
@/home/simonm/projects/duckdb/anofox-statistics/test/wasm/README.md
@/home/simonm/projects/duckdb/anofox-statistics/vcpkg.json
</context>

<artifacts_this_phase_produces>
Every new/modified file this plan creates:
- `test/wasm/run.mjs` — main harness: boot engine, serve .wasm, per-file catalog isolation loop
- `test/wasm/sqllogic.mjs` — minimal sqllogictest-subset parser + runner (ported verbatim)
- `test/wasm/package.json` — pinned deps `@duckdb/duckdb-wasm@1.33.1-dev64.0`, `web-worker@1.2.0`
- `test/wasm/package-lock.json` — committed lockfile from `npm install`
- `test/wasm/README.md` — local run instructions + npm-version-to-engine-ABI verification procedure (DEP-01)
- `.gitignore` — add `test/wasm/node_modules/`
- `vcpkg.json` — `openssl` `!wasm32` platform guard (DEP-02)
</artifacts_this_phase_produces>

<tasks>

<task type="tracer">
  <name>Task 1: End-to-end tracer — boot DuckDB-Wasm, LOAD the built .wasm, run ONE .test file green</name>
  <files>test/wasm/run.mjs, test/wasm/sqllogic.mjs, test/wasm/package.json, .gitignore</files>
  <read_first>
    - /home/simonm/projects/duckdb/anofox-statistics/test/wasm/run.mjs (port target — the entire harness; read verbatim)
    - /home/simonm/projects/duckdb/anofox-statistics/test/wasm/sqllogic.mjs (port target — parser/runner; read verbatim)
    - /home/simonm/projects/duckdb/anofox-statistics/test/wasm/package.json (exact dep pins + comment field)
    - /home/simonm/projects/duckdb/anofox-forecast/.planning/phases/07-wasm-node-harness-local-green/07-RESEARCH.md (Patterns 1-5, Pitfalls 1-6)
    - /home/simonm/projects/duckdb/anofox-forecast/test/sql/ts_forecast_by.test (the tracer test file — uses `require json` + core forecasting, exercises the json auto-load path A2)
    - /home/simonm/projects/duckdb/anofox-forecast/.gitignore (target — append node_modules ignore)
  </read_first>
  <action>
    Wire ONE path through every layer of the harness end-to-end, proving the built `.wasm` LOADs and one real `.test` file passes before expanding to the full suite.

    Create `test/wasm/package.json` copied from the reference with these substitutions: `name` → `anofox-forecast-wasm-tests`, `description` → "DuckDB-Wasm smoke/regression harness for the anofox_forecast extension.". Keep `"type": "module"`, `"scripts": {"test": "node run.mjs"}`, `"engines": {"node": ">=18"}`, the `comment` field explaining the ABI pin, and the exact dependency pins `"@duckdb/duckdb-wasm": "1.33.1-dev64.0"` and `"web-worker": "1.2.0"` (do NOT use `^` or `~` — pins are exact).

    Create `test/wasm/sqllogic.mjs` by copying the reference file verbatim — it contains no extension name references, so no substitution is needed. It exports `parseTest`, `runRecords`, `compareQuery`, `FLOAT_ABS_TOL`, `FLOAT_REL_TOL`. Preserve the `SELECT COLUMNS(*)::VARCHAR FROM (...) AS _wrap` DECIMAL-fix wrap and its raw-query fallback exactly (research Pattern 4 / Pitfall 2).

    Create `test/wasm/run.mjs` by copying the reference file with these exact substitutions: set `EXT_NAME = 'anofox_forecast'` (this derives `EXT_FILE = 'anofox_forecast.duckdb_extension.wasm'`); change the header log string to `━━━ anofox_forecast — DuckDB-Wasm harness ━━━`; replace the `CURATED` array with anofox-forecast files (see Task 3 — for the tracer, a minimal CURATED of `['test/sql/ts_forecast_by.test']` is acceptable and will be expanded in Task 3); set `SKIP_FILES` to an empty `new Map()` (populated only if Task 3's --all run reveals a genuinely infeasible file). Preserve verbatim: `bootEngine()` (eh bundle, `db.instantiate(mainModule, null)`, `db.open({ allowUnsignedExtensions: true })` — pthreadWorker MUST be null per Pitfall 1), `startServer()` (version-agnostic localhost server, Pattern 2), `findExtension()` (prefers `wasm_eh`), and the per-file `db.open()` + reconnect + `LOAD` catalog-isolation loop (Pattern 3 / Pitfall 3). Use `createRequire` to load the CJS duckdb bundle from the ESM context (Pitfall 6).

    Append `test/wasm/node_modules/` to `.gitignore` (the repo has no `node_modules` ignore yet).

    Install deps and run the tracer against the existing pre-built artifact at `build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm`. Do NOT rebuild in this task — the tracer proves the harness plumbing against the artifact on disk; DEP-02 + rebuild happens in Task 2. The tracer file `ts_forecast_by.test` uses `require json`, so a green run also confirms the json auto-load path works in DuckDB-Wasm (research assumption A2).
  </action>
  <verify>
    <automated>npm --prefix test/wasm install && node test/wasm/run.mjs --file test/sql/ts_forecast_by.test --ext build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm; echo "EXIT=$?"</automated>
    <fails_when>Non-zero exit code, OR the output does not contain the line `✓ LOAD anofox_forecast succeeded`, OR the per-file summary reports any failed assertions for `ts_forecast_by.test` (line shows `✗` instead of `✓`), OR a `HARNESS ERROR` line appears (e.g. `TypeError: ... is not a function` from a non-null pthreadWorker, or `module is not defined` from a wrong web-worker version).</fails_when>
  </verify>
  <acceptance_criteria>
    - `npm --prefix test/wasm install` exits 0 and creates `test/wasm/package-lock.json`
    - Harness stdout contains `✓ LOAD anofox_forecast succeeded — extension loads in DuckDB-Wasm.`
    - Harness stdout contains a `DuckDB-Wasm engine:` line reporting `v1.5.5` (DEP-01 ABI match visible at runtime)
    - The `ts_forecast_by.test` summary line begins with `✓` and reports `0 failed`
    - Process exit code is 0
    - `test/wasm/package.json` contains the exact strings `"@duckdb/duckdb-wasm": "1.33.1-dev64.0"` and `"web-worker": "1.2.0"`
    - `run.mjs` contains `const EXT_NAME = 'anofox_forecast';` and `db.instantiate(mainModule, null)`
    - `.gitignore` contains `test/wasm/node_modules/`
  </acceptance_criteria>
  <done>The built .wasm LOADs in DuckDB-Wasm under Node and one real forecasting .test file passes end-to-end, committed.</done>
</task>

<task type="auto">
  <name>Task 2: Apply DEP-02 openssl !wasm32 guard, rebuild the WASM artifact, re-run the tracer against the fresh .wasm</name>
  <files>vcpkg.json</files>
  <read_first>
    - /home/simonm/projects/duckdb/anofox-forecast/vcpkg.json (target — current bare `"openssl"` string)
    - /home/simonm/projects/duckdb/anofox-statistics/vcpkg.json (reference — exact `!wasm32` object shape)
    - /home/simonm/projects/duckdb/anofox-forecast/.planning/phases/07-wasm-node-harness-local-green/07-RESEARCH.md (DEP-02 section + Validation Architecture table)
  </read_first>
  <reversibility rating="reversible">vcpkg.json is a one-line dependency-manifest change; native builds are unaffected and it reverts cleanly by restoring the bare string.</reversibility>
  <action>
    Edit `vcpkg.json` to replace the bare `"openssl"` string dependency with an object carrying a `!wasm32` platform guard, exactly matching the anofox-statistics reference. The final file is:

    a JSON object with a single `dependencies` array whose one element is an object with `name` set to `openssl` and `platform` set to `!wasm32`.

    Do NOT touch `CMakeLists.txt` — the `WASM_BUILD` / `TELEMETRY_SUPPORTED FALSE` guard already exists there; the vcpkg `!wasm32` guard is the dependency-manager complement that stops Emscripten from compiling/linking OpenSSL for the WASM target. Native builds continue to statically link OpenSSL (project rule: OpenSSL must stay statically linked on native — issues #211/#215).

    Rebuild the WASM extension so the harness runs against an artifact that reflects the vcpkg change (research Open Question 2: the existing artifact predates this change and the current codebase state). Use the extension-ci-tools WASM build path (`make wasm_eh`, or the repo's documented WASM build command). Then re-run the tracer against the freshly-rebuilt `.wasm` to confirm the DEP-02 change did not break the load path.

    If `make wasm_eh` is not a defined Makefile target in this repo, discover the WASM build entry point from `extension_config.cmake` / `CMakeLists.txt` / the DuckDB extension-ci-tools makefiles and use the correct command; the artifact must land at `build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm`.
  </action>
  <verify>
    <automated>node -e "const v=require('./vcpkg.json'); const d=v.dependencies[0]; if(typeof d!=='object'||d.name!=='openssl'||d.platform!=='!wasm32'){console.error('vcpkg guard missing/incorrect');process.exit(1)} console.log('vcpkg !wasm32 guard OK')" && node test/wasm/run.mjs --file test/sql/ts_forecast_by.test --ext build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm; echo "EXIT=$?"</automated>
    <fails_when>The vcpkg check prints `vcpkg guard missing/incorrect` and exits non-zero (the `dependencies[0]` element is still a string or lacks `platform: "!wasm32"`), OR the harness re-run against the rebuilt artifact does not print `✓ LOAD anofox_forecast succeeded` / exits non-zero (the DEP-02 change broke the WASM load path).</fails_when>
  </verify>
  <acceptance_criteria>
    - `vcpkg.json` `dependencies[0]` is an object equal to `{"name":"openssl","platform":"!wasm32"}` (verified by the node one-liner exiting 0 with `vcpkg !wasm32 guard OK`)
    - A WASM rebuild completes and produces `build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm`
    - The build log no longer contains OpenSSL compilation/link steps for the WASM target (Emscripten does not build openssl) — capture this observation in the SUMMARY
    - Re-running the tracer against the rebuilt artifact prints `✓ LOAD anofox_forecast succeeded` and exits 0
  </acceptance_criteria>
  <done>vcpkg.json guards openssl behind !wasm32, the WASM artifact is rebuilt without OpenSSL, and the harness loads the fresh .wasm green — committed.</done>
</task>

<task type="auto">
  <name>Task 3: Expand to the full 66-file suite, resolve the skip-list, and set the CURATED subset</name>
  <files>test/wasm/run.mjs</files>
  <read_first>
    - /home/simonm/projects/duckdb/anofox-forecast/test/wasm/run.mjs (target — CURATED and SKIP_FILES set in Task 1)
    - /home/simonm/projects/duckdb/anofox-forecast/.planning/phases/07-wasm-node-harness-local-green/07-RESEARCH.md (WASM-03 Skip-List Analysis + Assumptions A1-A3)
    - /home/simonm/projects/duckdb/anofox-forecast/test/sql/ts_parallel_correctness.test (uses `SET threads=1` / `RESET threads` — assumption A3 candidate)
    - /home/simonm/projects/duckdb/anofox-forecast/test/sql/ts_decomposition.test (uses `require json` — assumption A2 candidate)
  </read_first>
  <action>
    Run the full suite against the rebuilt artifact and drive the skip-list to a documented steady state. This task confirms the research baseline (zero skips expected) against runtime reality.

    Run `node test/wasm/run.mjs --all --ext build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm`. Inspect the totals line and any per-file `✗` failures.

    For EACH failing file, decide skip-vs-fix per the research rule: a file goes into `SKIP_FILES` ONLY if it is genuinely structurally infeasible on WASM (filesystem/network/httpfs/thread-dependent semantics that cannot run in wasm_eh) — with a specific one-line reason per entry. A file that fails due to a real value mismatch or a harness-comparison gap is a bug to fix in run.mjs/sqllogic.mjs (comparison tolerance, VARCHAR wrap, sort mode), NOT a skip. Do not add a file to SKIP_FILES to make a red run green.

    Specifically confirm the three research assumptions at runtime: A2 (`require json` files like `ts_decomposition.test` pass — json auto-loads at LOAD), A3 (`ts_parallel_correctness.test` — `SET threads=1` is a harmless no-op in single-threaded wasm_eh). If A3's file errors on `SET threads`, wrap only the offending block with a documented `mode skip` in that .test file OR add a SKIP_FILES entry with reason `SET threads unsupported in single-threaded wasm_eh` — prefer the narrower fix.

    Set the `CURATED` array in run.mjs to a representative breadth-across-families subset (the default when neither `--all` nor `--file` is given): include at minimum `ts_forecast_by.test`, `ts_features.test`, `ts_metrics.test`, `ts_decomposition.test`, `ts_diagnostics.test`, `ts_conformal.test`, `ts_cv_folds.test`, `ts_stats.test`. Every path must be a real existing file under `test/sql/`.

    Ensure `SKIP_FILES` reflects the final decision: if the --all run is fully green, leave it as an empty `new Map()` (the mechanism is present and logged, per research — an empty skip-list is the expected baseline). If any entry is added, each MUST have a specific documented reason string.
  </action>
  <verify>
    <automated>node test/wasm/run.mjs --all --ext build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm; echo "EXIT=$?"</automated>
    <fails_when>Non-zero exit code, OR the `Totals:` line reports any non-zero failed count, OR the closing line is not `✓ All assertions passed on DuckDB-Wasm.`, OR a `✗ Failing files:` line is present. Any file skipped must appear as a `⊘ ... — skipped (<reason>)` line with a non-empty reason (a skip without a reason is a failure of this task's intent even if exit is 0).</fails_when>
  </verify>
  <acceptance_criteria>
    - `node test/wasm/run.mjs --all` exits 0 and prints `✓ All assertions passed on DuckDB-Wasm.`
    - The `Totals:` line reports `0 failed` across all 66 files (minus any documented skips)
    - Every `⊘ ... skipped` line (if any) carries a specific one-line reason; if the run is fully green, `SKIP_FILES` is an empty `new Map()`
    - `ts_decomposition.test` (require json, A2) and `ts_parallel_correctness.test` (SET threads, A3) either pass or are handled with a documented narrow fix/skip
    - `CURATED` in run.mjs lists only real existing `test/sql/*.test` paths spanning forecasting, features, metrics, decomposition, diagnostics, conformal, CV, and stats families
  </acceptance_criteria>
  <done>The full 66-file suite runs green against the built .wasm, the skip-list is at a documented steady state, and CURATED is a real representative subset — committed.</done>
</task>

<task type="auto">
  <name>Task 4: Port the README with the DEP-01 version-verification procedure and commit the lockfile</name>
  <files>test/wasm/README.md, test/wasm/package-lock.json</files>
  <read_first>
    - /home/simonm/projects/duckdb/anofox-statistics/test/wasm/README.md (port target — read verbatim; substitute extension name)
    - /home/simonm/projects/duckdb/anofox-forecast/.planning/phases/07-wasm-node-harness-local-green/07-RESEARCH.md (DEP-01 section: engine-version mapping table + verification procedure)
    - /home/simonm/projects/duckdb/anofox-forecast/test/wasm/package.json (produced in Task 1 — must match the README pin)
  </read_first>
  <action>
    Create `test/wasm/README.md` by porting the reference README with `anofox_statistics` → `anofox_forecast` substituted throughout (extension name in prose, the `ANOFOX_WASM_EXT` example path, and the artifact filename `anofox_forecast.duckdb_extension.wasm`).

    Update the Coverage section to reflect anofox-forecast reality: this repo's suite is 66 files (not the reference's 99). State the actual pass count observed from Task 3's `--all` run and the final skip-list contents (empty, or the documented entries). Do NOT copy the reference's "2090 assertions / 99 files / quack.test skipped" numbers verbatim — quack.test does not exist in this repo.

    Preserve the DEP-01 "Version matching (important)" section verbatim in substance: the npm-version-≠-engine-version warning, the mapping table (`1.29.0`→v1.1.1, `1.32.0`→v1.4.3, `1.33.1-dev64.0`→v1.5.5), the statement that this harness pins `@duckdb/duckdb-wasm@1.33.1-dev64.0` (engine v1.5.5) matching the built DuckDB target, the note that there is no duckdb-wasm bundling v1.4.5 LTS, and the exact `strings -n4 duckdb-eh.wasm | grep -oE 'v1\.[0-9]+\.[0-9]+'` verification procedure plus the runtime `await db.getVersion()` alternative. This section IS the DEP-01 documented procedure.

    Ensure `test/wasm/package-lock.json` (generated by the Task 1 `npm install`) is present and committed alongside the sources so CI installs are reproducible.
  </action>
  <verify>
    <automated>test -f test/wasm/README.md && test -f test/wasm/package-lock.json && grep -q 'anofox_forecast.duckdb_extension.wasm' test/wasm/README.md && grep -q '1.33.1-dev64.0' test/wasm/README.md && grep -q 'v1.5.5' test/wasm/README.md && grep -qE "strings -n4.*duckdb-eh\.wasm" test/wasm/README.md && ! grep -q 'anofox_statistics' test/wasm/README.md && ! grep -q 'quack' test/wasm/README.md && echo "README OK"</automated>
    <fails_when>The command exits non-zero because any required file is missing, OR the README lacks the `anofox_forecast.duckdb_extension.wasm` filename / the `1.33.1-dev64.0` pin / the `v1.5.5` engine version / the `strings -n4 ... duckdb-eh.wasm` verification procedure, OR the README still contains a stale `anofox_statistics` reference or a `quack` reference copied from the source, OR `test/wasm/package-lock.json` is absent.</fails_when>
  </verify>
  <acceptance_criteria>
    - `test/wasm/README.md` exists and contains no literal `anofox_statistics` or `quack` strings (all substituted/removed)
    - README documents the run command (`npm --prefix test/wasm install` + `--ext`/`ANOFOX_WASM_EXT`) and the `--all` / `--file` options
    - README's version-matching section contains `1.33.1-dev64.0`, `v1.5.5`, the npm-version-≠-engine-version warning, and the `strings -n4 ... duckdb-eh.wasm | grep -oE 'v1\.[0-9]+\.[0-9]+'` procedure (DEP-01)
    - README Coverage section states the real 66-file count and the observed pass/skip result from Task 3 (not the reference's 99-file/2090-assertion numbers)
    - `test/wasm/package-lock.json` exists and is committed
  </acceptance_criteria>
  <done>The README documents local usage and the DEP-01 version-verification procedure with anofox-forecast-correct numbers, and the lockfile is committed.</done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| npm registry → dev/CI machine | `npm install` pulls `@duckdb/duckdb-wasm` + `web-worker` + transitive deps; supply-chain surface |
| localhost HTTP server → DuckDB-Wasm engine | run.mjs serves the built `.wasm` over `127.0.0.1`; only in-process, loopback-bound |
| built `.wasm` → DuckDB-Wasm runtime | `FORCE INSTALL` + `LOAD` of an unsigned locally-built extension (`allowUnsignedExtensions: true`) |

## STRIDE Threat Register

| Threat ID | Category | Component | Severity | Disposition | Mitigation Plan |
|-----------|----------|-----------|----------|-------------|-----------------|
| T-07-01 | Tampering | npm installs (`@duckdb/duckdb-wasm@1.33.1-dev64.0`, `web-worker@1.2.0`) | medium | mitigate | Exact-pin both deps (no `^`/`~`); commit `package-lock.json` for reproducible, integrity-checked installs. Both are the CI-verified pins from anofox-statistics PR #131 — see package-legitimacy note below. |
| T-07-02 | Spoofing | localhost extension server | low | accept | Server binds `127.0.0.1` on an ephemeral port (`listen(0)`), serves one file, and is torn down after the run. No remote exposure; loopback-only, single-process lifetime. |
| T-07-03 | Elevation of Privilege | `allowUnsignedExtensions: true` + `FORCE INSTALL` | low | accept | Required to load a locally-built unsigned extension; scoped to the ephemeral in-memory DuckDB-Wasm instance in a test harness. No production/persistent DB is touched. |
| T-07-SC | Tampering | npm/pip/cargo installs | medium | mitigate | Package-legitimacy: both npm deps are the exact pins already vetted and CI-green in anofox-statistics PR #131 (`@duckdb/duckdb-wasm` is the official DuckDB org package; `web-worker` is a well-known Node Worker polyfill). No `[ASSUMED]`/`[SUS]`/`[SLOP]` packages introduced. |
</threat_model>

<verification>
## Overall phase checks (goal-backward, lifts the 5 phase success criteria)

1. WASM-01 (boot + LOAD): `node test/wasm/run.mjs --file test/sql/ts_forecast_by.test --ext <artifact>` prints `✓ LOAD anofox_forecast succeeded` and exits 0.
2. WASM-02 (runner + DECIMAL + isolation): the same run passes a real .test file; sqllogic.mjs wraps queries in `::VARCHAR` and run.mjs re-opens the DB per file.
3. WASM-03 (full suite): `node test/wasm/run.mjs --all --ext <artifact>` prints `✓ All assertions passed on DuckDB-Wasm.` with 0 failed across 66 files; skip-list documented (empty baseline).
4. DEP-01 (ABI pin + procedure): package.json pins `@duckdb/duckdb-wasm@1.33.1-dev64.0` + `web-worker@1.2.0`; README documents the `strings`/`grep` verification procedure; harness prints engine `v1.5.5` at startup.
5. DEP-02 (openssl guard): vcpkg.json guards `openssl` behind `!wasm32`; a `make wasm_eh` rebuild omits OpenSSL and the harness loads the fresh artifact green.

**Final gate (run against the rebuilt artifact):**
`node test/wasm/run.mjs --all --ext build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm` → 0 failures, exit 0.
</verification>

<success_criteria>
- The built `.wasm` LOADs in DuckDB-Wasm under Node (WASM-01) — proven by the `✓ LOAD` line + exit 0.
- The sqllogic runner passes .test files with per-file catalog isolation and `::VARCHAR` DECIMAL formatting (WASM-02).
- All 66 `test/sql/**/*.test` files pass; skip-list explicit and reasoned (WASM-03).
- Engine pin (`1.33.1-dev64.0` / v1.5.5) matches the built DuckDB ABI; verification procedure documented (DEP-01).
- `vcpkg.json` guards `openssl` behind `!wasm32` and a WASM rebuild confirms OpenSSL is not compiled for WASM (DEP-02).
</success_criteria>

<output>
Create `.planning/phases/07-wasm-node-harness-local-green/07-01-SUMMARY.md` when done. Record: the observed `--all` totals (passed/failed/skipped across 66 files), the final `SKIP_FILES` contents (empty or documented entries + reasons), the runtime confirmation of assumptions A2/A3, the WASM rebuild command used, and confirmation the build log omitted OpenSSL compilation for the WASM target.
</output>