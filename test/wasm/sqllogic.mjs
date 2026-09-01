// Minimal sqllogictest-subset parser + runner for the DuckDB-Wasm smoke harness.
//
// This is NOT a full sqllogictest implementation. It supports the subset the
// anofox-forecast `test/sql/*.test` files actually use:
//   - `require <ext>`            → ignored (the harness loads the extension itself)
//   - `statement ok`             → run SQL, expect success
//   - `statement error [msg]`    → run SQL, expect failure (optional substring)
//   - `query <types> [sort]`     → run SQL, compare rows after `----`
//   - `mode skip` / `mode unskip`→ skip a block of records
//   - `# ...` comments and blank-line record separators
//
// Comparison is intentionally tolerant so it is robust across the DuckDB-Wasm
// Arrow value formatting differences vs. native sqllogictest text formatting:
//   - type letter `I` → integer compare
//   - type letter `R` → float compare within ABS/REL tolerance
//   - type letter `T` (or anything else) → trimmed string compare
// Rows are compared in returned order unless `sort`/`rowsort` is present, in
// which case both sides are sorted lexicographically by their joined columns.

// Tolerances are deliberately loose enough to absorb benign native-vs-WASM
// floating-point differences (Emscripten math can differ from native by a few
// ulps) while still catching real errors. This is a smoke/regression gate, not
// a bit-exact reproduction check.
export const FLOAT_ABS_TOL = 1e-6;
export const FLOAT_REL_TOL = 1e-4;

// ---- Parsing -------------------------------------------------------------

export function parseTest(text) {
  const lines = text.split(/\r?\n/);
  const records = [];
  let i = 0;
  let skipMode = false;

  const isBlank = (l) => l.trim() === '';
  const isComment = (l) => l.trimStart().startsWith('#');

  while (i < lines.length) {
    let line = lines[i];

    if (isBlank(line) || isComment(line)) { i++; continue; }

    const tokens = line.trim().split(/\s+/);
    const kw = tokens[0];

    if (kw === 'mode') {
      if (tokens[1] === 'skip') skipMode = true;
      else if (tokens[1] === 'unskip') skipMode = false;
      i++;
      continue;
    }

    if (kw === 'require' || kw === 'require-env' || kw === 'load' || kw === 'restart') {
      // Extension/environment directives are handled by the harness, not here.
      records.push({ type: 'directive', kw, args: tokens.slice(1), skip: skipMode });
      i++;
      continue;
    }

    if (kw === 'halt') {
      records.push({ type: 'halt', skip: skipMode });
      break;
    }

    if (kw === 'statement') {
      const expectOk = tokens[1] === 'ok';
      const errorSubstr = tokens[1] === 'error' ? tokens.slice(2).join(' ').trim() : null;
      i++;
      const sql = [];
      while (i < lines.length && !isBlank(lines[i])) { sql.push(lines[i]); i++; }
      records.push({
        type: 'statement', expectOk, errorSubstr,
        sql: sql.join('\n'), skip: skipMode,
      });
      continue;
    }

    if (kw === 'query') {
      const types = tokens[1] || '';
      const sortMode = tokens[2] && /^(sort|rowsort|valuesort|nosort)$/.test(tokens[2]) ? tokens[2] : 'nosort';
      i++;
      const sql = [];
      while (i < lines.length && !isBlank(lines[i]) && lines[i].trim() !== '----') { sql.push(lines[i]); i++; }
      let expected = null;
      if (i < lines.length && lines[i].trim() === '----') {
        i++;
        expected = [];
        while (i < lines.length && !isBlank(lines[i])) { expected.push(lines[i]); i++; }
      }
      records.push({
        type: 'query', types, sortMode,
        sql: sql.join('\n'), expected, skip: skipMode,
      });
      continue;
    }

    // Unknown directive — skip the line rather than throwing.
    i++;
  }

  return records;
}

// ---- Value comparison ----------------------------------------------------

// The anofox `.test` files use the sqllogictest type letters (I/R/T) loosely —
// e.g. `query I` is used even for DOUBLE columns. Real sqllogictest compares as
// text, so we do NOT trust the type letter to force integer rounding. Instead:
// if both sides parse as finite numbers, compare with float tolerance; otherwise
// compare as trimmed strings. This is correct for ints, floats, and text alike.
function valuesEqual(_typeLetter, expected, actual) {
  let exp = String(expected).trim();
  let act = actual === null || actual === undefined ? 'NULL' : String(actual).trim();

  if (exp === 'NULL' || act === 'NULL') return exp === act;

  // DuckDB sqllogictest renders BOOLEAN differently by column type: `query I`
  // → 1/0, `query T` → true/false. The Arrow value comes back as a JS boolean
  // (→ "true"/"false"). Normalize both sides so true≡1 and false≡0.
  const normBool = (s) => {
    const l = s.toLowerCase();
    return l === 'true' ? '1' : l === 'false' ? '0' : s;
  };
  exp = normBool(exp);
  act = normBool(act);

  const a = Number(exp), b = Number(act);
  const bothNumeric = exp !== '' && act !== '' && Number.isFinite(a) && Number.isFinite(b);
  if (bothNumeric) {
    const diff = Math.abs(a - b);
    return diff <= FLOAT_ABS_TOL || diff <= FLOAT_REL_TOL * Math.max(Math.abs(a), Math.abs(b));
  }
  return exp === act;
}

// DuckDB sqllogictest lays expected values out one-value-per-line (row-major).
// Flatten actual rows the same way, formatting each cell to a comparable string.
function flattenRows(rows) {
  const out = [];
  for (const row of rows) {
    for (const cell of row) {
      if (cell === null || cell === undefined) out.push('NULL');
      else if (typeof cell === 'bigint') out.push(cell.toString());
      else out.push(cell);
    }
  }
  return out;
}

export function compareQuery(record, rows) {
  if (record.expected === null) return { ok: true }; // no expected block → existence check only

  const nCols = record.types ? record.types.length : (rows[0] ? rows[0].length : 1);
  let actualFlat = flattenRows(rows);
  // DuckDB `.test` files put a multi-column row on ONE line with columns
  // separated by TABS. Split each expected line into its columns so the value
  // stream lines up with the flattened actual cells (a single-column line with
  // no tab splits to itself).
  let expectedFlat = record.expected.flatMap((line) => line.split('\t'));

  if (record.sortMode === 'rowsort' || record.sortMode === 'sort') {
    // Group into rows, sort rows by joined text, then reflatten.
    const groupRows = (flat) => {
      const g = [];
      for (let k = 0; k < flat.length; k += nCols) g.push(flat.slice(k, k + nCols));
      return g;
    };
    const sortKey = (r) => r.map((x) => String(x)).join('');
    const ag = groupRows(actualFlat).sort((a, b) => sortKey(a).localeCompare(sortKey(b)));
    const eg = groupRows(expectedFlat).sort((a, b) => sortKey(a).localeCompare(sortKey(b)));
    actualFlat = ag.flat();
    expectedFlat = eg.flat();
  } else if (record.sortMode === 'valuesort') {
    actualFlat = actualFlat.map(String).sort();
    expectedFlat = expectedFlat.map(String).sort();
  }

  if (actualFlat.length !== expectedFlat.length) {
    return {
      ok: false,
      reason: `row/value count mismatch: expected ${expectedFlat.length} values, got ${actualFlat.length}`,
    };
  }

  for (let k = 0; k < expectedFlat.length; k++) {
    const typeLetter = record.types[k % nCols] || 'T';
    if (!valuesEqual(typeLetter, expectedFlat[k], actualFlat[k])) {
      return {
        ok: false,
        reason: `value ${k} mismatch (type ${typeLetter}): expected "${expectedFlat[k]}", got "${actualFlat[k]}"`,
      };
    }
  }
  return { ok: true };
}

// ---- Running -------------------------------------------------------------

// `runQuery(sql)` must return an array of rows, each row an array of cell values.
export async function runRecords(records, runQuery, { file, log }) {
  const result = { file, passed: 0, failed: 0, skipped: 0, failures: [] };

  for (const rec of records) {
    if (rec.skip) { result.skipped++; continue; }
    if (rec.type === 'directive' || rec.type === 'halt') continue;

    if (rec.type === 'statement') {
      try {
        await runQuery(rec.sql);
        if (rec.expectOk) result.passed++;
        else {
          result.failed++;
          result.failures.push({ sql: rec.sql, reason: 'expected error but statement succeeded' });
        }
      } catch (err) {
        if (!rec.expectOk && (!rec.errorSubstr || String(err.message || err).includes(rec.errorSubstr))) {
          result.passed++;
        } else {
          result.failed++;
          result.failures.push({ sql: rec.sql, reason: `unexpected error: ${err.message || err}` });
        }
      }
      continue;
    }

    if (rec.type === 'query') {
      try {
        // Format results through DuckDB's own ::VARCHAR rather than reading JS
        // values: duckdb-wasm's Arrow-JS extraction mis-renders DECIMAL columns
        // (returns the unscaled integer — 1.0 → "10"), whereas DuckDB's text
        // cast applies the scale ("1.0"), matching native sqllogictest output.
        // Fall back to the raw query if the wrap fails to bind (e.g. a
        // non-projectable statement).
        const inner = rec.sql.trim().replace(/;\s*$/, '');
        let rows;
        try {
          rows = await runQuery(`SELECT COLUMNS(*)::VARCHAR FROM (\n${inner}\n) AS _wrap`);
        } catch {
          rows = await runQuery(rec.sql);
        }
        const cmp = compareQuery(rec, rows);
        if (cmp.ok) result.passed++;
        else {
          result.failed++;
          result.failures.push({ sql: rec.sql, reason: cmp.reason });
        }
      } catch (err) {
        result.failed++;
        result.failures.push({ sql: rec.sql, reason: `query threw: ${err.message || err}` });
      }
      continue;
    }
  }

  if (log && result.failed > 0) {
    for (const f of result.failures) {
      log(`    ✗ ${f.reason}\n      SQL: ${f.sql.replace(/\n/g, ' ').slice(0, 160)}`);
    }
  }
  return result;
}
