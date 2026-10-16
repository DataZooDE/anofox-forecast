---
phase: 18-correctness-regression-suite-audit-docs
plan: 06
subsystem: detection
tags: [duckdb, scalar-function, null-handling, period-detection, correctness-suite, docs]

requires:
  - phase: 15-period-detection-decomposition-peaks
    provides: "the _ts_detect_periods Rust-backed scalar function and GetMultiPeriodResultType struct that this plan's fix builds on"
  - phase: 17-upstream-consumption-diagnostics-parity
    provides: "the WR-01 SPECIAL_HANDLING fix pattern for ts_ljung_box/ts_residual_diagnostics (commit 6ceea19), reused verbatim here"
provides:
  - "ts_detect_periods and ts_detect_periods_by table macros return a populated row on the default call (no expected_periods) -- the all-NULL-row bug tracked as DET-01 since Phase 10 is fixed"
  - "FunctionNullHandling::SPECIAL_HANDLING on the 5-arg/6-arg _ts_detect_periods overloads, matching the Phase 17 WR-01 pattern"
  - "expected_periods param path reachable through the MAP form at all (fixed a second, independent json_extract-vs-json_extract_string macro bug)"
  - "correctness pins for the default case, expected_periods case, NULL-series case, and ts_detect_periods_agg (scalar + table macro) in test/sql/correctness/detection_decomposition.test and test/sql/ts_periods.test"
  - "every ts_detect_periods_by all-NULL workaround note in docs/ and plugins/ retired, with ts_detect_periods_agg kept as a documented, equally valid alternative"
affects: ["anofox-forecast-detection skill", "anofox-forecast-models skill", "future period-detection consumers"]

actuals:
  tokens: 5577
  tasks: 3
  commits: 3
  plan_head_before: 221816025577cd00eb003870554b2fa1b9257eea

tech-stack:
  added: []
  patterns:
    - "FunctionNullHandling::SPECIAL_HANDLING on a nullable-optional-arg overload, with the function body's existing per-row IsNull() checks treating NULL as 'not provided' -- same pattern as Phase 17 WR-01, now applied a second time to _ts_detect_periods"
    - "json_extract_string(...) CAST to an ARRAY type, not json_extract(...) CAST to an ARRAY type, for any MAP-param that must become a DuckDB list -- json_extract returns a quoted JSON scalar which cannot CAST to LIST/ARRAY types; json_extract_string returns the unquoted string DuckDB parses as a list literal (same working pattern ts_mstl_decomposition_by's seasonal_periods already used)"

key-files:
  created: []
  modified:
    - src/table_functions/ts_periods.cpp
    - src/macros/ts_macros.cpp
    - test/sql/correctness/detection_decomposition.test
    - test/sql/ts_periods.test
    - docs/api/07-forecasting.md
    - docs/reference/models/multi-seasonal/auto_mstl.md
    - docs/reference/models/multi-seasonal/auto_mfles.md
    - docs/reference/models/multi-seasonal/auto_tbats.md
    - docs/reference/models/state-space/auto_ets.md
    - docs/reference/models/theta/auto_theta.md
    - plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md
    - CHANGELOG.md
    - docs/release-notes/v0.10.0.md

key-decisions:
  - "Fixed the expected_periods json_extract-vs-json_extract_string bug in the same plan even though it was not named in the original gap description -- it directly blocks must_have #3 ('the expected_periods path still works'), is in the same already-in-scope file (src/macros/ts_macros.cpp), and without it the expected_periods path was never reachable through the documented MAP{} form at all, regardless of the SPECIAL_HANDLING fix. Rule 1 (bug directly blocking the current task), not a scope expansion."
  - "Applied SPECIAL_HANDLING to both the 5-arg and 6-arg _ts_detect_periods overloads, not just the 6-arg one the macro calls -- the 5-arg overload is directly callable from SQL and takes the same nullable expected_periods parameter, so leaving it unfixed would reintroduce the identical bug for any caller of that overload."
  - "Rewrote (rather than deleted) every retired workaround note to keep ts_detect_periods_agg documented as an equally valid alternative, per the plan's own must_have -- the aggregate form is genuinely useful inline in a larger GROUP BY query, not just a historical workaround."
  - "Added a correctness pin for ts_detect_periods_agg (not originally planned) because the new CHANGELOG.md entry backtick-mentions it by name, and check_correctness_inventory.py requires every CHANGELOG-mentioned, registered function to have non-comment correctness-suite coverage -- the gate caught a real gap (the function was previously only coverage-exempt because the CHANGELOG hadn't named it), not something to route around by de-identifying the mention."

patterns-established:
  - "When check_correctness_inventory.py reports MISSING for a function newly named in a CHANGELOG entry, add a real correctness pin for it rather than rewording the CHANGELOG to avoid the backtick match -- the gate's purpose is to force genuine coverage, and de-identifying the mention would defeat that purpose while leaving the actual gap open."

requirements-completed: [DET-01, TEST-03, DOCS-01]

coverage:
  - id: D1
    description: "ts_detect_periods / ts_detect_periods_by return a populated result in the default call (params := MAP{}), matching the 2-arg _ts_detect_periods(list, 'fft') result on the same period-12 sinusoid (primary_period = 12.0)"
    requirement: DET-01
    verification:
      - kind: integration
        ref: "test/sql/correctness/detection_decomposition.test (det01_periods fixture, default-case ts_detect_periods_by / ts_detect_periods pins)"
        status: pass
      - kind: e2e
        ref: "manual reproduction via ./build/release/duckdb against the built extension, both macros, before/after the fix"
        status: pass
    human_judgment: false
  - id: D2
    description: "The 6-arg _ts_detect_periods overload treats a NULL expected_periods as 'not provided' instead of short-circuiting the row to NULL (FunctionNullHandling::SPECIAL_HANDLING, same class as Phase 17 WR-01); the 5-arg overload gets the identical fix"
    requirement: DET-01
    verification:
      - kind: unit
        ref: "test/sql/ts_periods.test (6-arg NULL-expected_periods regression pin, NULL==[]::DOUBLE[] equivalence pin)"
        status: pass
    human_judgment: false
  - id: D3
    description: "The expected_periods path works end-to-end through the documented MAP params form (matches_expected / matched_expected_period populated); a NULL series still yields NULL on every surface touched"
    verification:
      - kind: integration
        ref: "test/sql/correctness/detection_decomposition.test (expected_periods case, det01_null NULL-series case)"
        status: pass
    human_judgment: false
  - id: D4
    description: "Correctness pins exist for both table macros (default case and expected_periods case) and ts_detect_periods_agg, using fixture/closed-form values; check_correctness_inventory.py exits 0"
    requirement: TEST-03
    verification:
      - kind: unit
        ref: "python3 scripts/check_correctness_inventory.py (exit 0, 0 missing correctness coverage)"
        status: pass
    human_judgment: false
  - id: D5
    description: "Every known-bug workaround note for ts_detect_periods_by in docs/ and plugins/ is removed or rewritten; ts_detect_periods_agg stays documented as an alternative; CHANGELOG and release notes record the fix; make check-docs is 0 FAIL / 0 STALE"
    requirement: DOCS-01
    verification:
      - kind: unit
        ref: "grep -rn 'known bug|known-bug|all-NULL row|KNOWN BUG' docs/ plugins/ examples/ CHANGELOG.md README.md (0 hits)"
        status: pass
      - kind: integration
        ref: "make check-docs -> check_doc_snippets: 344 PASS, 0 FAIL, 8 EXPECTED-ERROR, 154 SYNOPSIS, 4 SKIP-NETWORK, 0 STALE"
        status: pass
    human_judgment: false
  - id: D6
    description: "Full release DoD green: unittest suite count did not drop, make audit clean, Rust unchanged so no cargo steps required"
    verification:
      - kind: integration
        ref: "GEN=ninja make release; build/release/test/unittest \"test/sql/*\" -> 5593 assertions in 94 test cases (baseline 5580); make audit -> 34 hits, 34 allow-listed, 0 violations, 0 stale entries"
        status: pass
    human_judgment: false

duration: ~25 min (single session, commit span 2026-10-16T07:24:02+02:00 to 07:37:48+02:00)
completed: 2026-10-16
status: complete
---

# Phase 18 Plan 06: Fix ts_detect_periods(_by) Default-Case NULL (DET-01 Gap Closure) Summary

**Root-caused and fixed the Phase-10-era `ts_detect_periods`/`ts_detect_periods_by` all-NULL-row bug to DuckDB's default null handling on the `_ts_detect_periods` scalar overloads that take `expected_periods`, found and fixed a second independent `json_extract`-vs-`json_extract_string` macro bug blocking the `expected_periods` path entirely, pinned both in the correctness suite, and retired every doc/skill workaround note that told users to avoid the table macro.**

## Performance

- **Duration:** ~25 min (single session; commit span 07:24:02+02:00 → 07:37:48+02:00)
- **Started:** 2026-10-16T07:24:02+02:00 (plan head)
- **Completed:** 2026-10-16T07:37:48+02:00
- **Tasks:** 3 (all completed as planned, no checkpoints)
- **Files modified:** 13

## Accomplishments

- **Root cause confirmed empirically before touching code:** `_ts_detect_periods(list, 'fft', 0, -1.0, NULL::DOUBLE[], -1.0)` returned `NULL` but the identical call with `[]::DOUBLE[]` in place of `NULL::DOUBLE[]` returned `12.0` on the same series — proving DuckDB's default null-handling short-circuit, not the function body (which already had correct per-row `IsNull()` handling for every optional argument), was at fault. Same bug class as the Phase 17 WR-01 `ts_ljung_box`/`ts_residual_diagnostics` fix (commit `6ceea19`).
- **Fix:** set `null_handling = FunctionNullHandling::SPECIAL_HANDLING` on the 5-arg and 6-arg `_ts_detect_periods` overloads in `src/table_functions/ts_periods.cpp` (both take the nullable `expected_periods` parameter the macros always pass). `ts_detect_periods_by('s', g, ds, y, MAP{})` now returns a populated row (`primary_period = 12.0` on a period-12 sinusoid), matching the 2-arg form.
- **Second, independent bug found and fixed in the same scope:** `src/macros/ts_macros.cpp`'s `expected_periods` extraction used `json_extract(...)` cast to `DOUBLE[]`, which fails with a Conversion Error on a MAP-supplied list (`json_extract` returns a quoted JSON scalar, not a castable array). Switched both `ts_detect_periods` and `ts_detect_periods_by` to `json_extract_string(...)`, matching the working pattern `ts_mstl_decomposition_by`'s `seasonal_periods` already used. Without this fix, must_have #3 ("the expected_periods path still works") was unreachable regardless of the SPECIAL_HANDLING fix — confirmed by direct reproduction (`MAP{'expected_periods': '[12.0]'}` raised `Conversion Error: Expected ARRAY, but got VARCHAR` before, returns `matches_expected = true` after).
- **Correctness pins added:** `test/sql/correctness/detection_decomposition.test` gained a new section (default-case `ts_detect_periods_by`/`ts_detect_periods`, expected_periods case, NULL-series case, `ts_detect_periods_agg` parity) on a dedicated `det01_periods`/`det01_null` fixture, closed-form period-12 sinusoid, cited inline next to the pre-existing `_ts_detect_periods` 'fft' pin. `test/sql/ts_periods.test` gained a scalar-level regression pin for the 6-arg overload (NULL `expected_periods` == `[]::DOUBLE[]` equivalence, NULL-series-still-NULL).
- **Docs/skill workaround retirement:** removed the `KNOWN BUG` blockquote from `plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md` (both the dedicated section and the canonical detection pipeline's comment); removed the known-bug note from `docs/api/07-forecasting.md`'s "Handling Seasonality" step and switched its example to call `ts_detect_periods_by` directly; reworded the "aggregate form — the _by table-macro form has a known bug" comments in five per-model reference pages (`auto_mstl.md`, `auto_mfles.md`, `auto_tbats.md`, `auto_ets.md`, `auto_theta.md`) to describe both forms as equally valid. `ts_detect_periods_agg` stays documented everywhere as a genuine, equally valid alternative (not just a historical workaround), per the plan's own must_have.
- **CHANGELOG.md / release notes:** added a `### Fixed` entry under the `## [Unreleased] — v0.10.0` section recording both root causes and the fix, cross-referencing the Phase 17 WR-01 precedent; `docs/release-notes/v0.10.0.md` gained a one-line pointer under "Detection & decomposition".
- **Inventory gate caught a real gap of its own:** the new CHANGELOG entry backtick-mentions `ts_detect_periods_agg`, which `check_correctness_inventory.py` then required to have non-comment correctness-suite coverage (it previously had none). Added a genuine pin (`ts_detect_periods_agg(ds, y, 'fft').primary_period` on the same fixture, same closed-form `12.0`) rather than rewording the CHANGELOG to dodge the match.
- **Full DoD run and green:** `GEN=ninja make release` builds clean; `build/release/test/unittest "test/sql/*"` — 5593 assertions in 94 test cases (baseline 5580, did not drop; count rose because new pins were added, no test case count regression); `make audit` — 34 hits, 34 allow-listed, 0 violations, 0 stale entries; `make check-docs` — 344 PASS, 0 FAIL, 8 EXPECTED-ERROR, 154 SYNOPSIS, 4 SKIP-NETWORK, 0 STALE; `check_correctness_inventory.py` exit 0, 0 missing correctness coverage. No Rust files were touched by this plan, so `cargo fmt --check` / `cargo clippy -D warnings` / `cargo test --workspace --release` were not required per the plan's own conditional must_have.

## Task Commits

1. **Task 1: root-cause and fix the NULL optional-arg path** — `ed3c390` (fix)
2. **Task 2: correctness pins + regression tests** — `281560c` (fix) — includes the `expected_periods` `json_extract_string` macro fix, found and fixed within this task's scope
3. **Task 3: retire docs workarounds, CHANGELOG, release notes, full DoD** — `d0ea55a` (docs)

**Plan metadata:** (this commit, immediately following)

## Files Created/Modified

- `src/table_functions/ts_periods.cpp` — `SPECIAL_HANDLING` on the 5-arg/6-arg `_ts_detect_periods` overloads.
- `src/macros/ts_macros.cpp` — `expected_periods` extraction switched from `json_extract` to `json_extract_string` in both `ts_detect_periods` and `ts_detect_periods_by`.
- `test/sql/correctness/detection_decomposition.test` — default-case, expected_periods-case, NULL-series-case, and `ts_detect_periods_agg` parity pins.
- `test/sql/ts_periods.test` — 6-arg overload NULL-`expected_periods` scalar regression pins.
- `docs/api/07-forecasting.md` — "Handling Seasonality" step rewritten to call the fixed macro directly.
- `docs/reference/models/multi-seasonal/{auto_mstl,auto_mfles,auto_tbats}.md`, `docs/reference/models/state-space/auto_ets.md`, `docs/reference/models/theta/auto_theta.md` — workaround comments reworded.
- `plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md` — `KNOWN BUG` blockquote removed; canonical pipeline comment reworded.
- `CHANGELOG.md` — new `### Fixed` entry under `## [Unreleased] — v0.10.0`.
- `docs/release-notes/v0.10.0.md` — one-line DET-01 pointer added under "Detection & decomposition".

## Decisions Made

See frontmatter `key-decisions`. Most significant: fixing the independent `json_extract`-vs-`json_extract_string` macro bug within this plan (it directly blocked a stated must_have, in a file already in scope) rather than treating it as a separate deviation to defer, and adding a real correctness pin for `ts_detect_periods_agg` when the inventory gate caught the CHANGELOG's new mention of it, rather than wording around the gate.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `expected_periods` extracted via `json_extract` instead of `json_extract_string` in both table macros**
- **Found during:** Task 2, attempting to pin the expected_periods-case correctness test
- **Issue:** `src/macros/ts_macros.cpp`'s `COALESCE(CAST(json_extract(to_json(params), '$.expected_periods') AS DOUBLE[]), NULL)` raises `Conversion Error: Expected ARRAY, but got VARCHAR: "[12.0]"` for any `MAP{'expected_periods': '[12.0]'}` call — `json_extract` returns a quoted JSON scalar that cannot `CAST` to `DOUBLE[]`. This meant the `expected_periods` path was never reachable through the documented `MAP` params form at all, independent of the `SPECIAL_HANDLING` fix, and directly blocked must_have #3.
- **Fix:** Switched both occurrences (in `ts_detect_periods` and `ts_detect_periods_by`) to `json_extract_string(...)`, matching the already-working pattern `ts_mstl_decomposition_by`'s `seasonal_periods` extraction uses.
- **Files modified:** `src/macros/ts_macros.cpp`
- **Verification:** `MAP{'expected_periods': '[12.0]'}` now returns `matches_expected = true`, `matched_expected_period = 12.0`; full suite re-run clean (5592 assertions at that point).
- **Committed in:** `281560c` (Task 2 commit)

**2. [Rule 2 - Missing Critical] `ts_detect_periods_agg` correctness coverage missing after naming it in CHANGELOG.md**
- **Found during:** Task 3, running `check_correctness_inventory.py` as part of the full DoD
- **Issue:** The new CHANGELOG.md `### Fixed` entry backtick-mentions `ts_detect_periods_agg` by name (documenting it as the pre-existing workaround that's now an equal alternative). `check_correctness_inventory.py` requires every registered, CHANGELOG-mentioned function to be exercised on a non-comment line of a `test/sql/correctness/*.test` file; `ts_detect_periods_agg` had none (only a comment-line mention), so the gate failed with `MISSING ts_detect_periods_agg`.
- **Fix:** Added a real pin in `test/sql/correctness/detection_decomposition.test` — `ts_detect_periods_agg(ds, y, 'fft').primary_period` on the same `det01_periods` fixture, same closed-form `12.0` — rather than rewording the CHANGELOG to avoid the backtick match.
- **Files modified:** `test/sql/correctness/detection_decomposition.test`
- **Verification:** `check_correctness_inventory.py` exit 0, `0 missing correctness coverage`.
- **Committed in:** `d0ea55a` (Task 3 commit)

---

**Total deviations:** 2 auto-fixed (1 Rule 1 — bug blocking a stated must_have, 1 Rule 2 — missing correctness coverage surfaced by the inventory gate itself).
**Impact on plan:** Both fixes were necessary for the plan's own must_haves to hold (the expected_periods path literally did not work without #1; the inventory gate would not exit 0 without #2). No scope creep beyond what the plan's own acceptance criteria already required.

## Issues Encountered

None beyond the two deviations above, both resolved within task scope.

## Known Stubs

None — every deliverable is fully wired and verified against the built extension; no placeholder text or unwired examples.

## Threat Flags

None. This plan touches no new network endpoints, auth paths, or trust boundaries — it fixes null-handling and JSON-extraction behavior on an existing, already-reviewed scalar function family and its SQL macro wrappers.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- DET-01 is now fully closed (was "partial" per `.planning/v0.10.0-MILESTONE-AUDIT.md`): the SQL-facing `ts_detect_periods`/`ts_detect_periods_by` table macros work correctly in the default case, the `expected_periods` case, and the NULL-series case, matching the already-correct `_ts_detect_periods` core and `ts_detect_periods_agg` aggregate.
- Full release DoD is green at this plan's HEAD: 5593 assertions / 94 test cases, `make audit` clean, `make check-docs` 344 PASS / 0 FAIL / 0 STALE, `check_correctness_inventory.py` exit 0.
- No blockers for closing out the v0.10.0 milestone audit's remaining tech-debt items (unrelated to DET-01, tracked separately in the audit document).

---
*Phase: 18-correctness-regression-suite-audit-docs*
*Completed: 2026-10-16*

## Self-Check: PASSED

All claimed files found on disk (`src/table_functions/ts_periods.cpp`, `src/macros/ts_macros.cpp`,
`test/sql/correctness/detection_decomposition.test`, `test/sql/ts_periods.test`,
`docs/api/07-forecasting.md`, the five `docs/reference/models/**` pages, `plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md`,
`CHANGELOG.md`, `docs/release-notes/v0.10.0.md`, this SUMMARY) and all three claimed commits
(`ed3c390`, `281560c`, `d0ea55a`) found in `git log --oneline`.
