#!/usr/bin/env node
// DuckDB-Wasm smoke/regression harness for the anofox_forecast extension.
//
// What it proves (the thing a compile+link CI leg CANNOT):
//   1. The locally-built `.wasm` extension LOADs in DuckDB-Wasm under Node.
//   2. Representative forecasting functions return correct results at runtime.
//
// Recipe follows the verified Node approach used by query.farm's
// haybarn-extension-wasm-tester (see test/wasm/README.md for sources):
//   - @duckdb/duckdb-wasm/dist/duckdb-node.cjs + web-worker@1.2.0 (pinned)
//   - the `eh` bundle; db.instantiate(mainModule, null)
//   - db.open({ allowUnsignedExtensions: true })
//   - serve the built .wasm over localhost and FORCE INSTALL ... FROM it
//
// Usage:
//   node run.mjs [--ext <path-to-.duckdb_extension.wasm>] [--all] [--file <t.test> ...]
//   ANOFOX_WASM_EXT=<path> node run.mjs
//
// Exit code 0 on success; non-zero on any load or assertion failure.

import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
import { parseTest, runRecords } from './sqllogic.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(HERE, '..', '..');
const require = createRequire(path.join(HERE, 'package.json'));

const EXT_NAME = 'anofox_forecast';
const EXT_FILE = `${EXT_NAME}.duckdb_extension.wasm`;

// Curated WASM-appropriate subset: breadth across function families, kept small
// so the first CI gate is robust. Expand with --all once green. NOTE: this is an
// explicit, logged subset — not a silent truncation of the 66-file suite.
// Files skipped even under --all, with reason (logged, never silent):
//  (none — all 66 files are expected to pass on DuckDB-Wasm)
const SKIP_FILES = new Map();

const CURATED = [
  'test/sql/ts_forecast_by.test',
  'test/sql/ts_features.test',
  'test/sql/ts_metrics.test',
  'test/sql/ts_decomposition.test',
  'test/sql/ts_diagnostics.test',
  'test/sql/ts_conformal.test',
  'test/sql/ts_cv_folds.test',
  'test/sql/ts_stats.test',
];

function parseArgs(argv) {
  const args = { ext: process.env.ANOFOX_WASM_EXT || null, all: false, files: [] };
  for (let i = 2; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--ext') args.ext = argv[++i];
    else if (a === '--all') args.all = true;
    else if (a === '--file') args.files.push(argv[++i]);
    else if (a.endsWith('.test')) args.files.push(a);
  }
  return args;
}

// Locate the built extension .wasm if not given explicitly.
function findExtension(explicit) {
  if (explicit) {
    if (!fs.existsSync(explicit)) throw new Error(`--ext path does not exist: ${explicit}`);
    return path.resolve(explicit);
  }
  const roots = [
    path.join(REPO_ROOT, 'build'),
    REPO_ROOT,
  ];
  const found = [];
  const walk = (dir, depth) => {
    if (depth > 8) return;
    let entries;
    try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
    for (const e of entries) {
      const p = path.join(dir, e.name);
      if (e.isDirectory()) {
        if (e.name === 'node_modules' || e.name === '.git') continue;
        walk(p, depth + 1);
      } else if (e.name === EXT_FILE) {
        found.push(p);
      }
    }
  };
  for (const r of roots) walk(r, 0);
  if (found.length === 0) {
    throw new Error(
      `Could not find ${EXT_FILE}. Build the WASM extension first, or pass --ext <path> / set ANOFOX_WASM_EXT.`,
    );
  }
  // Prefer an eh build if multiple are present.
  found.sort((a, b) => (b.includes('wasm_eh') ? 1 : 0) - (a.includes('wasm_eh') ? 1 : 0));
  return found[0];
}

// A version-agnostic static server: any request whose path ends in the extension
// filename is served the one built artifact. This sidesteps the
// <version>/<wasm_platform>/ path segments DuckDB-Wasm injects (which must
// otherwise exactly match the engine version) — we just serve the file whatever
// version/platform dir it asks for.
function startServer(extPath) {
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

async function bootEngine() {
  const duckdb = require('@duckdb/duckdb-wasm/dist/duckdb-node.cjs');
  const Worker = require('web-worker');
  const DIST = path.dirname(require.resolve('@duckdb/duckdb-wasm/dist/duckdb-node.cjs'));

  const mainModule = path.join(DIST, 'duckdb-eh.wasm');
  const mainWorker = path.join(DIST, 'duckdb-node-eh.worker.cjs');

  const worker = new Worker(mainWorker);
  const logger = { log() {} }; // silence per-query engine logs; we report ourselves
  const db = new duckdb.AsyncDuckDB(logger, worker);
  await db.instantiate(mainModule, null); // pthreadWorker MUST be null for eh
  await db.open({ allowUnsignedExtensions: true });
  return { db, worker };
}

// Adapter: run a SQL string, return rows as arrays of JS cell values.
function makeRunQuery(conn) {
  return async (sql) => {
    const table = await conn.query(sql);
    const rows = [];
    for (const row of table.toArray()) {
      // Arrow row → array of column values in schema order.
      const obj = row.toJSON();
      rows.push(Object.values(obj));
    }
    return rows;
  };
}

async function main() {
  const args = parseArgs(process.argv);
  const log = (s) => process.stdout.write(s + '\n');

  log('━━━ anofox_forecast — DuckDB-Wasm harness ━━━');

  const extPath = findExtension(args.ext);
  log(`Extension artifact: ${path.relative(REPO_ROOT, extPath)}`);

  const { server, port } = await startServer(extPath);
  const base = `http://127.0.0.1:${port}`;

  let engine;
  let failedHard = false;
  try {
    engine = await bootEngine();
    const conn = await engine.db.connect();

    let version = 'unknown';
    try {
      const v = await conn.query('PRAGMA version;');
      version = Object.values(v.toArray()[0].toJSON()).join(' ');
    } catch { /* non-fatal */ }
    log(`DuckDB-Wasm engine: ${version}`);

    // Load the locally-built extension. FORCE INSTALL busts the Node FS cache.
    log(`Installing ${EXT_NAME} from ${base} ...`);
    await conn.query(`FORCE INSTALL ${EXT_NAME} FROM '${base}';`);
    await conn.query(`LOAD ${EXT_NAME};`);
    log(`✓ LOAD ${EXT_NAME} succeeded — extension loads in DuckDB-Wasm.`);
    await conn.close(); // per-file isolation below re-opens the catalog

    // Choose test files.
    let files;
    if (args.files.length) files = args.files;
    else if (args.all) {
      files = [];
      const sqlDir = path.join(REPO_ROOT, 'test', 'sql');
      const walk = (d) => {
        for (const e of fs.readdirSync(d, { withFileTypes: true })) {
          const p = path.join(d, e.name);
          if (e.isDirectory()) walk(p);
          else if (e.name.endsWith('.test')) files.push(path.relative(REPO_ROOT, p));
        }
      };
      walk(sqlDir);
      log(`Running ALL ${files.length} .test files (--all).`);
    } else {
      files = CURATED;
      log(`Running curated WASM subset (${files.length} files). Use --all for the full suite.`);
    }

    let totalPass = 0, totalFail = 0, totalSkip = 0;
    const failedFiles = [];
    for (const rel of files) {
      if (SKIP_FILES.has(rel)) { log(`  ⊘ ${rel} — skipped (${SKIP_FILES.get(rel)})`); continue; }
      const abs = path.join(REPO_ROOT, rel);
      if (!fs.existsSync(abs)) { log(`  ⚠ missing: ${rel} (skipped)`); continue; }
      const text = fs.readFileSync(abs, 'utf8');
      const records = parseTest(text);

      // Isolate each file with a fresh catalog (native sqllogictest resets state
      // per file). Re-open the DB, reconnect, and re-LOAD the already-installed
      // extension so CREATE TABLE / temp state cannot leak across files.
      await engine.db.open({ allowUnsignedExtensions: true });
      const c = await engine.db.connect();
      await c.query(`LOAD ${EXT_NAME};`);
      const r = await runRecords(records, makeRunQuery(c), { file: rel, log });
      await c.close();

      totalPass += r.passed; totalFail += r.failed; totalSkip += r.skipped;
      const mark = r.failed === 0 ? '✓' : '✗';
      log(`  ${mark} ${rel} — ${r.passed} passed, ${r.failed} failed, ${r.skipped} skipped`);
      if (r.failed > 0) failedFiles.push(rel);
    }

    log('──────────────────────────────────────────────');
    log(`Totals: ${totalPass} passed, ${totalFail} failed, ${totalSkip} skipped across ${files.length} files`);
    if (totalFail > 0) {
      log(`✗ Failing files: ${failedFiles.join(', ')}`);
      failedHard = true;
    } else {
      log('✓ All assertions passed on DuckDB-Wasm.');
    }
  } catch (err) {
    failedHard = true;
    log(`✗ HARNESS ERROR: ${err && err.stack ? err.stack : err}`);
  } finally {
    try { if (engine) { await engine.db.terminate(); engine.worker.terminate(); } } catch { /* ignore */ }
    server.close();
  }

  process.exit(failedHard ? 1 : 0);
}

main();
