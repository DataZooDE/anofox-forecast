---
phase: 19-wasm-traps-distribution-pipeline
reviewed: 2026-10-23T06:06:40Z
depth: standard
files_reviewed: 20
files_reviewed_list:
  - scripts/extension-upload.sh
  - scripts/extension-verify-upload.sh
  - scripts/verify-wasm-artifact.sh
  - scripts/wasm_signature_sections.py
  - .github/workflows/_extension_deploy.yml
  - .github/workflows/audit.yml
  - Makefile
  - CMakeLists.txt
  - crates/anofox-fcst-ffi/src/lib.rs
  - crates/anofox-fcst-ffi/tests/features_list_ffi.rs
  - src/include/anofox_fcst_ffi.h
  - src/table_functions/ts_features.cpp
  - src/macros/ts_macros.cpp
  - src/anofox_forecast_extension.cpp
  - src/include/anofox_forecast_extension.hpp
  - test/sql/ts_features.test
  - test/sql/ts_fill_forward_operator.test
  - test/sql/ts_fill_forward_native.test
  - test/wasm/run.mjs
  - test/wasm/post_upload_smoke.test
findings:
  critical: 0
  warning: 3
  info: 3
  total: 6
status: issues_found
fix_applied:
  fixed_at: 2026-10-23
  fix_scope: critical_warning
  fixed: 3
  deferred: 0
  skipped: 0
  commits:
    - "fced2a1 fix(19-review): WR-01 harden extension-upload.sh to set -euo pipefail"
    - "8640a1b fix(19-review): WR-03 pin the payload_len >= 256 boundary in wasm_signature_sections.py self-test"
    - "6f19875 fix(19-review): WR-02 cross-reference the ts_fill_forward_native WASM coverage gap"
---

# Phase 19: Code Review Report

**Reviewed:** 2026-10-23T06:06:40Z
**Depth:** standard
**Files Reviewed:** 20
**Status:** issues_found

## Summary

This phase (#272, #289) reworks the extension's WASM signature-section handling, adds a
post-upload S3 verification step with a real DuckDB-Wasm LOAD gate, fixes a real bind-data
vs. global-state bug in `ts_features_list`/`ts_features_config_template` (stale cursor on
`EXECUTE` of a prepared statement), pairs the FFI allocator used for the feature-name array
with a matching free function, and replaces the old native `ts_fill_forward_operator` table
function (which built unquoted SQL text via `StringUtil::Format` and hung/trapped on
WASM) with a macro that uses `COLUMNS([param])` AST substitution instead.

I traced the WASM custom-section parser (`wasm_signature_sections.py`) against its own
self-test plus manual edge cases (zero-size custom sections, malformed LEB128, truncated
files) and did not find a parsing bug — bounds checks are sound and malformed input is
turned into `WasmParseError`, never an unhandled traceback. I traced the new
`anofox_free_ts_features_list` / `anofox_ts_features_list` allocation pairing and the new
`InitGlobal`/`Execute` split in `ts_features.cpp` and found both correct: no double-free, no
leak on the `n == 0` path, and `current_idx` now lives in per-execution `GlobalTableFunctionState`
rather than cached `TableFunctionData`, which is the actual fix for the prepared-statement bug
(now pinned by an `EXECUTE`-twice test). The deploy/verify shell scripts correctly avoid
command-echo tracing and never print AWS environment variables; temp files use `mktemp -d`
with an `EXIT` trap.

No BLOCKER-level defects were found. Three WARNING-level issues are below: (1)
`extension-upload.sh` was substantially rewritten in this phase but was not hardened to the
`set -euo pipefail` standard the two new sibling scripts adopt, and keeps a redundant, now
orphaned `set -e`; (2) the WASM-only silent-data-loss bug in `_ts_fill_forward_native`
(returns the wrong row count instead of erroring) is worked around by moving the affected
assertions out of the WASM-gated test file into a file that is WASM-skip-listed entirely,
which removes WASM CI coverage of that failure mode going forward rather than gating it;
(3) `ts_fill_forward_operator`'s deprecation-by-macro changes the error surface for a
malformed/missing `source_table` compared to the old table function, and this isn't covered
by a test matching the new message shape.

**Fix pass (2026-10-23):** All three Warning findings were addressed in commits `fced2a1`
(WR-01), `6f19875` (WR-02), `8640a1b` (WR-03) — see each finding's "Resolution" note below for
detail. WR-01 and WR-03 are code/test fixes verified against `scratch-p19` fixtures and
`make audit`; WR-02 is a documented deferral (the underlying `_ts_fill_forward_native` bug
remains open and out of scope) with cross-referenced coverage-gap documentation instead of a
code change. Info findings (IN-01..IN-03) were out of this fix pass's scope and remain open.

## Warnings

### WR-01: extension-upload.sh not hardened to the same `set -euo pipefail` + quoting standard as its new siblings

**File:** `scripts/extension-upload.sh:14,86`
**Issue:** This phase rewrote large parts of `extension-upload.sh` (WASM pre/post guard calls,
the `.uploaded` manifest, `$dest` variables) and the two *new* sibling scripts it hands off to
(`extension-verify-upload.sh`, `verify-wasm-artifact.sh`) were both written with
`set -euo pipefail` plus consistently quoted variables. `extension-upload.sh` itself still only
has a bare `set -e` — declared twice (line 14 and again, redundantly, at line 86, right before
the new `.uploaded` manifest logic was inserted) — and most of its variable expansions
(`$ext`, `$ext.append`, `$ext.compressed`, `$ext.sign`, `$1`..`$7`) remain unquoted throughout
the file, including in lines touched by this diff (e.g. `cat $ext.sign >> $ext.append`). Without
`-u`, a missing positional argument (e.g. a workflow caller that forgets `$7`) silently expands
to an empty string instead of failing fast with a clear error, and this script handles the
extension signing private key (`$DUCKDB_EXTENSION_SIGNING_PK`) — the same credential-handling
blast radius the task asked to scrutinize.
**Fix:** Change line 14 to `set -euo pipefail`, delete the orphaned duplicate `set -e` at line
86, and quote the `$ext*` expansions touched by this diff (`"$ext.append"`, `"$ext.sign"`,
`"$ext.compressed"`, `"$dest"` is already quoted correctly as a model to follow).

**Resolution (fixed, commit `fced2a1`):** Changed line 14 to `set -euo pipefail`; deleted the
orphaned duplicate `set -e`; quoted every `$ext*` expansion touched by this diff
(`"$ext"`, `"$ext.append"`, `"$ext.sign"`, `"$ext.hash"`, `"$ext.compressed"`) plus the two
`aws s3 cp` source arguments; switched `[ -z "$AWS_ACCESS_KEY_ID" ]` and
`[ "$DUCKDB_EXTENSION_SIGNING_PK" != "" ]` to `${VAR:-}` defaulting so the new `-u` does not
abort the normal "no AWS key / no signing key" code paths. Verified identical behavior by
rerunning the 19-02-PLAN.md Task 1 dry-run + harness LOAD + double-signed negative control and
the Task 2 fake-S3 end-to-end upload/verify + double-signed-object negative control against the
`scratch-p19` fixtures (fake `aws` shim only; no real bucket touched, no credentials printed, no
`set -x`). All paths behaved identically to pre-fix.

### WR-02: WASM-only silent data-loss bug in `_ts_fill_forward_native` is worked around by removing WASM test coverage, not gated

**File:** `test/wasm/run.mjs:16-20` (SKIP_FILES), `test/sql/ts_fill_forward_native.test:336-396`
**Issue:** `test/sql/ts_fill_forward_native.test` documents (lines 336-353) a confirmed,
pre-existing WASM-only bug: `_ts_fill_forward_native` silently returns the original row count
(zero rows forward-filled) instead of filling or raising, for a single-group input or any
group whose date cadence equals the requested frequency. The assertions that previously caught
this (moved out of `test/sql/ts_fill_forward_operator.test`, which is in the WASM `CURATED`
list and actually run against the DuckDB-Wasm harness) now live only in
`test/sql/ts_fill_forward_native.test`, which `test/wasm/run.mjs`'s `SKIP_FILES` map skips
entirely on WASM (for an unrelated `___trap` issue). The net effect: this known silent
correctness bug on WASM has zero WASM CI coverage after this phase — it would not be caught by
any current CI job, native or WASM (native doesn't reproduce it; WASM-skip-listed test file
never runs it against WASM). This is accurately documented in-repo (`deferred-items.md`) as a
deferred, pre-existing issue, but the mechanism chosen (delete the regression test's WASM
exposure) means a future unrelated change that makes this bug worse, or introduces a similar
one, will not be flagged by CI.
**Fix:** Either (a) add a narrowly-scoped WASM test that asserts the *current* (wrong) row
count as an explicit `XFAIL`/known-bad pin so a future accidental fix or regression is visible
in the WASM harness output, or (b) track this as a release-blocking follow-up phase rather than
a deferred item, since it is a silent correctness/data-loss defect in a public SQL function
(`ts_fill_forward_by`) on a shipped platform target (WASM).

**Resolution (documented-deferral, commit `6f19875`):** The underlying `_ts_fill_forward_native`
bug is a pre-existing, out-of-scope defect and was deliberately NOT fixed here. Option (a) from
the suggested fix was evaluated and rejected: `test/wasm/sqllogic.mjs` (the harness's parser)
has no expected-failure/XFAIL record type — only pass-or-fail `statement`/`query` records — so
pinning the current wrong row count as an "expected" value would read as asserting that the
wrong count is correct, which is a worse failure mode than an explicit, well-documented skip.
Instead: `test/wasm/run.mjs`'s `SKIP_FILES` entry for `test/sql/ts_fill_forward_native.test` now
names the three WASM-unprotected cases verbatim (`test_series` '2024-01-08', `single_group`
'2024-01-05', `weird_single`) and points at `deferred-items.md`; `deferred-items.md`'s matching
"silently zero-fills" item now has a "WR-02 coverage-visibility note" pointing back at the
`SKIP_FILES` entry and stating explicitly that this item remains `status: open` and that closing
it requires either root-causing the arithmetic bug or adding real XFAIL semantics to the
harness — not a documentation change. Option (b) (escalate to a release-blocking phase) was left
to the project owner's prioritization, outside this fixer's authority to create new phases.

### WR-03: `wasm_signature_sections.py`'s `>= 256` payload check is a weaker invariant than the design comment implies, and is not covered by a self-test case at the boundary

**File:** `scripts/wasm_signature_sections.py:60,176-177`
**Issue:** `MIN_PAYLOAD_LEN = 256` and `check_bytes` only asserts `payload_len >= MIN_PAYLOAD_LEN`
— this is intentional (the module docstring on line 28 says "at least 256 bytes", correctly
accommodating a `duckdb_signature` section that carries metadata ahead of the 256-byte
signature). However, `extension-upload.sh`'s correctness depends on this same loose bound: it
calls `truncate -s -256 $ext.append` and appends exactly 256 fresh bytes, which is only
provably correct because the *trailing* 256 bytes of the payload are always the signature
placeholder — a fact the check function never directly asserts (it only asserts a lower bound
on total payload length, not that the file's docstring-assumed layout — "metadata then a
256-byte placeholder at the very end" — actually holds). The `--self-test` suite (5 synthetic
cases) does not include a case with payload_len > 256 to pin this interaction between the two
files' designs.
**Fix:** Add a self-test case building a `duckdb_signature` section with, e.g., a 4-byte
metadata prefix + 256-byte placeholder (payload_len = 260) to document and pin that this shape
is accepted by design, so the loose bound in `check_bytes` cannot be tightened to `== 256` by
a future edit without a visible test failure explaining why it was `>=`.

**Resolution (fixed, commit `8640a1b`):** Added self-test Case 6: builds a `duckdb_signature`
section with a 4-byte metadata prefix ahead of the 256-byte placeholder (payload_len = 260) and
asserts `check_bytes` returns ok and that `payload_len == 260`. Added an inline comment at the
`payload_len < MIN_PAYLOAD_LEN` check site explaining why the bound is deliberately `>=` rather
than `==` and pointing at Case 6. `python3 scripts/wasm_signature_sections.py --self-test` now
reports "6 synthetic module verdicts passed" and `make audit` (which runs this self-test) exits
0. The loose `>=` bound itself was intentionally left unchanged per the finding's own analysis
(it is correct design, not a bug) — only the missing pin was added.

## Info

### IN-01: `anofox_ts_features_list`'s two `malloc` calls are unchecked for OOM (pre-existing, adjacent to this diff's memory-safety-focused change)

**File:** `crates/anofox-fcst-ffi/src/lib.rs:3588,3592`
**Issue:** `malloc(n * std::mem::size_of::<*mut c_char>())` and `malloc(name_len)` are not
null-checked before `*names_ptr.add(i) = name_ptr;` / `ptr::copy_nonoverlapping(...)` write
through them. On OOM this is a null-pointer write (UB/crash), not a panic `catch_unwind` would
catch. This pattern predates this phase's diff (only the `else { *out_names = ptr::null_mut(); }`
branch and the new `anofox_free_ts_features_list` function are new), but it sits directly next
to the new allocator-pairing fix this phase was explicitly scoped to review for memory safety.
**Fix:** Check `malloc`'s return value for null and return `false` with `set_error(...)` (matching
this crate's established error-handling convention) before writing through either pointer.

### IN-02: `test/wasm/run.mjs` validates `--bundle`/`ANOFOX_WASM_BUNDLE` twice

**File:** `test/wasm/run.mjs:165-168,204-209`
**Issue:** `VALID_BUNDLES.has(bundle)` is checked once in `main()` before `bootEngine` is
called, and again as the first line of `bootEngine` itself. Harmless (both checks are correct
and the error message is identical), but it's duplicated validation logic that will drift if
one copy is edited without the other.
**Fix:** Keep the check only in `bootEngine` (or only in `main`), not both.

### IN-03: `anofox_ts_features_list`'s FFI return value is silently discarded at both C++ call sites

**File:** `src/table_functions/ts_features.cpp:38,180` (approx., `TsFeaturesListInitGlobal` / `TsFeaturesConfigTemplateInitGlobal`)
**Issue:** `anofox_ts_features_list(&names_raw, &n_names);` ignores the `bool` return value.
The accompanying comment states the function "never throws (no validation, FFI always returns
true)", which is accurate today, but nothing enforces that invariant if the Rust implementation
changes later to validate inputs and return `false` on error — the C++ side would silently
proceed with whatever (possibly stale/zeroed) `names_raw`/`n_names` values resulted.
**Fix:** `if (!anofox_ts_features_list(&names_raw, &n_names)) { throw DuckDB::Exception(...); }`,
or add a `static_assert`-style comment pointing back here if the FFI contract for this function
is ever changed to allow failure.

---

_Reviewed: 2026-10-23T06:06:40Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
