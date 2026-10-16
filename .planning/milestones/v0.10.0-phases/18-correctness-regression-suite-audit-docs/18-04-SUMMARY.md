---
phase: 18-correctness-regression-suite-audit-docs
plan: 04
subsystem: docs
tags: [docs, skills, changelog, release-notes, snippet-harness, DOCS-01, DOCS-02]

requires:
  - phase: 18-correctness-regression-suite-audit-docs (plans 18-01..18-03)
    provides: json statically linked, correctness suite + inventory gate, make audit
provides:
  - Permanent docs/skills snippet + function-name checker (scripts/check_doc_snippets.py, make check-docs)
  - docs/ and plugins/anofox-forecast/skills/ swept to 0 FAIL / 0 STALE against the built extension
  - CHANGELOG "Breaking changes in v0.10.0" consolidation and docs/release-notes/v0.10.0.md
affects: [docs, skills, release]

actuals:
  tasks: 3
  commits: 4

key-files:
  created:
    - scripts/check_doc_snippets.py
    - scripts/doc_snippets/fixtures.sql
    - scripts/doc_snippets/expected_errors.txt
    - scripts/doc_snippets/synopsis.txt
    - scripts/doc_snippets/removed_names.txt
    - docs/release-notes/v0.10.0.md
  modified:
    - Makefile
    - CHANGELOG.md
    - docs/API_REFERENCE.md
    - docs/api/00-api-design.md
    - docs/api/02-hierarchical.md
    - docs/api/03-statistics.md
    - docs/api/04-data-preparation.md
    - docs/api/05-period-detection.md
    - docs/api/07-forecasting.md
    - docs/api/08-cross-validation.md
    - docs/api/09-evaluation-metrics.md
    - docs/guides/02-model-selection.md
    - plugins/anofox-forecast/skills/anofox-forecast-backtest/SKILL.md
    - plugins/anofox-forecast/skills/anofox-forecast-data-prep/SKILL.md
    - plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md
    - plugins/anofox-forecast/skills/anofox-forecast-eda/SKILL.md
    - test/sql/correctness/metrics.test
    - test/sql/correctness/conformal_bootstrap.test

key-decisions:
  - "Per-phase snippet harnesses promoted to one permanent checker (D-06) with explicit EXPECTED-ERROR / SYNOPSIS / SKIP-NETWORK classifications and a removed-names STALE gate, so docs drift fails `make check-docs` instead of shipping."
  - "CHANGELOG naming three functions without correctness coverage (ts_smape_by, ts_r2_by, ts_conformal_predict_asymmetric) tripped the inventory gate; resolved by adding closed-form / fixture-sourced pins, not by trimming the CHANGELOG."

requirements-completed: [DOCS-01, DOCS-02]

coverage:
  - id: D1
    description: "Every docs/README/skill SQL snippet executes against the built extension"
    requirement: "DOCS-01"
    verification:
      - kind: integration
        ref: "make check-docs -> 343 PASS, 0 FAIL, 8 EXPECTED-ERROR, 154 SYNOPSIS, 4 SKIP-NETWORK, 0 STALE"
        status: pass
    human_judgment: false
  - id: D2
    description: "CHANGELOG breaking-changes section + release notes list every numerical-output behaviour change"
    requirement: "DOCS-02"
    verification:
      - kind: manual
        ref: "CHANGELOG.md [Unreleased] v0.10.0 section; docs/release-notes/v0.10.0.md"
        status: pass
    human_judgment: true
---

# Phase 18 Plan 04: Docs, Skills, CHANGELOG and Release Notes Summary

**One permanent `make check-docs` snippet checker, docs/ and all four affected skills swept to 0 FAIL / 0 STALE against the built extension, and a consolidated "Breaking changes in v0.10.0" CHANGELOG section plus `docs/release-notes/v0.10.0.md`.**

## Execution note

The executor committed all three tasks (d600404, 12b3642, 0702658) and then stalled for ~40 minutes during the final DoD without writing this SUMMARY. The orchestrator stopped it, re-ran the DoD itself, fixed the one gate that failed (inventory, below) and wrote this SUMMARY from the commits and its own command output.

## Commits

1. **Task 1: permanent snippet + function-name checker (D-06)**: `d600404`
2. **Task 2: docs/skills sweep to 0 FAIL / 0 STALE, corrected definitions (DOCS-01)**: `12b3642`
3. **Task 3: CHANGELOG breaking-changes consolidation + release notes (DOCS-02)**: `0702658`
4. **Close-out: inventory-gate coverage pins (orchestrator)**: `86d20da`

## Final DoD (run by the orchestrator at 86d20da)

- `make check-docs`: `343 PASS, 0 FAIL, 8 EXPECTED-ERROR, 154 SYNOPSIS, 4 SKIP-NETWORK, 0 STALE`
- `make audit`: 0 violations; `0 un-allow-listed length-only tests`
- `check_correctness_inventory.py`: exit 0, `116 CHANGELOG-mentioned registered functions, 0 missing correctness coverage, 2 not registered` (the 2 are the intentionally removed `ts_backtest_auto_by` / `_ts_backtest_native`, informational only)
- `build/release/test/unittest "test/sql/*"`: `All tests passed (5567 assertions in 94 test cases)`
- `objdump -p ... | grep NEEDED`: libgcc_s, libm, libstdc++, libc, ld-linux only; no ssl/crypto
- No C++ or Rust file changed in this plan, so the extension did not need rebuilding (the binary under test was already built from the code at this HEAD). cargo fmt / clippy / test status is unchanged from 18-02's green run.

## Deviations

**1. [Rule 1 - Gate fix] Inventory gate failed after the CHANGELOG rewrite**
- **Found during:** orchestrator close-out DoD
- **Issue:** the consolidated CHANGELOG names `ts_smape_by`, `ts_r2_by` and `ts_conformal_predict_asymmetric`, none of which had a `test/sql/correctness/*.test` pin, so `check_correctness_inventory.py` exited 1.
- **Fix:** closed-form pins on the 10-row probe for sMAPE (`mean_i 2/(2i+1) * 100 = 23.6174915556`) and R2 (`1 - 10/82.5`); fixture-sourced pin for the asymmetric conformal margins (`asym_n20`: lower 0.81, upper 3.5125 at alpha 0.2).
- **Commit:** `86d20da`

## Deferred (logged in deferred-items.md)

- `examples/metrics/README.md` and `examples/backtesting/m5_backtest_examples.sql` still use the bare-table-name metric `_by` form; the checker does not scan `examples/`.
- `ts_features_config_from_json` / `_csv` ignore the `"features"` allow-list (all 117 features returned). The EDA skill documents the observed behaviour instead of claiming the filter works.

## Self-Check: PASSED

- All four commits present in `git log`; files listed under key-files exist on disk.
