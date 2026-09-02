# Phase 7: WASM Node Harness + Local Green — Research

**Researched:** 2026-09-01
**Domain:** DuckDB-Wasm Node test harness, vcpkg platform guards, npm version-to-engine-ABI mapping
**Confidence:** HIGH

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
*None explicitly locked — all implementation choices are at Claude's discretion.*

### Claude's Discretion
All implementation choices are at Claude's discretion — pure infrastructure phase. Use the ROADMAP phase goal, the five success criteria, the known gotchas from issue #255, and the anofox-statistics PR #131 reference implementation to guide decisions.

### Deferred Ideas (OUT OF SCOPE)
- Browser-based (not just Node) WASM E2E harness (WASM-F1)
- Shared-memory `wasm_threads` build — blocked upstream (WASM-F2)
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| WASM-01 | Node harness boots DuckDB-Wasm (`eh` bundle, `pthreadWorker=null`, `web-worker` pinned), serves locally-built `.wasm` over localhost, and `FORCE INSTALL` + `LOAD`s it successfully | Harness architecture and boot sequence extracted verbatim from anofox-statistics PR #131 `run.mjs` |
| WASM-02 | Sqllogictest-subset runner executes `test/sql` `.test` files against the loaded WASM extension; re-opens DB per file for catalog isolation; formats results through `::VARCHAR` so DECIMAL scale matches native sqllogictest output | `sqllogic.mjs` implementation extracted verbatim from PR #131; DECIMAL quirk documented |
| WASM-03 | Full 66-file `test/sql/**/*.test` suite passes; any genuinely infeasible test is explicitly skip-listed with a documented reason | Skip-list analysis completed; candidate categories identified |
| DEP-01 | `@duckdb/duckdb-wasm` and `web-worker@1.2.0` pinned to versions whose engine ABI matches the built DuckDB version; version-verification procedure documented | `1.33.1-dev64.0` confirmed for DuckDB v1.5.5; verification procedure extracted from PR #131 README |
| DEP-02 | `openssl` declared as `!wasm32` dependency in `vcpkg.json`; Emscripten no longer compiles OpenSSL for WASM target | Exact `vcpkg.json` object syntax verified from anofox-statistics reference |
</phase_requirements>

---

## Summary

This phase ports the reference implementation from anofox-statistics PR #131 (now merged into the `gsd/v0.2.0-wasm-support` branch of `/home/simonm/projects/duckdb/anofox-statistics`) into anofox-forecast. The reference is a complete, CI-verified implementation: two JS files (`run.mjs`, `sqllogic.mjs`), a `package.json`, and a `README.md`, all under `test/wasm/`. The PR ran the full 99-file anofox-statistics suite with 2090/2090 assertions passing in DuckDB-Wasm. The anofox-forecast equivalent is a 66-file suite.

The technical territory is well-understood. There are no open architectural unknowns: the boot sequence (eh bundle + `pthreadWorker=null` + `web-worker@1.2.0`), the version-agnostic localhost server, the `FORCE INSTALL` + per-file-catalog-isolation pattern, the `::VARCHAR` DECIMAL fix, and the `!wasm32` vcpkg guard are all present in the reference and verified to work. The only plan-phase determination is whether any of the 66 test files need to be added to the skip-list — but the reference ran `--all` successfully, so the baseline expectation is zero skips.

The built DuckDB submodule version is v1.5.5 [VERIFIED: `git describe --tags` on duckdb submodule]. The correct `@duckdb/duckdb-wasm` npm pin is `1.33.1-dev64.0` (engine v1.5.5). There is no stable duckdb-wasm release shipping a v1.5.5 engine; the `@next` dev build is the correct choice. The v1.4.5 LTS DuckDB target has no matching duckdb-wasm package, so the harness intentionally targets v1.5.5 only — matching what anofox-statistics did.

**Primary recommendation:** Port `test/wasm/run.mjs`, `test/wasm/sqllogic.mjs`, `test/wasm/package.json`, and `test/wasm/README.md` verbatim from anofox-statistics, substituting `anofox_statistics` → `anofox_forecast` throughout, then add `vcpkg.json` `!wasm32` guard (DEP-02).

---

## Project Constraints (from CLAUDE.md)

Directives from `CLAUDE.md` that apply to this phase:

- **Tech stack**: No new languages. Work lives in `test/wasm/` (Node/JS), `vcpkg.json`, and docs.
- **No custom threading**: WASM harness uses DuckDB's own engine thread model; no custom threading in the harness.
- **`cargo fmt --check` in DoD**: Any Rust changes (none expected this phase) must be fmt-clean before commit.
- **Verify executor reports vs git+disk**: Confirmed — commit hashes and SUMMARY must exist on disk after each commit.
- **GSD workflow enforcement**: All file changes go through a GSD command before editing.
- **`LINKED_LIBS` pattern is established**: `extension_config.cmake` already has `LINKED_LIBS "$<TARGET_FILE:anofox_fcst_ffi-static>"` for WASM. Do not remove it.
- **OpenSSL must be statically linked on native**: The `!wasm32` guard removes OpenSSL only from WASM builds; native builds must still link it statically. This is already enforced in `CMakeLists.txt` via `WASM_BUILD` flag.

---

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| DuckDB-Wasm engine boot | Node harness (`run.mjs`) | — | Entry point; instantiates AsyncDuckDB with eh bundle |
| Extension load verification | Node harness (`run.mjs`) | — | `FORCE INSTALL` + `LOAD`; the gap CI compile/link cannot close |
| `.test` file parsing | sqllogic runner (`sqllogic.mjs`) | — | Parses `statement ok/error`, `query`, `mode skip`, `require` directives |
| Result comparison | sqllogic runner (`sqllogic.mjs`) | — | Float tolerance, bool normalization, `::VARCHAR` DECIMAL fix |
| Catalog isolation | Node harness (`run.mjs`) | — | Calls `db.open()` + reconnect + re-`LOAD` per test file |
| Localhost server | Node harness (`run.mjs`) | — | Version-agnostic `http` server serving the one built `.wasm` |
| npm dependency pinning | `test/wasm/package.json` | — | Pins `@duckdb/duckdb-wasm` + `web-worker@1.2.0` |
| OpenSSL guard | `vcpkg.json` | `CMakeLists.txt` (WASM_BUILD) | `!wasm32` platform expression removes OpenSSL dep for Emscripten |

---

## Standard Stack

### Core

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| `@duckdb/duckdb-wasm` | `1.33.1-dev64.0` | DuckDB-Wasm engine (Node bundle) | The `@next` dev pin whose embedded engine is v1.5.5 — matches the extension's built DuckDB target |
| `web-worker` | `1.2.0` | Node Worker polyfill required by duckdb-wasm CJS bundle | `1.5.x` throws `module is not defined` loading the CJS worker; `1.2.0` is the pinned version from the reference |

[VERIFIED: /home/simonm/projects/duckdb/anofox-statistics/test/wasm/package.json:1-18]

```json
{
  "name": "anofox-statistics-wasm-tests",
  "version": "0.0.0",
  "private": true,
  "type": "module",
  "description": "DuckDB-Wasm smoke/regression harness for the anofox_statistics extension.",
  "scripts": {
    "test": "node run.mjs"
  },
  "engines": {
    "node": ">=18"
  },
  "comment": "@duckdb/duckdb-wasm is pinned to the build whose bundled DuckDB engine matches the extension's target (v1.5.5). npm version != engine version: 1.33.1-dev64.0 -> engine v1.5.5 (verified by the storage-version list embedded in dist/duckdb-eh.wasm). This is the @next dev build; no STABLE duckdb-wasm ships 1.5.x yet. Bump in lockstep when the extension moves to a new DuckDB version.",
  "dependencies": {
    "@duckdb/duckdb-wasm": "1.33.1-dev64.0",
    "web-worker": "1.2.0"
  }
}
```

**Installation command:**

```bash
npm --prefix test/wasm install
```

### Supporting (built-in Node modules, no install required)

| Module | Purpose |
|--------|---------|
| `node:http` | Localhost server to serve the built `.wasm` |
| `node:fs` | Read `.wasm` artifact and `.test` files |
| `node:path` | Path construction |
| `node:url` | `fileURLToPath` for ESM `__dirname` equivalent |
| `node:module` | `createRequire` to `require()` the CJS duckdb bundle from ESM context |

---

## DEP-01: Engine Version Pinning

**The problem:** `@duckdb/duckdb-wasm` npm package versions do NOT map 1:1 to DuckDB engine versions. The extension is ABI-locked to the DuckDB engine it was compiled against. Using the wrong npm version causes `LOAD` to fail.

**Known mapping (verified against anofox-statistics reference):**

| npm version | DuckDB engine | Status |
|-------------|---------------|--------|
| `1.29.0` | v1.1.1 | Old; do not use |
| `1.32.0` | v1.4.3 | Old LTS |
| `1.33.1-dev64.0` | v1.5.5 | **Current — use this** |

[VERIFIED: /home/simonm/projects/duckdb/anofox-statistics/test/wasm/README.md:89-113]

The anofox-forecast DuckDB submodule is pinned to **v1.5.5** [VERIFIED: `git describe --tags` on duckdb submodule at `/home/simonm/projects/duckdb/anofox-forecast/duckdb`]. Therefore pin `@duckdb/duckdb-wasm@1.33.1-dev64.0`.

**There is no duckdb-wasm pin for DuckDB v1.4.5 LTS** — the gap between `1.32.0` (v1.4.3) and `1.33.1-dev*` (v1.5.5) skips v1.4.5. The harness intentionally targets v1.5.5 artifact only, consistent with the anofox-statistics approach.

**Version-verification procedure** (must be documented in `test/wasm/README.md`):

```bash
# Method 1: inspect the wasm binary (works offline, no Node runtime needed)
url=$(npm view @duckdb/duckdb-wasm@<candidate-version> dist.tarball)
curl -sL "$url" | tar xz -C /tmp/dw
strings -n4 /tmp/dw/package/dist/duckdb-eh.wasm | grep -oE 'v1\.[0-9]+\.[0-9]+' | sort -uV | tail -1

# Method 2: runtime check — the harness prints engine version at startup
node test/wasm/run.mjs --ext <path>
# Output: "DuckDB-Wasm engine: v1.5.5 ..."
```

**When to update the pin:** Whenever the `duckdb` submodule is bumped to a new DuckDB version, run Method 1 against candidate npm versions until the engine version matches. Update `test/wasm/package.json` dependencies and the comment field.

---

## DEP-02: vcpkg.json OpenSSL `!wasm32` Guard

**Current state:** `/home/simonm/projects/duckdb/anofox-forecast/vcpkg.json` declares `openssl` as a bare string dependency with no platform guard [VERIFIED: /home/simonm/projects/duckdb/anofox-forecast/vcpkg.json:1-5]:

```json
{
  "dependencies": [
    "openssl"
  ]
}
```

**Target state:** Convert the string to an object with a `"platform": "!wasm32"` guard, matching the anofox-statistics reference [VERIFIED: /home/simonm/projects/duckdb/anofox-statistics/vcpkg.json:1-15]:

```json
{
  "dependencies": [
    {
      "name": "openssl",
      "platform": "!wasm32"
    }
  ]
}
```

**Why this is safe:** The `CMakeLists.txt` already has a `WASM_BUILD` guard that sets `TELEMETRY_SUPPORTED FALSE` on WASM, so the telemetry code path is already not compiled for WASM [VERIFIED: /home/simonm/projects/duckdb/anofox-forecast/CMakeLists.txt:92-159]. The `!wasm32` vcpkg guard is the dependency-manager complement — it prevents Emscripten from trying to compile and link OpenSSL at all for the WASM target. Native builds are unaffected; they continue to statically link OpenSSL as before.

**Verification:** After the change, run `make wasm_eh` and confirm the build log no longer contains OpenSSL compilation steps. The native build (`make release`) must still succeed and link OpenSSL.

---

## Architecture Patterns

### System Architecture Diagram

```
test/sql/**/*.test files (66 files)
         │
         │ fs.readFileSync
         ▼
   sqllogic.mjs (parseTest)
   ─── statement ok/error ──────────────────────────┐
   ─── query <types> + expected ────────────┐        │
                                            │        │
                               run.mjs (per-file loop)
                                  │
                  db.open() + reconnect + LOAD       │
                                  │                  │
                            runRecords()             │
                              │        │             │
                   runQuery()          │             │
                (SELECT COLUMNS(*)::VARCHAR FROM     │
                 (...) AS _wrap)       │    runQuery()
                              │        │    (raw sql for statement)
                              ▼        ▼
                    DuckDB-Wasm engine (AsyncDuckDB)
                    ─ eh bundle, Node worker
                    ─ db.open({ allowUnsignedExtensions: true })
                              │
                     FORCE INSTALL anofox_forecast
                     FROM 'http://127.0.0.1:<port>';
                              │
                   localhost HTTP server (run.mjs)
                   ─ serves built .wasm for any
                     <version>/<platform>/ path
                              │
            build/wasm_eh/extension/anofox_forecast/
            anofox_forecast.duckdb_extension.wasm
```

### Recommended Project Structure

```
test/wasm/
├── run.mjs         # Main harness: boot engine, serve .wasm, run test files
├── sqllogic.mjs    # Minimal sqllogictest-subset parser + runner
├── package.json    # Pinned deps: @duckdb/duckdb-wasm@1.33.1-dev64.0, web-worker@1.2.0
├── package-lock.json  # Committed lockfile
└── README.md       # How to run locally + version-matching procedure
```

The `node_modules/` directory goes into `.gitignore`.

### Pattern 1: DuckDB-Wasm Engine Boot

**What:** Boot the `eh` (exception-handling) bundle in Node using the CJS require path.

**Critical constraints:**
- MUST use `@duckdb/duckdb-wasm/dist/duckdb-node.cjs` (CJS bundle, not the ESM one)
- MUST use `createRequire` from ESM context (the package.json has `"type": "module"`)
- MUST pass `null` as the pthread worker — `duckdb-eh.wasm` does not use pthreads; passing a real worker causes `TypeError: ... is not a function` on the first extension call
- MUST call `db.open({ allowUnsignedExtensions: true })` — locally-built extensions are unsigned

[VERIFIED: /home/simonm/projects/duckdb/anofox-statistics/test/wasm/run.mjs:131-145]

```javascript
async function bootEngine() {
  const duckdb = require('@duckdb/duckdb-wasm/dist/duckdb-node.cjs');
  const Worker = require('web-worker');
  const DIST = path.dirname(require.resolve('@duckdb/duckdb-wasm/dist/duckdb-node.cjs'));

  const mainModule = path.join(DIST, 'duckdb-eh.wasm');
  const mainWorker = path.join(DIST, 'duckdb-node-eh.worker.cjs');

  const worker = new Worker(mainWorker);
  const logger = { log() {} }; // silence per-query engine logs
  const db = new duckdb.AsyncDuckDB(logger, worker);
  await db.instantiate(mainModule, null); // pthreadWorker MUST be null for eh
  await db.open({ allowUnsignedExtensions: true });
  return { db, worker };
}
```

### Pattern 2: Version-Agnostic Localhost Server

**What:** Serve the locally-built `.wasm` over HTTP without needing to know the exact `<version>/<platform>/` path DuckDB-Wasm requests.

**Why:** DuckDB-Wasm constructs an install URL of the form `<repository>/<duckdb_version>/<platform>/<extension>.duckdb_extension.wasm`. The locally-built artifact is at a fixed path, not matching this pattern. The server intercepts any request whose URL ends in the extension filename and serves the one file.

[VERIFIED: /home/simonm/projects/duckdb/anofox-statistics/test/wasm/run.mjs:106-129]

```javascript
function startServer(extPath) {
  const EXT_FILE = 'anofox_forecast.duckdb_extension.wasm';
  const wasm = fs.readFileSync(extPath);
  const server = http.createServer((req, res) => {
    const url = decodeURIComponent((req.url || '').split('?')[0]);
    if (url.endsWith(`/${EXT_FILE}`) || url.endsWith(EXT_FILE)) {
      res.setHeader('Content-Type', 'application/wasm');
      res.setHeader('Access-Control-Allow-Origin', '*');
      res.end(wasm);
    } else {
      res.statusCode = 404;
      res.end('not found');
    }
  });
  return new Promise((resolve) => {
    server.listen(0, '127.0.0.1', () => {
      const { port } = server.address();
      resolve({ server, port });
    });
  });
}
```

### Pattern 3: FORCE INSTALL + Per-File Catalog Isolation

**What:** Install the extension once (using `FORCE INSTALL` to bust Node's FS cache), then for each `.test` file: call `db.open()` again (resets catalog) + `db.connect()` + `LOAD <ext>`.

**Why `FORCE INSTALL`:** DuckDB-Wasm caches installed extensions in the Node virtual filesystem. `FORCE` busts this cache so the freshly-built artifact is always used.

**Why per-file `db.open()`:** Native sqllogictest resets database state between files. `db.open()` on an already-open database resets the catalog — equivalent to a fresh DB connection. Without this, `CREATE TABLE` from file N leaks into file N+1.

[VERIFIED: /home/simonm/projects/duckdb/anofox-statistics/test/wasm/run.mjs:186-232]

```javascript
// One-time install from the localhost server
await conn.query(`FORCE INSTALL ${EXT_NAME} FROM '${base}';`);
await conn.query(`LOAD ${EXT_NAME};`);
await conn.close(); // close before the per-file loop

// Per-file isolation loop
for (const rel of files) {
  await engine.db.open({ allowUnsignedExtensions: true }); // reset catalog
  const c = await engine.db.connect();
  await c.query(`LOAD ${EXT_NAME};`); // re-load (already installed)
  const r = await runRecords(records, makeRunQuery(c), { file: rel, log });
  await c.close();
}
```

### Pattern 4: DECIMAL Fix via `::VARCHAR`

**What:** Wrap all `query`-type SQL in `SELECT COLUMNS(*)::VARCHAR FROM (...) AS _wrap` before execution.

**Why:** DuckDB-Wasm returns query results as Apache Arrow data. Arrow-JS mis-renders DuckDB `DECIMAL` columns by returning the unscaled integer value (`DECIMAL(10,1)` value `1.0` → Arrow bigint `10`). DuckDB's own `::VARCHAR` cast applies the scale before Arrow sees it, producing `"1.0"` — matching what native sqllogictest prints.

[VERIFIED: /home/simonm/projects/duckdb/anofox-statistics/test/wasm/sqllogic.mjs:228-239]

```javascript
// In sqllogic.mjs runRecords(), for 'query' type records:
const inner = rec.sql.trim().replace(/;\s*$/, '');
let rows;
try {
  rows = await runQuery(`SELECT COLUMNS(*)::VARCHAR FROM (\n${inner}\n) AS _wrap`);
} catch {
  rows = await runQuery(rec.sql); // fallback for non-projectable statements
}
```

### Pattern 5: sqllogic.mjs — Parser Subset

**What:** A minimal `.test` file parser that handles the directives the anofox suite actually uses.

**Supported directives** [VERIFIED: /home/simonm/projects/duckdb/anofox-statistics/test/wasm/sqllogic.mjs:1-19]:
- `require <ext>` → directive record (ignored; harness loads the extension itself)
- `require-env <var>` → directive record (ignored)
- `load`, `restart` → directive record (ignored)
- `statement ok` → run SQL, expect success
- `statement error [msg]` → run SQL, expect failure (optional substring match)
- `query <types> [sort]` → run SQL, compare rows after `----`
- `mode skip` / `mode unskip` → skip a block
- `halt` → stop parsing
- `#` comments and blank-line separators

**Not supported** (not in the suite): `loop`, `foreach`, `concurrent`, `connection`.

**Sort modes:** `nosort` (default), `rowsort`, `sort`, `valuesort`.

**Multi-column row format:** Expected values in `.test` files use TAB-separation within a single line for multi-column rows. `sqllogic.mjs` splits on `\t` [VERIFIED: /home/simonm/projects/duckdb/anofox-statistics/test/wasm/sqllogic.mjs:159].

**Float tolerance:** `FLOAT_ABS_TOL = 1e-6`, `FLOAT_REL_TOL = 1e-4`. Both sides numeric → float compare; otherwise trimmed string compare.

**Boolean normalization:** Arrow returns JS booleans (`true`/`false`); sqllogictest type `I` expects `1`/`0`. `normBool()` converts both sides [VERIFIED: /home/simonm/projects/duckdb/anofox-statistics/test/wasm/sqllogic.mjs:117-126].

---

## WASM-03: Skip-List Analysis

**Baseline expectation:** Zero skips. The anofox-statistics reference ran `--all` (99 files, 2090 assertions) successfully. The anofox-forecast suite is structurally similar — pure SQL against in-process Rust functions, no filesystem or network I/O.

### Categories and assessment

| Category | Candidate files | Risk | Assessment |
|----------|----------------|------|------------|
| Threading (`SET threads=1`) | `ts_parallel_correctness.test` | Low | DuckDB-Wasm supports `SET threads`; wasm_eh is single-threaded but `SET threads=1` is a no-op / harmless. Run and see. |
| `require json` | 50 files | Low | Extension auto-loads json at `LOAD` time via `ExtensionHelper::TryAutoLoadExtension(db, "json")` [VERIFIED: src line 27-29]. DuckDB-Wasm bundles json. The `require json` directive is a sqllogictest hint only; the harness ignores `require` directives. |
| `require anofox_forecast` | All 66 files | None | Standard directive; harness ignores it and loads the extension itself. |
| Telemetry / PostHog | `feedback.test` | Low | `feedback.test` tests opt-out knob (`SET datazoo_banner`) and error annotation — no raw HTTP; `datazoo_banner` is a pure SQL SET. No WASM incompatibility. |
| Extension comparison | `extension_comparison.test` | Low | Pure SQL joins and aggregates against in-memory data. No external extension references beyond `require json` (handled above). |
| COPY/EXPORT/filesystem | None found | None | `grep -l "httpfs\|read_csv\|COPY.*TO\|EXPORT\|IMPORT"` returned no matches. |

### Confirmed skip-list entries

Based on analysis, only one entry is expected to be needed:

| File | Reason |
|------|--------|
| *(none at port time)* | All 66 files are expected to pass without skips |

The harness MUST maintain an explicit `SKIP_FILES` map (as in the reference), even if empty, so that skipped files are logged rather than silently excluded. If a file fails at runtime rather than being structurally infeasible, it becomes a bug to fix — not a skip.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Float-tolerant assertion | Custom comparator | `sqllogic.mjs valuesEqual()` from reference | Already handles numeric / boolean / NULL / string polymorphism |
| DECIMAL rendering fix | Arrow-level type inspection | `SELECT COLUMNS(*)::VARCHAR FROM (...)` | DuckDB itself applies scale; Arrow never sees the raw integer |
| WASM engine boot | Custom Worker setup | Exact `bootEngine()` pattern from reference | `pthreadWorker=null` constraint is non-obvious; wrong value causes silent failure |
| Extension serve | CDN / remote URL | `startServer()` localhost pattern | FORCE INSTALL must point to a controllable URL; remote URLs don't serve locally-built artifacts |
| Version detection | Parse wasm binary manually | `strings -n4 duckdb-eh.wasm | grep -oE 'v1\.[0-9]+'` or `db.getVersion()` | Both are standard patterns documented in the reference |

---

## Common Pitfalls

### Pitfall 1: pthreadWorker not null causes TypeError on first extension call

**What goes wrong:** Calling `db.instantiate(mainModule, someWorker)` with a non-null pthread worker for the eh/mvp bundle. The first call into the extension returns `TypeError: r is not a function`.

**Why it happens:** The eh bundle uses Emscripten exception handling but does not use pthreads. Passing a pthread worker causes a thread initialization path that the extension function lookup table doesn't expect.

**How to avoid:** Always `db.instantiate(mainModule, null)` for the eh bundle.

[VERIFIED: /home/simonm/projects/duckdb/anofox-statistics/test/wasm/README.md:53-55]

### Pitfall 2: DECIMAL renders as unscaled integer without `::VARCHAR`

**What goes wrong:** A `query I` block expecting `1.0` gets `10` from Arrow-JS; the test fails with a value mismatch that looks like an off-by-10x error.

**Why it happens:** Arrow-JS reads DuckDB DECIMAL columns as a bigint of the unscaled integer. `DECIMAL(10,1)` value `1.0` → bigint `10`.

**How to avoid:** Wrap all query results in `SELECT COLUMNS(*)::VARCHAR FROM (...) AS _wrap`. The `sqllogic.mjs` implementation does this automatically with a fallback to the raw query for non-projectable statements.

[VERIFIED: /home/simonm/projects/duckdb/anofox-statistics/test/wasm/README.md:74-80]

### Pitfall 3: Catalog state leaks across test files without per-file `db.open()`

**What goes wrong:** A `CREATE TABLE foo` in `ts_forecast.test` persists into `ts_features.test`, causing an `already exists` error.

**Why it happens:** A single DuckDB connection reuses the catalog across connections. Only `db.open()` (which re-initializes the database) resets catalog state.

**How to avoid:** Call `await engine.db.open({ allowUnsignedExtensions: true })` at the top of every per-file iteration, then reconnect and re-`LOAD`.

[VERIFIED: /home/simonm/projects/duckdb/anofox-statistics/test/wasm/run.mjs:222-229]

### Pitfall 4: npm version ≠ engine version ABI mismatch causes LOAD failure

**What goes wrong:** `FORCE INSTALL` succeeds (HTTP 200), but `LOAD` throws an ABI-version error.

**Why it happens:** DuckDB extensions carry the DuckDB version they were compiled against, and the engine refuses to load extensions built for a different version.

**How to avoid:** Use the `strings -n4 duckdb-eh.wasm | grep -oE 'v1\.[0-9]+\.[0-9]+'` procedure to verify the engine version in the npm package before pinning. Current correct pin for DuckDB v1.5.5 is `@duckdb/duckdb-wasm@1.33.1-dev64.0`.

[VERIFIED: /home/simonm/projects/duckdb/anofox-statistics/test/wasm/package.json:12-14 + README.md:88-113]

### Pitfall 5: `web-worker` v1.5.x breaks CJS worker loading

**What goes wrong:** `node test/wasm/run.mjs` throws `module is not defined` during Worker instantiation.

**Why it happens:** `web-worker@1.5.x` changed how it loads CJS modules. The `duckdb-node-eh.worker.cjs` file is a CommonJS file and fails under the new loader.

**How to avoid:** Pin `web-worker@1.2.0` exactly. Do not upgrade without testing.

[VERIFIED: /home/simonm/projects/duckdb/anofox-statistics/test/wasm/README.md:49-51]

### Pitfall 6: `require` in ESM context for the CJS duckdb bundle

**What goes wrong:** `import ... from '@duckdb/duckdb-wasm/dist/duckdb-node.cjs'` fails or the Worker path resolves incorrectly.

**Why it happens:** `run.mjs` uses `"type": "module"` in `package.json`, making it ESM. The duckdb package ships a CJS bundle that must be loaded with `require()`, not `import`. ESM doesn't have `require` by default.

**How to avoid:** Use `createRequire(path.join(HERE, 'package.json'))` to get a `require()` function, then `require('@duckdb/duckdb-wasm/dist/duckdb-node.cjs')`. Use `require.resolve(...)` to find the DIST directory for the worker and wasm paths.

[VERIFIED: /home/simonm/projects/duckdb/anofox-statistics/test/wasm/run.mjs:30-35, 132-135]

---

## Code Examples

### Complete run.mjs adaptation guide (key substitutions)

The reference `run.mjs` requires these substitutions for anofox-forecast:

```
EXT_NAME = 'anofox_statistics'   →   EXT_NAME = 'anofox_forecast'
EXT_FILE = 'anofox_statistics.duckdb_extension.wasm'   →   EXT_FILE = 'anofox_forecast.duckdb_extension.wasm'
CURATED list  →  update with representative anofox-forecast test files
SKIP_FILES map  →  start empty or with documented WASM-infeasible entries
Header log line  →  'anofox_forecast — DuckDB-Wasm harness'
```

The `bootEngine()`, `startServer()`, `findExtension()`, and `main()` functions are structurally identical and can be ported verbatim after EXT_NAME substitution.

### WASM artifact path (confirmed from local build)

```
build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm
```

[VERIFIED: `find /home/simonm/projects/duckdb/anofox-forecast/build -name "*.wasm"` — artifact exists at this path from a prior `make wasm_eh` run]

### CI artifact name pattern (for Phase 8 reference)

The `_extension_distribution.yml` names artifacts as:
```
${extension_name}-${duckdb_version}-extension-${duckdb_arch}
```

For this repo: `anofox_forecast-v1.5.5-extension-wasm_eh`

[VERIFIED: /home/simonm/projects/duckdb/anofox-forecast/extension-ci-tools/.github/workflows/_extension_distribution.yml:1204]

The artifact contains the file at:
```
build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm
```

[VERIFIED: /home/simonm/projects/duckdb/anofox-forecast/extension-ci-tools/.github/workflows/_extension_distribution.yml:1206]

### vcpkg.json final state (DEP-02)

```json
{
  "dependencies": [
    {
      "name": "openssl",
      "platform": "!wasm32"
    }
  ]
}
```

[VERIFIED: exact syntax from /home/simonm/projects/duckdb/anofox-statistics/vcpkg.json:1-15]

### Run command (local developer experience)

```bash
# Install harness deps (one-time)
npm --prefix test/wasm install

# Run against locally-built wasm_eh artifact (auto-discovered)
node test/wasm/run.mjs --all

# Or with explicit path
node test/wasm/run.mjs --all --ext build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm

# Or via env var
ANOFOX_WASM_EXT=build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm \
  npm --prefix test/wasm test
```

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| No WASM runtime gate | Node harness + `FORCE INSTALL` + `--all` suite | anofox-statistics PR #131 (2026-08) | Compile+link CI can't catch load/runtime failures; Node harness closes the gap |
| duckdb-wasm `@latest` | Version-pinned `@next` dev build | PR #131 | `@latest` → engine v1.4.3, not v1.5.5; ABI mismatch → LOAD failure |
| Arrow-JS value extraction | `SELECT COLUMNS(*)::VARCHAR FROM (...)` | PR #131 | Eliminates DECIMAL scale bug; results match native sqllogictest |
| `web-worker` unpinned | `web-worker@1.2.0` pinned | PR #131 | Prevents `module is not defined` regression on 1.5.x |

**No deprecated patterns in this domain** — the entire harness approach is new-for-this-milestone.

---

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Node.js ≥ 18 | `run.mjs`, `npm install` | ✓ | 20+ (CI uses `actions/setup-node@v4` with `node-version: 20`) | None — Node is required |
| npm | `npm --prefix test/wasm install` | ✓ | Bundled with Node | None |
| `make wasm_eh` / Emscripten | WASM build (prerequisite) | Build already exists | `build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm` exists locally | Download from CI artifact |
| DuckDB submodule v1.5.5 | Artifact already built | ✓ (submodule at v1.5.5) | v1.5.5 | N/A |

**Missing dependencies with no fallback:** None for the harness itself. The built `.wasm` must exist (either from a local `make wasm_eh` or downloaded from CI); the harness errors clearly if it can't find it.

---

## Validation Architecture

### Natural validation seams

This phase is itself a validation harness, so there are no unit tests for the harness code. The validation contract is:

| Gate | Command | Pass condition |
|------|---------|----------------|
| DEP-02 (vcpkg guard) | `make wasm_eh` | Build log contains "Telemetry disabled: ... WASM" and no OpenSSL compilation |
| WASM-01 (LOAD) | `node test/wasm/run.mjs --ext <path>` | "✓ LOAD anofox_forecast succeeded" line in output |
| WASM-02 (runner) | `node test/wasm/run.mjs --all --ext <path>` | Zero failures across all files |
| WASM-03 (full suite) | `node test/wasm/run.mjs --all --ext <path>` | All 66 files pass (or skip-listed entries logged with reason) |
| DEP-01 (ABI match) | `PRAGMA version;` output from harness | Engine version printed at startup matches v1.5.5 |

**Sampling rate:**
- Per task: `node test/wasm/run.mjs --file <specific-test-file>` (smoke a representative file)
- Per wave / final gate: `node test/wasm/run.mjs --all --ext build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm`

---

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | All 66 `test/sql/**/*.test` files are structurally compatible with WASM (no filesystem, no network, no httpfs) | WASM-03 Skip-List | One or more files may fail; must diagnose and either fix the test (e.g., wrap in `mode skip` block) or add to skip-list with documented reason |
| A2 | `TryAutoLoadExtension(db, "json")` in `LoadInternal()` succeeds in DuckDB-Wasm (json is bundled) | Skip-list / `require json` | The 50 files with `require json` would need `mode skip` blocks wrapping json-dependent queries; unlikely given DuckDB-Wasm bundles json |
| A3 | `SET threads=1` / `RESET threads` in `ts_parallel_correctness.test` is a no-op in single-threaded wasm_eh | WASM-03 | If DuckDB-Wasm errors on `SET threads`, that test file needs a skip-list entry |

**If the assumptions table is non-empty:** Items A1–A3 require runtime confirmation during execution. The harness's `--all` run is the confirmation step — run it, inspect failures, decide skip vs. fix per failure.

---

## Open Questions

1. **Does `TryAutoLoadExtension(db, "json")` succeed in DuckDB-Wasm?**
   - What we know: DuckDB-Wasm bundles core extensions including json. The call is in `LoadInternal()`, which fires at `LOAD anofox_forecast`.
   - What's unclear: Whether DuckDB-Wasm's `TryAutoLoad` path invokes the bundled json without a network call.
   - Recommendation: Run `node test/wasm/run.mjs --file test/sql/ts_decomposition.test` (uses `require json`) early in the plan. If it passes, A2 is confirmed and no further action is needed.

2. **Does the existing `build/wasm_eh` artifact match the current codebase state?**
   - What we know: The artifact file exists at `build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm`. The repo is on branch `milestone/v0.8.0-ensemble-forecasting` with uncommitted changes to `src/include/anofox_fcst_ffi.h`.
   - What's unclear: Whether the build is stale relative to the current codebase.
   - Recommendation: The plan should include a `make wasm_eh` step to rebuild, or the test plan should explicitly note the artifact must be rebuilt after DEP-02 is applied to `vcpkg.json`.

---

## Sources

### Primary (HIGH confidence — VERIFIED from local file reads this session)

- `/home/simonm/projects/duckdb/anofox-statistics/test/wasm/run.mjs` — complete harness implementation (all patterns)
- `/home/simonm/projects/duckdb/anofox-statistics/test/wasm/sqllogic.mjs` — complete parser/runner implementation
- `/home/simonm/projects/duckdb/anofox-statistics/test/wasm/package.json` — pinned deps
- `/home/simonm/projects/duckdb/anofox-statistics/test/wasm/README.md` — version-matching procedure, known gotchas
- `/home/simonm/projects/duckdb/anofox-statistics/vcpkg.json` — `!wasm32` platform guard syntax
- `/home/simonm/projects/duckdb/anofox-forecast/extension_config.cmake` — `LINKED_LIBS` pattern
- `/home/simonm/projects/duckdb/anofox-forecast/CMakeLists.txt` — `WASM_BUILD` / `TELEMETRY_SUPPORTED` guards
- `/home/simonm/projects/duckdb/anofox-forecast/vcpkg.json` — current state (no platform guard)
- `/home/simonm/projects/duckdb/anofox-forecast/.github/workflows/MainDistributionPipeline.yml` — DuckDB version targets (v1.4.5 LTS, v1.5.5)
- `/home/simonm/projects/duckdb/anofox-forecast/extension-ci-tools/.github/workflows/_extension_distribution.yml` — artifact name pattern
- `/home/simonm/projects/duckdb/anofox-forecast/src/anofox_forecast_extension.cpp` — json auto-load
- `gh pr view 131 --repo DataZooDE/anofox-statistics` — PR metadata confirming merge

### Secondary (MEDIUM confidence)

- `npm view @duckdb/duckdb-wasm versions --json` — registry-confirmed version list
- `npm view @duckdb/duckdb-wasm dist-tags` — confirms `next: '1.33.1-dev64.0'`
- `git describe --tags` on duckdb submodule — v1.5.5 confirmed

---

## Metadata

**Confidence breakdown:**
- Standard stack (npm deps, boot sequence): HIGH — verbatim from CI-verified reference implementation
- Architecture (server, catalog isolation, DECIMAL fix): HIGH — verbatim from CI-verified reference
- Skip-list analysis: MEDIUM — structural analysis of 66 test files; runtime confirmation required
- DEP-02 vcpkg syntax: HIGH — exact syntax verified from anofox-statistics vcpkg.json

**Research date:** 2026-09-01
**Valid until:** 2027-03-01 (stable; only invalidated by DuckDB version bump or duckdb-wasm npm changes)
