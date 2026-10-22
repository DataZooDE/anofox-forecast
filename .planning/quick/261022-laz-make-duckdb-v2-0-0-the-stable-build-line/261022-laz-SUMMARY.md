---
phase: quick-261022-laz
plan: 01
status: complete
subsystem: ci-build
tags: [duckdb-2.0, ci, submodules, docs]
dependency-graph:
  requires: []
  provides: [duckdb-v2.0.0-stable-line]
  affects: [MainDistributionPipeline.yml, WasmTest.yml, _extension_deploy.yml, scripts/smoke_test.py]
tech-stack:
  added: []
  patterns:
    - "Smoke-test CLI download tries DuckDB >= 2.0 tarball asset name first, falls back to legacy v1.x zip"
    - "Local builds need DUCKDB_VERSION env var set to get a literal version string (not a commit hash) in the extension metadata footer, exactly like CI's reusable workflow does via `DUCKDB_VERSION: ${{ inputs.duckdb_version }}`"
key-files:
  created: []
  modified:
    - .gitmodules
    - duckdb (submodule pointer)
    - extension-ci-tools (submodule pointer)
    - scripts/smoke_test.py
    - .github/workflows/MainDistributionPipeline.yml
    - .github/workflows/WasmTest.yml
    - .github/workflows/_extension_deploy.yml
    - test/wasm/package.json
    - test/wasm/README.md
    - .claude/CLAUDE.md
    - README.md
    - docs/API_REFERENCE.md
    - CHANGELOG.md
    - plugins/anofox-forecast/skills/anofox-forecast-backtest/SKILL.md
    - plugins/anofox-forecast/skills/anofox-forecast-data-prep/SKILL.md
    - plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md
    - plugins/anofox-forecast/skills/anofox-forecast-eda/SKILL.md
    - plugins/anofox-forecast/skills/anofox-forecast-models/SKILL.md
decisions:
  - "Local `rm -rf build && make release` requires DUCKDB_VERSION=v2.0.0 exported for the extension metadata footer to read 'v2.0.0' literally; without it, DuckDB's own CMakeLists.txt (release_version.txt + git rev-list dev-iteration count) always appends a '-devN' suffix to the computed version, which the normalization logic then replaces with the raw git commit hash. CI's reusable workflow sets this via a job-level `DUCKDB_VERSION: ${{ inputs.duckdb_version }}` env var; the equivalent local-build step needed the same env var, applied as a Rule-3 (missing env var) auto-fix."
metrics:
  duration: 105min
  completed: 2026-10-22
actuals:
  tokens: 7341
  tasks: 3
  commits: 4
plan_head_before: b2fbfbe
---

# Quick Task 261022-laz: Make DuckDB v2.0.0 the Stable Build Line — Summary

Pinned the `duckdb` submodule to v2.0.0 and `extension-ci-tools` to v2.0-cyanoptera, made the shipped smoke test tolerant of v2.0's renamed CLI release assets, restructured `MainDistributionPipeline.yml` into three full build/smoke/deploy lines (v2.0.0 stable, v1.5.6, v1.4.5 LTS), and updated every doc/skill that states the supported DuckDB version — proven by a real local build, the full 5591-assertion test suite, and a smoke test against the official DuckDB v2.0.0 CLI.

## What Was Built

### Task 1 — Submodule pins, smoke-test CLI fallback, and local proof (tracer)

- `duckdb` submodule checked out at the `v2.0.0` tag commit `d54cbbe3df17e8e5ce68312f483b4155836112aa`.
- `extension-ci-tools` submodule checked out at the `origin/v2.0-cyanoptera` tip `b8d325c37e82160886579880707493bd358c0e62`.
- `.gitmodules` `extension-ci-tools` section's `branch` changed to `v2.0-cyanoptera` (only change in that file).
- `scripts/smoke_test.py`: replaced the single-asset-per-arch `ARCH_TO_CLI_ZIP` map with an ordered `ARCH_TO_CLI_ASSETS: dict[str, tuple[str, ...]]` that tries the DuckDB >= 2.0 tarball name first (`duckdb-cli-<os>-<arch>.tar.gz`) and falls back to the legacy v1.x zip (`duckdb_cli-<os>-<arch>.zip`) on an HTTP 404. Added safe tar extraction (`tarfile.data_filter="data"` when available, else a manual absolute-path/`..`-traversal rejection pass before extracting — T-261022-03). Verified against both `v2.0.0` and `v1.5.6` live downloads.
- `datazoo-banner` and `posthog-telemetry` submodules left untouched (verified unchanged shas).

**Local proof (Step C):**
- Clean build `rm -rf build && make release` exited `0` (ran in background, ~17 min first pass without `DUCKDB_VERSION`, then a second full clean rebuild with it set — see Deviations).
- Extension metadata footer: `tail -c 600 .../anofox_forecast.duckdb_extension | strings` → `duckdb_signature`, `v2026.10.22`, `v2.0.0`, `linux_amd64`. Literal `v2.0.0` confirmed.
- Full test suite: `HOME=$(mktemp -d) ./build/release/test/unittest "$PWD/test/*"` → **`All tests passed (5591 assertions in 94 test cases)`** — exact match to the plan's expected counts, exit code `0`.
- Smoke test: `python3 scripts/smoke_test.py <artifact> v2.0.0 linux_amd64` →
  ```
  [1/3] Installing the artifact into a stock CLI
  [2/3] Loading and checking duckdb_extensions()
  [3/3] Calling a real function:
        SELECT count(*) AS n FROM ts_features_list();
        -> 117

  Smoke test PASSED (anofox_forecast on linux_amd64)
  ```
  against the official DuckDB v2.0.0 CLI (`v2.0.0 (Cyanoptera) d54cbbe3df`). The v1.5.6 zip CLI download path was also exercised directly and confirmed working (`v1.5.6 (Variegata) 069cc9f9b5`).

Commits:
- `4172dea` — `build(deps): pin duckdb v2.0.0 and extension-ci-tools v2.0-cyanoptera`
- `38ea279` — `fix(ci): smoke test fetches DuckDB v2.0 CLI tarballs (v2.0.0 renamed release assets)`

### Task 2 — CI restructuring

- `MainDistributionPipeline.yml`: renamed `duckdb-latest-{build,smoke-test,deploy}` → `duckdb-v15-{build,smoke-test,deploy}` (inputs unchanged, job name reworded to name the registry track `variegata`). Deleted the non-blocking `duckdb-next-build` job and added a full three-job stable line: `duckdb-stable-build` / `duckdb-stable-smoke-test` / `duckdb-stable-deploy`, mirroring the v1.5.6 line's structure (`reduced_ci_mode: disabled`, no `artifact_postfix`, same `exclude_archs`, `deploy_latest`/`deploy_versioned` copied verbatim). `duckdb-lts-*` jobs left byte-for-byte unchanged (verified against base `b2fbfbe`). `wasm-runtime-test` now depends on `duckdb-v15-build` and keeps downloading the `anofox_forecast-v1.5.6-extension-wasm_eh` artifact, with the npm-evidence reasoning recorded in a comment block.
- `WasmTest.yml` and `_extension_deploy.yml`: comment-only edits (both parse to the same YAML as base `b2fbfbe`).
- `test/wasm/package.json`: rewrote only the `"comment"` field; `dependencies` and `package-lock.json` untouched.
- `test/wasm/README.md`: added a "Current status (2026-10-22)" paragraph under "Bumping the pin".

Commit: `8efdbc7` — `ci: make DuckDB v2.0.0 the stable build/smoke/deploy line; keep v1.5.6 and v1.4.5 LTS`

### Task 3 — Docs

- `.claude/CLAUDE.md`: 6 version-statement lines updated (Tech stack, Runtime, Key Dependencies, Platform Requirements x2, Architectural Constraints "DuckDB version").
- `README.md`: badge now reads `DuckDB-2.0.0 | 1.5.6 | 1.4.5 LTS`; Quick Start requirement line updated; added a submodule-pin sentence under "From Source" and a build-availability sentence under the DataZoo repository section.
- `docs/API_REFERENCE.md`: `**DuckDB Version:**` line updated.
- All five `plugins/anofox-forecast/skills/*/SKILL.md` headers: `**DuckDB:** v1.4.5 LTS / v1.5.4+` → `**DuckDB:** v2.0.0 / v1.5.6 / v1.4.5 LTS`.
- `CHANGELOG.md`: new `## [Unreleased]` section (Changed: stable-line switch + WASM-stays-on-1.5.6 decision; Fixed: smoke-test asset rename) inserted directly above `## [2026.10.22]`. No historical line removed (verified via `git diff -U0`).

Commit: `7f33142` — `docs: DuckDB v2.0.0 is the stable line (README, CLAUDE.md, API reference, skills, CHANGELOG)`

## Verification Performed

1. `git submodule status` — duckdb @ `d54cbbe3df` (v2.0.0), extension-ci-tools @ `b8d325c37e82160886579880707493bd358c0e62` (origin/v2.0-cyanoptera tip), datazoo-banner and posthog-telemetry unchanged.
2. Clean local build exited `0`; artifact footer literally reads `v2.0.0`.
3. `HOME=$(mktemp -d) ./build/release/test/unittest "$PWD/test/*"` → **5591 assertions in 94 test cases, all passed, exit 0.**
4. `python3 scripts/smoke_test.py <artifact> v2.0.0 linux_amd64` → `Smoke test PASSED`; v1.5.6 CLI download path confirmed working too.
5. YAML validity + Task 2 structural check (every `needs:` resolves; every `needs.<job>.` expression is backed by a `needs:` entry) — passed on all `.github/workflows/*.yml`.
6. **v1.5.6 was NOT built locally.** It is covered by the CI `duckdb-v15-*` line, whose job definitions (`uses`/`with`/`if`/`secrets`) are verified byte-identical to the pre-change `duckdb-latest-*` job definitions apart from the id/name rename.
7. Nothing pushed, tagged, merged, or PR'd. `git log --oneline b2fbfbe..HEAD` shows exactly the four commits below; working tree is clean (`git status --short --untracked-files=no` empty).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - missing env var] Local build needed `DUCKDB_VERSION=v2.0.0` exported for the extension metadata footer to show the literal version string**
- **Found during:** Task 1, Step C (local proof — "Confirm the artifact really targets 2.0").
- **Issue:** A plain `rm -rf build && make release` (no env vars) produced a footer with the raw duckdb git commit hash (`d54cbbe3df`) instead of `v2.0.0`. Traced to `duckdb/CMakeLists.txt`: without an explicit `DUCKDB_EXPLICIT_VERSION` (set only from the `DUCKDB_VERSION` env var or `OVERRIDE_GIT_DESCRIBE`), the non-explicit branch always computes `DUCKDB_VERSION` as `v${RELEASE}.0-dev${ITERATION}` (even when `ITERATION` is `0`), which always matches the `-dev[0-9]+$` regex and therefore always normalizes the embedded footer version to the raw git commit hash — regardless of which tag is checked out. This is not a submodule-pin bug: the same thing would happen building the v1.5.6 line locally without the env var too.
- **Fix:** Exported `DUCKDB_VERSION=v2.0.0` before `rm -rf build && make release` and rebuilt clean. This exactly mirrors how CI's reusable `_extension_distribution.yml` workflow does it — it sets a job-level `DUCKDB_VERSION: ${{ inputs.duckdb_version }}` env var, which CMake's `DEFINED ENV{DUCKDB_VERSION}` branch picks up directly as `DUCKDB_EXPLICIT_VERSION`. No committed file was changed by this fix; it only changed the local build invocation used for verification.
- **Files modified:** None (build-invocation-only fix, not a code change).
- **Verification:** Second clean build confirmed footer literal `v2.0.0`; full unittest suite re-run against the resulting artifact reported the exact expected 5591/94 counts; smoke test passed against the official v2.0.0 CLI.

No other deviations. Plan executed as written otherwise.

## Commits

| Hash | Message |
|------|---------|
| `4172dea` | `build(deps): pin duckdb v2.0.0 and extension-ci-tools v2.0-cyanoptera` |
| `38ea279` | `fix(ci): smoke test fetches DuckDB v2.0 CLI tarballs (v2.0.0 renamed release assets)` |
| `8efdbc7` | `ci: make DuckDB v2.0.0 the stable build/smoke/deploy line; keep v1.5.6 and v1.4.5 LTS` |
| `7f33142` | `docs: DuckDB v2.0.0 is the stable line (README, CLAUDE.md, API reference, skills, CHANGELOG)` |

## WASM Keep-on-1.5.6 Decision

The WASM runtime test (both the `wasm-runtime-test` job in `MainDistributionPipeline.yml` and the dedicated `WasmTest.yml` badge workflow) intentionally stays on the v1.5.6 line. Evidence: `npm view @duckdb/duckdb-wasm dist-tags` returns `latest: 1.33.1-dev57.0`, `next: 1.33.1-dev65.0` (the newest published build, 2026-09-29). The storage-version list embedded in `dist/duckdb-eh.wasm` for `1.33.1-dev65.0` ends at `v1.5.6` — no `@duckdb/duckdb-wasm` npm build bundles a v2.0.0 engine yet, and DuckDB-Wasm rejects an extension built for a mismatched engine version. This is documented in the `wasm-runtime-test` job comment, `test/wasm/package.json`'s `"comment"` field, and a new "Current status (2026-10-22)" paragraph in `test/wasm/README.md`. When a duckdb-wasm build bundling v2.0.0 ships, bump the pin, `needs`, and the artifact name to the stable line together.

## Smoke-Test Asset-Rename Finding

DuckDB v2.0.0 renamed its CLI release assets: `duckdb_cli-linux-amd64.zip` / `duckdb_cli-osx-universal.zip` / `duckdb_cli-windows-amd64.zip` all 404 for `v2.0.0`. The v2.0.0 release instead ships `duckdb-cli-linux-amd64.tar.gz`, `duckdb-cli-linux-arm64.tar.gz`, `duckdb-cli-osx-universal.tar.gz`, `duckdb-cli-windows-amd64.tar.gz` (plus musl/arm64 variants), each holding a single bare `duckdb` (or `duckdb.exe`) at the archive root. `scripts/smoke_test.py` now tries the new tarball name first per arch and falls back to the legacy zip name, verified live against both `v2.0.0` and `v1.5.6`.

## v1.5.6 Not Built Locally

Per the plan, v1.5.6 was **not** rebuilt locally in this quick task — only covered by the CI `duckdb-v15-*` line, whose build/smoke-test/deploy job definitions were verified structurally identical (same `uses`/`with`/`if`/`secrets`) to the pre-change `duckdb-latest-*` jobs, apart from the id/name rename.

## Self-Check: PASSED
