---
phase: quick-261022-laz
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - .gitmodules
  - duckdb
  - extension-ci-tools
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
autonomous: true
requirements: [QUICK-261022-laz]

estimate:
  tokens: 100000
  raw_tokens: 100000
  tasks: 3
  confidence: low

must_haves:
  truths:
    - "The duckdb submodule points at v2.0.0 (d54cbbe3df17e8e5ce68312f483b4155836112aa), extension-ci-tools points at the current tip of origin/v2.0-cyanoptera, .gitmodules tracks branch v2.0-cyanoptera, and datazoo-banner / posthog-telemetry are unchanged"
    - "A clean local `rm -rf build && make release` exits 0, and the built artifact's metadata footer names v2.0.0, not v1.5.6"
    - "`HOME=$(mktemp -d) ./build/release/test/unittest \"$PWD/test/*\"` passes with 0 failures (expected: 5591 assertions in 94 test cases; exact numbers reported)"
    - "scripts/smoke_test.py installs, loads and runs ts_features_list() on the locally built artifact in the OFFICIAL DuckDB v2.0.0 CLI (new duckdb-cli-*.tar.gz asset), and the same script still fetches the v1.5.6 CLI (legacy duckdb_cli-*.zip asset)"
    - "MainDistributionPipeline.yml builds, smoke-tests and deploys three lines: v2.0.0 stable (ci-tools v2.0-cyanoptera), v1.5.6 (v1.5-variegata), v1.4.5 LTS (v1.4-andium, byte-for-byte unchanged job definitions). The non-blocking 2.0 job is gone, and every `needs:` / `needs.<job>.` reference resolves"
    - "The WASM runtime test (pipeline job and WasmTest.yml) stays on the v1.5.6 wasm_eh artifact, with the reason recorded: no @duckdb/duckdb-wasm npm build bundles a v2.0.0 engine yet"
    - "README, .claude/CLAUDE.md, docs/API_REFERENCE.md and the five skill headers state v2.0.0 as the stable/primary DuckDB line alongside v1.5.6 and v1.4.5 LTS. CHANGELOG.md has a new `## [Unreleased]` entry, and no historical CHANGELOG entry or docs/release-notes file changed"
  artifacts:
    - path: "scripts/smoke_test.py"
      provides: "Official-CLI download that tries the DuckDB >= 2.0 tarball asset name first and falls back to the legacy v1.x zip; safe tar extraction"
      contains: "duckdb-cli-"
    - path: ".github/workflows/MainDistributionPipeline.yml"
      provides: "duckdb-stable-{build,smoke-test,deploy} (v2.0.0), duckdb-v15-{build,smoke-test,deploy} (v1.5.6), unchanged duckdb-lts-*, wasm-runtime-test on duckdb-v15-build"
      contains: "duckdb-stable-deploy"
    - path: ".gitmodules"
      provides: "extension-ci-tools tracks v2.0-cyanoptera"
      contains: "v2.0-cyanoptera"
    - path: "CHANGELOG.md"
      provides: "## [Unreleased] entry describing the stable-line switch"
      contains: "## [Unreleased]"
  key_links:
    - from: "Makefile (include extension-ci-tools/makefiles/duckdb_extension.Makefile)"
      to: "duckdb submodule @ v2.0.0"
      via: "extension-ci-tools submodule @ v2.0-cyanoptera; local `make release` now builds against 2.0.0"
      pattern: "duckdb_extension.Makefile"
    - from: "duckdb-stable-smoke-test"
      to: "https://github.com/duckdb/duckdb/releases/download/v2.0.0/duckdb-cli-<os>-<arch>.tar.gz"
      via: "_extension_smoke_test.yml -> scripts/smoke_test.py <ext> v2.0.0 <arch>"
      pattern: "duckdb-cli-"
    - from: "duckdb-stable-deploy"
      to: "artifact anofox_forecast-v2.0.0-extension-<arch> -> s3://<bucket>/v2.0.0/<arch>/"
      via: "_extension_deploy.yml (duckdb_version v2.0.0, ci_tools_version v2.0-cyanoptera); unchanged, verified compatible"
      pattern: "duckdb_version: v2.0.0"
    - from: "wasm-runtime-test"
      to: "artifact anofox_forecast-v1.5.6-extension-wasm_eh"
      via: "needs: duckdb-v15-build"
      pattern: "needs: duckdb-v15-build"
---

<objective>
Make DuckDB v2.0.0 the stable build line of anofox-forecast. Bump the `duckdb` submodule to v2.0.0 and `extension-ci-tools` to v2.0-cyanoptera. Turn the non-blocking 2.0 CI job into a full build, smoke-test and deploy line. Keep the v1.5.6 and v1.4.5 LTS lines as full lines. Update the docs that state the supported DuckDB version.

Purpose: duckdb/community-extensions `UPDATING.md` ("Update path: After DuckDB release") requires us to bump the stable DuckDB version in the distribution workflow AND update the submodule, merge to main, then PR the descriptor. DuckDB v2.0.0 was released 2026-10-21 and is what the registry now builds by default.

Output: submodule pointer commits, a v2.0-aware `scripts/smoke_test.py`, a restructured `MainDistributionPipeline.yml`, WASM-decision comments, docs, and an `## [Unreleased]` CHANGELOG entry. Nothing is pushed, tagged or merged. The orchestrator owns push, PR and merge (merge commit).
</objective>

<execution_context>
@~/.claude/gsd-core/workflows/execute-plan.md
@~/.claude/gsd-core/templates/summary.md
</execution_context>

<context>
@.planning/STATE.md
@.claude/CLAUDE.md
@.github/workflows/MainDistributionPipeline.yml
@.github/workflows/_extension_smoke_test.yml
@.github/workflows/_extension_deploy.yml
@.github/workflows/WasmTest.yml
@scripts/smoke_test.py
@test/wasm/package.json
@test/wasm/README.md

## Live observations at planning time (2026-10-22), already verified by the planner, do not re-derive

- Branch `feat/duckdb-2.0.0-stable`, HEAD `b2fbfbe` (merge of PR #291). That is one release newer than the orchestrator's `25227eb` note: it already contains release v2026.10.22, which links the macOS Security framework for 2.0 (CMakeLists.txt). `b2fbfbe` is the BASE ref that every "unchanged vs base" check below uses.
- Submodules are clean (`git -C duckdb status --porcelain` and `git -C extension-ci-tools status --porcelain` are empty). The duckdb clone already has tag `v2.0.0` locally: `git -C duckdb rev-parse 'v2.0.0^{commit}'` = `d54cbbe3df17e8e5ce68312f483b4155836112aa`. The tip of origin `v2.0-cyanoptera` was `b8d325c37e82160886579880707493bd358c0e62` (2026-10-22T11:18Z). Use whatever the tip is at execution time and record the sha.
- `.gitmodules` currently says `branch = v1.5-variegata` for extension-ci-tools.
- **Smoke-test blocker found:** DuckDB v2.0.0 renamed its CLI release assets. `duckdb_cli-linux-amd64.zip`, `duckdb_cli-osx-universal.zip` and `duckdb_cli-windows-amd64.zip` all return HTTP 404 for v2.0.0. The v2.0.0 release ships `duckdb-cli-linux-amd64.tar.gz`, `duckdb-cli-linux-arm64.tar.gz`, `duckdb-cli-osx-universal.tar.gz` and `duckdb-cli-windows-amd64.tar.gz` (plus -musl and arm64 variants). Each tarball holds a single bare `duckdb` (or `duckdb.exe`) at the archive root. The legacy zip assets still exist for v1.5.6 and v1.4.5 (HTTP 200). The v2.0.0 CLI accepts the script's flags `-unsigned -noheader -list -c`, and its query output is unchanged even when `AI_AGENT` is set. Only `--version` may print an extra "agent mode" notice line first, so version checks below use substring matching.
- **Smoke test is a real version gate:** the stale `build/release` artifact (built against v1.5.6) is rejected by the official v2.0.0 CLI with "built specifically for DuckDB version 'v1.5.6'".
- **`_extension_deploy.yml` needs no functional change for v2.0.0.** It fetches tag `v2.0.0` and branch `v2.0-cyanoptera` fine. The v2.0-cyanoptera `scripts/modify_distribution_matrix.py` still accepts `--deploy_matrix --reduced_ci_mode auto --exclude`. With exclude `windows_amd64_rtools;windows_amd64_mingw` the simulated deploy matrix is linux_amd64, linux_arm64, osx_arm64, wasm_eh, wasm_mvp, wasm_threads and windows_amd64. On cyanoptera, osx_amd64, the musl archs and windows_arm64 are opt-in, so the build job does not produce them and they are not deployed. That is a ci-tools default, not an exclusion of ours. The v2.0 build workflow names artifacts `<ext>-<inputs.duckdb_version>-extension-<arch><postfix>` (ci_phase.py upload), with the file at the artifact root. That matches what `_extension_smoke_test.yml` and `_extension_deploy.yml` download. `scripts/extension-upload.sh` derives S3 paths from `git tag --points-at HEAD` in duckdb (giving v2.0.0), so they do not collide with the v1.5.6 paths.
- **WASM decision: keep the runtime test on the v1.5.6 line.** `npm view @duckdb/duckdb-wasm dist-tags` gives `latest: 1.33.1-dev57.0`, `next: 1.33.1-dev65.0`, and dev65.0 (2026-09-29) is the newest published version. Its `dist/duckdb-eh.wasm` storage-version strings end at `v1.5.6` and there is no v2.0.0 anywhere in it. No npm build provides a DuckDB v2.0.0 runtime, so the harness pin (`1.33.1-dev65.0`) stays as is.
- main has no branch protection and no rulesets (`gh api .../branches/main/protection/required_status_checks` returns 404 "Branch not protected", `rulesets` returns `[]`). Renaming job ids and names cannot orphan required checks.
- CONTRIBUTING.md states no DuckDB version (its only hit is "DuckDB version" in the bug-report checklist), so it needs no edit.
- No `actionlint` is installed locally. YAML is validated with PyYAML 6.0.3 (present).
</context>

<tasks>

<task type="tracer">
  <name>Task 1: Tracer: pin submodules to v2.0.0 / v2.0-cyanoptera, make the smoke test fetch the v2.0 CLI, then prove build, full suite and official-CLI load locally</name>
  <files>.gitmodules, duckdb, extension-ci-tools, scripts/smoke_test.py</files>
  <action>
This is the thinnest end-to-end path through every layer the phase touches: the submodule pin, the local build, the full sqllogictest suite, and the exact smoke script CI runs, loading the shipped `.duckdb_extension` into the official DuckDB v2.0.0 CLI.

Step A: submodules (leave `datazoo-banner` and `posthog-telemetry` untouched).
1. Run `git -C duckdb checkout v2.0.0` and confirm that `git -C duckdb rev-parse HEAD` is d54cbbe3df17e8e5ce68312f483b4155836112aa. If the tag is somehow missing, first run `git -C duckdb fetch --depth 1 origin tag v2.0.0`.
2. Run `git -C extension-ci-tools fetch origin v2.0-cyanoptera`, then `git -C extension-ci-tools checkout FETCH_HEAD`. Record the resulting sha for the SUMMARY.
3. Edit `.gitmodules` so the `extension-ci-tools` section's `branch` is `v2.0-cyanoptera`. That is the only change in the file.

Step B: `scripts/smoke_test.py` (CI's `_extension_smoke_test.yml` calls this for every line, so it must keep working for v1.4.5, v1.5.6 and v2.0.0).
- Replace the `ARCH_TO_CLI_ZIP` mapping with an ordered per-arch list of candidate asset names, for example `ARCH_TO_CLI_ASSETS: dict[str, tuple[str, ...]]`. List the DuckDB >= 2.0 tarball name first and the existing legacy v1.x zip name second, keeping today's arch-to-CLI choices:
  - linux_amd64: `duckdb-cli-linux-amd64.tar.gz`, then `duckdb_cli-linux-amd64.zip`
  - linux_arm64: `duckdb-cli-linux-arm64.tar.gz`, then `duckdb_cli-linux-aarch64.zip`
  - linux_amd64_musl: keep the current glibc choice, `duckdb-cli-linux-amd64.tar.gz`, then `duckdb_cli-linux-amd64.zip`
  - osx_amd64 and osx_arm64: `duckdb-cli-osx-universal.tar.gz`, then `duckdb_cli-osx-universal.zip`
  - windows_amd64: `duckdb-cli-windows-amd64.tar.gz`, then `duckdb_cli-windows-amd64.zip`
  Keep the unsupported-arch SystemExit.
- In `_download_duckdb_cli`, try each candidate URL (`https://github.com/duckdb/duckdb/releases/download/{version}/{asset}`) in order and save it under its own asset name in `dest_dir`. On a `urllib.error.HTTPError` with code 404, print a one-line "not found, trying next asset name" and continue. Re-raise any other error. If every candidate 404s, raise SystemExit listing every URL tried.
- Extract `.zip` with `zipfile` exactly as today. Extract `.tar.gz` with `tarfile.open(path, "r:gz")`. When `hasattr(tarfile, "data_filter")`, use `extractall(dest_dir, filter="data")`. Otherwise first reject any member whose name is absolute or contains a `..` path component, then extract (this blocks path traversal; see T-261022-03).
- Keep locating the binary at `dest_dir/duckdb` (or `duckdb.exe` on Windows), plus the chmod. Make the not-found error list the archive's members for both formats.
- Update the module docstring (or a comment by the mapping) to say: v2.0.0 renamed the release assets to `duckdb-cli-<os>-<arch>.tar.gz` (v1.x used `duckdb_cli-<os>-<arch>.zip`), and both are tried so one script serves the v1.4.5, v1.5.6 and v2.0.0 lines.
- Do NOT change `EXTENSION_NAME`, `SMOKE_QUERY`, `_run_sql`, or the three-step install / load / call logic.

Step C: local proof.
1. Run the clean build: `LOG=${TMPDIR:-/tmp}/anofox-261022-laz-build.log; ( rm -rf build && make release ) > "$LOG" 2>&1; echo $? > "$LOG.exit"`. Use exactly this `LOG` path, because the verify reads it. It is outside the working tree on purpose. It takes about 15 to 25 minutes, which exceeds the 10-minute Bash timeout, so run it with `run_in_background` and wait until `$LOG.exit` exists before reading it. It must contain 0. On failure, report the last ~80 log lines and STOP. Do not add workarounds or exclusions.
2. Confirm the artifact really targets 2.0: `tail -c 600 build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension | strings` must show `v2.0.0` (the stale v1.5.6 build showed `v1.5.6` there).
3. Run the full suite: `HOME=$(mktemp -d) ./build/release/test/unittest "$PWD/test/*"` (a fresh HOME keeps a cached extension from masking results). Record the exact assertion and test-case counts from the final summary line (expected 5591 / 94, 0 failures).
4. Run the smoke test end to end: `python3 scripts/smoke_test.py "$PWD/build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension" v2.0.0 linux_amd64` must print `Smoke test PASSED`.

Step D: commits (only after Step C passes). Stage explicit paths only, never `git add -A` (the tree has untracked benchmark and .gsd junk).
- Commit 1: `git add .gitmodules duckdb extension-ci-tools`, message `build(deps): pin duckdb v2.0.0 and extension-ci-tools v2.0-cyanoptera`, with a body naming both shas.
- Commit 2: `git add scripts/smoke_test.py`, message `fix(ci): smoke test fetches DuckDB v2.0 CLI tarballs (v2.0.0 renamed release assets)`.
- Both messages end with the line `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`.
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast && LOG=${TMPDIR:-/tmp}/anofox-261022-laz-build.log && git submodule status && test "$(git -C duckdb rev-parse HEAD)" = d54cbbe3df17e8e5ce68312f483b4155836112aa && TIP=$(git ls-remote https://github.com/duckdb/extension-ci-tools refs/heads/v2.0-cyanoptera) && test -n "$TIP" && test "$(git -C extension-ci-tools rev-parse HEAD)" = "${TIP%%[[:space:]]*}" && test "$(git config -f .gitmodules submodule.extension-ci-tools.branch)" = v2.0-cyanoptera && test "$(git -C datazoo-banner rev-parse HEAD)" = 74eaa6966f24088a5c240e65afe9285aa5ab776c && test "$(git -C posthog-telemetry rev-parse HEAD)" = f550a663d491e29b521998a9c1ca63bb6503c83a && python3 -c "
import sys,tempfile,subprocess
sys.path.insert(0,'scripts'); import smoke_test as s
for v in ('v2.0.0','v1.5.6'):
    b=s._download_duckdb_cli(v,'linux_amd64',tempfile.mkdtemp())
    out=subprocess.run([b,'--version'],capture_output=True,text=True).stdout
    assert v in out,(v,out); print('CLI ok',v)
" && test "$(cat "$LOG.exit")" = 0 && tail -c 600 build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension | strings | grep -qx 'v2.0.0' && { HOME=$(mktemp -d) ./build/release/test/unittest "$PWD/test/*" > "$LOG.unittest" 2>&1; rc=$?; tail -3 "$LOG.unittest"; test $rc = 0; } && grep -q 'All tests passed' "$LOG.unittest" && python3 scripts/smoke_test.py "$PWD/build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension" v2.0.0 linux_amd64 | tail -1 | grep -q 'Smoke test PASSED'</automated>
  </verify>
  <done>Both submodules are pinned (duckdb d54cbbe3df = v2.0.0, extension-ci-tools = the origin v2.0-cyanoptera tip, .gitmodules branch v2.0-cyanoptera). The clean `make release` exited 0. The artifact metadata says v2.0.0. unittest reports "All tests passed" with exact counts recorded (expected 5591 assertions / 94 test cases). The smoke script downloads both the v2.0.0 tarball and the v1.5.6 zip CLI, and passes against the local artifact on the official v2.0.0 CLI. Two commits exist (verify with `git log --oneline -3` and `git show --stat HEAD~1 HEAD`).</done>
</task>

<task type="auto">
  <name>Task 2: CI: v2.0.0 becomes a full build/smoke/deploy stable line, the v1.5.6 line is renamed, the WASM test stays on v1.5.6 with the reason recorded</name>
  <files>.github/workflows/MainDistributionPipeline.yml, .github/workflows/WasmTest.yml, .github/workflows/_extension_deploy.yml, test/wasm/package.json, test/wasm/README.md</files>
  <action>
Edit `.github/workflows/MainDistributionPipeline.yml`:

1. LTS line (`duckdb-lts-build`, `duckdb-lts-smoke-test`, `duckdb-lts-deploy`): leave the job definitions untouched (the verify compares them to base b2fbfbe).

2. v1.5.6 line: rename job ids `duckdb-latest-build`, `duckdb-latest-smoke-test` and `duckdb-latest-deploy` to `duckdb-v15-build`, `duckdb-v15-smoke-test` and `duckdb-v15-deploy`. Update the smoke job's `needs` to `duckdb-v15-build` and the deploy job's `needs` to `[duckdb-v15-build, duckdb-v15-smoke-test]`. Set the build job name to `Build extension binaries (DuckDB v1.5.6, registry track variegata)`. Keep `Smoke test (v1.5.6)` and `Deploy extension binaries (v1.5.6)`. Add a short comment above the build job: this is a full build + deploy line kept for the community-extensions registry's `variegata` (1.5.x) track and for DataZoo repo users still on 1.5.6. Every `uses:`, `with:`, `if:` and `secrets:` value stays exactly as in base.

3. Stable line: delete the non-blocking `duckdb-next-build` job and its comment block. In its place (after `duckdb-v15-deploy`, before `wasm-runtime-test`) add three jobs that mirror the v1.5.6 line's structure:
   - `duckdb-stable-build`, named `Build extension binaries (DuckDB stable v2.0.0)`. It uses `duckdb/extension-ci-tools/.github/workflows/_extension_distribution.yml@v2.0-cyanoptera` with `duckdb_version: v2.0.0`, `ci_tools_version: v2.0-cyanoptera`, `extension_name: anofox_forecast`, `enable_rust: true`, `reduced_ci_mode: disabled` and `exclude_archs: 'windows_amd64_rtools;windows_amd64_mingw'` (the v1.5.6 line's exclusions verbatim, with the same two explanatory comment lines). Set NO `artifact_postfix`, so artifacts are named `anofox_forecast-v2.0.0-extension-<arch>`, which is what the local smoke and deploy workflows download.
   - `duckdb-stable-smoke-test`, named `Smoke test (v2.0.0)`, `needs: duckdb-stable-build`. It uses `./.github/workflows/_extension_smoke_test.yml` with `duckdb_version: v2.0.0`, `extension_name: anofox_forecast` and the same `exclude_archs`.
   - `duckdb-stable-deploy`, named `Deploy extension binaries (v2.0.0 stable)`, `needs: [duckdb-stable-build, duckdb-stable-smoke-test]`, `if: ${{ !cancelled() }}`. It uses `./.github/workflows/_extension_deploy.yml` with `secrets: inherit` and `duckdb_version: v2.0.0`, `ci_tools_version: v2.0-cyanoptera`, `extension_name: anofox_forecast`, the same `exclude_archs`, and `deploy_latest` / `deploy_versioned` copied verbatim from the v1.5.6 deploy job (true on `refs/tags/v*` and on main).
   - Comment block above `duckdb-stable-build`. It must say:
     - this is the stable line, and it matches the duckdb + extension-ci-tools submodules and duckdb/community-extensions' default build (ref v2.0, ci-tools v2.0-cyanoptera); bump the duckdb tag, the ci-tools ref and both submodules together;
     - `reduced_ci_mode: disabled` makes linux_amd64 run the full sqllogictest suite;
     - WASM pins Rust 1.86 via `ANOFOX_WASM_RUST_TOOLCHAIN` in CMakeLists.txt (ci-tools v2.0 uses Rust 1.97 + emsdk 3.1.71);
     - on cyanoptera, osx_amd64, the musl archs and windows_arm64 are opt-in ci-tools archs and are not built here (the v1.5.6 line still ships osx_amd64);
     - smoke tests use the official v2.0.0 CLI tarballs (see scripts/smoke_test.py).
   - Do NOT add any further `exclude_archs` entries. wasm_mvp, wasm_threads, Windows, macOS and arm64 on 2.0 have not been built yet, so CI decides, and the orchestrator handles failures.

4. `wasm-runtime-test`: change `needs` to `duckdb-v15-build` and its `if:` to `${{ !cancelled() && needs.duckdb-v15-build.result == 'success' }}`. Keep the job name and the artifact `anofox_forecast-v1.5.6-extension-wasm_eh`. Extend its comment to record the decision: it stays on the v1.5.6 line because the newest @duckdb/duckdb-wasm on npm (1.33.1-dev65.0, dist-tag next; latest is 1.33.1-dev57.0) bundles at most engine v1.5.6 (the storage-version list in dist/duckdb-eh.wasm ends at v1.5.6, checked 2026-10-22), and DuckDB-Wasm rejects an extension built for a different engine version. When a duckdb-wasm build bundling v2.0.0 ships, bump test/wasm/package.json, `needs` and the artifact name to the stable line together.

5. Add one header comment line near the top of the file (after the existing "Main pipeline..." line) listing the three lines: v2.0.0 stable (cyanoptera), v1.5.6 (variegata) and v1.4.5 LTS (andium). Each is built, smoke-tested and deployed.

Edit `.github/workflows/WasmTest.yml` (comments only, so its parsed YAML must equal base): above the download step add a comment saying it intentionally tests the v1.5.6-line wasm_eh artifact (no DuckDB-Wasm npm build bundles v2.0.0 yet; see the wasm-runtime-test comment in MainDistributionPipeline.yml). The artifact name stays the same.

Edit `.github/workflows/_extension_deploy.yml` (comments only, parsed YAML equals base): in the "Checkout extension-ci-tools to version" comment, change "Submodules are pinned to the stable DuckDB version; for the LTS deploy we need the matching tools so the parsed architecture matrix lines up with what the LTS build job produced" so that it names the stable version (v2.0.0) and says the v1.5.6 and v1.4.5 LTS deploys need their matching tools so the parsed matrix lines up with what that line's build job produced.

Edit `test/wasm/package.json`: rewrite only the `"comment"` string so it says the pin matches the wasm_eh artifact this harness tests, which is the v1.5.6 line (1.33.1-dev65.0 bundles engine v1.5.6, verified via the storage-version list in dist/duckdb-eh.wasm). The stable line is DuckDB v2.0.0, but as of 2026-10-22 no @duckdb/duckdb-wasm build bundles v2.0.0 (the newest is 1.33.1-dev65.0). When one ships, bump this pin and repoint the CI jobs to the v2.0.0 artifact together. Leave `dependencies` and `package-lock.json` untouched (no npm install).

Edit `test/wasm/README.md`: under "Bumping the pin", add a short "Current status (2026-10-22)" paragraph with the same facts (stable line v2.0.0, harness still on the v1.5.6 artifact, why, and what to bump together).

Commit: `git add` exactly the five files, message `ci: make DuckDB v2.0.0 the stable build/smoke/deploy line; keep v1.5.6 and v1.4.5 LTS`, ending with `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`.
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast && python3 -c "import yaml,sys;[yaml.safe_load(open(f)) for f in sys.argv[1:]]" .github/workflows/*.yml && python3 -c "
import yaml,re,glob,json,subprocess
B='b2fbfbe'
def base(p): return yaml.safe_load(subprocess.check_output(['git','show',B+':'+p]))
for f in sorted(glob.glob('.github/workflows/*.yml')):
    jobs=yaml.safe_load(open(f))['jobs']
    for j,v in jobs.items():
        n=v.get('needs',[]); n=[n] if isinstance(n,str) else n
        assert all(x in jobs for x in n),(f,j,n)
        for ref in re.findall(r'needs\.([A-Za-z0-9_-]+)\.', yaml.safe_dump(v)): assert ref in n,(f,j,ref)
M='.github/workflows/MainDistributionPipeline.yml'
m=yaml.safe_load(open(M))['jobs']; o=base(M)['jobs']
for k in ('duckdb-lts-build','duckdb-lts-smoke-test','duckdb-lts-deploy'): assert m[k]==o[k],k
assert not any(k.startswith('duckdb-latest') or k=='duckdb-next-build' for k in m), sorted(m)
for s in ('build','smoke-test','deploy'):
    a,b=m['duckdb-v15-'+s],o['duckdb-latest-'+s]
    assert a['uses']==b['uses'] and a['with']==b['with'] and a.get('if')==b.get('if') and a.get('secrets')==b.get('secrets'),s
assert m['duckdb-v15-smoke-test']['needs']=='duckdb-v15-build'
assert sorted(m['duckdb-v15-deploy']['needs'])==['duckdb-v15-build','duckdb-v15-smoke-test']
sb,ss,sd=m['duckdb-stable-build'],m['duckdb-stable-smoke-test'],m['duckdb-stable-deploy']
assert sb['uses'].endswith('_extension_distribution.yml@v2.0-cyanoptera')
assert sb['with']=={'duckdb_version':'v2.0.0','ci_tools_version':'v2.0-cyanoptera','extension_name':'anofox_forecast','enable_rust':True,'reduced_ci_mode':'disabled','exclude_archs':o['duckdb-latest-build']['with']['exclude_archs']},sb['with']
assert ss['needs']=='duckdb-stable-build' and ss['uses']==o['duckdb-latest-smoke-test']['uses'] and ss['with']=={**o['duckdb-latest-smoke-test']['with'],'duckdb_version':'v2.0.0'}
od=o['duckdb-latest-deploy']
assert sorted(sd['needs'])==['duckdb-stable-build','duckdb-stable-smoke-test'] and sd['if']==od['if'] and sd['secrets']=='inherit' and sd['uses']==od['uses']
assert sd['with']=={**od['with'],'duckdb_version':'v2.0.0','ci_tools_version':'v2.0-cyanoptera'},sd['with']
w=m['wasm-runtime-test']; assert w['needs']=='duckdb-v15-build' and 'needs.duckdb-v15-build.result' in w['if']
assert 'anofox_forecast-v1.5.6-extension-wasm_eh' in json.dumps(w)
for p in ('.github/workflows/WasmTest.yml','.github/workflows/_extension_deploy.yml'): assert yaml.safe_load(open(p))==base(p),p
pj=json.load(open('test/wasm/package.json')); pb=json.loads(subprocess.check_output(['git','show',B+':test/wasm/package.json']))
assert pj['dependencies']==pb['dependencies'] and 'v2.0.0' in pj['comment']
print('CI structure OK')
" && git diff --quiet b2fbfbe -- test/wasm/package-lock.json scripts/extension-upload.sh .github/workflows/_extension_smoke_test.yml .github/workflows/audit.yml && grep -q 'v2.0.0' test/wasm/README.md</automated>
  </verify>
  <done>All workflow files parse. Every `needs:` and `needs.<job>.` reference resolves. The LTS job definitions equal base. The duckdb-v15-* jobs carry the old v1.5.6 inputs unchanged. duckdb-stable-{build,smoke-test,deploy} exist with v2.0.0 / v2.0-cyanoptera inputs mirroring the v1.5.6 line (no artifact_postfix, reduced_ci_mode disabled, deploy on tags and main, secrets inherited). The non-blocking 2.0 job is gone. wasm-runtime-test depends on duckdb-v15-build and still downloads the v1.5.6 wasm_eh artifact, with the reason documented in the workflow comment, test/wasm/package.json and test/wasm/README.md. WasmTest.yml and _extension_deploy.yml changed in comments only. One commit exists for these five files.</done>
</task>

<task type="auto">
  <name>Task 3: Docs: state v2.0.0 as the stable line in README / CLAUDE.md / API reference / skill headers, and add an [Unreleased] CHANGELOG entry</name>
  <files>.claude/CLAUDE.md, README.md, docs/API_REFERENCE.md, CHANGELOG.md, plugins/anofox-forecast/skills/anofox-forecast-backtest/SKILL.md, plugins/anofox-forecast/skills/anofox-forecast-data-prep/SKILL.md, plugins/anofox-forecast/skills/anofox-forecast-detection/SKILL.md, plugins/anofox-forecast/skills/anofox-forecast-eda/SKILL.md, plugins/anofox-forecast/skills/anofox-forecast-models/SKILL.md</files>
  <action>
These are mostly one-line version-statement replacements. The five SKILL.md edits are a single mechanical token sweep, which is why this task lists more than five files. Do not touch any other content in these files, any historical CHANGELOG section, or anything under docs/release-notes/. CONTRIBUTING.md stays unchanged (it states no DuckDB version).

`.claude/CLAUDE.md` (edit only these lines; leave the GSD-managed "Developer Profile" section alone):
- line 15 Constraints "Tech stack": the DuckDB part becomes `DuckDB extension built for v2.0.0 (stable), v1.5.6 and v1.4.5 LTS`.
- line 37 Runtime: `DuckDB v2.0.0 (primary database engine; v1.5.6 and v1.4.5 LTS lines also built and shipped)`.
- line 61 Key Dependencies: `DuckDB v2.0.0 (submodule) - Extension host and table function framework`.
- line 81 Platform Requirements: `DuckDB development headers (via the duckdb submodule, pinned to v2.0.0)`.
- line 82: `DuckDB v2.0.0, v1.5.6, or v1.4.5 LTS (one extension build per line)`.
- line 292 Architectural Constraints "DuckDB version": `Built and tested on v2.0.0 (stable; submodule + extension-ci-tools v2.0-cyanoptera), v1.5.6 (v1.5-variegata) and v1.4.5 LTS (v1.4-andium) CI lines`, keeping the rest of that sentence (C++17 / constexpr note).
Use grep to confirm the line numbers before editing.

`README.md`:
- Badge (line 10): the shields.io label becomes `DuckDB-2.0.0%20%7C%201.5.6%20%7C%201.4.5%20LTS-green` (renders "2.0.0 | 1.5.6 | 1.4.5 LTS"). Keep alt text and link.
- Line 157: "(Requires DuckDB v1.4.5 LTS or v1.5.6+)" becomes "(Requires DuckDB v2.0.0, v1.5.6, or v1.4.5 LTS)".
- In "From Source", directly after the closing fence of the build code block, add one sentence: the `duckdb` submodule is pinned to v2.0.0, so a source build targets DuckDB v2.0.0. To build for v1.5.6 or v1.4.5 LTS, check out the `duckdb` submodule at that tag and `extension-ci-tools` at `v1.5-variegata` / `v1.4-andium`.
- After the "(Use `FORCE INSTALL … FROM 'http://get.erpl.io';` ...)" line, add one sentence: builds are published there for DuckDB v2.0.0, v1.5.6 and v1.4.5 LTS.

`docs/API_REFERENCE.md` line 6: the DuckDB Version line becomes `**DuckDB Version:** v2.0.0 (stable), v1.5.6, v1.4.5 LTS`.

The five `plugins/anofox-forecast/skills/*/SKILL.md` header lines: replace only the token `**DuckDB:** v1.4.5 LTS / v1.5.4+` with `**DuckDB:** v2.0.0 / v1.5.6 / v1.4.5 LTS`. The rest of each header line stays as is (the stale extension date there is out of scope).

`CHANGELOG.md`: insert a new section directly above `## [2026.10.22] - 2026-10-22 — macOS build on DuckDB v2.0.0`, following the file's existing style:
- Heading `## [Unreleased]`, then "No SQL signature or numeric output changes."
- `### Changed`:
  - **DuckDB v2.0.0 is now the stable build line.** The `duckdb` submodule is pinned to v2.0.0 and `extension-ci-tools` to v2.0-cyanoptera, so a local `make release` builds against 2.0.0. CI now builds, smoke-tests and deploys three lines: v2.0.0 (stable, ci-tools v2.0-cyanoptera), v1.5.6 (v1.5-variegata, the registry's `variegata` track) and v1.4.5 LTS (v1.4-andium). The former non-blocking 2.0 build-only job is folded into the stable line. This follows community-extensions' "Update path: After DuckDB release".
  - **WASM runtime test stays on the v1.5.6 artifact.** No `@duckdb/duckdb-wasm` npm build bundles a DuckDB v2.0.0 engine yet (the newest, 1.33.1-dev65.0, bundles v1.5.6). The Node harness moves to the stable line when one ships.
- `### Fixed`:
  - **Post-build smoke test on DuckDB v2.0.0.** v2.0.0 renamed its CLI release assets from `duckdb_cli-<os>-<arch>.zip` to `duckdb-cli-<os>-<arch>.tar.gz`. `scripts/smoke_test.py` now tries the new tarball first and falls back to the legacy zip, so one script serves all three lines.

Commit: `git add` exactly these nine files, message `docs: DuckDB v2.0.0 is the stable line (README, CLAUDE.md, API reference, skills, CHANGELOG)`, ending with `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`.
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast && python3 -c "
import glob,subprocess
c=open('.claude/CLAUDE.md').read(); assert '1.4.3' not in c and 'v1.5.4+' not in c and c.count('v2.0.0')>=4, 'CLAUDE.md'
r=open('README.md').read(); assert 'DuckDB-2.0.0%20%7C%201.5.6%20%7C%201.4.5%20LTS-green' in r and 'Requires DuckDB v2.0.0, v1.5.6, or v1.4.5 LTS' in r, 'README'
a=open('docs/API_REFERENCE.md').read(); assert '**DuckDB Version:** v2.0.0 (stable), v1.5.6, v1.4.5 LTS' in a, 'API_REFERENCE'
sk=sorted(glob.glob('plugins/anofox-forecast/skills/*/SKILL.md')); assert len(sk)==5
assert all('**DuckDB:** v2.0.0 / v1.5.6 / v1.4.5 LTS' in open(f).read() and 'v1.5.4+' not in open(f).read() for f in sk), 'skills'
ch=open('CHANGELOG.md').read(); assert ch.count('## [Unreleased]')==1 and ch.index('## [Unreleased]')<ch.index('## [2026.10.22]'), 'CHANGELOG order'
d=subprocess.check_output(['git','diff','-U0','b2fbfbe','--','CHANGELOG.md'],text=True)
removed=[l for l in d.splitlines() if l.startswith('-') and not l.startswith('---')]; assert not removed, removed
print('docs OK')
" && git diff --quiet b2fbfbe -- docs/release-notes/ CONTRIBUTING.md && test -z "$(git status --short --untracked-files=no)" && git log --oneline b2fbfbe..HEAD</automated>
  </verify>
  <done>The README badge and requirement line, the five CLAUDE.md version statements plus the DuckDB-version constraint line, the API reference header and all five skill headers name v2.0.0 (stable), v1.5.6 and v1.4.5 LTS. The README notes the submodule pin and the get.erpl.io lines. CHANGELOG.md has exactly one `## [Unreleased]` section above 2026.10.22, and the diff against b2fbfbe removes no line. docs/release-notes/ and CONTRIBUTING.md are unchanged. No tracked change is left uncommitted. `git log b2fbfbe..HEAD` shows the four commits from Tasks 1-3.</done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| GitHub (duckdb/duckdb, duckdb/extension-ci-tools) -> repo submodules | Upstream source pulled into the build by commit pointer |
| GitHub release assets -> CI runner / dev machine | `scripts/smoke_test.py` downloads and extracts the official DuckDB CLI archive and executes it |
| Reusable workflow `duckdb/extension-ci-tools@v2.0-cyanoptera` -> our CI | Third-party workflow code runs our build (no secrets passed to the build job) |
| CI deploy job -> S3 bucket (OIDC role, `secrets: inherit`) | The new stable deploy job publishes binaries to get.erpl.io |

## STRIDE Threat Register

| Threat ID | Category | Component | Severity | Disposition | Mitigation Plan |
|-----------|----------|-----------|----------|-------------|-----------------|
| T-261022-01 | Tampering | duckdb submodule pointer | high | mitigate | Task 1 verify asserts `git -C duckdb rev-parse HEAD` equals d54cbbe3df17e8e5ce68312f483b4155836112aa, the commit the official upstream `v2.0.0` tag resolves to, not a tag name alone |
| T-261022-02 | Tampering | extension-ci-tools submodule pointer | medium | mitigate | Task 1 verify asserts HEAD equals `git ls-remote https://github.com/duckdb/extension-ci-tools refs/heads/v2.0-cyanoptera`, and the sha is recorded in the commit body and SUMMARY |
| T-261022-03 | Tampering | `scripts/smoke_test.py` tar extraction | medium | mitigate | `tarfile` `filter="data"` when available, otherwise reject absolute or `..` member paths before extracting. Downloads come only from the fixed HTTPS prefix `https://github.com/duckdb/duckdb/releases/download/` |
| T-261022-04 | Elevation of Privilege | `duckdb-stable-deploy` with `secrets: inherit` | medium | mitigate | `deploy_latest`/`deploy_versioned` are copied verbatim from the existing v1.5.6 deploy (true only on `refs/tags/v*` and main). Task 2 verify asserts the inputs equal the v1.5.6 deploy apart from the version fields, and the build job receives no secrets |
| T-261022-05 | Tampering | reusable workflow referenced by branch `@v2.0-cyanoptera`, not a pinned SHA | low | accept | Same practice as the existing `@v1.5-variegata` / `@v1.4-andium` lines and the community-extensions registry, which builds from the same ci-tools branch. Pinning by SHA would desync from the registry |
| T-261022-06 | Information Disclosure / Tampering | S3 path collision between lines | low | accept | extension-upload.sh namespaces uploads by the DuckDB tag (`.../v2.0.0/<arch>/` vs `.../v1.5.6/<arch>/`), verified at planning time. No cross-line overwrite |
| T-261022-SC | Tampering | npm / pip / cargo installs | low | accept | No new or changed packages. `test/wasm/package.json` dependencies and `package-lock.json` stay unchanged (asserted in Task 2 verify). No package-manager install task exists, so the legitimacy gate does not apply |
</threat_model>

<verification>
Executor runs, in order, and reports exact outputs in the SUMMARY:
1. `git submodule status`: duckdb at d54cbbe3df (v2.0.0) and extension-ci-tools at the v2.0-cyanoptera tip (record the sha); datazoo-banner and posthog-telemetry unchanged.
2. Clean local build: `rm -rf build && make release` exits 0 (backgrounded, 15 to 25 minutes); artifact metadata shows v2.0.0.
3. `HOME=$(mktemp -d) ./build/release/test/unittest "$PWD/test/*"`: all tests pass. Report the exact counts (expected 5591 assertions / 94 test cases).
4. `python3 scripts/smoke_test.py <local artifact> v2.0.0 linux_amd64` prints `Smoke test PASSED` (official v2.0.0 CLI). The v1.5.6 CLI download path still works.
5. YAML validity: `python3 -c "import yaml,sys;[yaml.safe_load(open(f)) for f in sys.argv[1:]]" .github/workflows/*.yml`, plus the Task 2 structural check (every `needs:` resolves to an existing job id, and every `needs.<job>.` expression is backed by a `needs:` entry).
6. v1.5.6 still builds: NOT run locally. The CI `duckdb-v15-*` line covers it. State this explicitly in the SUMMARY.
7. Do not push, tag, merge or open a PR (the orchestrator does that). Verify claims against git and disk: `git log --oneline b2fbfbe..HEAD` shows the four commits, and SUMMARY.md exists on disk.
</verification>

<success_criteria>
- The submodules are pinned to DuckDB v2.0.0 and extension-ci-tools v2.0-cyanoptera; the local clean build and the full clean-HOME suite pass against 2.0.0 (exact counts reported).
- The shipped artifact loads and runs in the official DuckDB v2.0.0 CLI through the same `scripts/smoke_test.py` CI uses, and the script stays backward compatible with the v1.x zip assets.
- CI has three full lines (v2.0.0 stable / v1.5.6 / v1.4.5 LTS), each with build, smoke test and deploy. The LTS jobs are unchanged and the non-blocking 2.0 job is retired.
- The WASM runtime test is consciously kept on v1.5.6, with the npm evidence recorded in the workflow, package.json and README.
- Docs name v2.0.0 as the stable line, the CHANGELOG has an `[Unreleased]` entry, and history is untouched.
- Four conventional commits end with the Co-Authored-By line, and nothing outside the listed files was staged.
</success_criteria>

<output>
Create `.planning/quick/261022-laz-make-duckdb-v2-0-0-the-stable-build-line/261022-laz-SUMMARY.md` when done. Include:
- the extension-ci-tools sha
- the unittest counts
- the smoke-test output tail
- the four commit hashes
- the WASM keep-on-1.5.6 decision with its evidence
- the smoke-test asset-rename finding
- the explicit note that v1.5.6 was not built locally (covered by the CI duckdb-v15-* line)
</output>