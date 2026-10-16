# Phase 18: Correctness Regression Suite, Audit & Docs - Research

**Researched:** 2026-10-14
**Domain:** DuckDB extension test infrastructure (CMake/sqllogictest), static-analysis audit tooling (Python/grep), docs/skill verification harnesses, CHANGELOG consolidation
**Confidence:** HIGH (all load-bearing claims verified by reading source in this repo or running the already-built `unittest`/`duckdb` binaries read-only)

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

- **D-01:** Make the json extension available to the SQL test runner (statically loaded into the test/release build, e.g. via `extension_config.cmake` / `duckdb_extension_load(json)`), so the 54 of 83 `test/sql/*.test` files gated by `require json` actually run locally and in CI. Then repair the stale ones: renamed or removed functions, the DATE+BIGINT issue, and the numeric shifts from Phases 13–17, each re-pinned with a stated reason. A prior memory note says about 23 such files failed on stale API references when run under WASM. Must not change OpenSSL linkage or the WASM `LINKED_LIBS` setup.
- **D-02:** Correctness suite: `test/sql/correctness/<area>.test`, one file per area (stats, metrics, data quality, features, detection & decomposition, conformal & bootstrap, diagnostics, forecast parity). Each pins every user-facing function fixed this milestone with values taken from the checked-in reference fixtures. Not json-gated. The existing `*_reference.test` files stay.
- **D-03:** Length-only sweep: a grep-based script finds Rust tests in modules touched this milestone that only assert length/shape, and each is upgraded to fixture or closed-form values. The sweep script is kept as a check, and the success criterion is that it finds none in those modules.
- **D-04:** `scripts/audit_no_stats_in_cpp.py` scans `src/**/*.cpp` for statistical arithmetic (`sqrt`/`pow`/`log`/`exp` on data, `std::accumulate` / mean or variance loops, sorting for quantiles) and the macro SQL for statistical aggregates (`avg`, `stddev`, `quantile*`, `regr_*`, `corr`, …). It fails on any hit not in `scripts/audit_allowlist.txt`; each allow-list entry gives a file, a pattern and a justification (e.g. date arithmetic, list marshalling). It passes on HEAD.
- **D-05:** It runs in a new lightweight `.github/workflows/audit.yml` (on PRs and pushes) and as a `make audit` target. `MainDistributionPipeline.yml` is left untouched (it is the shared extension-ci-tools pipeline).
- **D-06:** Promote the per-phase snippet harnesses (`.planning/phases/1[3-7]-*/snippets/`) into one permanent `scripts/check_doc_snippets.py` (plus a `make` target). It runs every SQL snippet in `docs/` and in `plugins/anofox-forecast/skills/` (bodies and frontmatter descriptions) against the built extension. The milestone docs sweep is verified with it.
- **D-07:** The CHANGELOG gets a consolidated "Breaking changes in v0.10.0" section: an old → new table by area (stats, metrics, data quality, features, detection, conformal, diagnostics, forecasts) plus SQL signature changes (conformal `date_col` and row-per-(group, date) output, the calibration-table input, MASE/RMAE, removed jackknife+/wavelet, the 'Holt' remap, stationarity labels, `n_gaps` semantics). Plus release-notes text for the GitHub release.
- **D-08:** Fix the deferred bugs in this phase, each with a correctness-suite test:
  - WR-05: `ts_data_quality_summary` miscounts groups whose dates are all NULL (Phase 13 deferred-items);
  - `ts_coverage_by` / `ts_quantile_loss_by` pass `source` into a TABLE parameter without `query_table()` (Phase 16 deferred-items).
- **D-09:** 4 plans:
  1. json for the test runner + repairing the stale gated tests [TEST-03 prerequisite];
  2. correctness suite + length-only sweep + the D-08 fixes [TEST-02, TEST-03];
  3. audit script + allow-list + CI [RUST-10];
  4. snippet checker + docs/skills sweep + CHANGELOG breaking-changes section [DOCS-01, DOCS-02].

### Claude's Discretion

- Exact audit patterns and allow-list format, the correctness file split if an area is too large, and how the length-only sweep detects "length-only".

### Deferred Ideas (OUT OF SCOPE)

- None — the deferred bugs from Phases 13 and 16 are folded in (D-08).
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| TEST-02 | The existing length-only tests for every module touched in this milestone are upgraded to value assertions. | Don't Hand-Roll + Code Examples: reuse the existing `validation/reference/fixtures/<area>/` fixtures already loaded by the Phase-9 harness; grep-based sweep design per D-03 (Claude's discretion on exact detection heuristic — see Architecture Patterns/Pattern 1 sibling reasoning applied to Rust test bodies: a test asserting only `.len()`/`.shape()` with no value comparison is the target pattern). |
| TEST-03 | A SQL-level regression suite (`test/sql/correctness/*.test`) pins the corrected results of each user-facing function fixed in this milestone. | Architecture Patterns (Recommended Project Structure) gives the one-file-per-area layout; Pitfall 1 explains the json-gate prerequisite and its iterative-repair risk; Code Examples shows the reproducible current-state baseline (100 skipped / 4430 assertions) the suite must grow past. |
| RUST-10 | An audit script (grep-based, run in CI or as a test) confirms no statistical arithmetic remains in `src/**/*.cpp` or macro SQL beyond documented marshalling. | Summary + Code Examples (audit-script seed grep): 20 candidate files found, 3 spot-checked and confirmed `std::sort`-for-ordering (legitimate, allow-list candidates), 0 hits for stat aggregates in `src/macros/ts_macros.cpp`. Assumption A3 flags that the remaining 17 files still need reading before the allow-list is finalized. |
| DOCS-01 | `docs/` and `plugins/anofox-forecast/skills/` describe the corrected definitions (intervals source, MASE, strengths, features, diagnostics, conformal variants); every changed SQL snippet run against the built extension. | Don't Hand-Roll (reuse existing snippet-harness fence-extraction logic); Standard Stack confirms no new Python dependency is needed to merge the 5 forks into `scripts/check_doc_snippets.py`. |
| DOCS-02 | CHANGELOG / release notes list all behaviour changes that alter numerical outputs (breaking-change section). | State of the Art: confirms the CHANGELOG's `## [Unreleased] — v0.10.0` "Breaking changes" prose already substantially exists (read this session) — this requirement is a reformat-into-table-by-area task, not a from-scratch write. |
</phase_requirements>

## Summary

Phase 18 has five independent-but-sequenced jobs, matching D-09's 4-plan split (json-gate fix is really plan 1, folded conceptually with TEST-03's prerequisite): (1) make the `json` core extension statically available to the local/CI test binary so the 54/83 `require json`-gated `test/sql/*.test` files actually execute, then repair whatever they reveal; (2) write `test/sql/correctness/<area>.test`, a length-only Rust-test sweep, and fix two known-deferred bugs (WR-05, `ts_coverage_by`/`ts_quantile_loss_by`); (3) write `scripts/audit_no_stats_in_cpp.py` + an allow-list + a new lightweight `.github/workflows/audit.yml`; (4) merge the five per-phase snippet harnesses into one permanent `scripts/check_doc_snippets.py` and sweep docs/skills, plus consolidate the CHANGELOG's already-started "Breaking changes" prose into an old→new table.

The json-gating fix is low-risk and precedented: `duckdb_extension_load(json)` with no extra arguments is the exact pattern DuckDB's own `in_tree_extensions.cmake`/`bundled_extensions.cmake` use to statically build the in-tree `json` extension (present at `duckdb/extension/json`) into a test binary — it is a second, independent top-level call in `extension_config.cmake`, unrelated to the existing `anofox_forecast` `LOAD_TESTS`/`LINKED_LIBS` block, so it does not touch OpenSSL static linkage or the WASM `LINKED_LIBS` workaround for the Rust FFI archive. Running the already-built `unittest` binary confirms the current failure mode directly: **100 tests skip** with reason `require json` out of a run that otherwise passes (4430 assertions, 66 test cases) — json is `installed: true` (an extension file exists somewhere DuckDB's CLI can autoload from) but `loaded: false` in this build, and the test binary has no statically-linked `json` at all.

The RUST-10 audit's grep patterns are tractable: a first-pass grep for `sqrt(`/`pow(`/`log(`/`exp(`/`std::accumulate`/`std::sort(` across `src/**/*.cpp` currently hits 20 files, and spot-checking three of them shows every hit sampled is `std::sort` ordering rows/indices by date or group key (row marshalling), not quantile/statistics computation — consistent with the ROADMAP claim that the audit "passes on HEAD" once an allow-list exists for these legitimate sites. A parallel grep for statistical SQL aggregates (`avg(`, `stddev(`, `quantile(`, `regr_(`, `corr(`, `variance(`, `median(`) in `src/macros/ts_macros.cpp` returns **zero hits**, confirming the macro layer is already clean.

Two D-08 deferred bugs are pinned to exact, read-verified locations in `src/macros/ts_macros.cpp`: WR-05 (`ts_data_quality_summary`, lines 170-197) and the `ts_coverage_by`/`ts_quantile_loss_by` TABLE-parameter issue (lines 2490-2508). A live-binary reproduction attempt for the second bug was inconclusive — see Pitfall 3 — because Phases 16/17 are actively rebuilding the same extension binary; the fix must be re-verified against a fresh build once Phase 17 completes (which is already the documented phase-ordering rationale: Phase 18 "executes last, after Phase 17 completes, because it pins the final numbers").

**Primary recommendation:** Sequence the 4 plans as D-09 specifies (json-gate fix first, since TEST-03 depends on it; correctness-suite + D-08 fixes second; audit script third, independent; docs/snippet-checker/CHANGELOG fourth, consuming the final numbers) and treat every numeric value this phase pins as provisional until re-run against a `/gsd-execute-phase 17`-complete build — do not trust any live-query result gathered during this research against the currently-checked-out binary.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| SQL correctness regression tests | Database/Storage (`test/sql/correctness/*.test`, sqllogictest) | API/Backend (built extension under test) | Pins SQL-surface output values; runs through the same DuckDB test harness as existing `*.test`/`*_reference.test` files |
| json-extension test-gate fix | Build system (CMake, `extension_config.cmake`) | — | A build-time static-link decision, not a runtime/application-tier concern |
| Length-only Rust test sweep | Rust Core (`crates/anofox-fcst-core/src/**/*.rs` test modules) | — | Operates purely within Rust unit-test files; no FFI/C++/SQL involvement |
| No-stats-in-C++ audit script | Build/CI tooling (`scripts/*.py`, run via `make audit` and GH Actions) | API/Backend (scans `src/**/*.cpp`) | A static-analysis tool that inspects the API/Backend tier's source but itself runs at the CI/build tier |
| Snippet checker (docs/skills verification) | Build/CI tooling (`scripts/check_doc_snippets.py`) | Docs (`docs/`, `plugins/anofox-forecast/skills/`) | Executes SQL snippets from docs against the built extension; the tool lives in CI tooling, its subject matter lives in docs |
| CHANGELOG / release-notes consolidation | Docs | — | Pure documentation artifact; no code tier involved |
| D-08 bug fixes (`ts_data_quality_summary`, `ts_coverage_by`, `ts_quantile_loss_by`) | API/Backend (`src/macros/ts_macros.cpp`) | — | SQL macro layer bugs; fixes stay entirely in the macro-expansion text, no Rust/FFI change needed |

## Standard Stack

This phase adds **no new external dependencies** — every new artifact (audit script, snippet checker, correctness tests, CI workflow) is built from tools already vendored or already used elsewhere in this repo.

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| Python 3 stdlib (`re`, `os`, `subprocess`, `sys`, `pathlib`) | system Python (project already targets 3.11-3.12) | `scripts/audit_no_stats_in_cpp.py`, `scripts/check_doc_snippets.py` | `[VERIFIED: .planning/phases/16-conformal-bootstrap-correctness/snippets/run_snippets.py:16-19]` — every per-phase snippet harness this phase merges uses only `os, re, subprocess, sys`; no PyYAML or other package was needed even for markdown-fence extraction. `git grep -n "^import\|^from"` across phases 13-16's `run_snippets.py` confirms the same four-module stdlib set in all four. |
| DuckDB sqllogictest format (`test/sql/*.test`) | matches existing `test/sql/*_reference.test` files | `test/sql/correctness/<area>.test` | `[VERIFIED: project convention]` — this repo already has 83 `.test` files in this format; D-02 extends the same convention, not a new format. |
| `duckdb_extension_load()` CMake function | DuckDB submodule's own `extension/extension_build_tools.cmake:414` | json-gate fix in `extension_config.cmake` | `[VERIFIED: duckdb/extension/extension_build_tools.cmake:414-485]` — read the function signature directly; `duckdb_extension_load(json)` with no extra args triggers the in-tree default path (`duckdb/extension/json`), identical to `duckdb/.github/config/in_tree_extensions.cmake:11` and `bundled_extensions.cmake:18`. |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| `cargo test` + existing fixture loader | already present (`validation/reference/` harness, TEST-01, Phase 9) | Length-only sweep's fixed-up Rust tests | When D-03's sweep finds a Rust test asserting only `.len()`/shape, replace with a value assertion against an existing fixture file under `validation/reference/fixtures/<area>/` — do not invent a new fixture mechanism. |
| GitHub Actions reusable-workflow-free job | plain `ubuntu-latest` runner, no `uses:` to `duckdb/extension-ci-tools` | `.github/workflows/audit.yml` | D-05 explicitly wants this *not* routed through `MainDistributionPipeline.yml`'s reusable `_extension_distribution.yml` — the audit script only needs `python3` and a checkout, no DuckDB build. |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| grep-based Python audit script | `clang-tidy` custom check / AST-based scan | AST-based would catch more disguised cases (e.g., a `sqrt` hidden behind a macro) but is a much larger build/tooling investment than D-04's roadmap-stated grep approach; project's own `scripts/` directory (pre-commit, smoke_test.py) is all shell/regex-based, matching the existing tooling style. |
| Static `json` link via `duckdb_extension_load(json)` | Runtime `LOAD json;` autoinstall from the extension repository | Runtime autoinstall requires network access in CI and a writable extension cache directory — brittle for CI; static link matches how DuckDB's own LTS/latest test binaries ship json already (`bundled_extensions.cmake`). |

**Installation:**
No package installation needed — all new scripts use only the Python/Rust/CMake toolchains already present in this repo (CLAUDE.md: Python 3.11-3.12, Rust 1.89+, CMake 3.20+).

**Version verification:** N/A — no new packages. Package Legitimacy Audit is correspondingly a no-op this phase (see below).

## Package Legitimacy Audit

**No external packages are installed by this phase.** `scripts/audit_no_stats_in_cpp.py`, `scripts/check_doc_snippets.py`, and all new `test/sql/correctness/*.test` files use only Python stdlib, the existing `duckdb` CLI/`unittest` binaries, and Rust's own `cargo test` — all already present in the repo's toolchain. No `npm install`/`pip install`/`cargo add` of a new crate or package is required.

**Packages removed due to [SLOP] verdict:** none (n/a — no installs)
**Packages flagged as suspicious [SUS]:** none (n/a — no installs)

## Architecture Patterns

### System Architecture Diagram

```text
                    ┌─────────────────────────────────────────────┐
                    │   Phase 18 artifacts (this phase's output)   │
                    └─────────────────────────────────────────────┘

  [1] json-gate fix                [2] correctness suite            [3] audit                     [4] docs/snippets
  ─────────────────                ──────────────────────           ───────────                    ──────────────────
  extension_config.cmake           test/sql/correctness/*.test      scripts/audit_no_stats_         scripts/check_doc_
    + duckdb_extension_load(json)    (one file per area: stats,       in_cpp.py                       snippets.py
         │                            metrics, data quality,          │                                │
         ▼                            features, detection &           ▼                                ▼
  make test_release                   decomposition, conformal &   scans src/**/*.cpp +            runs every ```sql
    (rebuilds unittest binary)        bootstrap, diagnostics,       macro SQL in                   fence in docs/ and
         │                            forecast parity)               src/macros/ts_macros.cpp        plugins/.../skills/
         ▼                              │                             │                                (body + frontmatter)
  54/83 gated .test files now           ▼                             ▼                                │
  actually RUN (previously          values pinned from            compares hits against               ▼
  "require json" skipped)           validation/reference/          scripts/audit_allowlist.txt     against the BUILT
         │                           fixtures/<area>/*.json             │                            extension (duckdb CLI)
         ▼                              │                             ▼                                │
  repair stale expectations:            ▼                        fails CI on any un-                   ▼
  renamed/removed functions,        D-08 bug fixes folded in:     allow-listed hit;                pass-list.tsv /
  DATE+BIGINT, numeric shifts       ts_data_quality_summary        passes on HEAD                   0 FAIL required
  from Phases 13-17                 (WR-05), ts_coverage_by/            │                                │
                                     ts_quantile_loss_by                ▼                                ▼
                                          │                      .github/workflows/audit.yml      CHANGELOG.md "Breaking
                                          ▼                       + `make audit` target            changes" section ->
                                   length-only sweep (grep-                                         old->new table by area
                                   based) over Rust test
                                   modules touched this
                                   milestone -> 0 remaining
```

### Recommended Project Structure
```
scripts/
├── audit_no_stats_in_cpp.py     # NEW (D-04) — grep-based scan, exits non-zero on un-allow-listed hit
├── audit_allowlist.txt          # NEW (D-04) — one entry per (file, pattern, justification)
├── check_doc_snippets.py        # NEW (D-06) — merged from 5 per-phase snippets/run_snippets.py forks
├── pre-commit                   # existing
├── setup-hooks.sh               # existing
└── smoke_test.py                # existing
test/sql/correctness/            # NEW (D-02) — one .test file per area, not json-gated
├── stats.test
├── metrics.test
├── data_quality.test
├── features.test
├── detection_decomposition.test
├── conformal_bootstrap.test
├── diagnostics.test
└── forecast_parity.test
.github/workflows/
└── audit.yml                    # NEW (D-05) — lightweight, python3-only job, on PR+push
```

### Pattern 1: json-gate fix via a second top-level `duckdb_extension_load` call
**What:** Add `duckdb_extension_load(json)` as an independent statement in `extension_config.cmake`, alongside (not inside) the existing `anofox_forecast` block.
**When to use:** Exactly this situation — a test-only dependency on a DuckDB core extension that this extension's own build does not otherwise need at runtime.
**Example:**
```cmake
# Source: duckdb/.github/config/in_tree_extensions.cmake:11 (verified pattern, read this session)
duckdb_extension_load(json)

duckdb_extension_load(anofox_forecast
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    LOAD_TESTS
    LINKED_LIBS "$<TARGET_FILE:anofox_fcst_ffi-static>"
)
```
`register_extension`'s dedup check (`extension_build_tools.cmake:424-427`, "If extension was set already, we ignore subsequent calls") means calling `duckdb_extension_load(json)` here is safe even if some other included CMake file also tries to load it later — first call wins, no double-build.

### Anti-Patterns to Avoid
- **Re-blessing a stale gated test's expected value without checking which phase caused the shift:** Phases 10, 12, 13, and 16 each left a `deferred-items.md` entry explaining exactly why a specific SQL file's expected values are stale (see Common Pitfalls below) — read those before re-blessing a value; some shifts are intentional (e.g., `SeasonalES` now auto-detects period) and some are genuine remaining bugs (e.g., WR-05).
- **Writing the audit script's allow-list as a blanket per-file exemption:** D-04 requires "a file, a pattern and a justification" per entry — a bare filename allow-list would defeat the audit's purpose (a new, real stat-arithmetic addition to an already-allow-listed file would pass silently).
- **Routing `audit.yml` through `MainDistributionPipeline.yml`'s reusable extension-build workflow:** D-05 explicitly keeps `MainDistributionPipeline.yml` untouched; the audit needs no DuckDB build at all (pure `python3 scripts/audit_no_stats_in_cpp.py`), so a separate, fast, `ubuntu-latest`-only job is correct and avoids coupling audit speed to extension build time.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Markdown SQL-fence extraction for the snippet checker | A new regex/parser from scratch | The existing `.planning/phases/1[3-6]-*/snippets/run_snippets.py` fence-extraction regex | `[VERIFIED: .planning/phases/16-conformal-bootstrap-correctness/snippets/run_snippets.py:1-19]` — four prior phases already built and ran this logic against ```sql fences in changed `.md` files; D-06 asks to *promote* it, not reinvent it. |
| json core-extension availability for tests | A custom extension-loading shim, a runtime `INSTALL`/`LOAD json` hack in test setup, or vendoring a json parser | `duckdb_extension_load(json)` in `extension_config.cmake` | This is DuckDB's own supported mechanism (`duckdb/.github/config/in_tree_extensions.cmake`/`bundled_extensions.cmake` use the identical call); any other approach fights the build system DuckDB itself uses. |
| Statistical-arithmetic detection in C++ | A full C++ AST parser (libclang bindings) | grep/regex over `src/**/*.cpp` with an explicit allow-list | ROADMAP success criterion 3 only requires the audit to "fail when statistical arithmetic appears ... outside a documented marshalling allow-list" — a regex gate with a maintained allow-list satisfies this; the project's existing `scripts/` tooling is entirely shell/regex-based (no AST tooling precedent in this repo). |

**Key insight:** Every sub-deliverable in this phase already has a working prior-art version somewhere in `.planning/phases/{10,12,13,16}/` (deferred-items.md notes) or `.planning/phases/{13..16}/snippets/` (harness forks) — this phase's job is consolidation and closure, not net-new design.

## Common Pitfalls

### Pitfall 1: Un-gating json reveals *more* than the ~23 WASM-stale files the memory note warns about
**What goes wrong:** The `[project_wasm_suite_reveals_test_debt]` memory note says "23 files fail on stale API refs/DATE+BIGINT/cascade" under the WASM full suite, but that number was observed under WASM, not under a native build with json newly linked. The native test binary currently skips 100 *tests* (not files — a `.test` file can contain multiple `require json`-gated test blocks) across the 54 gated files; some of those 54 files may fail for reasons the WASM run never exercised (e.g., a file-local fixture that WASM's harness never reached because an earlier failure aborted the file first).
**Why it happens:** DuckDB's native sqllogictest harness aborts the rest of a `.test` FILE on first unexpected failure `[VERIFIED: project memory note, Phase 10 10-05 SUMMARY cross-ref]` — so a file with 5 gated blocks might reveal only 1 failure per run, require a fix, then reveal the next on the following run.
**How to avoid:** Plan for an iterative "un-gate, run, fix, re-run" loop per file rather than a single fix-everything pass; budget this as its own plan step (already D-09 plan 1's intent).
**Warning signs:** A file passes on the first re-run after json is linked but a later, unrelated commit in Phase 18 causes it to fail again — check whether the file ever had >1 gated block before assuming a regression.

### Pitfall 2: WR-05's silent-zero is an undercount, not an overcount, despite the deferred-items.md wording
**What goes wrong:** Reading `src/macros/ts_macros.cpp:170-197` (`ts_data_quality_summary`) shows: when a group's `date_col` values are ALL NULL, the `FILTER (WHERE date_col IS NOT NULL)` empties both input LISTs, `_ts_data_quality([], [], NULL, n_short)` returns a NULL struct (line 180 of `ts_data_quality.cpp`, `FlatVector::SetNull(result, row_idx, true); continue;`), and then every `CASE WHEN (_quality).overall_score >= 0.8 THEN 1 ELSE 0 END`-style expression evaluates its `ELSE 0` branch because `NULL >= 0.8` is `NULL`, not `TRUE`. `COUNT(*)` still counts the row (contributing to `n_total`), but it contributes `0` to all three of `n_good`/`n_fair`/`n_poor` sums and is skipped entirely by `AVG()`. Net effect: `n_good + n_fair + n_poor < n_total` for any input containing an all-NULL-dated group — the group is silently excluded from classification while inflating the denominator implicitly used by a caller computing percentages against `n_total`.
**Why it happens:** `SUM(CASE WHEN x THEN 1 ELSE 0 END)` treats SQL's three-valued logic (`UNKNOWN`) as `FALSE`, which is standard SQL behavior, but the macro author did not add an explicit NULL-handling branch (e.g., a 5th `n_unscored` bucket, or filtering the group out of `quality_data` entirely before counting).
**How to avoid:** The correctness-suite test for this area must assert `n_good + n_fair + n_poor = n_total` as an invariant on a fixture containing at least one all-NULL-dated group, not just assert the four counts independently.
**Warning signs:** Any future SQL aggregate macro using `SUM(CASE WHEN (struct_col).field ... )` on a column that can itself be a NULL struct needs the same audit — this exact pattern (`(_quality).overall_score` inside a `CASE WHEN`) is specific to `ts_data_quality_summary` but the underlying three-valued-logic trap is generic.

### Pitfall 3: Live-binary verification is unreliable while Phases 16/17 build concurrently
**What goes wrong:** This research session attempted to reproduce the `ts_coverage_by`/`ts_quantile_loss_by` TABLE-parameter bug directly against the already-built extension (`build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension`, loaded into `build/release/duckdb`). All three structurally-identical `_by` macros tested (`ts_rmae_by`, `ts_coverage_by`, `ts_quantile_loss_by` — all three call a native function whose first parameter is declared `LogicalType::TABLE`, confirmed by reading `src/table_functions/ts_metrics_native.cpp:1231-1236` for `_ts_rmae_native` and `:1523-1528` for `_ts_coverage_native`) returned `Binder Error: Macro ... does not support the supplied arguments` — including `ts_rmae_by`, which is NOT flagged as broken in any deferred-items.md. This strongly suggests the currently-loaded binary is stale or mid-rebuild relative to the checked-out `ts_macros.cpp` source (expected, per this research task's own instructions: "another agent builds there").
**Why it happens:** Phases 16 and 17 are executing on the same branch/repo concurrently with this research task; the built `.duckdb_extension` file on disk does not necessarily correspond 1:1 to the current `src/macros/ts_macros.cpp` content at any given moment.
**How to avoid:** Do not trust any live-query result gathered during Phase 18 planning/research against a binary built before Phase 17 fully lands. The D-08 bug descriptions and exact line locations in this RESEARCH.md come from reading `src/macros/ts_macros.cpp` and `src/table_functions/ts_metrics_native.cpp` directly (source-of-truth), which remains valid regardless of binary staleness — but the *reproduction* must be redone by the Phase 18 executor against a fresh build.
**Warning signs:** If `ts_rmae_by` (not in any deferred-items.md) still fails identically to `ts_coverage_by`/`ts_quantile_loss_by` against a freshly-rebuilt extension, the D-08 bug description needs revisiting — it may be a broader TABLE-parameter macro issue, not specific to the two named functions. Flagged as an Open Question below.

### Pitfall 4: `ts_mase_by`/`ts_rmae_by` use the identical `source` (bare, unwrapped) calling convention as the two "broken" macros
**What goes wrong:** `src/macros/ts_macros.cpp:2474` (`ts_mase_by`) and `:2484` (`ts_rmae_by`) both do `SELECT * FROM _ts_mase_native(source, ...)` / `_ts_rmae_native(source, ...)` — the exact same bare-`source`-into-a-TABLE-parameter pattern that the Phase 16 deferred-items.md flags as the root cause for `ts_coverage_by`/`ts_quantile_loss_by`. If `ts_mase_by`/`ts_rmae_by` genuinely work in production (Phase 13 marked STAT-06/MASE complete, and the backtest skill documents `ts_rmae_by` as a working function), then either (a) DuckDB's table-macro substitution mechanism treats a string-literal argument passed to a `TABLE`-typed parameter differently from how the deferred-items.md describes, or (b) `ts_mase_by`/`ts_rmae_by` have the same latent bug and it has simply not been exercised by any passing test yet.
**Why it happens:** Table macros expand by textual substitution of the macro's SQL body with the call-site's literal arguments; whether DuckDB's binder auto-wraps a VARCHAR constant passed to a `TABLE` parameter into an implicit `query_table()` call (sugar) — as opposed to requiring the macro author to write `query_table(source::VARCHAR)` explicitly, as `ts_data_quality_summary` does at line 184 — was not confirmed in this research session (see Open Questions).
**How to avoid:** The Phase 18 plan that fixes D-08's `ts_coverage_by`/`ts_quantile_loss_by` bug MUST first write a passing SQL test for `ts_rmae_by`/`ts_mase_by` against a fresh build to establish whether the bare-`source` pattern is sound in general, before concluding the fix is "wrap in `query_table()`" vs. some other difference between the working and broken pairs (e.g., argument count, parameter ordering, or a DuckDB version-specific binder quirk).
**Warning signs:** If the fix (wrapping `source` in `query_table(source::VARCHAR)`) is applied to `ts_coverage_by`/`ts_quantile_loss_by` and it resolves the binder error while `ts_rmae_by` continues to work unwrapped, that is strong evidence the two functions differ in some other way (e.g., argument types passed through) not yet identified by this research.

## Runtime State Inventory

N/A — this phase is not a rename/refactor/migration phase (it adds tests, an audit script, and docs; it does not rename or move existing identifiers, keys, or stored state). Skipped per the trigger condition in the verification protocol.

## Code Examples

### json-gate fix (D-01)
```cmake
# Source: duckdb/.github/config/in_tree_extensions.cmake:11 (read this session — verified pattern)
# duckdb/extension/json/ confirmed present (ls: CMakeLists.txt, include, json_common.cpp,
# json_config.py, json_deserializer.cpp) — this is an in-tree extension, no GIT_URL needed.
duckdb_extension_load(json)
```

### Confirming the current json-skip failure mode (reproduced this session, read-only)
```text
$ ./build/release/test/unittest
...
=================================================
All tests passed (100 skipped tests, 4430 assertions in 66 test cases)

Skipped tests for the following reasons:
require json: 100
```
```text
$ echo "SELECT extension_name, loaded, installed FROM duckdb_extensions() WHERE extension_name='json';" | ./build/release/duckdb
┌────────────────┬─────────┬───────────┐
│ extension_name │ loaded  │ installed │
│    varchar     │ boolean │  boolean  │
├────────────────┼─────────┼───────────┤
│ json           │ false   │ true      │
└────────────────┴─────────┴───────────┘
```

### WR-05 root cause, exact macro SQL (read this session)
```sql
-- Source: src/macros/ts_macros.cpp:170-197 (verbatim, read this session)
{"ts_data_quality_summary", {"source", "unique_id_col", "date_col", "value_col", "n_short", nullptr}, {{nullptr, nullptr}},
R"(
WITH quality_data AS (
    SELECT
        unique_id_col AS unique_id,
        _ts_data_quality(
            LIST(value_col::DOUBLE ORDER BY date_col) FILTER (WHERE date_col IS NOT NULL),
            LIST(date_col::TIMESTAMP ORDER BY date_col) FILTER (WHERE date_col IS NOT NULL),
            NULL::VARCHAR,
            n_short::INTEGER
        ) AS _quality
    FROM query_table(source::VARCHAR)
    GROUP BY unique_id_col
)
SELECT
    COUNT(*) AS n_total,
    SUM(CASE WHEN (_quality).overall_score >= 0.8 THEN 1 ELSE 0 END) AS n_good,
    SUM(CASE WHEN (_quality).overall_score >= 0.5 AND (_quality).overall_score < 0.8 THEN 1 ELSE 0 END) AS n_fair,
    SUM(CASE WHEN (_quality).overall_score < 0.5 THEN 1 ELSE 0 END) AS n_poor,
    AVG((_quality).overall_score) AS avg_score
FROM quality_data
)"
```

### `ts_coverage_by` / `ts_quantile_loss_by` suspected root cause (read this session)
```cpp
// Source: src/macros/ts_macros.cpp:2490-2508 (verbatim, read this session)
{"ts_coverage_by", {"source", "date_col", "actual_col", "lower_col", "upper_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_coverage_native(source, date_col, actual_col, lower_col, upper_col)
)",
...
{"ts_quantile_loss_by", {"source", "date_col", "actual_col", "forecast_col", "quantile", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_quantile_loss_native(source, date_col, actual_col, forecast_col, quantile)
)",
```
```cpp
// Source: src/table_functions/ts_metrics_native.cpp:1523-1528 (verbatim, read this session) —
// confirms the first parameter IS declared TABLE-typed, matching the deferred-items.md claim.
void RegisterTsCoverageNativeFunction(ExtensionLoader &loader) {
    TableFunction func(
        "_ts_coverage_native",
        {LogicalType::TABLE, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR},
        nullptr, TsCoverageNativeBind, TsCoverageNativeInitGlobal, TsCoverageNativeInitLocal);
```
Per Pitfall 4, `_ts_rmae_native` has the IDENTICAL `{LogicalType::TABLE, VARCHAR, VARCHAR, VARCHAR, VARCHAR}` signature (`src/table_functions/ts_metrics_native.cpp:1231-1233`) and the identical bare-`source` macro call — the executor must determine why one pair is reported broken and the other is not before writing the fix.

### Audit-script seed grep (ran this session, read-only; informs the allow-list starting point)
```text
$ grep -rlnE "\bsqrt\(|\bpow\(|\blog\(|\bexp\(|std::accumulate|std::sort\(" src --include="*.cpp" | wc -l
20
$ grep -noE "\bavg\(|\bstddev[a-z_]*\(|\bquantile[a-z_]*\(|\bregr_[a-z]+\(|\bcorr\(|\bvariance\(|\bmedian\(" src/macros/ts_macros.cpp | sort | uniq -c
(no output — zero hits)
```
Spot-checked 3 of the 20 flagged files (`ts_aggregate_hierarchy.cpp`, `ts_cv_split_native.cpp`, `ts_metrics_native.cpp`): every hit found was `std::sort` ordering rows/indices by date or group key — row marshalling, not statistics. These are the kind of entries the allow-list needs; the planner should NOT assume all 20 files are clean without reading each hit (only 3 of 20 were sampled this session).

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|---------------|--------|
| `test/sql/*.test` files silently skip when `json` is unavailable | `json` statically linked via `duckdb_extension_load(json)` so all 54 gated files actually run | This phase (D-01) | Assertion count jumps from the current 4430 (66 cases, 100 skipped) to a materially larger number — exact figure unknown until re-run post-fix; roadmap explicitly expects "assertion count should jump substantially" |
| CHANGELOG's "Breaking changes" section is free-form prose under `## [Unreleased] — v0.10.0` | A structured old→new table by area, plus a dedicated SQL-signature-changes subsection | This phase (D-07) | Easier for downstream users to scan; the prose content already largely exists (confirmed by reading `CHANGELOG.md` head) — this is a reformatting/consolidation task, not a from-scratch write |
| Per-phase snippet harnesses (5 independent forks under `.planning/phases/{13,14,15,16,17}-*/snippets/`) | One permanent `scripts/check_doc_snippets.py` + `make` target | This phase (D-06) | Removes the fork-per-phase maintenance burden; future phases/docs changes reuse one tool instead of re-copying `run_snippets.py` |

**Deprecated/outdated:**
- The `.planning/phases/{10,12,13,14,15,16,17}-*/snippets/run_snippets.py` forks become dead weight once `scripts/check_doc_snippets.py` exists — Phase 18's docs plan should note (not necessarily delete) that these phase-local copies are now superseded.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `duckdb_extension_load(json)` will not increase native build time or binary size enough to matter, and has no WASM-specific gotcha analogous to the Rust FFI `LINKED_LIBS` issue | Architecture Patterns / Pattern 1 | If wrong, the WASM build (which the memory notes flag as fragile — `project_extension_wasm_linked_libs.md`) could silently drop json support or add meaningful build time; the planner should add a WASM-build smoke check as a verification step for this specific sub-task, not assume parity with the native build. |
| A2 | The 23-file WASM-staleness figure from the memory note (`project_wasm_suite_reveals_test_debt.md`) is a reasonable lower bound for how many of the 54 native-gated files will need repair once json is linked natively | Pitfall 1 | If the real number is much higher (or lower), the json-gate-fix plan's scope/time estimate will be off; this was not independently re-verified this session because it requires rebuilding with json linked, which this read-only research task could not do. |
| A3 | Spot-checking 3 of 20 grep-flagged files is representative enough to conclude the RUST-10 audit "passes on HEAD" once an allow-list for `std::sort` row-ordering is added | Code Examples / audit-script seed grep | The other 17 files were not read this session; one or more could contain genuine statistical arithmetic (e.g., an un-migrated `sqrt`/`pow` computing a metric) that the planner must still grep-verify before claiming the audit passes clean. |
| A4 | `ts_rmae_by`/`ts_mase_by` actually work against a correctly-built extension (i.e., the live binder-error reproduction failures observed this session are a build-staleness artifact, not a real, broader bug) | Pitfall 3, Pitfall 4 | If `ts_rmae_by` is ALSO broken against a fresh build, D-08's scope (currently just `ts_coverage_by`/`ts_quantile_loss_by`) is too narrow, and the fix pattern (wrap `source` in `query_table()`) needs validating against a working sibling before being treated as the template. |

## Open Questions

1. **Does DuckDB's table-macro binder auto-wrap a VARCHAR literal argument passed to a `TABLE`-typed native-function parameter, or does every such macro require an explicit `query_table(source::VARCHAR)` wrap?**
   - What we know: `ts_data_quality_summary` (works, per no open-bug report) explicitly writes `FROM query_table(source::VARCHAR)`. `ts_rmae_by`/`ts_mase_by`/`ts_coverage_by`/`ts_quantile_loss_by` all instead do `FROM _ts_X_native(source, ...)` with `source` passed bare into a declared `LogicalType::TABLE` parameter — and `ts_rmae_by` is NOT reported broken while `ts_coverage_by` IS.
   - What's unclear: Whether this is a real behavioral difference between the two groups (and if so, what causes it) or whether both groups are equally broken/working and the deferred-items.md's Phase-16-discovery of the coverage/quantile_loss bug was incomplete in scope.
   - Recommendation: The Phase 18 plan covering D-08 must, as its first step, run (not just read) `ts_rmae_by` and `ts_coverage_by` side-by-side against a Phase-17-complete fresh build, before writing the fix — this determines whether the fix is "add `query_table()` to 2 macros" or "add `query_table()` to 4+ macros."

2. **What is the real failure count/content once json is statically linked and the native suite is re-run?**
   - What we know: 100 tests across 54 files currently skip; the WASM memory note observed ~23 files failing for unrelated-to-WASM reasons (stale API refs, DATE+BIGINT, cascade) under a different (WASM) build configuration.
   - What's unclear: The exact native-build failure list, since this research task could not rebuild with json linked (would require editing `extension_config.cmake` and running `make`, both out of scope for this read-only research session, and another agent is actively building on this branch).
   - Recommendation: Treat the json-gate-fix plan as "add the CMake line, rebuild, run the full suite, triage every new failure" — do not pre-estimate the failure list from the WASM number alone; they are different build configurations.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| `json` core DuckDB extension (in-tree source) | D-01 json-gate fix | ✓ (source present) | matches duckdb submodule (`duckdb/extension/json`) | — |
| Already-built `unittest`/`duckdb` binaries | Read-only verification this session | ✓ | `build/release/test/unittest`, `build/release/duckdb` | None needed — these already exist from a prior build; this research task was explicitly told not to rebuild |
| Python 3 stdlib | `scripts/audit_no_stats_in_cpp.py`, `scripts/check_doc_snippets.py` | ✓ | project targets 3.11-3.12 | — |
| GitHub Actions `ubuntu-latest` runner | `.github/workflows/audit.yml` | ✓ (used by existing workflows) | — | — |

**Missing dependencies with no fallback:** none identified.
**Missing dependencies with fallback:** none identified.

## Security Domain

`security_enforcement` is `true` in `.planning/config.json` (ASVS level 1, block on `high`), so this section is required even though Phase 18 adds no new user-facing attack surface (it is tests, a static-analysis script, and docs).

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | No | Phase adds no auth surface |
| V3 Session Management | No | Phase adds no session surface |
| V4 Access Control | No | Phase adds no access-control surface |
| V5 Input Validation | Marginal | `scripts/audit_no_stats_in_cpp.py` and `scripts/check_doc_snippets.py` read only repo-local files (`src/**/*.cpp`, `docs/`, `plugins/`) under CI control — no untrusted external input is parsed. The correctness-suite `.test` files themselves assert against fixed, checked-in fixture values (`validation/reference/fixtures/`), not external input. |
| V6 Cryptography | No | No cryptographic code touched |

### Known Threat Patterns for this stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| A new CI workflow (`audit.yml`) running untrusted PR code | Tampering/Elevation of Privilege | Keep the audit job read-only (grep/scan only, no `eval`/`exec` of repo content) and run on the default `ubuntu-latest` runner with the standard `GITHUB_TOKEN` permissions (no `secrets: inherit` needed, unlike `_extension_deploy.yml`) — mirror the existing `build-and-test-rust` job's permission scope in `MainDistributionPipeline.yml`, which uses no extra secrets. |
| Snippet checker executing SQL from docs against a live DuckDB process | Tampering | The snippets are first-party, checked-in docs content (not user-supplied at runtime) — same trust boundary as the existing per-phase `run_snippets.py` harnesses, which already execute doc SQL against the built extension shell without incident across 5 prior phases. |

## Sources

### Primary (HIGH confidence — read directly this session)
- `duckdb/extension/extension_build_tools.cmake:414-489` — `duckdb_extension_load` function definition and parameter semantics
- `duckdb/.github/config/in_tree_extensions.cmake:11`, `duckdb/.github/config/bundled_extensions.cmake:18` — precedent for `duckdb_extension_load(json)` with no extra args
- `duckdb/extension/json/` directory listing — confirms in-tree json source presence
- `extension_config.cmake` (this repo) — current anofox_forecast-only load block
- `Makefile`, `.github/workflows/MainDistributionPipeline.yml` — current build/CI structure, confirms no existing json handling and no existing audit job
- `src/macros/ts_macros.cpp:160-197, 2471-2508` — `ts_data_quality_summary`, `ts_mase_by`, `ts_rmae_by`, `ts_coverage_by`, `ts_quantile_loss_by` macro bodies
- `src/table_functions/ts_data_quality.cpp:150-224` — `_ts_data_quality`/`RegisterTsDataQualitySummaryFunction` (placeholder/TODO at line 223)
- `src/table_functions/ts_metrics_native.cpp:1230-1236, 1523-1528` — `_ts_rmae_native`/`_ts_coverage_native` TABLE-parameter signatures
- `.planning/phases/{10,12,13,16}-*/deferred-items.md` — source of D-08 bug claims and the RUST-10/Phase-18 routing notes
- `.planning/phases/{13,14,15,16}-*/snippets/run_snippets.py` — existing snippet-harness implementations (import list, docstring)
- `scripts/` directory listing — confirms no pre-existing audit script, confirms existing tooling style (shell/regex)
- Live, read-only command output from `build/release/test/unittest` and `build/release/duckdb` (this session) — confirms the 100-skip/json-unloaded state and the inconclusive macro-binder reproduction (Pitfall 3/4)

### Secondary (MEDIUM confidence)
- CHANGELOG.md head section (`## [Unreleased] — v0.10.0 Statistical Correctness`) — confirms D-07's target content substantially already exists as prose
- `docs/`, `plugins/anofox-forecast/skills/anofox-forecast-models/SKILL.md` frontmatter — confirms docs/skills structure D-06 must sweep

### Tertiary (LOW confidence)
- None — every claim above was either read from source this session or is an explicit `[ASSUMED]` in the Assumptions Log.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — no new packages; every tool/pattern cited was read from source this session
- Architecture: HIGH — json-gate fix pattern matches DuckDB's own in-tree convention, read directly
- Pitfalls: MEDIUM-HIGH — WR-05 root cause fully traced and quoted verbatim; the `ts_coverage_by`/`ts_rmae_by` discrepancy (Pitfall 4) is HIGH-confidence on the code-level signature match but the live reproduction is explicitly inconclusive (documented, not hidden)

**Research date:** 2026-10-14
**Valid until:** Re-verify immediately after Phase 17 completes and a fresh build exists — the two live-binary findings in this research (Pitfall 1's skip count, Pitfall 3/4's binder errors) are tied to a specific, in-flux build state and should not be treated as stable beyond this session.

## RESEARCH COMPLETE
