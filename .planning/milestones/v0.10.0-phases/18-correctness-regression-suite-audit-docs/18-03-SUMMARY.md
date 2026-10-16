---
phase: 18-correctness-regression-suite-audit-docs
plan: 03
subsystem: testing
tags: [audit, static-analysis, ci, github-actions, duckdb-extension, grep-based-tooling, rust-10]

requires:
  - phase: 18-correctness-regression-suite-audit-docs
    provides: 18-02's correctness suite + D-03 length-only sweep (precondition confirmed before starting: scripts/check_length_only_tests.py exits 0)
provides:
  - scripts/audit_no_stats_in_cpp.py -- RUST-10 grep-based audit scanning src/**/*.cpp, src/**/*.hpp, and raw-string macro SQL, with --self-test, a line-level allow-list, and stale-entry detection
  - scripts/audit_allowlist.txt -- all 34 HEAD hits dispositioned (28 row/index-ordering sorts, 4 moment-loop sites, 2 SQL reporting-aggregate sites), each with a line-specific regex and a concrete marshalling-class justification
  - .github/workflows/audit.yml -- least-privilege "Static audits" CI job (no build) running the audit and the 18-02 length-only sweep
  - Makefile `audit` target (self-test -> real scan -> length-only sweep, in that order)
affects: [18-04-docs-snippets-changelog]

actuals:
  tokens: 7604
  tasks: 2
  commits: 2

tech-stack:
  added: []
  patterns:
    - "Single character-scanning pass over C++ source that strips // and /* */ comments and ordinary \"...\" string contents (replacing with spaces, preserving line numbers/newlines exactly) while separately extracting every raw-string literal's content with its starting line number -- cpp-category regexes run on the stripped code, sql-category regexes run only on the raw-string content"
    - "SQL `-- ...` line-comment stripping inside extracted raw-string content before matching sql-aggregate/sql-math regexes, so a SQL comment that merely mentions an aggregate's name (e.g. a doc comment explaining AVG()'s NULL semantics) is never treated as a real use of that aggregate"
    - "Allow-list entries use the exact re.escape'd stripped line text (or an equally variable-name-specific regex) and are spot-checked with `grep -cE <pattern> <file>` to confirm each matches only the intended line(s) in that file -- never a bare whole-file function-name pattern that would silently allow a future, unrelated statistical use of the same function"

key-files:
  created:
    - scripts/audit_no_stats_in_cpp.py
    - scripts/audit_allowlist.txt
    - .github/workflows/audit.yml
  modified:
    - Makefile

key-decisions:
  - "All 34 HEAD hits were read in full context (not just pattern-matched) before being allow-listed: 28 std::sort/std::stable_sort sites order rows/indices by timestamp or (group, date)/(unique_id, date) key for output or pre-FFI-call marshalling; 2 SQL AVG() sites are reporting aggregates of already Rust-computed per-group scalars (AVG(length), AVG((_quality).overall_score)); 2 cpp-moment-loop sites in ts_periods.cpp sum Rust-computed per-row n_periods counts purely to pre-reserve ListVector capacity (list marshalling). Zero hits required switching a call site to an existing Rust FFI function, and zero were escalated as a RUST-10 blocker."
  - "ts_aggregate_hierarchy.cpp's `cell.sum += row.value` (the 1 remaining cpp-moment-loop hit) is a plain additive SUM rollup across hierarchy siblings at a fixed timestamp -- this function makes no Rust FFI call at all. Judged structurally equivalent to SQL's own exact, model-free SUM()/GROUP BY aggregate, not a 'statistic of series values' in RUST-10's sense (mean/variance/quantile/scale/transform, which all require numerically-careful, R/scipy-verified algorithms this milestone is moving to Rust). This extends the interfaces block's given vocabulary (row ordering / date arithmetic / list marshalling / reporting aggregates / counting) with one additional class -- 'structural rollup of already-computed quantities' -- documented here and in the allow-list entry's justification rather than left implicit."
  - "SQL `-- ...` line comments inside raw-string macro bodies are stripped before sql-category matching. This is not explicitly required by the must_haves (which only mandate C++ // and /* */ comment stripping), but without it `ts_data_quality_summary`'s own WR-05 explanatory SQL comment ('-- AVG() skips it, silently excluding the group...') would falsely trigger a sql-aggregate hit on comment text, not real SQL. Confirmed via self-test negative and via the real scan (0 false hits from that comment)."
  - ".github/workflows/audit.yml stays fully independent of MainDistributionPipeline.yml: its own file, its own trigger/permissions block, calling `python3 scripts/*.py` directly rather than `make audit` (the Makefile `include`s the extension-ci-tools submodule makefile, which this audit job's checkout never fetches -- `make audit` would fail there for an unrelated reason)."

patterns-established:
  - "`path | regex | justification` pipe-delimited allow-list format, matching scripts/length_only_allowlist.txt's existing 18-02 convention -- one shared allow-list grammar across both Phase 18 sweep scripts."
  - "`make audit` runs self-test -> real scan -> sibling D-03 sweep, in that fixed order (mandated by the plan's own Task 1 action step 4); `.github/workflows/audit.yml` calls the identical three python3 commands directly, in the same order, so CI and local `make audit` can never drift apart silently."

requirements-completed: [RUST-10]

coverage:
  - id: D1
    description: "scripts/audit_no_stats_in_cpp.py scans C++ (cpp-math/cpp-accumulate/cpp-moment-loop/cpp-order-stat) and raw-string SQL (sql-aggregate/sql-math); --self-test covers every category's positive case, 3 negatives (line comment, block comment, ordinary string literal, plus a description-string SQL negative), and 4 allow-list behaviors"
    requirement: "RUST-10"
    verification:
      - kind: unit
        ref: "python3 scripts/audit_no_stats_in_cpp.py --self-test: '12 positive/negative category checks + 4 allow-list checks passed', exit 0"
        status: pass
    human_judgment: false
  - id: D2
    description: "Every HEAD hit is dispositioned via a line-specific, justified allow-list entry; the real scan and `make audit` both exit 0 on HEAD"
    requirement: "RUST-10"
    verification:
      - kind: integration
        ref: "python3 scripts/audit_no_stats_in_cpp.py: 'audit: 34 hits, 34 allow-listed, 0 violations, 0 stale entries', exit 0; `make audit` exit 0 (self-test + real scan + check_length_only_tests.py all pass)"
        status: pass
    human_judgment: false
  - id: D3
    description: "The audit provably fails when statistical arithmetic is injected (both a C++ std::sqrt variance line and a raw-string stddev() SQL query), and recovers to a clean pass once the probe is removed"
    requirement: "RUST-10"
    verification:
      - kind: integration
        ref: "Task 1 <verify> automated block, run verbatim: 'probe exit=1', 'probe lines named: 2' (both injected lines named), 'probe removed: 0', 'clean exit=0'"
        status: pass
    human_judgment: false
  - id: D4
    description: ".github/workflows/audit.yml is least-privilege (top-level contents:read only, no job-level override), has exactly the 3 required triggers, no pull_request_target/secrets, checkout with persist-credentials:false, runs exactly the 3 required python3 commands, uses no reusable extension-ci-tools workflow; MainDistributionPipeline.yml has zero diff"
    requirement: "RUST-10"
    verification:
      - kind: integration
        ref: "Task 2 <verify> automated block, run verbatim: PyYAML checks all pass (triggers=['pull_request','push','workflow_dispatch'], perms={'contents':'read'}, job perms override=False, target/secrets=(False,False), checkout creds=False, runs scripts=True, reusable=False); `cargo fmt --all -- --check` clean; `git diff --quiet HEAD -- .github/workflows/MainDistributionPipeline.yml` exit 0"
        status: pass
    human_judgment: false
  - id: D5
    description: "Two allow-list judgment calls -- classifying ts_aggregate_hierarchy.cpp's additive SUM rollup as 'not a statistic of series values', and stripping SQL line-comments before matching -- are sound scope decisions, not loophole-widening"
    verification: []
    human_judgment: true
    rationale: "Both decisions extend the audit's behavior slightly beyond the must_haves' literal wording (vocabulary list; C++-only comment stripping) based on reading the actual code/SQL in context. A human should spot-check that the hierarchy-rollup classification doesn't inadvertently create a loophole for a future genuine statistic disguised as a 'sum', and that SQL comment-stripping doesn't hide a real aggregate call that happens to follow a `--` on the same line."

duration: 15min
completed: 2026-10-15
status: complete
---

# Phase 18 Plan 3: No-Statistics-in-C++/SQL Audit (RUST-10) Summary

**A grep-based audit script with a line-level justified allow-list (34 HEAD hits, all legitimate marshalling/reporting aggregates — zero genuine statistics found in C++ or macro SQL), proven to fail on injected statistical arithmetic and to pass cleanly on HEAD, wired into both `make audit` and a new least-privilege `.github/workflows/audit.yml`.**

## Performance

- **Duration:** ~15 min
- **Started:** 2026-10-15T19:36:00Z
- **Completed:** 2026-10-15T19:51:00Z
- **Tasks:** 2
- **Files modified:** 4 (3 created, 1 modified)

## Accomplishments

- **`scripts/audit_no_stats_in_cpp.py` (D-04):** scans every `src/**/*.cpp`/`src/**/*.hpp` file on disk (excluding the generated `src/include/anofox_fcst_ffi.h`), stripping C++ comments and ordinary string literals while separately extracting raw-string literal content (and its own `--` SQL comments) for the SQL-aggregate/SQL-math categories. `--self-test` exercises all 5 C++/SQL category pairs (6 positive cases), 4 negative cases (line comment, block comment, ordinary string, SQL description string), and 4 allow-list behaviors (same-file match passes; same text in another file fails; empty justification fails; unmatched entry reported stale).
- **`scripts/audit_allowlist.txt`:** every one of the 34 hits found on HEAD was read in full context (not just pattern-matched) and allow-listed with a line-specific regex (verified via `grep -cE` to match only the intended line(s) — see the Disposition Table below) and a concrete justification from the marshalling vocabulary. Zero hits required switching a call site to an existing Rust FFI function; zero were escalated as a RUST-10 blocker.
- **Both directions proven:** `python3 scripts/audit_no_stats_in_cpp.py` and `make audit` both exit 0 on HEAD (`audit: 34 hits, 34 allow-listed, 0 violations, 0 stale entries`); a temporary `src/zz_audit_probe.cpp` injecting a `std::sqrt` variance line and a raw-string `SELECT stddev(x) FROM t` query makes the real run exit 1, naming both probe lines, and is deleted in the same command (`git status --porcelain -- src` confirmed clean afterward).
- **`Makefile` `audit` target:** runs, in the mandated order, the self-test, the real scan, and the 18-02 `check_length_only_tests.py` sweep.
- **`.github/workflows/audit.yml` (D-05):** a new "Static audits" job — triggers `push`/`pull_request`/`workflow_dispatch`, top-level `permissions: contents: read` only (no job-level override), `actions/checkout@v4` with `persist-credentials: false` and `submodules: false`, no secrets/env tokens/cache, no `pull_request_target`, no `uses:` of `extension-ci-tools` or any reusable workflow, 5-minute timeout. Runs the audit self-test, the real audit, and the length-only sweep via three direct `python3` steps (not `make audit`, since the Makefile `include`s the extension-ci-tools submodule makefile this job's checkout never fetches). Validated with PyYAML; `MainDistributionPipeline.yml` confirmed byte-identical (`git diff --quiet` exit 0).

## Task Commits

Each task was committed atomically:

1. **Task 1: tracer — audit script (C++ + macro SQL categories), allow-list, `make audit` target** — `b5333e8` (test)
2. **Task 2: `.github/workflows/audit.yml` (least privilege, no build)** — `69de339` (feat)

**Plan metadata:** commit pending (this SUMMARY)

## HEAD Hit Disposition Table

All 34 hits found by the real scan (`python3 scripts/audit_no_stats_in_cpp.py` run with an empty allow-list), grouped by category:

| Category | Count | Disposition |
|---|---|---|
| cpp-order-stat (`std::sort`/`std::stable_sort`) | 28 | Allow-listed: every site orders rows/indices by timestamp, or by a (group, date)/(unique_id, date) composite key, before a Rust FFI call or before output marshalling. Read in full context (see Task 1 execution). |
| cpp-moment-loop | 4 | 3x `ts_periods.cpp` (`total_periods += ...n_periods`, 3 near-identical period-detection variants): list marshalling, pre-reserving `ListVector` capacity from a sum of Rust-computed per-row counts. 1x `ts_aggregate_hierarchy.cpp` (`cell.sum += row.value`): structural rollup (plain additive SUM across hierarchy siblings at a fixed date, no Rust FFI call in this function at all — see key-decisions). |
| sql-aggregate | 2 | Both `AVG(...)` in `src/macros/ts_macros.cpp`: `AVG(length)` (mean of `ts_stats`' per-series length column across groups) and `AVG((_quality).overall_score)` (mean of `_ts_data_quality`'s per-group Rust-computed score) — reporting aggregates of already Rust-computed per-group scalars, the exact example given in the plan's vocabulary. |
| cpp-math, cpp-accumulate, sql-math | 0 | No hits on HEAD in any of these three categories. |

Allow-list regex spot-check (`grep -cE <pattern> <file>`, confirming each entry matches only the intended line(s), not an unrelated future statistical use of the same function):

| File | Pattern style | `grep -cE` count | Intended lines |
|---|---|---|---|
| `ts_metrics_native.cpp` | `std::sort\(indices\.begin\(\), indices\.end\(\),` | 5 | 459, 847, 1168, 1463, 1751 (5 near-identical metric functions, identical idiom) |
| `ts_periods.cpp` | `total_periods \+= row_results\[row_idx\]\.result\.n_periods;` | 3 | 159, 299, 700 (3 near-identical period-detection variants) |
| `ts_cv_split_native.cpp` | `std::sort\(train_ends\.begin\(\), train_ends\.end\(\)\);` / `std::sort\(indices\.begin\(\), indices\.end\(\),` | 1 each | 229 / 418 (two distinct sorts, both captured, neither collides with the other) |
| `ts_cv_forecast_native.cpp` | `std::sort\(train_indices...` / `std::sort\(test_indices...` | 1 each | 666 / 677 |
| all other 22 single-occurrence files | exact `re.escape`'d stripped line text | 1 | the one intended line in each file |

## Files Created/Modified

- `scripts/audit_no_stats_in_cpp.py` — new (D-04): comment/string-stripping + raw-string extraction, 5 C++/SQL category pairs, allow-list parsing/validation/stale-detection, `--self-test`
- `scripts/audit_allowlist.txt` — new: 34 HEAD hits, each a `path | regex | justification` entry, grouped by file with a disposition comment
- `.github/workflows/audit.yml` — new (D-05): least-privilege "Static audits" job
- `Makefile` — `audit` target added (`.PHONY` + 3-command target)

## Decisions Made

See `key-decisions` in frontmatter. In summary: every hit was read in context before being allow-listed; the one hit that didn't fit the interfaces block's literal 5-item vocabulary (the hierarchy-rollup SUM) was reasoned through explicitly and documented, not silently folded into an existing category; SQL comment-stripping was added as a scope extension beyond the must_haves' literal wording to avoid a false positive on the project's own explanatory comment text, and is flagged in `coverage.D5` for a human spot-check.

## Deviations from Plan

None — plan executed exactly as written. (The SQL-comment-stripping behavior and the hierarchy-rollup-SUM classification are judgment calls within the plan's own "patterns at Claude's discretion within the categories of the must_haves" latitude, not deviations from it; both are documented above and in `coverage.D5` rather than treated as silent scope creep.)

## Issues Encountered

- None. The plan's seed-scan estimate of "22 files" for the C++ math/accumulate/sort grep matched exactly once comment-text false matches were excluded (3 of the 31 raw grep lines were `std::sort`-mentioning comments, not real calls); the actual real-hit count (34, after also including the 4 moment-loop and 2 SQL-aggregate hits the seed grep didn't cover) is reported precisely above rather than re-estimated.
- Task 2's own `<verify>` command's `make audit 2>&1 | tail -3` does not literally show the real audit's own `0 violations`/`0 stale entries` line in its last 3 lines of output — Task 1's mandated command order (self-test, then real audit, then the length-only sweep) means the length-only sweep's ~76 `INFO (zero-assertion, not a failure)` lines are what ends up in the tail, not the audit's own summary line. This is a cosmetic gap in the plan's own verify-script assumption, not a real failure: `make audit`'s exit code is 0, and the audit's own summary line (`audit: 34 hits, 34 allow-listed, 0 violations, 0 stale entries`) was independently confirmed present in the full output (see the Disposition Table / Task Commits above). Not fixed by reordering the Makefile target, since that order is explicitly mandated by Task 1's action step 4.

## User Setup Required

None — no external service configuration required.

## Follow-ups

- **First CI run of "Static audits" is pending.** This plan could not push to a remote (no push performed, matching every prior Phase 18 plan). The next push/PR on `fix/statistical-correctness` must show the "Static audits" job green.
- **Phase 12 deferred-items (c) — not implemented, correctly out of scope here.** `.planning/phases/12-*/deferred-items.md` item (c) ("Correct-but-private list-marshalling helpers not yet consolidated onto `ts_list_marshal.hpp`" — `ts_stats.cpp`, `ts_forecast.cpp`, `ts_forecast_scalar.cpp`, `ts_forecast_inspect_scalar.cpp` each read list arguments via their own private helper rather than the shared `ListDoubleReader`/`ScalarArgReader`) was routed to "RUST-10" by the Phase 12 executor, but is a marshalling-consolidation concern, not D-04's statistics-detection scope, and was explicitly confirmed by the user NOT to be folded into this plan (18-CONTEXT.md interfaces block). Left open for a future plan; it is not a RUST-10 gap (no statistical arithmetic involved), just a code-duplication cleanup.

## Next Phase Readiness

- RUST-10 is enforced both locally (`make audit`) and in CI (`audit.yml`), independent of `MainDistributionPipeline.yml`, alongside the D-03 length-only sweep — ROADMAP Phase 18 success criterion 3 is met.
- Plan 18-04 (docs/snippets/CHANGELOG) can proceed; this plan found no production code requiring a Rust-side fix, so there is no new numeric-behavior change for 18-04 to document.
- No known blockers.

## Self-Check: PASSED

- Confirmed on disk: `scripts/audit_no_stats_in_cpp.py`, `scripts/audit_allowlist.txt`, `.github/workflows/audit.yml`, modified `Makefile` — all present.
- Confirmed in `git log --oneline`: `b5333e8` and `69de339` both present, in that order, on `fix/statistical-correctness`.
- Re-ran `python3 scripts/audit_no_stats_in_cpp.py --self-test`: exit 0.
- Re-ran `make audit`: exit 0 (`audit: 34 hits, 34 allow-listed, 0 violations, 0 stale entries`; length-only sweep: `0 un-allow-listed length-only tests`).
- Re-ran `git diff --diff-filter=D --name-only HEAD~1 HEAD` after each commit: no deletions.
- Re-ran `git status --porcelain -- src`: clean (no stray probe file).
- Re-ran `git diff --quiet HEAD -- .github/workflows/MainDistributionPipeline.yml`: exit 0 (untouched).
- `.planning/STATE.md`, `.planning/ROADMAP.md`, `.planning/REQUIREMENTS.md` were NOT edited by this plan, per the plan's own Execution environment instructions.
