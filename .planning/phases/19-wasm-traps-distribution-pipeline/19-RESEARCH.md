# Phase 19: WASM Traps & Distribution Pipeline - Research

**Researched:** 2026-10-22/23
**Domain:** DuckDB C++ extension internals (table macros, table-in/out functions), WASM/Emscripten build+runtime behavior, S3/CloudFront distribution pipeline, WebAssembly binary format (custom sections)
**Confidence:** HIGH — nearly every claim below was verified by directly running the real built extension (native v2.0.0 CLI, native v1.5.6 CLI, and the actual wasm_eh/wasm_mvp artifacts from today's green CI run) rather than by reading code alone.

## Summary

This phase fixes two independent classes of bug: (1) WASM traps in `ts_fill_forward_operator` and the two zero-arg `ts_features_*` catalog functions (#289), and (2) a broken WASM distribution pipeline that double-signs artifacts and ships stale/CORS-blocked builds (#272).

For #289, I reproduced the native hang and the WASM trap directly against the real built `anofox_forecast` v2.0.0 extension, then **built and verified an exact, working replacement macro** for `ts_fill_forward_operator` using DuckDB's `COLUMNS([macro_param])` dynamic-column-selection feature — this lets the new macro keep the OLD function's exact calling convention (VARCHAR string arguments naming columns, e.g. `'store_id'`), resolves the original unescaped-identifier bug for free (DuckDB's macro substitution is AST-level, not string formatting), and honors `frequency` correctly. I ran this against reserved-word and space-containing column names and confirmed it works. **Critical finding:** the underlying `_ts_fill_forward_native` function that both the old `ts_fill_forward_by` and the new macro delegate to has its **own, separate, pre-existing WASM-only bugs** on calendar frequencies (`'1mo'`/`'1q'`/`'1y'` return wrong row counts; TIMESTAMP+`'1h'` crashes with `Maximum call stack size exceeded` and corrupts subsequent query results) — confirmed by an apples-to-apples native-v1.5.6-vs-wasm-v1.5.6 comparison using the *same* CI-built extension artifact. Daily (`'1d'`) and raw-integer-offset frequencies are unaffected. This is NOT caused by the nested-query bug this phase targets, is NOT in CONTEXT.md's locked scope, and must be called out explicitly so the Phase 19 WASM-pinned test doesn't accidentally pick a frequency that trips it.

For the `ts_features_list()`/`ts_features_config_template()` trap, I reproduced `memory access out of bounds` directly, on both `wasm_eh` and `wasm_mvp`. I also found that the existing WASM-harness skip-list entry blaming this on "TSFresh heap overflow" is **empirically wrong** — the file it skips (`test/sql/ts_features.test`) uses only 10-row series and the two trapping calls take zero arguments; the real common factor is that these are the **only two zero-argument table functions in the whole extension**, the **only call sites of the `DATAZOO_GUARD` template wrapper**, and the **only place a Rust-`malloc()`'d `char**` array is freed manually with libc `free()` instead of through a matching FFI free function** — I ruled out `DATAZOO_GUARD` itself as the likely cause by confirming anofox-statistics uses the identical wrapper on 9 working, WASM-tested functions.

For #272, I confirmed the double-`duckdb_signature`-section bug is **still live today** on get.erpl.io, found that **anofox-statistics already merged a fix** for exactly this bug (PR #193, not yet pulled into the stale local clone I initially checked) that is directly portable, and confirmed via live HTTP checks that CORS is indeed completely absent and that `v1.5.4` and `v1.5.5` are **fully orphaned** (all 7 platforms frozen since 2026-07-17 and 2026-09-26 respectively — the current 3-line CI pipeline never rebuilds them). For `wasm_mvp`'s `_setThrew is not defined`, I reproduced it directly but found it is **narrower than the issue comment implies**: it fires only on a specific deeply-nested `invoke_ii` exception-trampoline chain (hit during certain error-path assertions in `ts_diagnostics.test`/`ts_conformal.test`), and the curated 8-file WASM gate currently reports **415/415 passed** despite it — `wasm_mvp` LOAD itself succeeds with today's artifact, unlike what the original issue comment reported.

**Primary recommendation:** Delete `ts_fill_forward_operator.cpp`; replace it with a `COLUMNS([...])`-based macro in `ts_macros.cpp` (exact tested body in Code Examples); pin the WASM sqllogictest to `'1d'` or `'2d'` only (never a calendar or hourly-TIMESTAMP frequency, until the separate `_ts_fill_forward_native` WASM bug is filed and fixed); investigate the `ts_features.cpp` malloc/free + DATAZOO_GUARD pairing as the top two root-cause candidates for BUG-02 with a debug build before attempting a fix; port anofox-statistics PR #193's `extension-upload.sh` diff; add the post-upload check as a step inside the existing `deploy` job of `_extension_deploy.yml` (it already has AWS credentials — pull the uploaded object straight from S3, no need to wait on CORS); and treat DIST-03 as "verify empirically once the post-upload harness exists" rather than "fix or drop," since LOAD already succeeds.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| `ts_fill_forward_operator` SQL surface | SQL Macro (`src/macros/ts_macros.cpp`) | Table Function (`_ts_fill_forward_native`) | Macro layer owns identifier/string resolution via `COLUMNS()`; native layer owns the fill-forward algorithm. No change to tier boundaries — this phase removes a C++ table function that duplicated macro-tier responsibility. |
| `ts_features_list`/`ts_features_config_template` catalog listing | Table Function (`src/table_functions/ts_features.cpp`) | Rust FFI (`anofox_ts_features_list`) | Static catalog data generation belongs in Rust core; C++ only marshals the `char**` array. The bug is in this marshaling boundary. |
| WASM artifact signing | Build/Release tooling (`scripts/extension-upload.sh`) | CI (`_extension_deploy.yml`) | Binary post-processing belongs in the shell script; CI orchestrates invocation and (new) post-upload verification. |
| WASM runtime LOAD verification | CI (Node DuckDB-Wasm harness, `test/wasm/run.mjs`) | — | Already the established pattern (`WasmTest.yml`, `wasm-runtime-test` job) — this phase extends it to run post-upload, not just post-build. |
| get.erpl.io CORS / stale paths | Infrastructure (S3 bucket / CloudFront, owner-controlled) | — | Explicitly out of scope for code changes per CONTEXT.md; this phase only documents the exact fix. |

## User Constraints (from CONTEXT.md)

### Locked Decisions

- `ts_fill_forward_operator` keeps its public signature but becomes a SQL macro that delegates to `ts_fill_forward_by` (same args: source table name, group, date, value, end date, frequency). The C++ table function with the nested `QueryMaterialized` in InitGlobal is deleted. The macro binder takes care of identifier quoting, and `frequency` is honoured through `ts_fill_forward_by`. The macro is marked deprecated in the CHANGELOG and docs and removed in the following release (same one-release policy as #280). The `anofox_fcst_ts_fill_forward_operator` alias follows the same path.
- `ts_features_list()` / `ts_features_config_template()` traps: reproduce first in the Node DuckDB-Wasm harness (test/wasm/run.mjs), find the root cause (suspects: static table data or an FFI string buffer in no-argument table functions), then fix it.
- Done means 117 rows natively AND in DuckDB-Wasm (wasm_eh), the instance stays usable for the next statement, and both functions plus the fill-forward macro are added to the CI-gated curated WASM subset.
- `scripts/extension-upload.sh`: for WASM, replace the last 256 bytes (the zeroed placeholder signature) of the existing trailing `duckdb_signature` custom section, exactly like the native path. Never append a new section header. Guard: abort unless the artifact ends with exactly one `duckdb_signature` section.
- The post-upload check runs in CI (`.github/workflows/_extension_deploy.yml`) right after the upload: download the uploaded artifact (decompressing brotli `content-encoding: br`), assert exactly one `duckdb_signature` section, and for `wasm_eh` LOAD it in the Node DuckDB-Wasm harness. Any failure fails the deploy job. Note: the harness's @duckdb/duckdb-wasm bundles engine v1.5.6, so the LOAD check applies to the v1.5.6 line; for v2.0.0, assert the section count only and record that.
- `wasm_mvp` `_setThrew is not defined`: time-boxed root-cause investigation. If it's ours (link flags, Rust panic=abort/unwind, emscripten exception settings), fix it. If it's in DuckDB-Wasm or emsdk, stop deploying wasm_mvp to get.erpl.io (exclude it in the deploy matrix), document why, and draft an upstream issue.
- The exact S3 CORS rule (GET/HEAD from `*`), the stale old-version paths (found by listing get.erpl.io over HTTP, read-only) and the redeploy steps go in `docs/distribution/get-erpl-io.md`, plus a comment on #272 linking to it. The owner applies the bucket changes; this phase makes NO bucket changes.

### Claude's Discretion

- Exact structure of the WASM section parser (bash + python helper vs a small Node script), as long as CI can run it.
- How the harness repro for the traps is structured (a separate `.test` file vs added to an existing one).

### Deferred Ideas (OUT OF SCOPE)

- DuckDB-Wasm runtime LOAD check for v2.0.0 artifacts (needs a duckdb-wasm release bundling engine 2.0).
- Applying the CORS rule and deleting stale paths (owner action).

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| BUG-01 | `ts_fill_forward_operator` no longer runs a nested query that hangs (native) or traps (WASM) | Verified hang reproduction; verified working `COLUMNS()`-based macro replacement (exact SQL in Code Examples); flagged pre-existing `_ts_fill_forward_native` WASM frequency bug that threatens the "non-default frequency" acceptance test |
| BUG-02 | `ts_features_list()`/`ts_features_config_template()` run in WASM without trapping, return 117 rows | Verified trap reproduction on wasm_eh and wasm_mvp; narrowed root-cause candidates to malloc/free asymmetry + DATAZOO_GUARD (ruled less likely) + zero-arg-ness; found stale/wrong skip-list justification |
| DIST-01 | `extension-upload.sh` produces exactly one `duckdb_signature` section | Verified current script's exact bug; found and extracted anofox-statistics' already-merged, directly portable fix (PR #193) |
| DIST-02 | Post-upload check counts sections and LOADs in Node | Verified exact deploy-job structure (`_extension_deploy.yml`, 3 version lines); identified that the job already has AWS creds, so verification can pull straight from S3 without needing CORS fixed first |
| DIST-03 | `wasm_mvp` LOADs or `_setThrew` is documented with a decision | Reproduced the exact error live; found it is narrower than reported (LOAD succeeds; curated suite is 415/415 green despite it) — recommend re-verifying rather than assuming fix-or-drop is still needed |
| DIST-04 | CORS rule + stale paths documented | Verified live: no CORS headers at all, OPTIONS preflight 403; verified v1.5.4 and v1.5.5 are fully stale (all 7 platforms, since 2026-07-17/2026-09-26); got exact S3 bucket name from repo vars |

</phase_requirements>

## Project Constraints (from CLAUDE.md)

- DuckDB v1.4.3+ extension; Rust 1.89+ core via FFI; C++17 — no new languages (this phase touches no Rust and adds no new build-system surface beyond `extension-upload.sh`/CI YAML).
- Parallelism stays at the DuckDB GROUP BY / scalar-function layer — no custom threading or table-in/table-out. **Directly relevant**: deleting `ts_fill_forward_operator`'s custom table-in/out C++ function in favor of a SQL macro is itself an *instance* of this rule being enforced, not just respected.
- Every new SQL function must be exercised by a runnable example against the built extension before it counts as done — applies to the replacement macro.
- GSD Workflow Enforcement: file-changing work for this phase must go through `/gsd-execute-phase` (planning-only here; no edits were made).
- `make audit` forbids statistics in C++/SQL — not implicated by this phase's changes (fill-forward is data prep; the WASM/distribution fixes are build tooling).

## Standard Stack

No new external packages are introduced by this phase (see Package Legitimacy Audit — not applicable). The "stack" here is entirely existing in-repo/in-ecosystem tooling:

| Component | Version (verified) | Purpose |
|-----------|---------------------|---------|
| DuckDB macro engine (`COLUMNS([...])`, `query_table()`) | v2.0.0 and v1.5.6 (both tested) | Dynamic column-by-string-name selection — the mechanism that lets the replacement macro keep the old VARCHAR-argument calling convention |
| `@duckdb/duckdb-wasm` (npm, pinned in `test/wasm/package.json`) | `1.33.1-dev65.0` (bundles DuckDB engine v1.5.6) `[VERIFIED: test/wasm/package.json, test/wasm/node_modules/@duckdb/duckdb-wasm/package.json]` | Node-based WASM LOAD/runtime harness, reused for the post-upload check |
| Node.js | v24.13.1 (local), pinned to `node-version: 20` in CI | Runs the harness |
| `brotli` CLI | — (used by `extension-upload.sh` for wasm compression; must be present in the post-upload-check step's runner) | Decompress the just-uploaded artifact for inspection |
| `aws` CLI | — (already used by the `deploy` job) | Can fetch the just-uploaded object directly from S3 for the post-upload check, bypassing CORS/CDN entirely |

**No version verification table is needed under the npm/pip/cargo registry gate** — nothing new is installed.

## Package Legitimacy Audit

**Not applicable.** This phase installs no new external packages (npm, PyPI, crates, or otherwise). It modifies existing SQL macros, deletes a C++ table function, edits an existing shell script, and edits existing GitHub Actions YAML. `brotli`/`aws` CLI are already dependencies of the existing deploy pipeline.

## Architecture Patterns

### System Architecture Diagram — BUG-01 fix (fill-forward family)

```
SQL caller
  │
  ├─ ts_fill_forward_operator('tbl','grp','date','val', target, freq)   [NEW: SQL macro, was: C++ table fn]
  │     │
  │     ▼
  │  COLUMNS([group_col]) / COLUMNS([date_col]) / COLUMNS([value_col])  (dynamic column resolution by
  │     │                                                                 string name; replaces the old
  │     │                                                                 unescaped StringUtil::Format
  │     │                                                                 nested-query build)
  │     ▼
  │  query_table(source_table::VARCHAR)   (existing DuckDB builtin — table-by-name lookup)
  │     │
  │     ▼
  └─ _ts_fill_forward_native(TABLE, target_date, frequency)   [UNCHANGED internal function —
        │                                                       but see Common Pitfalls: has its own
        │                                                       pre-existing WASM-only bugs on
        │                                                       calendar/hourly frequencies]
        ▼
     Rust core (anofox-fcst-core) — fill-forward algorithm, frequency parsing
```

`ts_fill_forward_by` already routes through this exact same `_ts_fill_forward_native` path (unchanged) — the fix makes `ts_fill_forward_operator` share it instead of running its own nested `QueryMaterialized` SQL string.

### System Architecture Diagram — DIST-01/02 fix (upload pipeline)

```
extension-ci-tools build job
  │
  ▼
anofox_forecast.duckdb_extension.wasm   (already ends with ONE duckdb_signature section:
  │                                        metadata + 256-byte zeroed placeholder — VERIFIED
  │                                        today's artifact has exactly 1 occurrence)
  ▼
scripts/extension-upload.sh   [FIX: stop unconditionally appending a NEW section for wasm*;
  │                              truncate -s -256 + replace placeholder, same as native path]
  ▼
aws s3 cp  →  s3://get.erpl.io/<ver-path>/...   (object bytes are brotli-compressed on disk;
  │                                                content-encoding: br is response metadata only)
  ▼
[NEW] post-upload check, same `deploy` job, same matrix entry, AWS creds already in scope:
  aws s3 cp s3://get.erpl.io/<same-path> downloaded.wasm.br
  brotli -d downloaded.wasm.br
  count 'duckdb_signature' occurrences → must be exactly 1
  if matrix.duckdb_arch == 'wasm_eh' && inputs.duckdb_version == 'v1.5.6':
      node test/wasm/run.mjs --ext downloaded.wasm   (actually LOADs via the real public-facing bytes)
  else:
      record "section-count-only verified" (v2.0.0 / v1.4.5 / non-eh wasm variants)
```

Pulling from S3 directly (not over public HTTPS through get.erpl.io) means the check does **not** depend on CORS being fixed, and avoids CDN cache staleness entirely — this is new information discovered this session, not in CONTEXT.md, and meaningfully simplifies the DIST-02 design.

### Recommended changes to existing structure

```
src/macros/ts_macros.cpp         # ADD: ts_fill_forward_operator entry to ts_table_macros[] (near
                                  #      the existing ts_fill_forward_by entry, line ~438-457)
src/table_functions/
  ts_fill_forward_operator.cpp   # DELETE entirely
src/anofox_forecast_extension.cpp # REMOVE line 46: RegisterTsFillForwardOperatorFunction(loader);
CMakeLists.txt                   # REMOVE line 227: src/table_functions/ts_fill_forward_operator.cpp
test/sql/ts_fill_forward_operator.test  # REWRITE: currently tests ts_fill_forward_by only (misnamed
                                  #      content) — must test the new macro's actual VARCHAR-string
                                  #      calling convention + pin the #289 repro
test/wasm/run.mjs                # ADD ts_fill_forward_operator.test, ts_features.test (NOT
                                  #      ts_features_config.test — see Pitfall 4) to CURATED[]
                                  #      ADD/correct the SKIP_FILES entry that wrongly blames
                                  #      "heap overflow" for the real ts_features_list()/
                                  #      ts_features_config_template() bug
plugins/anofox-forecast/skills/anofox-forecast-data-prep/SKILL.md  # FIX stale line calling the
                                  #      operator a "windowed variant" (it never was)
CHANGELOG.md                     # ADD deprecation note, one-release policy, matching #280's style
scripts/extension-upload.sh      # PORT anofox-statistics PR #193's diff (see Code Examples)
.github/workflows/_extension_deploy.yml  # ADD post-upload verification step inside the `deploy` job
docs/distribution/get-erpl-io.md # NEW: CORS rule + stale-path list + redeploy steps (handover doc)
```

### Pattern: Dynamic column resolution via `COLUMNS([macro_param])`

**What:** DuckDB table macros can accept a VARCHAR parameter and use `COLUMNS([param])` to select the column whose name equals that string's *runtime* value, entirely inside the macro's AST-substituted SQL — no dynamic-SQL/`PRAGMA` trickery needed.
**When to use:** Any time an old C++ function's calling convention passed column names as string literals (not bare identifiers) and the replacement must be a pure SQL macro.
**Verified example:** see Code Examples below — tested end-to-end against the real built v2.0.0 extension, including reserved-word (`order`) and space-containing (`my date`) column names, and non-default frequency (`'2d'`).

### Anti-Patterns to Avoid

- **Nested `QueryMaterialized` inside a table function's `InitGlobal`:** this is exactly what `ts_fill_forward_operator.cpp` did (line 138) and is the root cause of both the native hang and the WASM trap per #289's own analysis. Any future internal table function must not re-enter the query engine on the same `ClientContext` from inside its own init/execute path.
- **`StringUtil::Format` with `%s` substitution of user-supplied identifiers into SQL text:** this is the unescaped-identifier bug (#289's second finding). DuckDB macro parameter substitution (AST-level) or `KeywordHelper::WriteOptionallyQuoted` are the two sanctioned alternatives; this phase uses the former.
- **Manually `free()`-ing a Rust-`malloc()`-allocated buffer instead of calling a matching FFI free function:** `anofox_ts_features_list` is the only FFI export in this codebase without a companion `anofox_free_*` function; every other array-returning FFI call (e.g., `anofox_free_warnings`) is paired. See Common Pitfalls/Pitfall 2 for why this is a leading BUG-02 root-cause candidate.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Resolve a column by its runtime string name inside a macro | Custom string-formatting of SQL text, or a new C++ table function | `COLUMNS([macro_param])` | AST-level substitution means DuckDB handles quoting/escaping automatically — verified against reserved words and spaces in this session |
| Parse a WASM module's custom sections | A hand-rolled byte offset guesser | The LEB128-based parser pattern from anofox-visualization's `strip_wasm_signature.py` (reused verbatim as the Python half of DIST-01/02's guard) | Already correct, tested, and exists specifically for this exact bug family |
| Fix the upload script's wasm handling | A fresh rewrite from CONTEXT.md's description alone | anofox-statistics PR #193's diff (exact fix, already merged and production-tested there) | Avoids re-deriving byte-offset math CONTEXT.md didn't fully specify (e.g., detecting whether an artifact already has the section vs. always assuming it does) |

**Key insight:** every one of this phase's six requirements has a *directly reusable, already-correct reference implementation* sitting in a sibling repo or in DuckDB's own macro engine — the main planning risk is not finding a fix, it's correctly porting/adapting one without silently picking up its sibling repo's slightly different assumptions (see Pitfall 5).

## Common Pitfalls

### Pitfall 1: The "non-default frequency" acceptance test can hit a *different*, pre-existing WASM bug

**What goes wrong:** ROADMAP.md's phrasing for this exact criterion requires testing "a non-default `frequency`" in the WASM harness. I found, via a controlled native-v1.5.6-vs-wasm-v1.5.6 comparison on the identical CI-built extension artifact, that `_ts_fill_forward_native` — the function both `ts_fill_forward_by` and the new macro delegate to — has **independent, pre-existing WASM-only bugs**:
- `'1mo'` (monthly): WASM returns 2 rows where native returns 4 (both v1.5.6).
- `'1q'` (quarterly): WASM returns **216** rows where native returns 4.
- `'1y'` (yearly): WASM returns 1 row where native returns 4.
- TIMESTAMP + `'1h'` (hourly): WASM raises `Maximum call stack size exceeded` and **silently corrupts subsequent query results** in the same connection (verified: a fresh quarterly query run right after the crash, with no intervening error, returned the same wrong 216-row result as when run in total isolation — so the corruption is not purely a cascading side effect, there is a second, independent bug on the quarterly path too).
- `'1d'` (daily) and raw-integer-offset (`'1'` on a BIGINT date column): correct in WASM, matching native exactly.

**Why it happens:** Unknown — not yet root-caused in this session (time-boxed). Likely candidate: the calendar-interval arithmetic DuckDB/the Rust core uses for month/quarter/year stepping behaves differently under wasm32-unknown-emscripten than under native x86_64 (possibly floating-point, `usize` width, or a date-library difference) — but this is a hypothesis, not verified.
**How to avoid:** Pin the WASM sqllogictest for `ts_fill_forward_operator`/`ts_fill_forward_by` to `'1d'` or another non-calendar, non-hourly-TIMESTAMP frequency (`'2d'` verified working) for the "non-default frequency" requirement. File a **separate** GitHub issue for the calendar/hourly WASM bug in `_ts_fill_forward_native` — it is out of CONTEXT.md's locked scope for Phase 19 and should not silently block this phase's Done criteria, but leaving it undocumented would let it resurface as a future #289-style surprise.
**Warning signs:** Any WASM-run sqllogictest assertion against `_ts_fill_forward_native`/`ts_fill_forward_by`/`ts_fill_forward_operator` using `'1mo'`/`'1q'`/`'1y'`/an hourly-or-finer TIMESTAMP frequency will either assert a wrong row count or crash with `Maximum call stack size exceeded`.

### Pitfall 2: The existing WASM skip-list's stated reason for skipping `ts_features.test` is wrong

**What goes wrong:** `test/wasm/run.mjs`'s `SKIP_FILES` map currently says: `['test/sql/ts_features.test', 'WASM heap overflow in large TSFresh feature extraction']`. I ran this file's actual first two test groups directly (bypassing the skip) and reproduced `memory access out of bounds` on exactly `ts_features_list()` and `ts_features_config_template()` — the same error #289 reports, and the same error whether the eh or mvp bundle is used. These are zero-argument catalog functions; they never touch "117 features × many series" data at all (the rest of the file's aggregate tests use only 10-row `range(1,11)` series — nowhere near a 32MB heap).
**Why it happens:** The skip-list comment appears to have been written for a different, hypothetical concern (large aggregate feature extraction) without being re-verified against what's actually failing in that file.
**How to avoid:** When implementing BUG-02's fix, update or remove this `SKIP_FILES` entry with the correct description, and verify the *rest* of `ts_features.test` (the `range(1,11)`-based aggregate tests) genuinely passes in WASM once the two catalog functions are fixed — do not assume the whole file was failing for the stated reason.
**Warning signs:** If the fix for BUG-02 is verified only by a narrow, hand-written new SQL snippet (not by un-skipping and running the existing `ts_features.test`), a regression here could slip through the CI gate unnoticed.

### Pitfall 3: `ts_features.cpp`'s two trapping functions are architectural outliers in three separate ways — don't assume any single one is "the" cause without a debug-build bisection

**What goes wrong:** Three candidate root causes co-occur only in this file, and it would be easy to "fix" the wrong one and declare victory if the fix happens to also mask the real cause:
1. `ts_features_list`/`ts_features_config_template` are the **only two zero-argument** table functions in the entire extension (`TableFunction(name, {}, ...)`); every other table function, including the 4 already-WASM-verified-working zero-and-nonzero-arg functions from the curated subset (`ts_aggregate_hierarchy`, `ts_combine_keys`, `ts_split_keys`, `ts_validate_separator`), takes real arguments.
2. `DATAZOO_GUARD` is used **only** in `ts_features.cpp` across the whole C++ source tree (`grep -rln DATAZOO_GUARD src/` → 1 file). I checked the sibling `anofox-statistics` extension, which uses the *same* `DATAZOO_GUARD` macro on 9 table functions (`ols_fit`, `ridge_fit`, `wls_fit`, etc.) that **do** take arguments and **are** in its own curated, passing WASM subset — this is evidence against `DATAZOO_GUARD` itself being the cause, but is not conclusive (zero-arg + GUARD together might still interact badly).
3. `anofox_ts_features_list`'s C++ call site manually calls libc `free()` on a `char**` array that Rust allocated with `malloc()`, with no matching FFI-provided free function — this is the only place in the codebase that does this; every other array-of-strings FFI export (e.g., `anofox_free_warnings`) is deallocated through a matching Rust-side free function.
**Why it happens:** N/A — this is a list of correlated, not yet causally disambiguated, candidates.
**How to avoid:** Before attempting a fix, get a debug/ASan-capable native build (or at minimum, a WASM build with `-g`/assertions) and bisect: (a) try a zero-arg function WITHOUT `DATAZOO_GUARD` and WITHOUT the malloc/free pattern (confirms/refutes zero-arg-ness alone), (b) try adding a proper `anofox_free_ts_features_list` FFI function and have C++ call it instead of raw `free()` (tests the allocator-mismatch theory — the most actionable, cheapest experiment to try first), (c) only reach for removing/reworking `DATAZOO_GUARD` last, given the anofox-statistics counter-evidence.
**Warning signs:** A fix that "works" only on `wasm_eh` but not `wasm_mvp` (or vice versa) suggests the real cause is the exception-handling-dependent `DATAZOO_GUARD` wrapper, not the allocator pairing.

### Pitfall 4: `test/sql/ts_features_config.test` does NOT exercise the BUG-02 functions — don't un-skip it expecting that to fix anything

**What goes wrong:** The skip-list also lists `test/sql/ts_features_config.test` under the same (wrong) "heap overflow" reason. I read this file: it tests `ts_features_config_from_json`/`ts_features_config_from_csv` exclusively — entirely unrelated functions. Un-skipping it is not required by BUG-02 and should not be conflated with the fix.
**Why it happens:** Likely copy-paste of the same (incorrect) skip reason onto a file with a similar name.
**How to avoid:** Scope BUG-02's un-skip to `test/sql/ts_features.test` only; investigate `ts_features_config.test` separately if it genuinely has its own WASM issue (not verified either way in this session — out of this phase's locked scope).

### Pitfall 5: Porting the anofox-statistics `extension-upload.sh` fix verbatim conflicts slightly with CONTEXT.md's simpler locked design

**What goes wrong:** anofox-statistics PR #193's fix (merged, see Code Examples) is **conditional**: it detects whether the wasm artifact already carries a `duckdb_signature` section (`grep -qa 'duckdb_signature'` in the tail 1024 bytes) and only then treats it like native (truncate+replace); if no section is found, it still appends one (backward-compat with older extension-ci-tools wasm artifacts). CONTEXT.md's locked decision for this phase is simpler and unconditional: "replace the last 256 bytes... Never append a new section header."
**Why it happens:** anofox-statistics needed to support both old and new extension-ci-tools wasm artifact shapes across its deploy history; this phase's CONTEXT.md decision assumes (correctly, per this session's verification — today's CI-built wasm_eh artifact has exactly 1 section before upload-script processing) that the current artifact always already has the section.
**How to avoid:** Follow CONTEXT.md's simpler, unconditional design (it is the locked decision and is empirically sufficient for the current artifact shape), but keep anofox-statistics' PR #193 as the reference/fallback pattern in case a future extension-ci-tools bump changes the artifact shape again.
**Warning signs:** If a future CI run ever produces a wasm artifact with zero `duckdb_signature` sections, the unconditional-replace approach will corrupt the file (truncating 256 bytes off a section that was never there) — this is a known, accepted risk of the simpler design, not a defect in this research.

### Pitfall 6: `wasm_mvp`'s `_setThrew` bug may already be "fixed enough" — don't over-invest in a fix-or-drop decision prematurely

**What goes wrong:** The #272 issue comment reports `wasm_mvp` fails to LOAD entirely. I reproduced `ReferenceError: _setThrew is not defined` with the exact same `@duckdb/duckdb-wasm` pin (`1.33.1-dev65.0`) and today's wasm_mvp artifact — but **LOAD itself succeeded**, and the full curated 8-file/415-assertion WASM subset passed end-to-end (`415 passed, 0 failed`) despite the error appearing in the console during `ts_conformal.test`/`ts_cv_folds.test`. The error occurs deep inside a nested `invoke_viiii → invoke_vii → invoke_vii → invoke_vii → invoke_ii` exception-trampoline chain, apparently during an error-path (`statement error`) assertion, and does not fail the overall query result the harness checks.
**Why it happens:** Not root-caused — plausibly DuckDB's C++ exception handling recovers at a layer above wherever this specific `invoke_ii` trampoline combination is needed, so the user-visible SQL error is still correct even though a JS-level `ReferenceError` was logged/thrown and caught somewhere in the stack.
**How to avoid:** Re-run the full `--all` 66-file suite (not just curated) against `wasm_mvp` once the post-upload harness exists, and treat DIST-03 as "measure current state, then decide" rather than assuming the issue comment's "fails to LOAD" framing still holds. A full fix-or-drop decision tree is pre-mature before that measurement.
**Warning signs:** If the `--all` suite shows hard failures (not just console noise) tied to this `invoke_ii` chain, the "stop shipping wasm_mvp" branch of CONTEXT.md's decision becomes the right call; if it stays cosmetic/non-fatal, documenting-and-shipping may be acceptable — this is a judgment call for the plan, not resolved by this research.

## Code Examples

### Verified working replacement for `ts_fill_forward_operator` (macro body)

Tested directly against the real built v2.0.0 `anofox_forecast` extension — correct row counts, correct frequency honoring, correct handling of reserved-word (`order`) and space-containing (`my date`) column names, no hang, no trap:

```sql
-- Add to ts_table_macros[] in src/macros/ts_macros.cpp, near the existing
-- ts_fill_forward_by entry (same file, ~line 438-457). Registering here (rather
-- than as a standalone RegisterTableFunction call) means RegisterTsTableMacros()
-- auto-generates the anofox_fcst_ts_fill_forward_operator alias for free.
{"ts_fill_forward_operator",
 {"source_table", "group_col", "date_col", "value_col", "target_date", "frequency", nullptr},
 {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_fill_forward_native(
    (SELECT COLUMNS([group_col]), COLUMNS([date_col]), COLUMNS([value_col])
     FROM query_table(source_table::VARCHAR)),
    target_date,
    frequency::VARCHAR
)
)",
 "DEPRECATED: use ts_fill_forward_by with column references instead. Forward-fills "
 "missing timestamps to a target date, resolving group/date/value columns by name.",
 "SELECT * FROM ts_fill_forward_operator('sales', 'store_id', 'sale_date', 'qty', TIMESTAMP '2024-01-10', '1d')",
 "data-preparation"},
```

Verified behavior (this session, against the built v2.0.0 extension, source: direct execution, not documentation):

```sql
-- #289's exact repro — previously hung (30s+ timeout). With the macro above:
CREATE TABLE raw_sales AS SELECT (DATE '2024-01-01' + i::INT) AS sale_date,
  i::DOUBLE AS quantity, 'S' || (i % 2) AS store_id FROM range(6) t(i);
SELECT count(*) FROM ts_fill_forward_operator('raw_sales', 'store_id', 'sale_date',
  'quantity', TIMESTAMP '2024-01-10', '1d');
-- → 15 (returns immediately; was a 30s+ hang before)

-- Reserved word + space in column name (the original escaping bug):
CREATE TABLE weird ("order" VARCHAR, "my date" DATE, val DOUBLE);
SELECT * FROM ts_fill_forward_operator('weird', 'order', 'my date', 'val', TIMESTAMP '2024-01-04', '1d');
-- → works correctly, no manual quoting needed in the macro body (COLUMNS([...]) handles it)

-- frequency IS honored (was previously hardcoded to 86400s regardless of input):
SELECT count(*) FROM ts_fill_forward_operator('raw_sales','store_id','sale_date','quantity', TIMESTAMP '2024-01-20', '2d'); -- → 20
SELECT count(*) FROM ts_fill_forward_operator('raw_sales','store_id','sale_date','quantity', TIMESTAMP '2024-01-20', '1d'); -- → 35
```

### anofox-statistics PR #193 — the directly portable `extension-upload.sh` fix

Source: `gh pr diff 193 -R DataZooDE/anofox-statistics` (merged 2026-10-14). Note Pitfall 5 — CONTEXT.md's locked design is the simpler unconditional variant of this:

```diff
-# wasm artifacts do not carry the placeholder, so the strip is gated on
-# the native-binary path only.
+# wasm artifacts from current extension-ci-tools also end with a complete
+# `duckdb_signature` custom section (metadata + zeroed 256-byte placeholder).
+# Appending a second section breaks LOAD in DuckDB-Wasm ("Unknown ABI type",
+# #192), so a wasm artifact that already has the section is treated like a
+# native one: only its placeholder is replaced. Older wasm artifacts without
+# the section still get one appended.
+has_wasm_sig_section=0
+if [[ $4 == wasm* ]] && tail -c 1024 $ext | grep -qa 'duckdb_signature'; then
+  has_wasm_sig_section=1
+fi
+
 cat $ext > $ext.append
-if [[ $4 != wasm* ]]; then
+if [[ $4 != wasm* || $has_wasm_sig_section == 1 ]]; then
   truncate -s -256 $ext.append
 fi

-if [[ $4 == wasm* ]]; then
+if [[ $4 == wasm* && $has_wasm_sig_section == 0 ]]; then
   # ... existing section-append logic, now gated to legacy artifacts only ...

 # append signature to extension binary
 cat $ext.sign >> $ext.append

+# A wasm extension must carry exactly one duckdb_signature section (#192).
+if [[ $4 == wasm* ]]; then
+  n_sig=$(tail -c 2048 $ext.append | grep -ao 'duckdb_signature' | wc -l)
+  if [[ $n_sig != 1 ]]; then
+    echo "error: $ext.append has $n_sig duckdb_signature sections, expected 1" >&2
+    exit 1
+  fi
+fi
+
 # compress extension binary
```

### anofox-visualization's `strip_wasm_signature.py` — reusable LEB128 custom-section parser

Source: `gh api repos/DataZooDE/anofox-visualization/contents/scripts/strip_wasm_signature.py` (fetched and decoded this session, reproduced verbatim below — exactly the parser needed for the Python half of the DIST-01/02 guard, or for a one-off repair of already-double-signed live artifacts):

```python
#!/usr/bin/env python3
import sys

def leb(b, i):
    r = s = 0
    while True:
        x = b[i]
        i += 1
        r |= (x & 0x7F) << s
        s += 7
        if x < 0x80:
            return r, i

def signature_sections(b):
    if b[:4] != b"\0asm":
        raise ValueError("not a wasm module")
    i, out = 8, []
    while i < len(b):
        start, sid = i, b[i]
        size, i = leb(b, i + 1)
        end = i + size
        if sid == 0:
            nl, j = leb(b, i)
            if b[j : j + nl] == b"duckdb_signature":
                out.append((start, end))
        i = end
    return out

def fix(path):
    b = open(path, "rb").read()
    sig = signature_sections(b)
    if len(sig) == 2 and sig[-1][1] == len(b):
        b = b[: sig[-1][0]]
        open(path, "wb").write(b)
        print(f"{path}: dropped the duplicate duckdb_signature section")
    n = len(signature_sections(b))
    if n != 1:
        sys.exit(f"{path}: expected 1 duckdb_signature section, found {n}")
    print(f"{path}: ok ({len(b)} bytes, 1 signature section)")

if __name__ == "__main__":
    for p in sys.argv[1:]:
        fix(p)
```

### Current live evidence of the DIST-01 bug on get.erpl.io (this session, read-only HTTP)

```
$ curl -s --compressed -o live.wasm https://get.erpl.io/v1.5.6/wasm_eh/anofox_forecast.duckdb_extension.wasm
$ python3 -c "print(open('live.wasm','rb').read().count(b'duckdb_signature'))"
2   # still broken live today; a correct artifact has exactly 1
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|---------------|--------|
| Native C++ table-in/out function for fill-forward (`ts_fill_forward_operator`) | SQL macro delegating to `_ts_fill_forward_native` via `COLUMNS()` | This phase | Removes a nested-`QueryMaterialized` hang/trap and an unescaped-identifier injection vector; `ts_fill_forward_by` already made this transition (commit `ca7d4f6`, Phase 18) — the operator was the last holdout |
| `extension-upload.sh` unconditionally appending a wasm signature section | Detect-or-assume-exists, then truncate+replace (anofox-statistics PR #193 pattern) | anofox-statistics: 2026-10-14 (merged); anofox-forecast: this phase | Fixes get.erpl.io LOAD failures for all wasm variants |
| WASM runtime verification only at build time (`wasm-runtime-test` job, pre-upload) | Also verify post-upload, against the actual deployed bytes | This phase | Catches upload-script defects that build-time testing cannot see (the exact #272 bug slipped through the existing pre-upload gate) |

**Deprecated/outdated:**
- `ts_fill_forward_operator` (public, documented): deprecated this phase, removed next release (one-release policy, matching #280's precedent in CHANGELOG.md).
- The `SKIP_FILES` entry for `test/sql/ts_features.test` citing "heap overflow": factually incorrect as of this session; must be corrected regardless of how BUG-02 is fixed.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | The root cause of BUG-02 is the malloc/free allocator mismatch and/or the zero-argument-ness of the two functions, not `DATAZOO_GUARD` itself | Pitfall 3 | If wrong, a fix targeting the malloc/free pairing alone would not resolve the trap, costing a wasted implementation cycle before the planner/executor falls back to the `DATAZOO_GUARD` hypothesis |
| A2 | The calendar-frequency `_ts_fill_forward_native` WASM bug (Pitfall 1) is unrelated to the #289 nested-query bug and should be filed as a separate issue rather than fixed in this phase | Pitfall 1 | If wrong (i.e., CONTEXT.md intended this phase to also fix it), the phase would be incomplete; low risk since CONTEXT.md's text only discusses the nested-query/identifier-escaping/frequency-ignored bugs, not calendar-arithmetic correctness |
| A3 | Pulling the post-upload verification artifact directly from S3 (via the `deploy` job's existing AWS credentials) is an acceptable substitute for fetching over public HTTPS through get.erpl.io | Architecture Patterns, DIST-02 diagram | If the team wants the check to specifically validate the CDN-served bytes (not just the S3-stored bytes), this would need adjustting — low risk, since S3 and CDN bytes are identical pre-CORS-fix (CloudFront doesn't transform wasm payloads) |

## Open Questions

1. **What is the actual root cause of the BUG-02 trap?**
   - What we know: narrowed to 3 candidates (zero-arg-ness, malloc/free allocator mismatch, `DATAZOO_GUARD`), with evidence weighing against `DATAZOO_GUARD` alone (anofox-statistics counter-example) and the `ts_features.test` skip-list reason being provably wrong.
   - What's unclear: which of the remaining two (or a combination) is the actual trigger; this needs a debug/instrumented build to fully bisect, which I could not do in a research-only, no-source-modification session.
   - Recommendation: the plan should include a Wave 0/early task that tries the matching-FFI-free-function fix first (cheapest, most isolated change) before touching the zero-arg registration pattern.

2. **Is `wasm_mvp`'s `_setThrew` bug actually "ours" or upstream, and does it matter given LOAD succeeds today?**
   - What we know: reproduced live; confined to a specific nested invoke_ii trampoline chain hit during certain error-path test assertions; does not currently fail the curated 8-file/415-assertion gate.
   - What's unclear: whether the `--all` 66-file suite surfaces a hard failure tied to this, and whether it's triggered by any currently-shipped, user-facing function (vs. only by sqllogictest's deliberate error-path probing).
   - Recommendation: run `--all` against wasm_mvp as part of this phase's verification before deciding fix vs. drop; do not assume the issue comment's framing is still accurate.

3. **Will future extension-ci-tools wasm builds ever lack the `duckdb_signature` section (making CONTEXT.md's unconditional-replace design wrong)?**
   - What we know: today's artifact always has exactly 1 section pre-upload (verified).
   - What's unclear: whether this is guaranteed stable across future DuckDB/extension-ci-tools versions.
   - Recommendation: CONTEXT.md's locked decision is fine for now; the post-upload guard (assert exactly 1 section after processing) will catch a future mismatch loudly rather than silently, so this is a self-correcting risk.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Node.js | WASM harness (`test/wasm/run.mjs`), post-upload check | ✓ | v24.13.1 (local); CI pins `node-version: 20` | — |
| `@duckdb/duckdb-wasm` npm package | WASM harness | ✓ | 1.33.1-dev65.0 (bundles DuckDB engine v1.5.6) | — |
| `aws` CLI | Deploy job, post-upload check | ✓ (already used in `_extension_deploy.yml`) | — | — |
| `brotli` CLI | Post-upload check decompression | Not verified present on the `ubuntu-latest` deploy runner by this session — **must confirm/install in the deploy job** | — | `apt-get install brotli` if missing |
| `gh` CLI | Research only (artifact download, issue/PR inspection) | ✓ | — | — |
| A native DuckDB v1.5.6 CLI, matching the wasm_eh engine version | Apples-to-apples native-vs-WASM comparisons (used extensively this session) | ✓ (downloaded ad hoc from duckdb/duckdb GitHub releases) | v1.5.6 | Not needed for the actual fix — only for verification/diagnosis |

**Missing dependencies with no fallback:** none blocking.
**Missing dependencies with fallback:** `brotli` on the deploy runner — confirm it's present (it's already used by the existing script, so almost certainly is) before adding the post-upload decompression step; install via `apt-get` if not.

## Security Domain

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-------------------|
| V2 Authentication | No | — |
| V3 Session Management | No | — |
| V4 Access Control | No | — |
| V5 Input Validation | **Yes** | The #289 bug's second finding (unescaped identifiers in `StringUtil::Format`-built SQL) is a textbook unvalidated-input-into-query-text pattern. The fix (DuckDB macro AST substitution via `COLUMNS([param])`) is the correct control — equivalent in spirit to parameterized queries, since the engine never re-parses attacker-influenced text. |
| V6 Cryptography | No new work | The extension-signing step (`openssl pkeyutl -sign`) is pre-existing and untouched by this phase's scope; only the byte-layout *around* the signature (section framing) changes. |

### Known Threat Patterns for this stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|----------------------|
| Unescaped identifier/value interpolation into dynamically-built SQL text (the original `ts_fill_forward_operator` bug) | Tampering | Use the query engine's own AST-level parameter/macro substitution instead of string formatting — exactly this phase's fix |
| Malformed/double-signed binary artifact silently accepted by a deploy pipeline | Tampering / Denial of Service | Fail-closed guard: abort the upload (or the CI job) unless the artifact's signature section count is exactly 1, both before upload (in-script) and after (post-upload CI check) — both already locked in CONTEXT.md |
| A crash/trap in a user-reachable SQL function corrupting the DuckDB-Wasm instance for subsequent, unrelated queries (observed this session with `_ts_fill_forward_native` + calendar frequencies) | Denial of Service | Not in this phase's locked scope to fix (see Pitfall 1), but should be filed as a tracked issue — an attacker-controllable crash that corrupts session state for later queries is a legitimate DoS/data-integrity concern in any multi-tenant DuckDB-Wasm deployment |

**Note:** an unrelated repository-configuration observation from this session was reported to the owner directly and is out of scope for this phase.

## Sources

### Primary (HIGH confidence — verified this session via direct tool execution)

- Direct execution against the real built `anofox_forecast` v2.0.0 extension (`./build/release/duckdb`) — native hang reproduction, macro design verification, reserved-word/frequency tests.
- Direct execution against the official DuckDB v1.5.6 CLI (downloaded from `github.com/duckdb/duckdb` releases) + the actual CI-built `v1.5.6/linux_amd64` extension artifact — apples-to-apples native comparison for Pitfall 1.
- Direct execution of the Node DuckDB-Wasm harness (`test/wasm/run.mjs` and a custom repro script built on its `bootEngine`/`startServer` pattern) against today's real `wasm_eh` and `wasm_mvp` CI artifacts (downloaded via `gh run download` from run `39846248910`).
- Live, read-only HTTP checks against `https://get.erpl.io/*` (CORS headers, signature-section byte counts, `Last-Modified` across `v1.4.5`/`v1.5.4`/`v1.5.5`/`v1.5.6`/`v2.0.0` × 7 platforms).
- `git show`/`gh pr diff`/`gh issue view` against the real `DataZooDE/anofox-statistics` and `DataZooDE/anofox-forecast` GitHub repos (issues #289, #272, #192; PR #193).
- `gh api .../contents/scripts/strip_wasm_signature.py` — exact file content from `DataZooDE/anofox-visualization`.
- Source reading of this repo's own `src/table_functions/ts_fill_forward_operator.cpp`, `src/table_functions/ts_fill_forward_native.cpp`, `src/table_functions/ts_features.cpp`, `src/macros/ts_macros.cpp`, `src/anofox_forecast_extension.cpp`, `CMakeLists.txt`, `scripts/extension-upload.sh`, `.github/workflows/_extension_deploy.yml`, `.github/workflows/MainDistributionPipeline.yml`, `.github/workflows/WasmTest.yml`, `test/wasm/run.mjs`, `test/wasm/README.md`, `datazoo-banner/include/datazoo_banner_duckdb.hpp`, `src/include/anofox_forecast_banner.hpp`.

### Secondary (MEDIUM confidence)

- CHANGELOG.md / ROADMAP.md precedent text for deprecation-note style and the exact "non-default frequency" acceptance-criterion wording.

### Tertiary (LOW confidence / unverified hypotheses, clearly marked as such above)

- The specific mechanism by which the malloc/free mismatch or zero-argument registration actually triggers `memory access out of bounds` in WASM (Pitfall 3, Open Question 1) — narrowed via elimination, not confirmed via instrumented build.
- The specific mechanism by which the `_ts_fill_forward_native` calendar-frequency bug produces exactly 216 rows for the quarterly case (Pitfall 1) — reproduced and isolated, but not root-caused.

## Metadata

**Confidence breakdown:**
- Standard stack: N/A (no new packages) — HIGH confidence there is nothing to audit here.
- Architecture (macro replacement design): HIGH — tested end-to-end against the real extension, not just read from docs.
- Architecture (upload-script fix): HIGH — ported from a merged, production-tested sibling-repo PR, not re-derived.
- Pitfalls 1, 2, 4, 5, 6: HIGH — each is a directly reproduced, isolated observation with exact commands/output captured this session.
- Pitfall 3 (BUG-02 root cause): MEDIUM — strong circumstantial narrowing, not a confirmed root cause.

**Research date:** 2026-10-22/23
**Valid until:** Short shelf-life advisory — the live get.erpl.io findings (DIST-01 bug still present, CORS absent, v1.5.4/v1.5.5 staleness) are accurate as of this session but will change the moment anyone redeploys or touches the bucket; re-verify immediately before executing DIST-01/02/04 if there is any gap between this research and plan execution. The code-level findings (BUG-01/02 root causes, Pitfall 1/3) are stable until the relevant source files change.
