# Phase 8: CI Gating + Dedicated Workflow + Badge — Research

**Researched:** 2026-09-02
**Domain:** GitHub Actions CI wiring — WASM gating job, dedicated workflow, README badge
**Confidence:** HIGH

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**LOCKED — CI must gate on the CURATED subset, not `--all`**

This is the single most important constraint (from Phase 7 verification). The CI gate MUST run the harness against the **curated green subset** (`node test/wasm/run.mjs` — 8 files / 396 assertions, exit 0), NOT `node test/wasm/run.mjs --all`. The full 66-file `--all` run currently has 184 failures across 23 files that are **pre-existing `test/sql` test debt** (removed/renamed API refs, DATE+BIGINT bugs, cascades) — NOT WASM problems. Gating on `--all` would make CI permanently red on unrelated test debt. Full-suite green is tracked separately as an out-of-scope triage effort.

The gate's job is to catch **WASM load/runtime regressions** (extension fails to LOAD, harness crashes, curated assertions regress) — which the curated subset does exactly.

### Claude's Discretion

All other implementation choices are at Claude's discretion — pure CI/infrastructure phase. Use the ROADMAP goal, the three success criteria, the anofox-statistics PR #131 reference (`WasmTest.yml` + `wasm-runtime-test` job), and this repo's existing `.github/workflows/` conventions.

### Deferred Ideas (OUT OF SCOPE)

- Making the full 66-file `--all` suite green (pre-existing test-suite triage) — do NOT gate CI on it.
- Browser-based (not just Node) WASM E2E harness (WASM-F1) — deferred at milestone open.
- Shared-memory `wasm_threads` build (WASM-F2) — blocked upstream.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| CI-01 | Gating job that `needs:` the wasm build, downloads `wasm_eh` artifact, runs the harness, turns red on WASM load/runtime error — verified by a broken `.wasm`/failing test producing a red run | `wasm-runtime-test` job pattern from anofox-statistics `MainDistributionPipeline.yml` (lines 68-101); artifact name `anofox_forecast-v1.5.5-extension-wasm_eh`; harness invocation `node test/wasm/run.mjs --ext "$EXT"` (curated default) |
| CI-02 | A dedicated WASM workflow separate from the main distribution pipeline | `WasmTest.yml` in anofox-statistics — triggered via `workflow_run` on "Main Extension Distribution Pipeline", downloads artifact via `run-id`, single job `wasm-runtime-test` |
| CI-03 | README badge wired to the dedicated WASM workflow | Badge URL pattern: `https://github.com/DataZooDE/anofox-forecast/actions/workflows/WasmTest.yml/badge.svg?branch=main`; placement at README.md lines 9-12 alongside existing badges |
</phase_requirements>

---

## Summary

Phase 8 is a pure CI/infrastructure port. Every technical decision has a reference implementation to copy from: `anofox-statistics` PR #131 delivered exactly this CI pattern (`WasmTest.yml` + the `wasm-runtime-test` gating job inside `MainDistributionPipeline.yml`). The port is mechanical: substitute `anofox_statistics` → `anofox_forecast` and change the harness invocation from `--all` to the curated default.

The single structural difference from the reference is the harness invocation. The `anofox-statistics` workflow runs `node test/wasm/run.mjs --all --ext "$EXT"` because its test suite is fully green. This repo MUST NOT pass `--all` — the locked constraint requires the curated default (no `--all` flag, no `--file` flag), which runs exactly the 8-file CURATED array (396 assertions, exit 0). The harness exit code is the gate: exit 0 → green, exit non-zero → red.

The two-workflow structure (CI-01 gating job inside the main pipeline + CI-02 dedicated WASM workflow) satisfies both requirements with distinct purposes: the gating job blocks PRs/pushes when WASM breaks; the dedicated workflow powers a badge that is immune to unrelated pipeline failures (a flaky Linux smoke test would flip the main pipeline red but leave the WASM badge green, which is the correct signal).

**Primary recommendation:** Port `WasmTest.yml` verbatim (substituting extension name and harness invocation); add the `wasm-runtime-test` gating job to `MainDistributionPipeline.yml` after `duckdb-latest-build`; add the badge to README.md line 12.

---

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| CI gating (CI-01) | MainDistributionPipeline.yml — `wasm-runtime-test` job | — | Lives in the main pipeline so it blocks PR merges via branch-protection required-checks |
| Dedicated WASM workflow (CI-02) | WasmTest.yml — standalone workflow file | — | Separate workflow required for badge isolation; `workflow_run` trigger fires after the main pipeline without rebuilding the extension |
| README badge (CI-03) | README.md header badge block | — | Badge URL points at the dedicated workflow file name, not the main pipeline |

---

## Standard Stack

### Core

| Component | Version/Pattern | Purpose | Why Standard |
|-----------|----------------|---------|--------------|
| `actions/download-artifact@v4` | v4 | Download `wasm_eh` artifact in both jobs | Same version used throughout this repo's existing workflows [VERIFIED: .github/workflows/_extension_deploy.yml:126] |
| `actions/setup-node@v4` | v4 | Install Node 20 for harness execution | Same version used in anofox-statistics reference [VERIFIED: anofox-statistics/.github/workflows/WasmTest.yml:38] |
| `workflow_run` trigger | GitHub Actions native | CI-02 dedicated workflow fires after the main pipeline completes | Allows reusing the artifact produced by the main pipeline without rebuilding; avoids a second wasm_eh build [VERIFIED: anofox-statistics/.github/workflows/WasmTest.yml:14-17] |
| `actions/checkout@v4` | v4 | Checkout repo to access `test/wasm/` | Required to run `npm --prefix test/wasm install` and `node test/wasm/run.mjs` |

### Artifact name (critical — must match exactly)

The artifact name is constructed by `duckdb/extension-ci-tools/_extension_distribution.yml` and must be referenced verbatim. From the deploy workflow:

```
${{ inputs.extension_name }}-${{ inputs.duckdb_version }}-extension-${{matrix.duckdb_arch}}${{...}}.wasm
```

For this repo's `duckdb-latest-build` job (v1.5.5, `@v1.5-variegata`), the wasm_eh artifact name is:

**`anofox_forecast-v1.5.5-extension-wasm_eh`**

[VERIFIED: .github/workflows/MainDistributionPipeline.yml:60-67 — `duckdb-latest-build` uses `duckdb_version: v1.5.5`, `extension_name: anofox_forecast`; deploy workflow line 128 confirms pattern `${extension_name}-${duckdb_version}-extension-${duckdb_arch}` (no `.wasm` suffix in artifact name — the `.wasm` suffix appears only in the artifact file path inside the downloaded artifact, not in the artifact name itself)]

Note: the LTS build (`duckdb-lts-build`, v1.4.5) does NOT have a matching `@duckdb/duckdb-wasm` npm package for Node harness testing. Per Phase 7 research and the CONTEXT.md specifics, the gate covers only v1.5.5 wasm_eh.

---

## Architecture Patterns

### Pattern 1: CI-01 Gating Job Inside MainDistributionPipeline.yml

**What:** A job added to the existing main pipeline that `needs: duckdb-latest-build`, downloads the `wasm_eh` artifact produced by that build job (available within the same workflow run via `actions/download-artifact@v4` without `run-id`), and runs the curated harness subset.

**When to use:** Blocks PRs and pushes. The job result is what branch-protection rules reference for "required status checks."

**Exact YAML shape** (ported from anofox-statistics `MainDistributionPipeline.yml` lines 68-101, with two substitutions):

```yaml
  wasm-runtime-test:
    name: WASM runtime test (Node harness, v1.5.5 wasm_eh)
    # A green wasm build only proves the extension COMPILES + LINKS. This job
    # proves it actually LOADS and RUNS in DuckDB-Wasm. It consumes the wasm_eh
    # artifact the build job already produced and runs the curated harness subset
    # (8 files / ~396 assertions). Gating: no continue-on-error.
    needs: duckdb-latest-build
    if: ${{ !cancelled() && needs.duckdb-latest-build.result == 'success' }}
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        # Submodules not needed: harness only uses test/ + downloaded .wasm.

      - name: Download built wasm_eh extension
        uses: actions/download-artifact@v4
        with:
          name: anofox_forecast-v1.5.5-extension-wasm_eh
          path: wasm-artifact

      - uses: actions/setup-node@v4
        with:
          node-version: 20

      - name: Install harness dependencies
        run: npm --prefix test/wasm install

      - name: Run DuckDB-Wasm load + runtime harness (curated subset)
        run: |
          EXT=$(find wasm-artifact -name 'anofox_forecast.duckdb_extension.wasm' | head -1)
          if [ -z "$EXT" ]; then echo "::error::built wasm extension not found in artifact"; exit 1; fi
          echo "Testing extension: $EXT"
          node test/wasm/run.mjs --ext "$EXT"
```

**Key differences from reference:**
- `anofox_statistics` → `anofox_forecast` (artifact name + find glob)
- `--all` flag **removed** — curated default runs without it [LOCKED constraint]
- Artifact name: `anofox_forecast-v1.5.5-extension-wasm_eh`

[VERIFIED: anofox-statistics/.github/workflows/MainDistributionPipeline.yml:68-101 — source of the pattern]

### Pattern 2: CI-02 Dedicated WASM Workflow (WasmTest.yml)

**What:** A standalone workflow file `.github/workflows/WasmTest.yml` that triggers via `workflow_run` after the main pipeline completes. It re-downloads the `wasm_eh` artifact from the triggering workflow run using `run-id: ${{ github.event.workflow_run.id }}` and `github-token: ${{ secrets.GITHUB_TOKEN }}`, then runs the same curated harness.

**Why separate:** GitHub Actions badge URLs track a whole workflow, not a single job. If the badge pointed at `MainDistributionPipeline.yml`, it would flip red on any unrelated pipeline failure (flaky Linux smoke test, Rust clippy, etc.). The dedicated workflow ensures the badge means exactly "the WASM extension loads and runs correctly." [VERIFIED: anofox-statistics/.github/workflows/WasmTest.yml:1-11 — the file header comment states this rationale verbatim]

**Exact YAML** (ported verbatim from anofox-statistics `WasmTest.yml`, with three substitutions):

```yaml
# Dedicated WASM status workflow — powers the "WASM" badge in README.
#
# GitHub status badges track a whole workflow, not a single job. The WASM
# runtime test also runs as a gating job inside MainDistributionPipeline.yml
# (blocking PRs/pushes), but a badge pointed at that pipeline would flip red on
# ANY unrelated failure (e.g. a flaky linux smoke test). This workflow isolates
# the WASM verdict so the badge means exactly "the extension loads and runs in
# DuckDB-Wasm".
#
# It does NOT rebuild the extension: it reuses the wasm_eh artifact the
# distribution pipeline already produced, via workflow_run.
name: WASM

on:
  workflow_run:
    workflows: ["Main Extension Distribution Pipeline"]
    types: [completed]

permissions:
  actions: read
  contents: read

jobs:
  wasm-runtime-test:
    name: DuckDB-Wasm load + runtime test
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Download built wasm_eh extension from the pipeline run
        uses: actions/download-artifact@v4
        with:
          name: anofox_forecast-v1.5.5-extension-wasm_eh
          run-id: ${{ github.event.workflow_run.id }}
          github-token: ${{ secrets.GITHUB_TOKEN }}
          path: wasm-artifact

      - uses: actions/setup-node@v4
        with:
          node-version: 20

      - name: Install harness dependencies
        run: npm --prefix test/wasm install

      - name: Run DuckDB-Wasm load + runtime harness (curated subset)
        run: |
          EXT=$(find wasm-artifact -name 'anofox_forecast.duckdb_extension.wasm' | head -1)
          if [ -z "$EXT" ]; then echo "::error::built wasm extension not found in artifact"; exit 1; fi
          echo "Testing extension: $EXT"
          node test/wasm/run.mjs --ext "$EXT"
```

**Substitutions from reference:**
- `anofox_statistics` → `anofox_forecast` (artifact name + find glob)
- `--all` flag removed

[VERIFIED: anofox-statistics/.github/workflows/WasmTest.yml:1-51 — ported verbatim]

### Pattern 3: CI-03 README Badge

**Reference badge (anofox-statistics README.md line 7):**

```markdown
[![WASM](https://github.com/DataZooDE/anofox-statistics/actions/workflows/WasmTest.yml/badge.svg?branch=main)](https://github.com/DataZooDE/anofox-statistics/actions/workflows/WasmTest.yml)
```

[VERIFIED: /home/simonm/projects/duckdb/anofox-statistics/README.md:7 — verbatim]

**Adapted for this repo:**

```markdown
[![WASM](https://github.com/DataZooDE/anofox-forecast/actions/workflows/WasmTest.yml/badge.svg?branch=main)](https://github.com/DataZooDE/anofox-forecast/actions/workflows/WasmTest.yml)
```

**Placement in README.md:** After the existing badges block (lines 9-12). Current badges:

```markdown
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-BSL%201.1-blue.svg" alt="License: BSL 1.1"></a>
  <a href="https://duckdb.org"><img src="https://img.shields.io/badge/DuckDB-1.4.5%20LTS%20%7C%201.5.5-green.svg" alt="DuckDB"></a>
  <img src="https://img.shields.io/badge/build-passing-brightgreen.svg" alt="Build Status">
  <img src="https://img.shields.io/badge/Tests-295%20Rust%20tests%20passed-brightgreen.svg" alt="Tests">
```

[VERIFIED: /home/simonm/projects/duckdb/anofox-forecast/README.md:9-12]

The WASM badge should be inserted as a new `<a>` tag inside the `<p align="center">` block. Use Markdown link-image syntax wrapped in an `<a>` tag for consistency with the License badge style, or use raw Markdown if simpler:

```html
  <a href="https://github.com/DataZooDE/anofox-forecast/actions/workflows/WasmTest.yml"><img src="https://github.com/DataZooDE/anofox-forecast/actions/workflows/WasmTest.yml/badge.svg?branch=main" alt="WASM"></a>
```

The `?branch=main` query parameter is recommended: it anchors the badge to the `main` branch status so development branches don't flip the visible badge. [VERIFIED: anofox-statistics README.md:7 — uses `?branch=main`]

---

## CI-01 vs CI-02 Boundary: One Structure, Two Purposes

**Recommendation: implement both, as the reference does.**

The reference (anofox-statistics) uses both:
1. A `wasm-runtime-test` job **inside `MainDistributionPipeline.yml`** — this is CI-01. It gates on `needs: duckdb-stable-build` and blocks the pipeline if WASM breaks. [VERIFIED: anofox-statistics/MainDistributionPipeline.yml:68-101]
2. A separate `WasmTest.yml` workflow — this is CI-02. It fires via `workflow_run` and powers the badge, independent of pipeline flakiness.

They run the same harness. The duplication is intentional and serves different consumers:
- CI-01 (main pipeline job): fails PRs, blocks merges, visible in the PR check list.
- CI-02 (dedicated workflow): badge signal, runnable on-demand via `workflow_dispatch` if added, independently observable.

A single workflow cannot satisfy both: you cannot point a badge at a job (only at a whole workflow), and you cannot gate PRs with a `workflow_run`-triggered workflow (it runs after the triggering workflow completes, so its result is not available as a PR required check in time).

**Both jobs run `node test/wasm/run.mjs --ext "$EXT"` (curated default) — no `--all`.**

---

## CI-01 Negative Control: Verifying the Gate Turns Red

The success criterion (CI-01) requires demonstrating the gate actually fails on a broken `.wasm` or failing curated test. Options:

**Option A (recommended — documented manual test):** In the plan, include a verification step:
> After the gating job first runs green, temporarily corrupt the test by introducing a bad assertion in one curated test file (e.g., change an expected value in `test/sql/ts_diagnostics.test`), push to a branch, confirm the `wasm-runtime-test` job turns red, then revert.

This is the lightest credible approach — no infrastructure changes required, reproducible by any reviewer, and the result is observable in the GitHub Actions UI.

**Option B (harness exit-code self-check):** The `run.mjs` harness already exits non-zero on any assertion failure or LOAD error (verified in Phase 7 — exit 0 on curated, exit 1 on --all). The gate inherits this: if `node test/wasm/run.mjs` exits non-zero, the step fails, the job fails, the pipeline turns red. This is intrinsic — no extra verification step needed for the mechanism itself.

**Option C (artifact corruption):** Temporarily upload a zero-byte or truncated `.wasm` to force a LOAD failure. More realistic but harder to trigger in CI without a code change.

**Recommendation:** Plan includes Option A as the documented negative-control verification step (one-liner test corruption, push, observe red, revert). Option B is the implicit guarantee that the mechanism is correct by construction.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| WASM artifact download | Custom `curl`/`gh` download step | `actions/download-artifact@v4` | Native GitHub Actions artifact API; handles authentication, retry, and cross-run access via `run-id` |
| Cross-workflow artifact access | Separate build in WasmTest.yml | `workflow_run` trigger + `run-id: ${{ github.event.workflow_run.id }}` | Reuses the artifact the main pipeline already built; no Emscripten toolchain needed in the badge workflow |
| Badge isolation | Per-job badge URL | Dedicated `WasmTest.yml` workflow | GitHub badge URLs resolve to workflow level, not job level |
| Node setup | Manual `apt install nodejs` | `actions/setup-node@v4` | Handles PATH, caching, version management |

---

## Common Pitfalls

### Pitfall 1: Using `--all` in the CI harness invocation
**What goes wrong:** CI becomes permanently red on 184 pre-existing test failures unrelated to WASM.
**Why it happens:** The reference (`anofox-statistics`) uses `--all` because its suite is fully green. This repo's suite is not.
**How to avoid:** Never pass `--all` in either the gating job or the dedicated workflow. The curated default (`node test/wasm/run.mjs --ext "$EXT"` with no further flags) runs exactly the 8-file CURATED array.
**Warning signs:** CI log shows `Totals: ... 184 failed` — this is the --all signature.

### Pitfall 2: Wrong artifact name
**What goes wrong:** `actions/download-artifact@v4` fails with "No artifacts found" — the job errors before the harness runs.
**Why it happens:** The artifact name must match exactly what `duckdb/extension-ci-tools/_extension_distribution.yml` produces. The correct name is `anofox_forecast-v1.5.5-extension-wasm_eh` (no `.wasm` suffix in the artifact name; the `.wasm` suffix appears inside the artifact as the file path).
**How to avoid:** Use the exact name from the deploy workflow pattern [VERIFIED: _extension_deploy.yml:128].
**Warning signs:** `download-artifact` step fails with artifact-not-found error.

### Pitfall 3: `workflow_run` workflow does not gate PRs
**What goes wrong:** The dedicated `WasmTest.yml` (CI-02) is added to branch-protection "required status checks" — but it runs after the PR pipeline completes, so the PR merge is not actually blocked in real time.
**Why it happens:** `workflow_run` workflows are asynchronous by design.
**How to avoid:** Use the `wasm-runtime-test` job inside `MainDistributionPipeline.yml` (CI-01) as the required status check. The `WasmTest.yml` is for the badge only.
**Warning signs:** Branch protection is set but WASM failures don't block PR merge.

### Pitfall 4: Badge points at wrong workflow
**What goes wrong:** Badge always shows status from the main pipeline (which includes Rust tests, smoke tests, deploy jobs) — any unrelated red makes the WASM badge red.
**Why it happens:** Using `MainDistributionPipeline.yml` in the badge URL instead of `WasmTest.yml`.
**How to avoid:** Badge URL must reference `WasmTest.yml`: `https://github.com/DataZooDE/anofox-forecast/actions/workflows/WasmTest.yml/badge.svg?branch=main`.
**Warning signs:** Badge flips red when Rust clippy fails on a PR that has nothing to do with WASM.

### Pitfall 5: Missing `github-token` in WasmTest.yml download step
**What goes wrong:** `actions/download-artifact@v4` cannot access artifacts from a different workflow run (the triggering pipeline run).
**Why it happens:** Cross-run artifact access requires explicit `github-token` + `run-id` parameters.
**How to avoid:** Include both parameters as shown in the reference: `run-id: ${{ github.event.workflow_run.id }}` and `github-token: ${{ secrets.GITHUB_TOKEN }}`. Also add `permissions: actions: read` at the workflow level.
**Warning signs:** Download step fails with permissions or artifact-not-found error in the dedicated workflow but not in the main pipeline job.

### Pitfall 6: Submodules checkout in the harness jobs
**What goes wrong:** `actions/checkout@v4` with `submodules: recursive` takes several minutes for this repo (DuckDB submodule is large).
**Why it happens:** Default checkout includes submodules if the action is configured that way.
**How to avoid:** Both harness jobs (CI-01 gating job and CI-02 dedicated workflow) do NOT need submodules — the harness only reads `test/wasm/` files and the downloaded `.wasm`. Omit `submodules:` from the checkout step. [VERIFIED: anofox-statistics/MainDistributionPipeline.yml:79 — no submodules in the wasm-runtime-test job comment: "Submodules not needed: the harness only uses test/ (this repo) + the downloaded .wasm; skipping them keeps this leg fast."]

---

## Exact File Change Summary

Three files change in this phase:

### 1. `.github/workflows/MainDistributionPipeline.yml` (CI-01)

Add the `wasm-runtime-test` job after the `duckdb-latest-build` job (around line 67). The job is inline (not a reusable workflow call). No other jobs are modified.

### 2. `.github/workflows/WasmTest.yml` (CI-02, NEW FILE)

Create from scratch — port of anofox-statistics `WasmTest.yml` with:
- `anofox_statistics` → `anofox_forecast` (2 occurrences: artifact name, find glob)
- `--all` removed from harness invocation

### 3. `README.md` (CI-03)

Add one badge `<a>` tag to the existing badge block (lines 9-12). No other README changes.

---

## Validation Architecture

The phase has no code under test — it is YAML + a README change. Nyquist validation is not applicable in the traditional sense. The functional validation is the negative-control step described under CI-01 Negative Control above.

**Phase gate:** After pushing the three file changes to a branch and triggering CI, confirm:
1. `wasm-runtime-test` job appears in the PR check list and shows green.
2. `WasmTest.yml` workflow fires via `workflow_run` after the pipeline completes.
3. The README badge renders correctly (may take one green run on `main` to populate).

---

## Security Domain

No security-sensitive changes. GitHub Actions YAML with `permissions: actions: read, contents: read` (read-only) for the dedicated workflow. The gating job inside the main pipeline inherits the pipeline's `permissions: id-token: write, contents: read` — the WASM test steps do not use OIDC or AWS credentials.

---

## Environment Availability

| Dependency | Required By | Available | Notes |
|------------|------------|-----------|-------|
| GitHub Actions `ubuntu-latest` | Both jobs | ✓ | Standard runner — no special setup |
| Node 20 | Harness execution | ✓ (via `actions/setup-node@v4`) | Installed by action |
| `actions/download-artifact@v4` | Both jobs | ✓ | Used throughout existing workflows |
| `wasm_eh` artifact name `anofox_forecast-v1.5.5-extension-wasm_eh` | Both jobs | ✓ | Produced by `duckdb-latest-build` job [VERIFIED: MainDistributionPipeline.yml:60-67] |

---

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | The `wasm_eh` artifact produced by `duckdb/extension-ci-tools/_extension_distribution.yml@v1.5-variegata` is named `anofox_forecast-v1.5.5-extension-wasm_eh` (no `.wasm` suffix in the artifact name; the file inside is `anofox_forecast.duckdb_extension.wasm`) | Artifact name section | Download step fails; job errors before harness runs. Mitigation: the deploy workflow line 128 confirms the pattern — verify by inspecting a live CI run's artifact list |
| A2 | `actions/download-artifact@v4` with `run-id` + `github-token` is sufficient to access artifacts from the triggering pipeline run in a `workflow_run`-triggered workflow, with `permissions: actions: read` | WasmTest.yml pattern | Dedicated workflow download fails; badge stays grey. The anofox-statistics reference uses this exact pattern [VERIFIED: WasmTest.yml:30-36] |
| A3 | The `duckdb-latest-build` job name in `MainDistributionPipeline.yml` remains stable (i.e., the `needs: duckdb-latest-build` reference in the new gating job is valid) | CI-01 gating job | `needs:` dependency broken, job never runs. Risk is low — the job name is in the same file being edited. |

---

## Sources

### Primary (HIGH confidence)
- `/home/simonm/projects/duckdb/anofox-statistics/.github/workflows/WasmTest.yml` — complete reference implementation, read verbatim this session
- `/home/simonm/projects/duckdb/anofox-statistics/.github/workflows/MainDistributionPipeline.yml` lines 68-101 — `wasm-runtime-test` gating job, read verbatim this session
- `/home/simonm/projects/duckdb/anofox-forecast/.github/workflows/MainDistributionPipeline.yml` — this repo's pipeline, artifact name source, read verbatim this session
- `/home/simonm/projects/duckdb/anofox-forecast/.github/workflows/_extension_deploy.yml` line 128 — artifact naming pattern, read verbatim this session
- `/home/simonm/projects/duckdb/anofox-forecast/test/wasm/run.mjs` lines 32-33, 66-75, 77-87 — EXT_NAME, CURATED array, parseArgs, read verbatim this session
- `/home/simonm/projects/duckdb/anofox-statistics/README.md` line 7 — badge URL pattern, read verbatim this session
- `/home/simonm/projects/duckdb/anofox-forecast/README.md` lines 9-12 — existing badge block placement, read verbatim this session
- `/home/simonm/projects/duckdb/anofox-forecast/.planning/phases/07-wasm-node-harness-local-green/07-VERIFICATION.md` — curated vs --all constraint source, read verbatim this session

---

## Metadata

**Confidence breakdown:**
- YAML job/step shapes: HIGH — ported verbatim from a working reference in the same org
- Artifact name: HIGH — derived from verified deploy workflow pattern + confirmed job parameters
- Badge URL: HIGH — verbatim from anofox-statistics reference, substituting org/repo/file name
- Harness invocation (curated default): HIGH — verified from `run.mjs` source this session; the CURATED array and parseArgs logic are both confirmed

**Research date:** 2026-09-02
**Valid until:** Stable until DuckDB extension-ci-tools changes artifact naming conventions or GitHub Actions changes `download-artifact` cross-run API — neither is expected in the near term.

---

## RESEARCH COMPLETE
