# Phase 18: Correctness Regression Suite, Audit & Docs - Pattern Map

**Mapped:** 2026-10-14
**Files analyzed:** 12 (new/modified, per D-09's 4-plan split)
**Analogs found:** 10 / 12

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|---|---|---|---|---|
| `extension_config.cmake` (add `duckdb_extension_load(json)`) | config | request-response (build-time) | existing block in same file (`extension_config.cmake`) + `duckdb/.github/config/in_tree_extensions.cmake:11` | exact (same CMake function, verified upstream precedent) |
| `test/sql/*.test` (repair ~23-54 stale gated files) | test | request-response | the files themselves (pre-existing, same format as `test/sql/ts_stats_reference.test`) | exact |
| `test/sql/correctness/stats.test` | test | CRUD (SELECT value-assertions) | `test/sql/ts_stats_reference.test` | exact |
| `test/sql/correctness/metrics.test` | test | request-response | `test/sql/ts_metrics_reference.test` | exact |
| `test/sql/correctness/data_quality.test` | test | request-response | `test/sql/ts_data_quality_reference.test` | exact |
| `test/sql/correctness/features.test`, `detection_decomposition.test`, `conformal_bootstrap.test`, `diagnostics.test`, `forecast_parity.test` | test | request-response | `test/sql/ts_stats_reference.test` (structural template); `validation/reference/fixtures/<area>/*.json` (value source) | role-match |
| Rust length-only sweep targets (various `crates/anofox-fcst-core/src/**/*.rs` test modules) | test | transform | existing fixture-based tests in same crate (use `validation/reference/` loader pattern, TEST-01/Phase 9) | role-match |
| `scripts/audit_no_stats_in_cpp.py` | utility | batch (static scan) | `.planning/phases/16-conformal-bootstrap-correctness/snippets/run_snippets.py` (script shape: stdlib-only, subprocess/regex-driven, exit-code gate) | role-match |
| `scripts/audit_allowlist.txt` | config | — | none in-repo (new format); Claude's discretion per CONTEXT | no analog |
| `.github/workflows/audit.yml` | config (CI) | event-driven | `.github/workflows/MainDistributionPipeline.yml` (job/trigger shape), but intentionally NOT routed through its reusable `_extension_distribution.yml` per D-05 | role-match (structure only, not reuse) |
| `scripts/check_doc_snippets.py` | utility | batch (file-I/O + subprocess) | `.planning/phases/16-conformal-bootstrap-correctness/snippets/run_snippets.py` (full merge target — same fence-extraction/dependency-closure/run-against-binary logic) | exact |
| `CHANGELOG.md` (restructure Breaking-changes section) | config/docs | transform | same file's existing `## [Unreleased] — v0.10.0 Statistical Correctness` prose block | exact |
| `src/macros/ts_macros.cpp` (D-08 fixes: `ts_data_quality_summary`, `ts_coverage_by`, `ts_quantile_loss_by`) | config (SQL macro layer) | CRUD | same file, `ts_rmae_by`/`ts_mase_by` (working sibling macros using `query_table()` explicitly where correct) | exact |

## Pattern Assignments

### `extension_config.cmake` (config, build-time)

**Analog:** `extension_config.cmake` itself (existing `anofox_forecast` block) + upstream `duckdb/.github/config/in_tree_extensions.cmake:11`

**Current full content to extend** (verbatim, read this session):
```cmake
# This file is included by DuckDB's CMake build system to load this extension

duckdb_extension_load(anofox_forecast
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    LOAD_TESTS
    LINKED_LIBS "$<TARGET_FILE:anofox_fcst_ffi-static>"
)
```

**Pattern to add** (independent top-level call, before or after the existing block — order does not matter per `register_extension`'s dedup-by-first-call semantics):
```cmake
# json is required at test-time only (54/83 test/sql/*.test files use `require json`
# to assert JSON-typed intermediate results). Statically link the in-tree json
# extension so the local/CI unittest binary has it; this does not touch
# anofox_forecast's own OpenSSL static linkage or WASM LINKED_LIBS setup above.
duckdb_extension_load(json)
```
Do NOT nest this inside the `anofox_forecast` block or add `LINKED_LIBS`/`LOAD_TESTS` to it — it is a plain in-tree default-path load, identical to upstream's own `in_tree_extensions.cmake:11`.

---

### `test/sql/correctness/<area>.test` (test, request-response)

**Analog:** `test/sql/ts_stats_reference.test` (full file read this session)

**Header pattern** (lines 1-9):
```sql
# name: test/sql/correctness/<area>.test
# description: <area> value assertions against reference fixtures (Phase 18, TEST-02/TEST-03)
# group: [sql]
#
# Non-json-gated so it runs on every native build. Literals below are copied from
# validation/reference/fixtures/<area>/<fixture>.json via:
#   python3 -c "import json; d=json.load(open('validation/reference/fixtures/<area>/<fixture>.json')); print(d['vectors']['<key>'])"

require anofox_forecast
```

**Core value-pin pattern** (same file, body): `CREATE TABLE ... AS SELECT * FROM (VALUES (DATE '...' + INTERVAL (n) DAY, v), ...) t(ds, v);` then a `query I` / value-assertion block comparing the SQL function's output to the literal fixture numbers — not a `require json` block, and not a length/shape-only assertion.

**Key difference from existing `*_reference.test` files:** correctness/ files must cover every user-facing function *fixed this milestone* (Phases 9-17), not just the subset `ts_stats_reference.test` already covers — use `validation/reference/fixtures/{moments,r_entropy,gap_counts,r_stl_strength,quality_scores,r_mase,tsfresh_features,numpy_detrend,peaks,changepoints,r_decomposition,period_detection,conformal,bootstrap,r_forecast,r_diagnostics}/*.json` as the value source per area, matching D-02's split (stats, metrics, data quality, features, detection & decomposition, conformal & bootstrap, diagnostics, forecast parity).

**Existing `*_reference.test` files stay untouched** (per CONTEXT D-02) — correctness/ is additive, not a replacement.

---

### `scripts/audit_no_stats_in_cpp.py` and `scripts/check_doc_snippets.py` (utility, batch)

**Analog:** `.planning/phases/16-conformal-bootstrap-correctness/snippets/run_snippets.py` (read in full this session, 220+ lines)

**Imports pattern** (lines 1-19 of analog):
```python
#!/usr/bin/env python3
"""<docstring>"""
import os
import re
import subprocess
import sys

SNIP_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = subprocess.run(
    ["git", "rev-parse", "--show-toplevel"], cwd=SNIP_DIR, capture_output=True, text=True, check=True
).stdout.strip()
```
Reuse this exact `REPO_ROOT`-via-`git rev-parse` idiom in both new scripts so they are runnable from any cwd (CI checkout root or local dev tree) — matches the project's existing shell/regex tooling style (no PyYAML, no argparse framework beyond stdlib).

**Regex-gate / scan pattern** (analog lines ~85-105, `extract_sql_blocks`): a compiled regex constant at module scope, a single pass over `lines = text.splitlines()`, accumulate matches, return structured tuples — directly reusable for `check_doc_snippets.py`'s fence-extraction (`FENCE_RE`, `extract_sql_blocks`) and adaptable for `audit_no_stats_in_cpp.py`'s line-by-line regex scan over `src/**/*.cpp`.

**Subprocess-execute-and-check pattern** (analog lines ~130-142, `run_block`):
```python
def run_block(sql_text, timeout=120):
    try:
        result = subprocess.run(
            [DUCKDB_BIN, "-unsigned", "-bail", ":memory:"],
            input=sql_text, cwd=REPO_ROOT, capture_output=True, text=True, timeout=timeout,
        )
        return result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired as e:
        return 124, "", f"TIMEOUT after {timeout}s"
```
`check_doc_snippets.py` should reuse this verbatim (it already is the tool that runs SQL against the built extension shell, per D-06's "merge the five per-phase harnesses"). `audit_no_stats_in_cpp.py` has no subprocess step (pure file read + regex), but should mirror the same exit-code contract: 0 on clean, 1 on any un-allow-listed hit, printed diagnostics to stderr.

**Exit-code / CI-gate contract:** analog's docstring states "Exit code 1 if any FAIL row, else 0." Both new scripts must follow this so `make audit` / `make check-docs` and the CI workflow can gate on process exit code alone.

**Allow-list pattern for `audit_no_stats_in_cpp.py`:** no existing allow-list file exists in this repo (new format, Claude's discretion per CONTEXT D-04/RESEARCH). Recommended minimal format echoing the project's own `scripts/` comment style and the one used in `extension_config.cmake`'s inline rationale comments:
```text
# file | pattern | justification
src/table_functions/ts_cv_split_native.cpp | std::sort | orders fold boundaries by date index, not a statistic
src/table_functions/ts_metrics_native.cpp | std::sort | orders rows by group/date key for output marshalling
```
Each entry must carry file + pattern + justification per D-04 — a bare-filename allow-list is an explicit anti-pattern (RESEARCH.md "Anti-Patterns to Avoid").

---

### `.github/workflows/audit.yml` (config/CI, event-driven)

**Analog (structure only — explicitly NOT reused as a dependency):** `.github/workflows/MainDistributionPipeline.yml` (read lines 1-60 this session)

**Trigger/permissions pattern to copy** (lines 5-9, adapted — drop the `id-token: write`/OIDC permission since no secrets are needed):
```yaml
name: No-Stats-In-C++ Audit
on:
  push:
    branches: [main]
  pull_request:
  workflow_dispatch:

permissions:
  contents: read
```
**Job body — do NOT use `uses: duckdb/extension-ci-tools/...` or `./.github/workflows/_extension_distribution.yml`** (per D-05, this must stay independent of `MainDistributionPipeline.yml`'s reusable extension-build workflow). Instead a plain inline job:
```yaml
jobs:
  audit:
    name: Audit no-stats-in-C++
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: '3.12'
      - run: python3 scripts/audit_no_stats_in_cpp.py
```

---

### `Makefile` (`make audit` / `make check-docs` targets)

**Analog:** existing `fmt`/`check`/`header` targets (lines 1-29, read this session):
```makefile
.PHONY: rust rust_debug rust_test fmt check header benchmark-setup benchmark-m4 benchmark-m5

fmt:
	cargo fmt

check:
	cargo check
	cargo clippy
```
**Pattern to add** — new `.PHONY` entries following the same flat, one-command-per-target style (no multi-line shell scripts inline; delegate to the Python scripts):
```makefile
.PHONY: audit check-docs
audit:
	python3 scripts/audit_no_stats_in_cpp.py

check-docs:
	python3 scripts/check_doc_snippets.py
```

---

### `src/macros/ts_macros.cpp` (D-08 fixes)

**Analog (working sibling pattern):** same file — the `ts_data_quality_summary` macro's explicit `FROM query_table(source::VARCHAR)` wrap (verbatim, lines 170-197 of this file, read via RESEARCH.md citation this session):
```sql
FROM query_table(source::VARCHAR)
```
**Broken pattern to fix** (same file, lines 2490-2508, verbatim):
```cpp
{"ts_coverage_by", {"source", "date_col", "actual_col", "lower_col", "upper_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_coverage_native(source, date_col, actual_col, lower_col, upper_col)
)",
```
**Required first step before writing the fix** (per RESEARCH.md Pitfall 4/Open Question 1): run `ts_rmae_by`/`ts_mase_by` (same bare-`source` pattern, same `LogicalType::TABLE`-typed native-function signature at `src/table_functions/ts_metrics_native.cpp:1230-1236`) against a fresh Phase-17-complete build to determine whether the fix scope is 2 macros or 4+. Do not apply `query_table(source::VARCHAR)` to `ts_coverage_by`/`ts_quantile_loss_by` without this check — if `ts_rmae_by` also fails fresh, the same wrap must be applied there too and WR-05's correctness-suite test extended accordingly.

**WR-05 fix location** (same file, lines 170-197, `ts_data_quality_summary`): the three `SUM(CASE WHEN (_quality).overall_score ... THEN 1 ELSE 0 END)` branches undercount when `(_quality)` is a NULL struct (all-NULL-dated group). Fix must preserve the invariant `n_good + n_fair + n_poor = n_total` — the correctness-suite test (`test/sql/correctness/data_quality.test`) must assert this invariant directly on a fixture containing an all-NULL-dated group, per RESEARCH.md Pitfall 2.

---

### `CHANGELOG.md` (restructure)

**Analog:** the file's own existing prose (lines 1-20+, read this session) under `## [Unreleased] — v0.10.0 Statistical Correctness` / `### Breaking changes`. D-07 is reformat-not-rewrite: keep the existing narrative bullets, but add a structured old→new table by area (stats, metrics, data quality, features, detection, conformal, diagnostics, forecasts) plus a dedicated SQL-signature-changes subsection enumerating: conformal `date_col` + row-per-(group,date) output, calibration-table input, MASE/RMAE, removed jackknife+/wavelet, 'Holt' remap, stationarity labels, `n_gaps` semantics.

## Shared Patterns

### stdlib-only Python tooling style
**Source:** `.planning/phases/{13,14,15,16}-*/snippets/run_snippets.py` (consistent across all 4 forks: `os, re, subprocess, sys` only)
**Apply to:** `scripts/audit_no_stats_in_cpp.py`, `scripts/check_doc_snippets.py`
No new pip dependency; matches `scripts/pre-commit`, `scripts/setup-hooks.sh`, `scripts/smoke_test.py` (existing shell/regex style).

### Non-json-gated SQL test file header
**Source:** `test/sql/ts_stats_reference.test` lines 1-9
**Apply to:** All `test/sql/correctness/*.test` files
```sql
# name: test/sql/correctness/<area>.test
# description: <...>
# group: [sql]
require anofox_forecast
```
Deliberately omit `require json` (D-02: correctness suite is not json-gated).

### CI job independence from the shared extension-build reusable workflow
**Source:** `.github/workflows/MainDistributionPipeline.yml` jobs (`uses: duckdb/extension-ci-tools/.github/workflows/_extension_distribution.yml@...`)
**Apply to:** `.github/workflows/audit.yml` — explicitly must NOT follow this `uses:` pattern; stays a flat `ubuntu-latest` job with no DuckDB build step (D-05).

## No Analog Found

| File | Role | Data Flow | Reason |
|---|---|---|---|
| `scripts/audit_allowlist.txt` | config | — | No allow-list file format exists elsewhere in this repo; format is Claude's discretion per CONTEXT.md. Recommended `file \| pattern \| justification` pipe-delimited lines (see Pattern Assignments above), matching the project's inline-comment-rationale style used in `extension_config.cmake`. |
| Length-only Rust test sweep script (the grep heuristic itself) | utility | batch | No prior sweep script exists; D-03 leaves "how the length-only sweep detects 'length-only'" to Claude's discretion. Suggested approach: grep Rust test functions in modules touched this milestone for `assert_eq!(result.len()` / `.shape()` with no sibling value-comparison assertion in the same `#[test] fn` body — a textual heuristic in the same grep-based style as `audit_no_stats_in_cpp.py`, not a new AST tool. |

## Metadata

**Analog search scope:** `test/sql/*.test`, `.planning/phases/{13,14,15,16}-*/snippets/`, `extension_config.cmake`, `Makefile`, `.github/workflows/*.yml`, `src/macros/ts_macros.cpp`, `CHANGELOG.md`, `scripts/`
**Files scanned:** ~15 read/grepped directly this session
**Pattern extraction date:** 2026-10-14

## PATTERN MAPPING COMPLETE
