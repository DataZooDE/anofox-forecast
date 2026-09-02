# anofox_forecast — DuckDB-Wasm Node harness

Smoke/regression test harness that proves the locally-built `.wasm` extension
**loads and runs correctly in DuckDB-Wasm under Node.js** — the thing a plain
compile+link CI leg cannot verify.

## What it validates

1. The built `.wasm` artifact loads in the DuckDB-Wasm `eh` (exception-handling)
   bundle without signature errors.
2. A curated set of 8 SQL test files (396 assertions) pass end-to-end.
3. Optionally, the full 66-file suite can be exercised with `--all`.

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

# Run the full 66-file suite (23 files fail on pre-existing test-suite debt — see below)
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

Expected output for the current artifact: `v1.5.5`

**Step 2 — confirm the npm package bundles that same engine version:**

| `@duckdb/duckdb-wasm` npm version | Bundled DuckDB engine |
|-----------------------------------|-----------------------|
| 1.33.1-dev64.0                    | v1.5.5                |
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

**Bumping the pin** — when the extension moves to a new DuckDB version:

1. Update the DuckDB submodule and rebuild.
2. Find the `@duckdb/duckdb-wasm` dev-build whose bundled engine matches — the
   storage-version list embedded in `duckdb-eh.wasm` is the authoritative source.
3. Update `dependencies` in `package.json`, run `npm --prefix test/wasm install`
   to regenerate `package-lock.json`, add a row to the table above, and commit.

## Running modes

| Command                          | What runs                                              |
|----------------------------------|--------------------------------------------------------|
| `node run.mjs`                   | 8-file curated subset (396 assertions, always green)   |
| `node run.mjs --all`             | All 66 `.test` files under `test/sql/`                 |
| `node run.mjs --file <f.test>`   | Single named file (relative to repo root)              |
| `node run.mjs --ext <path>`      | Override the artifact path (also: `ANOFOX_WASM_EXT`)  |

Exit code 0 on success, 1 on any assertion failure or harness error.

## Full-suite status

Measured against a **fresh `wasm_eh` artifact built locally from HEAD**
(2026-09-02) — not a stale CI artifact. DuckDB engine: v1.5.5 / `wasm_eh`.

| Metric          | Count |
|-----------------|-------|
| Files tested    | 66    |
| Files passing   | 39    |
| Files failing   | 23    |
| Files skipped   | 4     |
| Total passing assertions | 2259 |
| Total failing assertions | 184  |

### Skip-listed files (structurally WASM-infeasible)

These four files are excluded from all runs (logged, never silent):

| File                              | Reason                                                        |
|-----------------------------------|---------------------------------------------------------------|
| `ts_features.test`                | WASM heap overflow in large TSFresh feature extraction        |
| `ts_features_config.test`         | WASM heap overflow in large TSFresh feature extraction        |
| `ts_forecast_mfles_stability.test`| UNNEST not supported in DuckDB v1.5.5 WASM engine            |
| `ts_fill_forward_native.test`     | Emscripten abort trap (`___trap`) in WASM runtime            |

### Known failures in the 23 failing files — pre-existing test-suite debt

**These failures are NOT WASM-specific and NOT artifact drift.** They were
confirmed against a fresh artifact built from HEAD, so a HEAD rebuild does *not*
fix them. They are pre-existing rot in the `test/sql` suite, exposed here because
DuckDB-Wasm auto-loads the `json` extension at `LOAD anofox_forecast` and so
actually runs these files — whereas the native `unittest` runner **skips** them
on an unsatisfied `require json`, masking the same failures natively.

Root causes (a handful of roots cascade into the 184 assertion failures):

- **Stale references to removed/renamed API** — tests call functions absent from
  the current build *natively too*: `ts_backtest_auto_by` (removed; now the
  `ts_cv_folds_by` + `ts_cv_forecast_by` two-step), `ts_hydrate_features_by`,
  `ts_prepare_regression_input_by`, and `ts_validate_separator` with an
  out-of-date argument signature.
- **Cascade failures** — one failed `CREATE TABLE … AS SELECT` (because of the
  above) leaves later statements referencing a table that was never created
  (e.g. 30+ `Table with name explain_panel does not exist`).
- **Pre-existing test bugs** — `'2024-01-01'::DATE + i` where `i` is a `BIGINT`
  from `generate_series` / `range`; DuckDB only supports `DATE + INTEGER`, so
  these fail on native and WASM alike.
- **Parser syntax errors** in a few test files.

None of these files is structurally WASM-infeasible, so none is skip-listed;
they remain in the `--all` failure list as a **tracked baseline of pre-existing
test debt** to be triaged separately from the WASM harness work. CI gates on the
curated green subset until that debt is cleared.

## Architecture

```
run.mjs                   # harness entry point
 ├─ bootEngine()          # loads @duckdb/duckdb-wasm eh bundle + web-worker
 ├─ startServer()         # http.createServer serves the .wasm artifact on a random port
 ├─ FORCE INSTALL … FROM  # installs the artifact from localhost into the WASM VFS
 ├─ per-file db.open()    # catalog isolation: fresh catalog for each .test file
 └─ runRecords()          # parses and runs sqllogictest subset (sqllogic.mjs)

sqllogic.mjs              # minimal sqllogictest-subset parser + runner
 ├─ parseTest()           # statement ok/error, query <types> [sort], mode skip/unskip
 ├─ runRecords()          # drives the parsed records; reports pass/fail/skip
 └─ compareQuery()        # tolerant comparison (FLOAT_ABS_TOL=1e-6, FLOAT_REL_TOL=1e-4)
```

Key implementation notes:

- **`pthreadWorker` must be `null`** for the `eh` bundle — passing the pthread
  worker causes silent hangs.
- **`SELECT COLUMNS(*)::VARCHAR FROM (…)`** wraps query results so DuckDB
  formats `DECIMAL` columns with their scale rather than returning unscaled
  integers via Arrow-JS. The harness falls back to the raw query if the wrapper
  fails to bind.
- **Per-file `db.open()`** resets the catalog so `CREATE TABLE` state from one
  test file cannot leak into the next.
- **Version-agnostic server** — any HTTP request whose path ends in
  `anofox_forecast.duckdb_extension.wasm` is served the one artifact, regardless
  of the `<version>/<platform>/` path prefix DuckDB-Wasm injects.

## Sources

Pattern adapted from the verified Node approach used by
[query.farm/haybarn-extension-wasm-tester](https://github.com/query-farm/haybarn-extension-wasm-tester)
and first integrated into this project via the anofox-statistics WASM harness
(PR #131).
