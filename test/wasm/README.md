# anofox_forecast — DuckDB-Wasm Node harness

Smoke/regression test harness that proves the locally-built `.wasm` extension
**loads and runs correctly in DuckDB-Wasm under Node.js** — the thing a plain
compile+link CI leg cannot verify.

## What it validates

1. The built `.wasm` artifact loads in the DuckDB-Wasm `eh` (exception-handling)
   bundle without signature errors.
2. A curated set of 12 SQL test files passes end-to-end.
3. Optionally, the full suite (110 files) can be exercised with `--all`.

## Requirements

- Node.js ≥ 18
- A built WASM extension artifact at
  `build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm`

## Quick start

```bash
# Install pinned dependencies (one-time)
npm --prefix test/wasm install

# Run the curated green subset (default)
node test/wasm/run.mjs

# Run against an explicit artifact path
node test/wasm/run.mjs --ext path/to/anofox_forecast.duckdb_extension.wasm

# Run the full suite (7 known failing records, see below)
node test/wasm/run.mjs --all

# Run a single test file
node test/wasm/run.mjs --file test/sql/ts_forecast_by.test
```

## DEP-01 — Version-verification procedure

The `@duckdb/duckdb-wasm` npm package version does **not** equal the DuckDB
engine version it bundles. Before bumping either, verify alignment:

**Step 1 — read the DuckDB version embedded in the extension artifact:**

```bash
strings build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm \
  | grep '^v[0-9]'
```

Expected output for the current artifact: `v1.5.6`

**Step 2 — confirm the npm package bundles that same engine version:**

| `@duckdb/duckdb-wasm` npm version | Bundled DuckDB engine |
|-----------------------------------|-----------------------|
| 1.33.1-dev65.0                    | v1.5.6                |
| 1.29.0                            | v1.1.3                |

> Only the version in use is listed. Add a row when you bump the pin.

**Step 3 — confirm the extension artifact's platform tag:**

```bash
strings build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm \
  | grep -E '^wasm_(eh|mvp)$'
```

Expected: `wasm_eh`. A mismatch between the artifact platform and the bundle
variant used by the harness (`duckdb-eh.wasm`) will produce `Unknown ABI type`
or silent load failures.

**Current status (2026-10-22):** the stable build line is DuckDB v2.0.0, but
this harness stays on the v1.5.6 artifact because no `@duckdb/duckdb-wasm` npm
build bundles a v2.0.0 engine yet (the newest, 1.33.1-dev65.0, bundles
v1.5.6 — see the table above). When a duckdb-wasm build bundling v2.0.0 ships,
bump this pin, `test/wasm/package.json`, and the CI jobs that reference the
v1.5.6 wasm_eh artifact to the v2.0.0 line together.

**Bumping the pin** — when the extension moves to a new DuckDB version:

1. Update the DuckDB submodule and rebuild.
2. Find the `@duckdb/duckdb-wasm` dev-build whose bundled engine matches — the
   storage-version list embedded in `duckdb-eh.wasm` is the authoritative source.
3. Update `dependencies` in `package.json`, run `npm --prefix test/wasm install`
   to regenerate `package-lock.json`, add a row to the table above, and commit.
4. Also bump `WASM_HARNESS_ENGINE` in `scripts/verify-wasm-artifact.sh` (the
   post-upload check, #272 DIST-02) and re-verify `test/wasm/post_upload_smoke.test`
   still passes against a freshly built artifact on the new engine version
   before relying on the post-upload gate again.

## Running modes

| Command                          | What runs                                              |
|----------------------------------|--------------------------------------------------------|
| `node run.mjs`                   | 12-file curated subset (always green)                  |
| `node run.mjs --all`             | All `.test` files under `test/sql/`                    |
| `node run.mjs --file <f.test>`   | Single named file (relative to repo root)              |
| `node run.mjs --ext <path>`      | Override the artifact path (also: `ANOFOX_WASM_EXT`)  |
| `node run.mjs --bundle mvp`      | Boot the `mvp` DuckDB-Wasm bundle instead of the default `eh` (also: `ANOFOX_WASM_BUNDLE`). An unrecognized value exits 1 before booting anything. See the "wasm_mvp status (DIST-03)" section of `docs/distribution/get-erpl-io.md` for why this exists and what it found. |

Exit code 0 on success, 1 on any assertion failure or harness error.

## Full-suite status

Measured 2026-11-06 with the strict runner (below) against the v1.5.6
`wasm_eh` artifact CI built from `main` at `2a40cd9a` (DuckDB-Wasm engine
v1.5.6, `@duckdb/duckdb-wasm` 1.33.1-dev65.0).

| Metric                          | Count |
|---------------------------------|-------|
| Files tested                    | 110   |
| Files passing                   | 103   |
| Files failing                   | 6     |
| Files skipped                   | 1     |
| Passing assertions              | 4389  |
| Failing assertions              | 7     |
| Queries compared without types  | 4     |

### Skip-listed files (structurally WASM-infeasible)

| File                               | Reason                                          |
|------------------------------------|-------------------------------------------------|
| `ts_forecast_mfles_stability.test` | UNNEST not supported in the DuckDB-Wasm engine  |

`ts_features.test`, `ts_features_config.test` and `ts_fill_forward_native.test`
were skip-listed once and pass now (#289; see the comment on
`RegisterTsFeaturesListFunction` in `src/table_functions/ts_features.cpp`).

### Known failures under `--all`

None of these is in `CURATED`, and none is caused by the runner:

- **`SET threads = N` (6 records in 5 files):** `union_source.test` (twice,
  once per `foreach pio` value), `ts_agg_window.test`,
  `ts_forecast_agg_window.test`, `ts_grouping_sets.test`,
  `ts_stats_by_order_independence.test`. The `wasm_eh` engine is built without
  threads and rejects `SET threads`; the records after it in those files pass.
- **`extension_comparison.test:52`:** expects `round(mean, 1) = 159.0`; the
  mean of `100 + 2i + 10 sin(i * 3.14159 / 7)` over `i = 0..59` is 159.4
  (60 samples are not a whole number of 14-sample sine periods). The native
  runner accepts it because DuckDB compares DOUBLEs within 1%; the expected
  value is wrong.

## The sqllogic runner (`sqllogic.mjs`)

Ported from anofox-optimize (DataZooDE/anofox-optimize PR #21). What it
supports follows DuckDB's own runner, so "passed in DuckDB-Wasm" means what
"passed natively" means:

- `statement ok`, `statement error [msg]` and `statement maybe`, the expected
  message after `----` (a substring, or `<REGEX>:` matching the whole message).
  The statement itself runs; an `INTERNAL` error never satisfies an expected
  error.
- `query <types> [nosort|rowsort|valuesort] [label]`, with one row per line
  (tab-separated columns), one value per line, or `N values hashing to <md5>`.
- `foreach` / `loop` ... `endloop` (nestable; `${name}` and `{name}` replaced),
  `mode skip` / `unskip`, `halt`, `require`, `load`, `restart`.
- Any other record (`onlyif`, `skipif`, `concurrentloop`, `foreach <integral>`,
  ...) fails with its file and line rather than being skipped.

Comparison:

- Values are rendered by DuckDB (`COLUMNS(*)::VARCHAR`), with each column's
  type from `typeof(COLUMNS(*))` in the same statement. Only a query that wrap
  cannot parse or bind (PRAGMA, SHOW, ...) runs unwrapped and is compared by
  type letter; the harness prints how many did.
- The column count must equal the type string's length and the row count must
  match before any value is compared.
- Integers compare exactly (as BigInt), text and nested values exactly,
  BOOLEAN as true/false/1/0, DOUBLE/FLOAT/DECIMAL within 1e-9 relative.
- Each record has a 60 s timeout (`ANOFOX_WASM_RECORD_TIMEOUT_MS`); a timeout
  aborts the run naming the record.

`node test/wasm/sqllogic.mjs --self-test` checks the comparator, the parser
and the statement semantics against known-good and known-bad cases without
booting DuckDB. CI runs it before the suite (and in the static audits, on
every PR).

## Architecture

```
run.mjs                   # harness entry point
 ├─ bootEngine()          # loads @duckdb/duckdb-wasm eh bundle + web-worker
 ├─ startServer()         # http.createServer serves the .wasm artifact on a random port
 ├─ FORCE INSTALL … FROM  # installs the artifact from localhost into the WASM VFS
 ├─ per-file db.open()    # catalog isolation: fresh catalog for each .test file
 └─ runRecords()          # runs the parsed records (sqllogic.mjs)

sqllogic.mjs              # sqllogictest-subset parser + strict runner
 ├─ parseTest()           # records, loops expanded; unknown records kept as failures
 ├─ runRecords()          # runs records with a per-record timeout
 ├─ compareQuery()        # typed comparison, shape checks, rowsort/valuesort, hashes
 └─ selfTest()            # --self-test
```

Key implementation notes:

- **`pthreadWorker` must be `null`** for the `eh` bundle — passing the pthread
  worker causes silent hangs.
- **Rows are read by column index**, not through Arrow row objects keyed by
  name, so duplicate column names survive the column-count check.
- **Per-file `db.open()`** resets the catalog so `CREATE TABLE` state from one
  test file cannot leak into the next.
- **A `--file` that does not exist fails the run** instead of being skipped.
- **Version-agnostic server** — any HTTP request whose path ends in
  `anofox_forecast.duckdb_extension.wasm` is served the one artifact, regardless
  of the `<version>/<platform>/` path prefix DuckDB-Wasm injects.

## Sources

Pattern adapted from the verified Node approach used by
[query.farm/haybarn-extension-wasm-tester](https://github.com/query-farm/haybarn-extension-wasm-tester)
and first integrated into this project via the anofox-statistics WASM harness
(PR #131).
