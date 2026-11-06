// sqllogictest-subset parser + runner for the DuckDB-Wasm harness (run.mjs).
// Ported from anofox-optimize's test/wasm/sqllogic.mjs (DataZooDE/anofox-optimize
// PR #21), extended with the `foreach`/`loop` and `statement maybe` records the
// anofox-forecast suite uses.
//
// Not a full sqllogictest implementation, but what it does support follows
// DuckDB's runner (test/sqlite/sqllogic_test_runner.cpp, result_helper.cpp), so
// "passed in DuckDB-Wasm" means what "passed natively" means:
//   - `require <ext>`, `require-env`, `load`, `restart` -> directives the harness
//     handles itself
//   - `statement ok`
//   - `statement error [msg]`, the expected message optionally after `----`:
//     a substring of the error, or `<REGEX>:pattern` matching the whole message
//   - `statement maybe` + `----` + message: success passes, and so does an
//     error that matches the message
//   - an error containing `INTERNAL` never satisfies `statement error`/`maybe`
//     (DuckDB's always_fail_error_messages)
//   - `query <types> [nosort|rowsort|valuesort] [label]`, results after `----`,
//     one row per line with tab-separated columns or one value per line, or
//     `N values hashing to <md5>`
//   - `foreach <name> <v1> <v2> ...` / `loop <name> <start> <end>` ... `endloop`
//     (nestable): the body is repeated with `${name}` and `{name}` replaced, a
//     comma-separated name taking comma-separated values; the `<integral>`-style
//     type collections are not supported and fail
//   - `mode skip` / `mode unskip`, `halt`, `#` comments
// Any other record keyword (concurrentloop, onlyif, skipif, ...) is a FAILURE
// naming the file and line: silently skipping it would run the records inside
// it unguarded, or not at all.
//
// Comparison (DuckDB semantics, stricter for floating point):
//   - values are rendered by DuckDB itself (COLUMNS(*)::VARCHAR), NULL as
//     `NULL` and the empty string as `(empty)`;
//   - the column count must equal the length of the type string, and the
//     number of rows must match, before any value is compared;
//   - identical text is equal; otherwise the expected value is interpreted in
//     the column's actual SQL type (typeof, in the same statement): integers exactly (BigInt, so
//     BIGINT keeps its precision), BOOLEAN as true/false/1/0, DOUBLE/FLOAT/
//     DECIMAL within FLOAT_REL_TOL relative, everything else (VARCHAR, LIST,
//     STRUCT, ...) as exact text. DuckDB allows 1% on doubles; this harness
//     allows 1e-9, enough for Emscripten's libm and nothing more.
//
// `node sqllogic.mjs --self-test` checks the comparator and the parser against
// known-good and known-bad shapes without booting DuckDB.

import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';

export const FLOAT_REL_TOL = 1e-9;
export const FLOAT_ABS_TOL = 1e-12; // only for values that are ~0
export const RECORD_TIMEOUT_MS = 60_000;

// ---- Parsing -------------------------------------------------------------

const DIRECTIVES = new Set(['require', 'require-env', 'load', 'restart']);
const SORT_MODES = new Set(['nosort', 'rowsort', 'valuesort']);

// Replace a loop iterator the way DuckDB's StringReplaceLoopIterator does:
// `${name}` first, then `{name}`. A comma-separated name takes comma-separated
// values, one per name.
function replaceIterator(text, name, value) {
  const names = name.split(',');
  const values = names.length > 1 ? value.split(',') : [value];
  if (values.length !== names.length) return null;
  names.forEach((n, k) => {
    text = text.split('${' + n + '}').join(values[k]).split('{' + n + '}').join(values[k]);
  });
  return text;
}

export function parseTest(text) {
  return parseLines(text.split(/\r?\n/), 0, { skipMode: false, halted: false });
}

// `lines` is a block of the file whose first line is file line `offset + 1`;
// `state` carries `mode skip` and `halt` across loop bodies.
function parseLines(lines, offset, state) {
  const records = [];
  let i = 0;

  const isBlank = (l) => l.trim() === '';
  const isComment = (l) => l.trimStart().startsWith('#');
  const unknown = (kw, lineNo) => records.push({ type: 'unknown', kw, line: lineNo, skip: false });

  while (i < lines.length && !state.halted) {
    const line = lines[i];
    if (isBlank(line) || isComment(line)) { i++; continue; }

    const lineNo = offset + i + 1;
    const tokens = line.trim().split(/\s+/);
    const kw = tokens[0];

    if (kw === 'foreach' || kw === 'loop') {
      // Find the matching endloop (loops nest).
      let depth = 1, j = i + 1;
      for (; j < lines.length; j++) {
        const k = lines[j].trim().split(/\s+/)[0];
        if (k === 'foreach' || k === 'loop' || k === 'concurrentforeach' || k === 'concurrentloop') depth++;
        else if (k === 'endloop' && --depth === 0) break;
      }
      if (j >= lines.length) { unknown(`${line.trim()} (missing endloop)`, lineNo); break; }
      let values = null;
      if (kw === 'foreach' && tokens.length >= 3 && !tokens.slice(2).some((t) => t.startsWith('<') || t.startsWith('!'))) {
        values = tokens.slice(2);
      } else if (kw === 'loop' && tokens.length === 4 && /^-?\d+$/.test(tokens[2]) && /^-?\d+$/.test(tokens[3])) {
        values = [];
        for (let v = Number(tokens[2]); v < Number(tokens[3]); v++) values.push(String(v));
      }
      if (values === null) { unknown(line.trim(), lineNo); i = j + 1; continue; }
      const body = lines.slice(i + 1, j);
      for (const v of values) {
        const replaced = body.map((l) => replaceIterator(l, tokens[1], v));
        if (replaced.includes(null)) { unknown(`${line.trim()} (value "${v}" does not match the names)`, lineNo); break; }
        for (const r of parseLines(replaced, offset + i + 1, state)) {
          records.push({ ...r, iteration: `${tokens[1]}=${v}` });
        }
        if (state.halted) break;
      }
      i = j + 1;
      continue;
    }

    if (kw === 'endloop') { unknown('endloop without loop', lineNo); i++; continue; }

    if (kw === 'mode') {
      if (tokens[1] === 'skip') state.skipMode = true;
      else if (tokens[1] === 'unskip') state.skipMode = false;
      else unknown(line.trim(), lineNo);
      i++;
      continue;
    }

    if (DIRECTIVES.has(kw)) {
      records.push({ type: 'directive', kw, args: tokens.slice(1), line: lineNo, skip: state.skipMode });
      i++;
      continue;
    }

    if (kw === 'halt') {
      records.push({ type: 'halt', line: lineNo, skip: state.skipMode });
      state.halted = true;
      break;
    }

    if (kw === 'statement') {
      const kind = tokens[1];
      const known = kind === 'ok' || kind === 'error' || kind === 'maybe';
      if (!known) unknown(line.trim(), lineNo);
      let errorSubstr = kind === 'error' || kind === 'maybe' ? tokens.slice(2).join(' ').trim() || null : null;
      i++;
      const sql = [];
      while (i < lines.length && !isBlank(lines[i]) && lines[i].trim() !== '----') { sql.push(lines[i]); i++; }
      // The expected message after `----`. Folding it into the SQL would make
      // the record fail on a syntax error and "pass" without running the real
      // statement.
      if (i < lines.length && lines[i].trim() === '----') {
        i++;
        const msg = [];
        while (i < lines.length && !isBlank(lines[i])) { msg.push(lines[i]); i++; }
        if (msg.length) errorSubstr = msg.join('\n').trim();
      }
      if (kind === 'maybe' && !errorSubstr) {
        // DuckDB's parser rejects this too: without a message, any error passes.
        unknown('statement maybe without an expected error message', lineNo);
      } else if (known) {
        records.push({ type: 'statement', kind, expectOk: kind === 'ok', errorSubstr, sql: sql.join('\n'), line: lineNo, skip: state.skipMode });
      }
      continue;
    }

    if (kw === 'query') {
      const types = tokens[1] || '';
      let sortMode = 'nosort';
      if (tokens[2] !== undefined) {
        if (SORT_MODES.has(tokens[2])) sortMode = tokens[2];
        else if (tokens.length > 3) {
          // A third token that is not a sort mode, followed by more: not a form we know.
          unknown(line.trim(), lineNo);
        }
        // `query I label` (label without sort mode) is accepted, label ignored.
      }
      if (!/^[ITR]+$/.test(types)) unknown(line.trim(), lineNo);
      i++;
      const sql = [];
      while (i < lines.length && !isBlank(lines[i]) && lines[i].trim() !== '----') { sql.push(lines[i]); i++; }
      let expected = null;
      if (i < lines.length && lines[i].trim() === '----') {
        i++;
        expected = [];
        while (i < lines.length && !isBlank(lines[i])) { expected.push(lines[i]); i++; }
      }
      records.push({ type: 'query', types, sortMode, sql: sql.join('\n'), expected, line: lineNo, skip: state.skipMode });
      continue;
    }

    // Unknown record keyword: fail loudly, then skip to the end of the record.
    unknown(line.trim(), lineNo);
    i++;
    while (i < lines.length && !isBlank(lines[i])) i++;
  }

  return records;
}

// ---- Value comparison ----------------------------------------------------

const INTEGER_TYPES = /^(TINYINT|SMALLINT|INTEGER|BIGINT|HUGEINT|UTINYINT|USMALLINT|UINTEGER|UBIGINT|UHUGEINT)$/;
const FLOAT_TYPES = /^(FLOAT|DOUBLE|REAL|DECIMAL(\(\d+,\s*\d+\))?)$/;
const INT_RE = /^[+-]?\d+$/;
const NUM_RE = /^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$/;

// Render one cell the way DuckDB's sqllogictest runner does.
export function renderCell(cell) {
  if (cell === null || cell === undefined) return 'NULL';
  const s = typeof cell === 'bigint' ? cell.toString() : String(cell);
  return s === '' ? '(empty)' : s;
}

function boolOf(s) {
  const l = s.toLowerCase();
  if (l === 'true' || l === '1') return true;
  if (l === 'false' || l === '0') return false;
  return null;
}

function floatsEqual(a, b) {
  const x = Number(a), y = Number(b);
  if (!Number.isFinite(x) || !Number.isFinite(y)) return false;
  const diff = Math.abs(x - y);
  return diff <= FLOAT_ABS_TOL || diff <= FLOAT_REL_TOL * Math.max(Math.abs(x), Math.abs(y));
}

// `sqlType` is the column's DuckDB type from typeof(), or null when unknown
// (then the sqllogictest type letter decides: I integer, R float, T text).
export function valuesEqual(sqlType, letter, expected, actual) {
  if (expected === actual) return true;
  if (expected === 'NULL' || actual === 'NULL') return false;

  const t = sqlType ? sqlType.toUpperCase() : null;
  if (t === 'BOOLEAN') {
    const e = boolOf(expected), a = boolOf(actual);
    return e !== null && e === a;
  }
  if (t ? INTEGER_TYPES.test(t) : letter === 'I') {
    if (INT_RE.test(expected) && INT_RE.test(actual)) return BigInt(expected) === BigInt(actual);
    if (!t) {
      // Unknown type under `query I`: booleans and floats are legal here too.
      const e = boolOf(expected), a = boolOf(actual);
      if (e !== null && a !== null) return e === a;
      if (NUM_RE.test(expected) && NUM_RE.test(actual) && !(INT_RE.test(expected) && INT_RE.test(actual))) {
        return floatsEqual(expected, actual);
      }
    }
    return false;
  }
  if (t ? FLOAT_TYPES.test(t) : letter === 'R') {
    return NUM_RE.test(expected) && NUM_RE.test(actual) && floatsEqual(expected, actual);
  }
  return false; // text, nested and every other type: exact text only
}

// Split the expected block into rows of cells, DuckDB style: one row per line
// with tab-separated columns, or (when that does not fit) one value per line.
function expectedRows(lines, nCols, nActualRows) {
  const split = lines.map((l) => l.split('\t'));
  if (split.every((r) => r.length === nCols)) return { rows: split };
  if (split.every((r) => r.length === 1) && lines.length === nActualRows * nCols) {
    const rows = [];
    for (let k = 0; k < lines.length; k += nCols) rows.push(lines.slice(k, k + nCols));
    return { rows };
  }
  const bad = split.findIndex((r) => r.length !== nCols);
  return { error: `expected line ${bad + 1} has ${split[bad].length} column(s), the query declares ${nCols}` };
}

const HASH_RE = /^(\d+) values hashing to ([0-9a-f]{32})$/;

// Row ordering in DuckDB's runner, applied to both sides: rowsort sorts whole
// rows by their cells in column order (string comparison), valuesort sorts
// every value and refills rows of nCols.
function cmpRows(a, b) {
  for (let k = 0; k < Math.min(a.length, b.length); k++) {
    if (a[k] < b[k]) return -1;
    if (a[k] > b[k]) return 1;
  }
  return a.length - b.length;
}

function sortRows(rows, sortMode, nCols) {
  if (sortMode === 'rowsort') return [...rows].sort(cmpRows);
  if (sortMode !== 'valuesort') return rows;
  const vals = rows.flat().sort();
  const out = [];
  for (let k = 0; k < vals.length; k += nCols) out.push(vals.slice(k, k + nCols));
  return out;
}

// record: { types, sortMode, expected }. rows: array of arrays of cells.
// sqlTypes: array of column type names, or null.
export function compareQuery(record, rows, sqlTypes = null) {
  if (record.expected === null) return { ok: true }; // no expected block: must just run

  const nCols = record.types.length;
  const rendered = rows.map((r) => r.map(renderCell));
  const wrongWidth = rendered.findIndex((r) => r.length !== nCols);
  if (wrongWidth >= 0) {
    return { ok: false, reason: `result has ${rendered[wrongWidth].length} column(s), the query declares ${nCols} (${record.types})` };
  }
  const actual = sortRows(rendered, record.sortMode, nCols);

  const hash = record.expected.length === 1 && HASH_RE.exec(record.expected[0]);
  if (hash) {
    const flat = actual.flat();
    const digest = createHash('md5').update(flat.map((v) => v + '\n').join('')).digest('hex');
    if (Number(hash[1]) !== flat.length) return { ok: false, reason: `expected ${hash[1]} values, got ${flat.length}` };
    return digest === hash[2] ? { ok: true } : { ok: false, reason: `hash mismatch: expected ${hash[2]}, got ${digest}` };
  }

  const exp = expectedRows(record.expected, nCols, actual.length);
  if (exp.error) return { ok: false, reason: exp.error };
  if (exp.rows.length !== actual.length) {
    return { ok: false, reason: `row count mismatch: expected ${exp.rows.length}, got ${actual.length}` };
  }
  const expected = sortRows(exp.rows, record.sortMode, nCols);

  for (let r = 0; r < expected.length; r++) {
    for (let c = 0; c < nCols; c++) {
      const t = sqlTypes ? sqlTypes[c] : null;
      if (!valuesEqual(t, record.types[c], expected[r][c], actual[r][c])) {
        return {
          ok: false,
          reason: `row ${r + 1} column ${c + 1}${t ? ` (${t})` : ''}: expected "${expected[r][c]}", got "${actual[r][c]}"`,
        };
      }
    }
  }
  return { ok: true };
}

// ---- Running -------------------------------------------------------------

// Native sqllogictest semantics: a plain expected message is a substring of the
// error; `<REGEX>:pattern` must match the whole message (dot matches newlines).
export function errorMatches(expected, message) {
  if (!expected) return true;
  if (expected.startsWith('<REGEX>:')) {
    return new RegExp(`^(?:${expected.slice('<REGEX>:'.length)})$`, 's').test(message);
  }
  return message.includes(expected);
}

// DuckDB's always_fail_error_messages: an internal error is a bug, so it never
// satisfies `statement error` or `statement maybe`, whatever the message.
export function isInternalError(message) {
  return message.includes('INTERNAL') || message.includes('differs from original result!');
}

export class RecordTimeout extends Error {}

// A query that never returns would otherwise surface only as the CI job's
// timeout, with no record named. The hung query keeps the worker busy, so the
// caller must abort the run on a RecordTimeout rather than continue.
function withTimeout(promise, ms, what) {
  let timer;
  const timeout = new Promise((_, reject) => {
    timer = setTimeout(() => reject(new RecordTimeout(`timed out after ${ms / 1000} s: ${what}`)), ms);
  });
  return Promise.race([promise, timeout]).finally(() => clearTimeout(timer));
}

const RETRY_RAW = /Binder Error|Parser Error/;

// `runQuery(sql)` must return an array of rows, each an array of cells in
// column order (not keyed by name: duplicate column names must survive).
export async function runRecords(records, runQuery, { file, log, timeoutMs = RECORD_TIMEOUT_MS }) {
  const result = { file, passed: 0, failed: 0, skipped: 0, raw: 0, failures: [] };
  const where = (rec) => `${file}:${rec.line}${rec.iteration ? ` [${rec.iteration}]` : ''}`;
  const fail = (rec, reason) => {
    result.failed++;
    result.failures.push({ where: where(rec), sql: rec.sql || rec.kw || '', reason });
  };

  for (const rec of records) {
    if (rec.skip) { result.skipped++; continue; }
    if (rec.type === 'directive' || rec.type === 'halt') continue;
    if (rec.type === 'unknown') { fail(rec, `unsupported record "${rec.kw}"`); continue; }

    const what = where(rec);
    const run = (sql) => withTimeout(runQuery(sql), timeoutMs, what);

    if (rec.type === 'statement') {
      try {
        await run(rec.sql);
        if (rec.expectOk || rec.kind === 'maybe') result.passed++;
        else fail(rec, 'expected an error, but the statement succeeded');
      } catch (err) {
        if (err instanceof RecordTimeout) throw err;
        const msg = String(err.message || err);
        if (rec.expectOk) fail(rec, `unexpected error: ${msg}`);
        else if (isInternalError(msg)) fail(rec, `internal error, never an expected one: ${msg}`);
        else if (errorMatches(rec.errorSubstr, msg)) result.passed++;
        else fail(rec, `error message mismatch: expected "${rec.errorSubstr}", got "${msg}"`);
      }
      continue;
    }

    if (rec.type === 'query') {
      try {
        // Render through DuckDB's own ::VARCHAR, as native sqllogictest does,
        // rather than through Arrow-JS (which mis-renders DECIMAL), with each
        // column's type from typeof() in the same statement: the first half of
        // every row is the values, the second half their types. Only a query
        // the wrap cannot bind or parse (PRAGMA, SHOW, ...) is retried raw and
        // compared by type letter; a runtime error is the query's own and is
        // reported, not run a second time.
        const inner = rec.sql.trim().replace(/;\s*$/, '');
        let rows;
        let sqlTypes = null;
        try {
          const wide = await run(`SELECT COLUMNS(*)::VARCHAR, typeof(COLUMNS(*)) FROM (\n${inner}\n) AS _wrap`);
          const width = wide.length ? wide[0].length / 2 : 0;
          rows = wide.map((r) => r.slice(0, width));
          if (wide.length) sqlTypes = wide[0].slice(width).map(String);
        } catch (err) {
          if (err instanceof RecordTimeout || !RETRY_RAW.test(String(err.message || err))) throw err;
          result.raw++;
          rows = await run(rec.sql);
        }
        const cmp = compareQuery(rec, rows, sqlTypes);
        if (cmp.ok) result.passed++;
        else fail(rec, cmp.reason);
      } catch (err) {
        if (err instanceof RecordTimeout) throw err;
        fail(rec, `query threw: ${err.message || err}`);
      }
      continue;
    }
  }

  if (log && result.failed > 0) {
    for (const f of result.failures) {
      log(`    ✗ ${f.where}: ${f.reason}\n      SQL: ${f.sql.replace(/\n/g, ' ').slice(0, 160)}`);
    }
  }
  return result;
}

// ---- Self-test -----------------------------------------------------------

export function selfTest() {
  const q = (types, expected, sortMode = 'nosort') => ({ types, sortMode, expected });
  const cases = [
    // [name, record, rows, sqlTypes, expect ok]
    ['integers exact: 100000 vs 100001', q('I', ['100000']), [['100001']], ['BIGINT'], false],
    ['integers exact, no type info: 100000 vs 100001', q('I', ['100000']), [['100001']], null, false],
    ['BIGINT precision: 9007199254740993 vs ...992', q('I', ['9007199254740993']), [['9007199254740992']], ['BIGINT'], false],
    ['BIGINT equal beyond 2^53', q('I', ['9007199254740993']), [['9007199254740993']], ['BIGINT'], true],
    ['text exact: "001" vs "1"', q('T', ['001']), [['1']], ['VARCHAR'], false],
    ['text exact, no type info: "001" vs "1"', q('T', ['001']), [['1']], null, false],
    ['shape: rows [[1],[2]] vs "1\\t2" under query II', q('II', ['1\t2']), [['1'], ['2']], null, false],
    ['shape: one column returned, II declared', q('II', ['1', '2']), [['1'], ['2']], null, false],
    ['row count: 2 expected, 3 returned', q('I', ['1', '2']), [['1'], ['2'], ['3']], ['INTEGER'], false],
    ['two columns, tab separated', q('II', ['1\t2']), [['1', '2']], ['INTEGER', 'INTEGER'], true],
    ['two columns, one value per line', q('II', ['1', '2']), [['1', '2']], ['INTEGER', 'INTEGER'], true],
    ['double within 1e-9', q('R', ['0.30000000000000004']), [['0.3']], ['DOUBLE'], true],
    ['double off by 1e-6 relative', q('R', ['1.000001']), [['1.0']], ['DOUBLE'], false],
    ['DECIMAL scale: 1.0 vs 1.00', q('R', ['1.0']), [['1.00']], ['DECIMAL(18,2)'], true],
    ['boolean: query I true vs 1', q('I', ['1']), [['true']], ['BOOLEAN'], true],
    ['boolean: true vs false', q('I', ['true']), [['false']], ['BOOLEAN'], false],
    ['NULL vs NULL', q('I', ['NULL']), [[null]], ['INTEGER'], true],
    ['NULL vs 0', q('I', ['NULL']), [['0']], ['INTEGER'], false],
    ['empty string renders as (empty)', q('T', ['(empty)']), [['']], ['VARCHAR'], true],
    ['empty string vs NULL', q('T', ['(empty)']), [[null]], ['VARCHAR'], false],
    ['list text exact', q('T', ['[1, 2]']), [['[1,2]']], ['BIGINT[]'], false],
    ['rowsort sorts both sides', q('II', ['b\t1', 'a\t2'], 'rowsort'), [['a', '2'], ['b', '1']], ['VARCHAR', 'INTEGER'], true],
    ['nosort keeps order', q('I', ['2', '1']), [['1'], ['2']], ['INTEGER'], false],
    ['valuesort', q('I', ['3', '1', '2'], 'valuesort'), [['2'], ['3'], ['1']], ['INTEGER'], true],
    ['hash match', q('I', ['2 values hashing to ' + createHash('md5').update('1\n2\n').digest('hex')]), [['1'], ['2']], ['INTEGER'], true],
    ['hash mismatch', q('I', ['2 values hashing to ' + createHash('md5').update('1\n3\n').digest('hex')]), [['1'], ['2']], ['INTEGER'], false],
  ];
  const failures = [];
  for (const [name, rec, rows, types, want] of cases) {
    const got = compareQuery(rec, rows, types).ok;
    if (got !== want) failures.push(`${name}: expected ${want ? 'accept' : 'reject'}, got ${got ? 'accept' : 'reject'}`);
  }

  // Parser: unknown keywords become failing records with their line; the
  // `statement error` message is split from the SQL; loops expand.
  const checks = [];
  const check = (name, ok) => { checks.push(name); if (!ok) failures.push(`parser: ${name}`); };
  const parsed = parseTest('statement error\nSELECT 1/0;\n----\nboom\n\nconcurrentloop i 0 3\nquery I\nSELECT 1\n----\n1\nendloop\n\nskipif wasm\nquery I\nSELECT 1\n');
  const unknown = parsed.filter((r) => r.type === 'unknown').map((r) => r.line);
  check('statement error: SQL and message split', parsed[0].sql === 'SELECT 1/0;' && parsed[0].errorSubstr === 'boom');
  check(`unknown keywords fail with their line (got ${unknown.join(',')})`, unknown.join(',') === '6,13');
  check('REGEX matching', errorMatches('<REGEX>:.*cap.*', 'x cap y') && !errorMatches('<REGEX>:cap', 'x cap y'));

  const loops = parseTest('foreach p nan -5.0\n\nstatement error\nSELECT \'${p}\'::DOUBLE, {p};\n----\nbad\n\nloop i 0 2\nquery I\nSELECT ${i}\n----\n${i}\n\nendloop\n\nendloop\n\nquery I\nSELECT 9\n----\n9\n');
  const sqls = loops.map((r) => `${r.line}:${r.sql}`);
  check(`foreach/loop expand nested, ${'$'}{x} and {x} replaced (got ${sqls.join(' | ')})`,
    sqls.join(' | ') === "3:SELECT 'nan'::DOUBLE, nan; | 9:SELECT 0 | 9:SELECT 1 | 3:SELECT '-5.0'::DOUBLE, -5.0; | 9:SELECT 0 | 9:SELECT 1 | 18:SELECT 9");
  check('loop body expected block replaced', loops[1].expected[0] === '0' && loops[2].expected[0] === '1');
  check('foreach iteration labelled', loops[0].iteration === 'p=nan');
  check('foreach with comma names', parseTest('foreach a,b 1,2\n\nstatement ok\nSELECT ${a} + ${b}\n\nendloop\n')[0].sql === 'SELECT 1 + 2');
  check('foreach <integral> collection fails', parseTest('foreach t <integral>\n\nstatement ok\nSELECT 1\n\nendloop\n')[0].type === 'unknown');
  check('missing endloop fails', parseTest('foreach t a\n\nstatement ok\nSELECT 1\n')[0].type === 'unknown');
  check('statement maybe without message fails', parseTest('statement maybe\nSELECT 1\n')[0].type === 'unknown');
  check('statement maybe keeps its message', parseTest('statement maybe\nSELECT 1\n----\nnope\n')[0].errorSubstr === 'nope');
  check('mode skip carries into a loop body', parseTest('mode skip\n\nforeach x a\n\nstatement ok\nSELECT 1\n\nendloop\n')[0].skip === true);

  if (failures.length) {
    for (const f of failures) process.stderr.write(`SELF-TEST FAILURE: ${f}\n`);
    return Promise.resolve(1);
  }
  return selfTestRunner(cases.length, checks.length);
}

// Statement semantics through runRecords, against a fake engine: a statement
// that succeeds, one that fails with a message, one with an internal error.
async function selfTestRunner(nCases, nChecks) {
  const fake = async (sql) => {
    if (sql.includes('INTERNAL_BOOM')) throw new Error('INTERNAL Error: boom');
    if (sql.includes('BOOM')) throw new Error('Invalid Input Error: boom');
    return [];
  };
  const cases = [
    // [name, test text, expect passed, expect failed]
    ['statement error whose SQL succeeds fails', 'statement error\nSELECT 1\n----\nboom\n', 0, 1],
    ['statement error with a matching message passes', 'statement error\nSELECT BOOM\n----\nboom\n', 1, 0],
    ['statement error with another message fails', 'statement error\nSELECT BOOM\n----\ncapacity\n', 0, 1],
    ['statement error never accepts an internal error', 'statement error\nSELECT INTERNAL_BOOM\n----\nboom\n', 0, 1],
    ['statement maybe: success passes', 'statement maybe\nSELECT 1\n----\nboom\n', 1, 0],
    ['statement maybe: matching error passes', 'statement maybe\nSELECT BOOM\n----\nboom\n', 1, 0],
    ['statement maybe: other error fails', 'statement maybe\nSELECT BOOM\n----\ncapacity\n', 0, 1],
    ['statement ok with an error fails', 'statement ok\nSELECT BOOM\n', 0, 1],
    ['unknown record fails', 'onlyif wasm\nstatement ok\nSELECT 1\n', 0, 1],
  ];
  const failures = [];
  for (const [name, text, wantPass, wantFail] of cases) {
    const r = await runRecords(parseTest(text), fake, { file: 'self-test' });
    if (r.passed !== wantPass || r.failed !== wantFail) {
      failures.push(`${name}: expected ${wantPass} passed/${wantFail} failed, got ${r.passed}/${r.failed}`);
    }
  }
  if (failures.length) {
    for (const f of failures) process.stderr.write(`SELF-TEST FAILURE: ${f}\n`);
    return 1;
  }
  process.stdout.write(`sqllogic self-test: ${nCases} comparator cases, ${nChecks} parser checks and ${cases.length} statement cases passed\n`);
  return 0;
}

if (process.argv[1] === fileURLToPath(import.meta.url) && process.argv.includes('--self-test')) {
  selfTest().then((code) => process.exit(code));
}
