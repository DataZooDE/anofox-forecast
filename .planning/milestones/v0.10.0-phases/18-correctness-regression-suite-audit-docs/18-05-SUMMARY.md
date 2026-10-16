---
phase: 18-correctness-regression-suite-audit-docs
plan: 05
subsystem: forecasting
tags: [anofox-forecast, autoarima, sarima, r-forecast, gap-closure, docs]

requires:
  - phase: 17-upstream-consumption-diagnostics-parity (plan 17-03)
    provides: FCST-09 R forecast parity suite, D-12 root-cause table (RC-01..RC-05), D-16 AutoARIMA checkpoint decision
  - phase: 18-correctness-regression-suite-audit-docs (plans 18-01..18-04)
    provides: json statically linked, correctness suite + inventory gate, make audit, make check-docs, CHANGELOG/release-notes consolidation
provides:
  - anofox-forecast 0.16.2 consumed from crates.io (no [patch]/git/path override)
  - RC-05 (AutoARIMA AirPassengers 95% interval) flipped from known_gap to parity, measured not loosened
  - repro_rc_05 inverted into a standing regression guard (no longer #[ignore]d)
  - check_root_causes.py's D-16 one-allowed-AutoARIMA-gap carve-out retired, replaced by a general resolved-row exemption
  - docs/skills/CHANGELOG/release-notes stop describing the AutoARIMA interval as a known gap
affects: [forecasting, validation-reference, docs]

actuals:
  tokens: 16870
  tasks: 3
  commits: 3

tech-stack:
  added: []
  patterns:
    - "Root-cause row resolution convention: resolution field prefix ('fixed:' for extension-side, 'resolved:' for crate-side upstream fixes) drives exemption from autoarima_gaps, replacing a hardcoded single-check carve-out"
    - "Regenerate R fixtures via Rscript and diff key-by-key against the pre-bump copy to prove only tolerance/root-cause metadata changed, never the R-derived data values"

key-files:
  created: []
  modified:
    - Cargo.toml
    - Cargo.lock
    - crates/anofox-fcst-core/tests/r_forecast_parity.rs
    - crates/anofox-fcst-core/tests/r_parity_diagnosis.rs
    - validation/reference/fixtures/r_forecast/air_passengers.json
    - validation/reference/fixtures/r_forecast/simulated_series.json
    - validation/reference/r/gen_forecast_reference.R
    - validation/reference/README.md
    - .planning/phases/17-upstream-consumption-diagnostics-parity/diagnosis/check_root_causes.py
    - .planning/phases/17-upstream-consumption-diagnostics-parity/diagnosis/upstream-issues/RC-05-autoarima-sarima-interval-variance.md
    - test/sql/ts_forecast_reference.test
    - docs/api/07-forecasting.md
    - docs/reference/models/state-space/auto_arima.md
    - plugins/anofox-forecast/skills/anofox-forecast-models/SKILL.md
    - plugins/anofox-forecast/skills/anofox-forecast-backtest/SKILL.md
    - plugins/anofox-forecast/skills/anofox-forecast-data-prep/SKILL.md
    - plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md
    - plugins/anofox-forecast/skills/anofox-forecast-eda/SKILL.md
    - CHANGELOG.md
    - docs/release-notes/v0.10.0.md

key-decisions:
  - "air/auto_arima/AutoARIMA interval_tol set to 7e-3 (= 1.5x measured 4.592775e-3, rounded up to 1 significant digit) -- the same D-15 tolerance rule used for every other parity-tier AutoARIMA row, not a bespoke looser number."
  - "trend/auto_arima/AutoARIMA's tolerance tightened 6e-3 -> 1e-6 (the 1e-6 floor now binds on its re-measured 8.064310e-10 interval_dev) since the plan requires any moved ARIMA-family check to be re-tiered, never left looser than justified."
  - "repro_rc_05 inverted (asserts worst-horizon ratio >= 0.95, measured 0.9951) rather than deleted -- a standing regression guard if anofox-forecast later regresses this exact SARIMA-interval-variance defect. No longer #[ignore]d."
  - "check_root_causes.py's hardcoded ALLOWED_AUTOARIMA_GAP_CHECK single-check carve-out replaced by a general is_resolved_row() check (resolution field starts with 'fixed:' or 'resolved:') -- extensible to any future resolved root cause, not just RC-05."
  - "Updated all 5 plugin skill version banners (not just anofox-forecast-models) from v0.16.1 to v0.16.2 for consistency -- a direct, in-scope Rule 1 fix caused by this plan's own dependency bump."

requirements-completed: [FCST-09, UPST-06, DOCS-01, DOCS-02]

coverage:
  - id: D1
    description: "anofox-forecast 0.16.2 consumed from crates.io with no [patch]/git/path override; cargo tree -i confirms registry source"
    requirement: "UPST-06"
    verification:
      - kind: other
        ref: "cargo tree -i anofox-forecast (shows 0.16.2, registry+https://github.com/rust-lang/crates.io-index)"
        status: pass
    human_judgment: false
  - id: D2
    description: "RC-05 resolved: AutoARIMA's AirPassengers 95% interval is hard parity, tolerance justified by measurement, never loosened; no other parity check regressed"
    requirement: "FCST-09"
    verification:
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/r_forecast_parity.rs#r_forecast_parity_checks (36/36 pass, air/auto_arima now status=parity)"
        status: pass
      - kind: unit
        ref: "crates/anofox-fcst-core/tests/r_parity_diagnosis.rs#repro_rc_05 (inverted, worst_ratio=0.9951 >= 0.95)"
        status: pass
      - kind: other
        ref: ".planning/phases/17-upstream-consumption-diagnostics-parity/diagnosis/check_root_causes.py --stage final (rows=5 invalid=0 uncovered=0 missing_drafts=0 repro_mismatch=0 orphans=0 uncaused=0 autoarima_gaps=0)"
        status: pass
    human_judgment: false
  - id: D3
    description: "Docs, models skill, CHANGELOG and release notes stop describing the AutoARIMA AirPassengers interval as a known gap; make check-docs stays 0 FAIL / 0 STALE"
    requirement: "DOCS-01"
    verification:
      - kind: other
        ref: "make check-docs (343 PASS, 0 FAIL, 8 EXPECTED-ERROR, 154 SYNOPSIS, 4 SKIP-NETWORK, 0 STALE)"
        status: pass
      - kind: other
        ref: "grep -rn RC-05 sweep across docs/plugins/CHANGELOG -- every live-doc hit reads as resolved, not open"
        status: pass
    human_judgment: false
  - id: D4
    description: "CHANGELOG and release notes record the anofox-forecast 0.16.2 dependency bump and the RC-05 resolution as a numerical-output behaviour change"
    requirement: "DOCS-02"
    verification:
      - kind: manual
        ref: "CHANGELOG.md [Unreleased] v0.10.0 section (numerical-output-changes row + new RC-05-resolved bullet); docs/release-notes/v0.10.0.md (dependency line + AutoARIMA highlight)"
        status: pass
    human_judgment: true
    rationale: "CHANGELOG/release-notes wording quality is a judgment call about clarity and completeness, not something a test asserts."
  - id: D5
    description: "Full release DoD green on the 0.16.2 tree: fmt, clippy, cargo test --workspace, GEN=ninja make release, full SQL suite (baseline parity), make audit, check-docs, correctness inventory, objdump NEEDED"
    verification:
      - kind: other
        ref: "cargo fmt --check; cargo clippy --all-targets --all-features -- -D warnings; cargo test --workspace --release"
        status: pass
      - kind: integration
        ref: 'build/release/test/unittest "test/sql/*" (All tests passed, 5580 assertions in 94 test cases -- matches 18-04 baseline exactly)'
        status: pass
      - kind: other
        ref: "make audit (34 hits, 34 allow-listed, 0 violations, 0 stale; 0 un-allow-listed length-only tests); check_correctness_inventory.py (exit 0, 116 registered, 0 missing coverage, 2 intentionally-removed)"
        status: pass
      - kind: other
        ref: "objdump -p build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension | grep NEEDED (libgcc_s, libm, libstdc++, libc, ld-linux only -- no ssl/crypto)"
        status: pass
    human_judgment: false

duration: ~50min
completed: 2026-10-16
status: complete
---

# Phase 18 Plan 05: anofox-forecast 0.16.2 Gap Closure Summary

**Consumed anofox-forecast 0.16.2 from crates.io, flipped RC-05 (AutoARIMA's AirPassengers 95% interval) from a documented known gap to hard, measured R parity, and swept every doc/skill/CHANGELOG reference to match.**

## Performance

- **Duration:** ~50 min
- **Tasks:** 3 (all `type="auto"`, no checkpoints)
- **Files modified:** 20
- **Commits:** 3

## Accomplishments

- **Task 1 — dependency bump + re-measurement.** `Cargo.toml` pins `anofox-forecast = "0.16.2"` (no `[patch]`/git/path override; the argmin patch is untouched). `cargo tree -i anofox-forecast` confirms `v0.16.2` from `registry+https://github.com/rust-lang/crates.io-index`. Re-ran the R-parity suite and captured before (0.16.1, from Phase 17's own measurements) vs after (0.16.2, measured fresh):
  - `air/auto_arima/AutoARIMA`: interval_dev `1.846662e-1` -> `4.592775e-3` (point_dev `3.982169e-4` unchanged).
  - `rw/auto_arima/AutoARIMA`: interval_dev `3.276619e-8` -> `8.062277e-10`.
  - `trend/auto_arima/AutoARIMA`: interval_dev `3.361326e-3` -> `8.064310e-10`.
  - `repro_rc_05`'s worst crate/R half-width ratio: `0.818` -> `0.9951`.
  - Every other R-parity check (ETS/Holt/Theta/CrostonClassic families, 33 of 36 checks) is unaffected -- point_dev/interval_dev values identical to the 0.16.1 measurement, confirmed by a full `cargo test -p anofox-fcst-core --release` run (every test binary green).

- **Task 2 — RC-05 flipped to resolved, re-tiered, repro inverted.** Both R fixtures regenerated via `Rscript validation/reference/r/gen_forecast_reference.R` (R + `forecast`/`jsonlite` available locally) and diffed key-by-key against pre-bump copies: only `tolerance.root_causes[RC-05]` and the three `auto_arima` `parity_checks` rows' `status`/`cause`/`reason`/`follow_up`/`measured_*`/`*_tol` fields changed -- every R-derived data value (fits, points, intervals) is byte-identical, confirming the fixture regeneration touched nothing it shouldn't have.
  - `air/auto_arima/AutoARIMA`: `status` `known_gap` -> `parity`, `cause` `"RC-05"` -> `""`, `interval_tol` `0.3` -> `7e-3` (1.5x measured, rounded up to 1 sig fig -- the same D-15 rule used everywhere else).
  - `rw/auto_arima/AutoARIMA`: re-measured only, tolerance unchanged (the `1e-6` floor already bound).
  - `trend/auto_arima/AutoARIMA`: re-measured AND re-tiered, `interval_tol` `6e-3` -> `1e-6` (the floor now binds) -- tightened, never loosened.
  - `RC-05` root-cause row: `resolution` now `"resolved: fixed in anofox-forecast 0.16.2 (upstream PR #230, shipping ... PR #229) ..."`, `upstream` now `"fixed: anofox-forecast 0.16.2 (...)"` (was `"draft: RC-05-....md"`).
  - `check_root_causes.py`: removed the hardcoded `ALLOWED_AUTOARIMA_GAP_CHECK = "air/auto_arima/AutoARIMA"` D-16 carve-out; added a general `is_resolved_row()` helper (resolution starts with `"fixed:"` or `"resolved:"`) that exempts a resolved crate-location root-cause row from `autoarima_gaps` -- extensible to any future resolved RC, not a single hardcoded exception. `--stage final`: `rows=5 invalid=0 uncovered=0 missing_drafts=0 repro_mismatch=0 orphans=0 uncaused=0 autoarima_gaps=0`.
  - `r_forecast_parity.rs`: removed the D-16 "one allowed gap" special case -- every `AutoARIMA` check (air/rw/trend) must now be `status=parity`, full stop.
  - `repro_rc_05` (`r_parity_diagnosis.rs`): inverted, not deleted -- now asserts the fix holds (`worst_ratio >= 0.95`, measured `0.9951`) as a standing regression guard, and is no longer `#[ignore]d` (runs in the default `cargo test` suite).
  - RC-05's upstream-issue draft marked `RESOLVED` at the top (original diagnosis body kept unmodified below it as the historical record).
  - `test/sql/ts_forecast_reference.test`: stale "D-16 known gap, not asserted here" comments updated (no literal interval values were ever pinned in SQL for this check, so no numeric assertions needed to change).

- **Task 3 — docs/skills/CHANGELOG/release-notes sweep + full DoD.** `docs/api/07-forecasting.md` (parity table row, "24 of 36 / 12 known_gap" -> "25 of 36 / 11 known_gap", ~18% narrative rewritten past-tense with the 0.16.2 numbers), `docs/reference/models/state-space/auto_arima.md`, the models skill's parity summary line, and all 5 plugin skill version banners (`v0.16.1` -> `v0.16.2`, a direct Rule 1 fix) all updated. `validation/reference/README.md`'s AutoARIMA section, parity table, and root-cause table row all updated to the resolved/re-measured values. `CHANGELOG.md`'s numerical-output-changes row now cites `0.16.2`, plus a new dedicated bullet documenting the resolution (the original Phase 17 bullet is left otherwise intact as the historical record of what `0.16.1` shipped, with a forward pointer). `docs/release-notes/v0.10.0.md`'s dependency line and AutoARIMA highlight bullet bumped to `0.16.2` / interval parity.

  Full release DoD, all green on the 0.16.2 tree:
  - `cargo fmt --check`: clean.
  - `cargo clippy --all-targets --all-features -- -D warnings`: clean.
  - `cargo test --workspace --release`: every test binary + doc-tests green.
  - `GEN=ninja make release`: builds clean.
  - `build/release/test/unittest "test/sql/*"`: **All tests passed (5580 assertions in 94 test cases)** -- matches the plan's baseline exactly.
  - `make audit`: 34 hits, 34 allow-listed, 0 violations, 0 stale entries; 0 un-allow-listed length-only tests.
  - `make check-docs`: 343 PASS, 0 FAIL, 8 EXPECTED-ERROR, 154 SYNOPSIS, 4 SKIP-NETWORK, 0 STALE.
  - `check_correctness_inventory.py`: exit 0, 116 CHANGELOG-mentioned registered functions, 0 missing correctness coverage, 2 not registered (the same intentionally-removed `ts_backtest_auto_by` / `_ts_backtest_native` from 18-04, informational only).
  - `objdump -p build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension | grep NEEDED`: `libgcc_s.so.1`, `libm.so.6`, `libstdc++.so.6`, `libc.so.6`, `ld-linux-x86-64.so.2` only -- no `ssl`/`crypto`.
  - Repo-wide `grep -rn "RC-05"` sweep: every hit in a live doc (docs/, plugins/, CHANGELOG.md, validation/reference/README.md, the Rust test files) now reads as resolved; the remaining "open" style hits are confined to Phase 11/17 SUMMARY files, `.planning/PROJECT.md`'s architecture-decision log, `.planning/STATE.md` (orchestrator-owned, left untouched per this plan's instructions), and the RC-05 diagnosis draft's own historical body below its new "RESOLVED" banner.

## Task Commits

Each task was committed atomically:

1. **Task 1: bump to 0.16.2 and re-measure R parity** - `08db5fc` (feat)
2. **Task 2: flip RC-05 to resolved, re-tier, retire repro_rc_05** - `0c52f36` (fix)
3. **Task 3: docs, skill, CHANGELOG, release notes, full DoD** - `2cbb500` (docs)

_Note: no TDD-split commits in this plan -- `type="auto"` tasks, not `tdd="true"`._

## Files Created/Modified

- `Cargo.toml`, `Cargo.lock` - anofox-forecast 0.16.1 -> 0.16.2
- `crates/anofox-fcst-core/tests/r_forecast_parity.rs` - removed the D-16 single-exception carve-out
- `crates/anofox-fcst-core/tests/r_parity_diagnosis.rs` - `repro_rc_05` inverted, no longer `#[ignore]`d
- `validation/reference/fixtures/r_forecast/{air_passengers,simulated_series}.json` - regenerated via R, tolerance/root-cause metadata only
- `validation/reference/r/gen_forecast_reference.R` - RC-05 row + three `auto_arima` parity_checks rows updated
- `validation/reference/README.md` - AutoARIMA section, parity table, root-cause table
- `.planning/phases/17-upstream-consumption-diagnostics-parity/diagnosis/check_root_causes.py` - `is_resolved_row()` helper replaces the hardcoded D-16 carve-out
- `.planning/phases/17-upstream-consumption-diagnostics-parity/diagnosis/upstream-issues/RC-05-autoarima-sarima-interval-variance.md` - marked RESOLVED at top
- `test/sql/ts_forecast_reference.test` - stale comment updates (no numeric pins changed)
- `docs/api/07-forecasting.md`, `docs/reference/models/state-space/auto_arima.md` - AutoARIMA parity narrative updated
- `plugins/anofox-forecast/skills/anofox-forecast-{models,backtest,data-prep,detection,eda}/SKILL.md` - parity summary + version banners
- `CHANGELOG.md`, `docs/release-notes/v0.10.0.md` - dependency bump + RC-05 resolution documented

## Decisions Made

See `key-decisions` in frontmatter -- summarized: the new tolerance for `air/auto_arima` (7e-3) follows the existing D-15 "1.5x measured, rounded up to 1 sig fig" rule rather than a bespoke number; `trend/auto_arima`'s tolerance was tightened (never loosened) to match its much-improved measurement; `repro_rc_05` is inverted rather than deleted so a future crate regression of this exact defect is caught; `check_root_causes.py`'s single hardcoded exception was generalized into a reusable "resolved row" convention; all 5 plugin skills' version banners were bumped for consistency, not just the models skill the plan named.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Stale crate-version banners in 4 sibling plugin skills**
- **Found during:** Task 3, docs/skill sweep
- **Issue:** `anofox-forecast-backtest`, `-data-prep`, `-detection`, and `-eda` SKILL.md files all carried a header banner reading `anofox-forecast v0.16.1`, now factually wrong after this plan's own dependency bump. The plan's action text named only the `anofox-forecast-models` skill explicitly.
- **Fix:** Updated all 5 plugin skill banners (including `anofox-forecast-models`) from `v0.16.1` to `v0.16.2` in one `sed` pass, verified by grep.
- **Files modified:** `plugins/anofox-forecast/skills/anofox-forecast-{models,backtest,data-prep,detection,eda}/SKILL.md`
- **Verification:** `grep -rn "anofox-forecast\` v0.16" plugins/anofox-forecast/skills/` shows `v0.16.2` in all 5.
- **Committed in:** `2cbb500` (Task 3 commit)

---

**Total deviations:** 1 auto-fixed (Rule 1, stale version banners caused by this plan's own dependency bump)
**Impact on plan:** No scope creep -- the fix is a direct, inevitable consequence of the version bump this plan performs; leaving 4 of 5 sibling skill banners stale would have been inconsistent and misleading.

## Known Stubs

None. No stub patterns (hardcoded empty values, placeholder text, unwired data sources) were introduced by this plan.

## Threat Flags

None. This plan only changes a dependency version, test tolerances/root-cause metadata, and documentation -- no new network endpoints, auth paths, file-access patterns, or schema changes at a trust boundary.

## Issues Encountered

None. R, the `forecast`/`jsonlite` R packages, and the full Rust/C++/DuckDB toolchain were all available locally; no auth gates, no blocked package installs, no architectural decisions needed.

## User Setup Required

None -- no external service configuration required. anofox-forecast 0.16.2 was already confirmed published on crates.io by the orchestrator before this plan was dispatched.

## Next Phase Readiness

- RC-05 is closed: the FCST-09 suite is now 25 of 36 `parity` / 11 `known_gap` (was 24/12), with every remaining `known_gap` row backed by an identified, non-regressed root cause (RC-01 fixed, RC-02 reference-not-a-defect, RC-03/RC-04 still open upstream drafts).
- `anofox-forecast` 0.16.2 is the pinned, registry-sourced dependency with no override; the full release DoD (fmt/clippy/test/build/SQL-suite/audit/check-docs/inventory/objdump) is green on this tree.
- Requirements FCST-09 (already Complete per Phase 17) and UPST-06 (already Complete per Phase 17) are reaffirmed by this plan's re-measurement; DOCS-01 and DOCS-02 are completed by this plan's docs/skill/CHANGELOG sweep (building on 18-04's prior work on those same IDs).
- No blockers. This was the last outstanding Phase 18 gap item per `.planning/STATE.md`'s `stopped_at` note (orchestrator-owned; not edited by this plan) -- Phase 18 verification can proceed.

## Self-Check: PASSED

- Confirmed on disk: `Cargo.toml` pins `0.16.2`; `validation/reference/fixtures/r_forecast/air_passengers.json`'s `auto_arima` parity_checks row has `"status": "parity"`; `check_root_causes.py` defines `is_resolved_row`; `r_parity_diagnosis.rs`'s `repro_rc_05` has no `#[ignore]` attribute above it.
- Confirmed in `git log --oneline`: `08db5fc`, `0c52f36`, `2cbb500` all present on `fix/statistical-correctness`.
- `git rev-list --count dc0b05f..HEAD` (this plan's `plan_head_before`) = `3`, matching `actuals.commits` (SUMMARY commit not yet made at measurement time, as expected).

---
*Phase: 18-correctness-regression-suite-audit-docs*
*Completed: 2026-10-16*
