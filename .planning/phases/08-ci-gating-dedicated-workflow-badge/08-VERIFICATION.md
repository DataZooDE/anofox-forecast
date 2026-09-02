---
phase: 08-ci-gating-dedicated-workflow-badge
verified: 2026-09-02T00:45:00Z
status: human_needed
score: "3/3 criteria structurally verified + gate mechanism proven locally; live-CI plumbing observation remains (operator push)"
behavior_unverified: 0
human_verification:
  - item: "Push the branch and confirm the wasm-runtime-test job runs GREEN in GitHub Actions (this also confirms the artifact name anofox_forecast-v1.5.5-extension-wasm_eh resolves to a real uploaded artifact)."
    why: "The needs:/download-artifact/workflow_run plumbing and the derived artifact name can only be confirmed against a live GitHub Actions run — not locally."
  - item: "Introduce a deliberate breakage in one CURATED test file, push, and confirm wasm-runtime-test turns RED; then revert and confirm it returns GREEN."
    why: "CI-01 success criterion requires observing the gate turn red on a WASM failure in live CI. NOTE: the gate's core mechanism is already proven locally (see below) — this step only confirms the GitHub Actions job surfaces the non-zero exit as a red check."
  - item: "Confirm the dedicated WASM workflow (WasmTest.yml) fires via workflow_run after the pipeline completes, and the README badge URL renders/flips with its status."
    why: "Badge rendering and workflow_run chaining are GitHub-side behaviors observable only after a push to main."
---

# Phase 8: CI Gating + Dedicated Workflow + Badge — Verification Report

**Phase Goal:** WASM load/runtime regressions fail CI automatically, and WASM health is independently observable via a dedicated workflow and a README badge — the local green from Phase 7 is now enforced on every relevant build.

**Verified:** 2026-09-02T00:45:00Z
**Status:** human_needed (structural + local-mechanism verification complete; live-CI observation is the operator's step)

---

## Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 (CI-01) | A gating CI job `needs:` the WASM build, downloads the `wasm_eh` artifact, runs the harness, turns red on WASM load/runtime error | ◆ STRUCTURAL PASS + local mechanism proven; live red-run is `backstop` | `wasm-runtime-test` job in `MainDistributionPipeline.yml`: `needs: duckdb-latest-build`, `actions/download-artifact@v4` name `anofox_forecast-v1.5.5-extension-wasm_eh`, runs `node test/wasm/run.mjs --ext "$EXT"` (no `--all`). **Local negative control:** curated run exit 0 (396 passed) → one curated assertion broken → exit **1** (RED, 1 failed) → reverted → exit 0. A GitHub Actions `run:` step fails its job on non-zero exit, so the gate mechanism is confirmed; only the live-Actions plumbing observation remains. |
| 2 (CI-02) | A dedicated WASM workflow, separate from the main pipeline, runs the harness so WASM status is independently observable | ✓ STRUCTURAL PASS | `.github/workflows/WasmTest.yml` (new): `name: WASM`, `workflow_run` on "Main Extension Distribution Pipeline", cross-run artifact download (`run-id: github.event.workflow_run.id` + `github-token`), curated harness. Valid YAML. Separate file (required for badge granularity). |
| 3 (CI-03) | The README shows a WASM status badge wired to the dedicated workflow that flips with its latest run | ✓ STRUCTURAL PASS + `backstop` for live render | README badge: `https://github.com/DataZooDE/anofox-forecast/actions/workflows/WasmTest.yml/badge.svg?branch=main`, linked to the workflow; existing badges intact. Live render/flip observable only after push. |

---

## LOCKED Constraint Compliance

**CI gates on the CURATED subset, NOT `--all`.** Verified: `grep -rE "run\.mjs.*--all" .github/workflows/` returns nothing; both harness invocations are `node test/wasm/run.mjs --ext "$EXT"` (curated 8-file / 396-assertion subset). This is correct — the full `--all` run carries 184 pre-existing test-debt failures unrelated to WASM (see 07-VERIFICATION.md); gating on it would make CI permanently red.

---

## Requirements Coverage

| Requirement | Status | Evidence |
|-------------|--------|----------|
| CI-01 | ◆ Structural + local mechanism verified; live red-run = operator step | Gating job wired; harness returns non-zero on curated failure (proven locally) |
| CI-02 | ✓ SATISFIED | Dedicated `WasmTest.yml` created, `workflow_run`-triggered, curated harness |
| CI-03 | ✓ SATISFIED (structural) | README badge wired to `WasmTest.yml/badge.svg?branch=main` |

---

## Human Verification Required

The remaining verification is a **live GitHub Actions push** the operator must perform (see frontmatter `human_verification`). It cannot and should not be done autonomously (pushing to the remote + observing Actions/badge is the operator's call). The gate's pass/fail mechanism is already proven locally; what remains is confirming the Actions YAML plumbing and badge behave as designed against a real run.

**Resume:** `/gsd-verify-work 8` after observing green → red → green in CI, or reply "verified" with the run URLs.

---

_Verified: 2026-09-02T00:45:00Z_
_Verifier: Claude (autonomous orchestrator — structural + local-mechanism verification)_
