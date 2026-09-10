---
phase: 08-ci-gating-dedicated-workflow-badge
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - .github/workflows/WasmTest.yml
  - .github/workflows/MainDistributionPipeline.yml
  - README.md
autonomous: false
requirements: [CI-01, CI-02, CI-03]

estimate:
  tokens: 42000
  raw_tokens: 28000
  tasks: 4
  confidence: med

must_haves:
  truths:
    - "A dedicated workflow .github/workflows/WasmTest.yml (name: WASM) runs the curated Node harness against the v1.5.5 wasm_eh artifact and is independently observable (CI-02)."
    - "MainDistributionPipeline.yml contains a wasm-runtime-test job that needs duckdb-latest-build, downloads anofox_forecast-v1.5.5-extension-wasm_eh, and runs the harness with NO --all and NO --file (curated subset) (CI-01)."
    - "The harness gate turns the build RED on a WASM load/runtime failure (harness exit non-zero -> step fails -> job fails). [verification: backstop — a live CI push is required to observe red end-to-end]"
    - "README.md renders a WASM status badge wired to the WasmTest.yml badge URL with branch=main that flips with that workflow's latest run (CI-03). [verification: backstop — badge SVG populates only after a green run on main]"
  artifacts:
    - ".github/workflows/WasmTest.yml (new)"
    - ".github/workflows/MainDistributionPipeline.yml (modified — adds wasm-runtime-test job)"
    - "README.md (modified — adds WASM badge line)"
  key_links:
    - "wasm-runtime-test job needs duckdb-latest-build -> downloads the artifact that build job produces (same-run download, no run-id)."
    - "WasmTest.yml workflow_run on the Main Extension Distribution Pipeline -> downloads artifact from triggering run via run-id + github-token."
    - "Badge URL file name WasmTest.yml must exactly match the workflow file name for the badge to resolve."
    - "Harness invocation node test/wasm/run.mjs --ext $EXT (no --all) is the gate; passing --all would gate on 184 pre-existing test-debt failures."
---

<objective>
Wire the Phase 7 DuckDB-Wasm Node harness into CI so WASM load/runtime regressions fail the build automatically, expose WASM health as an independently observable dedicated workflow, and surface a README status badge — a verbatim port of the CI half of anofox-statistics PR #131, adapted for the LOCKED curated-subset constraint.

Purpose: The local green from Phase 7 becomes enforced on every relevant build; WASM breakage is caught by CI, not by users on clean machines.
Output: .github/workflows/WasmTest.yml (new), a wasm-runtime-test gating job inside MainDistributionPipeline.yml, and a WASM badge in README.md.
</objective>

<execution_context>
@~/.claude/gsd-core/workflows/execute-plan.md
@~/.claude/gsd-core/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/ROADMAP.md
@.planning/STATE.md
@.planning/phases/08-ci-gating-dedicated-workflow-badge/08-CONTEXT.md
@.planning/phases/08-ci-gating-dedicated-workflow-badge/08-RESEARCH.md

# Reference implementation (source of truth for the port — read verbatim):
@/home/simonm/projects/duckdb/anofox-statistics/.github/workflows/WasmTest.yml
@/home/simonm/projects/duckdb/anofox-statistics/.github/workflows/MainDistributionPipeline.yml

# Target files (this repo):
@.github/workflows/MainDistributionPipeline.yml
@README.md
@test/wasm/run.mjs
</context>

<tasks>

<task type="tracer">
  <name>Task 1: Create the dedicated WasmTest.yml workflow (CI-02) — self-contained, badge-powering path</name>
  <files>.github/workflows/WasmTest.yml</files>
  <read_first>
    - /home/simonm/projects/duckdb/anofox-statistics/.github/workflows/WasmTest.yml (the verbatim reference — port this file)
    - .github/workflows/MainDistributionPipeline.yml (this repo's pipeline — confirm the top-level name value is exactly "Main Extension Distribution Pipeline" for the workflow_run trigger)
    - test/wasm/run.mjs (confirm EXT_NAME is anofox_forecast, EXT_FILE is anofox_forecast.duckdb_extension.wasm, and that omitting --all runs the CURATED subset)
  </read_first>
  <action>
    Create .github/workflows/WasmTest.yml as a verbatim port of the anofox-statistics reference with exactly two substitutions applied (CI-02).

    Preserve verbatim from the reference: the leading comment block explaining badge isolation; name WASM; the on workflow_run trigger with workflows ["Main Extension Distribution Pipeline"] and types [completed]; permissions actions read + contents read; the single wasm-runtime-test job on ubuntu-latest; actions/checkout@v4 with NO submodules; the actions/download-artifact@v4 step with run-id github.event.workflow_run.id, github-token secrets.GITHUB_TOKEN, and path wasm-artifact; actions/setup-node@v4 with node-version 20; and the npm --prefix test/wasm install step.

    Substitution 1 (extension name, 2 occurrences): download-artifact name becomes anofox_forecast-v1.5.5-extension-wasm_eh, and the find glob in the harness step becomes anofox_forecast.duckdb_extension.wasm.

    Substitution 2 (LOCKED — curated subset per the CONTEXT.md locked constraint): the harness invocation is node test/wasm/run.mjs --ext "$EXT" — REMOVE the --all flag the reference carries. The reference is fully green so it uses --all; this repo has 184 pre-existing test-debt failures under --all unrelated to WASM, so gating on --all would keep CI permanently red. Curated default (no --all, no --file) runs the 8-file CURATED array / 396 assertions, exit 0.

    The artifact name anofox_forecast-v1.5.5-extension-wasm_eh is an assumption derived from the deploy pattern (RESEARCH A1); it is verifiable against a live CI run's artifact list. Do not change it without that confirmation.
  </action>
  <verify>
    <automated>python3 -c "import yaml; d=yaml.safe_load(open('.github/workflows/WasmTest.yml')); assert d['name']=='WASM'; wf=(d.get('on') or d.get(True))['workflow_run']['workflows']; assert 'Main Extension Distribution Pipeline' in wf; print('WasmTest.yml parses; name=WASM; workflow_run wired')"</automated>
    <automated>grep -q 'anofox_forecast-v1.5.5-extension-wasm_eh' .github/workflows/WasmTest.yml && grep -q 'run-id: ..{ github.event.workflow_run.id' .github/workflows/WasmTest.yml && grep -q 'github-token:' .github/workflows/WasmTest.yml && grep -Eq 'node test/wasm/run.mjs --ext' .github/workflows/WasmTest.yml && echo "keys present"</automated>
    <automated>! grep -Eq 'run.mjs .*--all' .github/workflows/WasmTest.yml && ! grep -q 'anofox_statistics' .github/workflows/WasmTest.yml && echo "no --all, no stale anofox_statistics refs"</automated>
    <fails_when>YAML does not parse, name is not WASM, workflow_run does not target the pipeline, the artifact name is wrong, --all is present, or anofox_statistics refs remain.</fails_when>
  </verify>
  <acceptance_criteria>
    - .github/workflows/WasmTest.yml exists and is valid YAML.
    - Top-level name is exactly WASM.
    - on.workflow_run.workflows contains exactly "Main Extension Distribution Pipeline".
    - permissions has actions read and contents read.
    - download-artifact step uses name anofox_forecast-v1.5.5-extension-wasm_eh, run-id github.event.workflow_run.id, github-token secrets.GITHUB_TOKEN, path wasm-artifact.
    - Harness step runs node test/wasm/run.mjs --ext "$EXT" with NO --all and NO --file.
    - No occurrence of the string anofox_statistics anywhere in the file.
  </acceptance_criteria>
  <done>WasmTest.yml is a valid, curated-subset port of the reference with the correct artifact name, cross-run download, and no --all.</done>
</task>

<task type="auto">
  <name>Task 2: Add the wasm-runtime-test gating job to MainDistributionPipeline.yml (CI-01)</name>
  <files>.github/workflows/MainDistributionPipeline.yml</files>
  <read_first>
    - /home/simonm/projects/duckdb/anofox-statistics/.github/workflows/MainDistributionPipeline.yml (the wasm-runtime-test job pattern, the source of the port)
    - .github/workflows/MainDistributionPipeline.yml (this repo — confirm the job name duckdb-latest-build exists at v1.5.5 and add the new job after it, above build-and-test-rust)
  </read_first>
  <reversibility rating="costly">Editing the release pipeline changes how PRs and pushes gate. The addition is a new, self-contained job (no edits to existing jobs), so revert is a clean deletion; but a mistake here can block merges on the release pipeline until fixed.</reversibility>
  <action>
    Add a new inline job wasm-runtime-test to .github/workflows/MainDistributionPipeline.yml, placed after the duckdb-latest-build job (do not modify any existing job). This is CI-01.

    The job: name "WASM runtime test (Node harness, v1.5.5 wasm_eh)"; needs duckdb-latest-build; if guard ${{ !cancelled() && needs.duckdb-latest-build.result == 'success' }}; runs-on ubuntu-latest. Steps: actions/checkout@v4 with NO submodules (harness only reads test/ + the downloaded .wasm; submodules waste minutes on the DuckDB submodule); actions/download-artifact@v4 with name anofox_forecast-v1.5.5-extension-wasm_eh and path wasm-artifact (SAME-RUN download — do NOT add run-id or github-token here; the artifact is produced within this same workflow run by duckdb-latest-build); actions/setup-node@v4 with node-version 20; a run step npm --prefix test/wasm install; then a final run step that resolves EXT via find wasm-artifact -name 'anofox_forecast.duckdb_extension.wasm' | head -1, errors with ::error:: and exit 1 if empty, echoes the path, and runs node test/wasm/run.mjs --ext "$EXT".

    LOCKED: NO --all, NO --file — curated subset only. The harness exit code is the gate: no continue-on-error, so a non-zero exit fails the step, fails the job, and turns the pipeline red. Include a short comment above the job noting that a green wasm build only proves compile+link; this job proves the extension actually LOADS and RUNS in DuckDB-Wasm.
  </action>
  <verify>
    <automated>python3 -c "import yaml; d=yaml.safe_load(open('.github/workflows/MainDistributionPipeline.yml')); j=d['jobs']['wasm-runtime-test']; assert j['needs']=='duckdb-latest-build', j['needs']; assert j['runs-on']=='ubuntu-latest'; steps=' '.join(str(s) for s in j['steps']); assert 'download-artifact' in steps; assert 'anofox_forecast-v1.5.5-extension-wasm_eh' in steps; assert 'run.mjs --ext' in steps; assert '--all' not in steps; print('wasm-runtime-test job wired; needs duckdb-latest-build; no --all')"</automated>
    <automated>python3 -c "import yaml; d=yaml.safe_load(open('.github/workflows/MainDistributionPipeline.yml')); assert 'duckdb-latest-build' in d['jobs']; assert 'duckdb-lts-deploy' in d['jobs'] and 'build-and-test-rust' in d['jobs']; print('existing jobs still present — no accidental deletion')"</automated>
    <automated>python3 -c "import yaml; d=yaml.safe_load(open('.github/workflows/MainDistributionPipeline.yml')); s=str(d['jobs']['wasm-runtime-test']['steps']); assert 'run-id' not in s and 'github-token' not in s, 'same-run download must NOT use run-id/github-token'; print('same-run download OK')"</automated>
    <fails_when>The job is missing, needs is not duckdb-latest-build, --all is present, run-id/github-token are wrongly added, any pre-existing job is deleted, or the file fails to parse.</fails_when>
  </verify>
  <acceptance_criteria>
    - jobs.wasm-runtime-test exists with name "WASM runtime test (Node harness, v1.5.5 wasm_eh)".
    - needs is duckdb-latest-build; if guard references needs.duckdb-latest-build.result == 'success'.
    - download-artifact name is anofox_forecast-v1.5.5-extension-wasm_eh, path wasm-artifact, with NO run-id and NO github-token (same-run download).
    - Harness step runs node test/wasm/run.mjs --ext "$EXT" with NO --all and NO --file.
    - checkout step has no submodules key.
    - All previously-existing jobs (duckdb-lts-build, duckdb-latest-build, duckdb-latest-deploy, build-and-test-rust, etc.) are unchanged and present.
  </acceptance_criteria>
  <done>MainDistributionPipeline.yml gains a curated-subset wasm-runtime-test gating job that needs the v1.5.5 build and fails the pipeline on any WASM load/runtime error; no existing job is altered.</done>
</task>

<task type="auto">
  <name>Task 3: Add the WASM status badge to README.md (CI-03)</name>
  <files>README.md</files>
  <read_first>
    - README.md (this repo — the header badge block is the p align=center block at lines 8-13, containing License / DuckDB / build / Tests badges)
    - /home/simonm/projects/duckdb/anofox-statistics/README.md (reference badge at line 7 — the WasmTest.yml badge URL pattern with branch=main)
  </read_first>
  <action>
    Insert one WASM status badge into the existing header badge block in README.md (the p align=center block, currently lines 8-13, after the Tests badge on line 12). This is CI-03.

    Add this line, matching the surrounding anchor-wrapped img style:
    <a href="https://github.com/DataZooDE/anofox-forecast/actions/workflows/WasmTest.yml"><img src="https://github.com/DataZooDE/anofox-forecast/actions/workflows/WasmTest.yml/badge.svg?branch=main" alt="WASM"></a>

    The badge file name WasmTest.yml MUST match the workflow file created in Task 1 exactly, or the badge will not resolve. Keep the ?branch=main query parameter so development branches do not flip the visible badge. Change nothing else in README.md.
  </action>
  <verify>
    <automated>grep -q 'actions/workflows/WasmTest.yml/badge.svg?branch=main' README.md && grep -q 'href="https://github.com/DataZooDE/anofox-forecast/actions/workflows/WasmTest.yml"' README.md && grep -q 'alt="WASM"' README.md && echo "WASM badge present with correct URL"</automated>
    <automated>test $(grep -c 'img.shields.io/badge/License' README.md) -eq 1 && grep -q 'Tests-295%20Rust' README.md && echo "existing badges intact"</automated>
    <fails_when>The badge line is missing, the workflow file name in the URL is not WasmTest.yml, ?branch=main is absent, or an existing badge was removed.</fails_when>
  </verify>
  <acceptance_criteria>
    - README.md contains an anchor to https://github.com/DataZooDE/anofox-forecast/actions/workflows/WasmTest.yml.
    - README.md contains an img src https://github.com/DataZooDE/anofox-forecast/actions/workflows/WasmTest.yml/badge.svg?branch=main with alt WASM.
    - The badge file name WasmTest.yml exactly matches the Task 1 workflow file name.
    - Existing License / DuckDB / build / Tests badges are unchanged.
  </acceptance_criteria>
  <done>README.md header renders a WASM badge wired to the dedicated WasmTest.yml workflow on the main branch.</done>
</task>

<task type="checkpoint:human-verify" gate="blocking-human">
  <name>Task 4: Negative-control — prove the gate turns RED on a WASM failure (CI-01 success criterion)</name>
  <read_first>
    - .planning/phases/08-ci-gating-dedicated-workflow-badge/08-RESEARCH.md (section "CI-01 Negative Control: Verifying the Gate Turns Red" — Option A is the documented approach)
  </read_first>
  <what-built>
    The three CI artifacts from Tasks 1-3: the dedicated WasmTest.yml workflow (CI-02), the wasm-runtime-test gating job inside MainDistributionPipeline.yml (CI-01), and the README WASM badge (CI-03). Structurally verified via YAML parse + grep, but the "gate turns red on WASM failure" and "artifact name is correct" claims can only be confirmed by a live CI run.
  </what-built>
  <how-to-verify>
    Push the branch, open a PR, and observe the GitHub Actions UI. Confirm: (1) wasm-runtime-test runs GREEN against the real anofox_forecast-v1.5.5-extension-wasm_eh artifact; (2) after breaking one curated test the job turns RED; (3) after revert it is GREEN again; (4) WasmTest.yml fires via workflow_run and the README badge resolves. Record the run URLs.
  </how-to-verify>
  <resume-signal>Human confirms green->red->green observed in CI, artifact name confirmed live, badge resolves, and no breakage is merged. Reply "verified" to resume, or report the failing direction.</resume-signal>
  <action>
    This is a documented, human-observed negative-control — it CANNOT be an automated pass, because observing CI turn red requires a live push and the GitHub Actions UI. State the failing direction honestly: a correctly-wired gate produces a RED run here; a green run means the gate is not actually gating.

    Procedure to present to the human:
    1. Push the three-file change (Tasks 1-3) to a branch and open a PR (or push to a CI-triggering branch). Confirm the wasm-runtime-test job appears in the checks list and runs GREEN against the real wasm_eh artifact — this validates the artifact name assumption (RESEARCH A1) against a live run.
    2. Introduce a deliberate breakage in ONE curated test file (e.g. change an expected value in a file listed in the CURATED array of test/wasm/run.mjs, such as test/sql/ts_diagnostics.test), push, and confirm the wasm-runtime-test job turns RED (harness exits non-zero -> step fails -> job fails).
    3. Revert the deliberate breakage; confirm the job returns to GREEN.
    4. After the pipeline runs on the branch, confirm WasmTest.yml fires via workflow_run and that the README badge resolves (badge may show grey until the first green run lands on main).

    Do NOT merge with the deliberate breakage in place. Record the observed run URLs (green, red, green-again) in the SUMMARY as the CI-01 negative-control evidence.
  </action>
  <verify>
    <human-check>Human confirms in the GitHub Actions UI: (a) wasm-runtime-test green on the clean artifact, (b) RED after the deliberate curated-test breakage, (c) green again after revert, (d) WasmTest.yml fired via workflow_run and the badge resolves. Run URLs recorded in SUMMARY.</human-check>
  </verify>
  <acceptance_criteria>
    - A live CI run shows wasm-runtime-test GREEN against the real anofox_forecast-v1.5.5-extension-wasm_eh artifact (confirms the artifact name).
    - A deliberate curated-test breakage produced a RED wasm-runtime-test run (the gate gates).
    - Revert returned the job to GREEN and no breakage is merged.
    - WasmTest.yml triggered via workflow_run and the README badge URL resolves.
  </acceptance_criteria>
  <done>The gate is proven to turn red on WASM/curated failure and green on success via observed CI runs; the artifact name is confirmed live; evidence (run URLs) is in the SUMMARY.</done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| GitHub-hosted runner -> downloaded artifact | The wasm_eh artifact is downloaded and executed by the Node harness; artifact provenance is the triggering CI run within the same org/repo. |
| workflow_run cross-run artifact access | WasmTest.yml reads an artifact from a different (triggering) workflow run using GITHUB_TOKEN with read-only actions scope. |

## STRIDE Threat Register

| Threat ID | Category | Component | Severity | Disposition | Mitigation Plan |
|-----------|----------|-----------|----------|-------------|-----------------|
| T-08-01 | Elevation of Privilege | WasmTest.yml permissions | low | mitigate | Workflow declares read-only permissions (actions: read, contents: read) verbatim from the reference; no write scopes, no OIDC. |
| T-08-02 | Tampering | Downloaded wasm_eh artifact | low | accept | Artifact is produced by the same org's trusted distribution pipeline within the triggering run; no third-party artifact source. No new package installs beyond the already-pinned test/wasm lockfile. |
| T-08-03 | Denial of Service | wasm-runtime-test gating job | low | accept | Job runs curated subset only (fast); checkout omits submodules to avoid the large DuckDB submodule; no untrusted network fetches beyond npm ci of the pinned lockfile. |
| T-08-SC | Tampering | npm install in harness steps | low | mitigate | No new dependencies added this phase; npm --prefix test/wasm install resolves the Phase-7 pinned package-lock.json (duckdb-wasm@1.33.1-dev64.0, web-worker@1.2.0). No package-manager additions -> package-legitimacy gate not triggered. |
</threat_model>

<verification>
Structural (automated, runnable now):
- YAML parses for WasmTest.yml and MainDistributionPipeline.yml (python3 yaml.safe_load).
- WasmTest.yml: name WASM, workflow_run on the pipeline, correct artifact name, run-id + github-token present, no --all, no anofox_statistics refs.
- MainDistributionPipeline.yml: wasm-runtime-test job present, needs duckdb-latest-build, same-run download (no run-id/github-token), no --all, all prior jobs intact.
- README.md: WASM badge with WasmTest.yml/badge.svg?branch=main, existing badges intact.

Functional (backstop — requires a live CI push, Task 4):
- wasm-runtime-test green on the real artifact (confirms artifact name).
- Gate turns red on a deliberate curated-test breakage, green again on revert.
- WasmTest.yml fires via workflow_run; README badge resolves.
</verification>

<success_criteria>
- CI-01: wasm-runtime-test job exists in MainDistributionPipeline.yml, needs the v1.5.5 build, downloads the wasm_eh artifact, runs the curated harness, and is proven (Task 4) to turn red on a WASM/curated failure.
- CI-02: WasmTest.yml exists as a dedicated, independently observable workflow running the same curated harness via workflow_run.
- CI-03: README shows a WASM badge wired to WasmTest.yml on branch main.
- No --all anywhere; no existing job or badge altered; no new dependencies.
</success_criteria>

<output>
Create .planning/phases/08-ci-gating-dedicated-workflow-badge/08-01-SUMMARY.md when done. Record: the three file changes, the structural verification results, and (from Task 4) the observed CI run URLs proving green -> red -> green, the live-confirmed artifact name, and badge resolution.
</output>
