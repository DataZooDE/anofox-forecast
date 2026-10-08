---
phase: quick-261006-ujg
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - CMakeLists.txt
  - .gitmodules
  - duckdb
  - extension-ci-tools
  - src/  # ONLY the C++ files the DuckDB 2.0 compiler rejects (unknown until build)
  - .gitignore
  - benchmark/duckdb2_compare.py
  - benchmark/src/common/anofox_runner.py
  - benchmark/sql/duckdb2_compare.sql
  - .planning/quick/261006-ujg-test-anofox-forecast-against-duckdb-2-0-/261006-ujg-BENCHMARK.md
  - .planning/quick/261006-ujg-test-anofox-forecast-against-duckdb-2-0-/evidence/
autonomous: true
requirements: [QUICK-261006-ujg]

estimate:
  tokens: 220000
  raw_tokens: 220000
  tasks: 3
  confidence: low

must_haves:
  truths:
    - "A native `make release` on this machine picks Rust target x86_64-unknown-linux-gnu even though wasm32-unknown-emscripten is installed, while `make wasm_eh` still picks wasm32-unknown-emscripten"
    - "anofox_forecast builds natively against duckdb branch v2.0-cyanoptera, LOADs in the matching 2.0 CLI, and ts_forecast_by output is identical to the v1.5.5 baseline on the tracer subset"
    - "The sqllogictest suite ran on BOTH builds; pass/fail counts are recorded and every 2.0-only failure is listed with a root-cause category"
    - "A wasm_eh build against 2.0 was attempted; its result (build ok, duckdb_signature section, unresolved anofox FFI imports, runtime harness PASS/FAIL/BLOCKED) is recorded honestly"
    - "M4 Daily (baseline, ets, theta, arima, mfles, mstl families), the 10k synthetic SQL, and the SQL micro-benchmark ran sequentially on both builds with no concurrent CPU load; times, delta %, and accuracy identity are tabulated"
    - "261006-ujg-BENCHMARK.md exists with environment, tests, WASM, benchmark tables, and the list of code changes needed for 2.0"
  artifacts:
    - path: "CMakeLists.txt"
      provides: "Rust target selection only uses wasm32-unknown-emscripten when EMSCRIPTEN is set"
    - path: ".gitmodules"
      provides: "extension-ci-tools branch = v2.0-cyanoptera"
    - path: "benchmark/duckdb2_compare.py"
      provides: "Driver with subcommands tracer, m4, sqlbench, synth10k, compare"
    - path: "benchmark/sql/duckdb2_compare.sql"
      provides: "Pure-SQL micro-benchmark; extension path supplied via CLI -cmd LOAD"
    - path: "benchmark/src/common/anofox_runner.py"
      provides: "ANOFOX_USE_CLI and ANOFOX_CLI_TIMEOUT env toggles to run TS_FORECAST_BY through the build's own CLI"
    - path: ".planning/quick/261006-ujg-test-anofox-forecast-against-duckdb-2-0-/261006-ujg-BENCHMARK.md"
      provides: "Final report"
  key_links:
    - from: "benchmark/duckdb2_compare.py"
      to: "<tree>/build/release/duckdb"
      via: "CLI derived from the extension path (extension/anofox_forecast/x -> ../../duckdb), same as _find_duckdb_cli"
      pattern: "_find_duckdb_cli|duckdb_cli"
    - from: "extension_config.cmake LINKED_LIBS"
      to: "duckdb/extension/extension_build_tools.cmake (2.0) wasm post-build emcc link"
      via: "DUCKDB_EXTENSION_ANOFOX_FORECAST_LINKED_LIBS"
      pattern: "LINKED_LIBS"
---

<objective>
Test anofox-forecast against the unreleased DuckDB 2.0 (branch `v2.0-cyanoptera` of duckdb/duckdb and duckdb/extension-ci-tools) on git branch `test/duckdb-2.0`. Cover native build, the sqllogictest suite, the wasm_eh build, and benchmarks against a v1.5.5 baseline.

Purpose: Find out early what DuckDB 2.0 breaks: compile errors, behavior changes, or performance regressions. Also confirm that forecast accuracy is identical across engine versions.

Output: a building 2.0 branch (submodule bump plus any required C++ fixes), a comparison driver and SQL micro-benchmark, an evidence folder, and `261006-ujg-BENCHMARK.md`.
</objective>

<execution_context>
@~/.claude/gsd-core/workflows/execute-plan.md
@~/.claude/gsd-core/templates/summary.md
</execution_context>

<context>
@.planning/STATE.md
@.claude/CLAUDE.md
@CMakeLists.txt
@extension_config.cmake
@benchmark/src/common/anofox_runner.py
@benchmark/src/common/benchmark_runner.py
@test/wasm/run.mjs

## Established facts (orchestrator + planner verification; do not re-derive)

Paths (absolute):
- MAIN = /home/simonm/projects/duckdb/anofox-forecast (branch `test/duckdb-2.0`, forked from main @ 7e23980; worktree isolation OFF)
- BASE = /home/simonm/projects/duckdb/anofox-forecast-baseline-v155 (detached @ 7e23980, submodules v1.5.5)
- QDIR = MAIN/.planning/quick/261006-ujg-test-anofox-forecast-against-duckdb-2-0-
- EVID = QDIR/evidence (small text/markdown evidence files only; never parquet or full logs)
- LOGS = MAIN/build/logs (gitignored via `build/`; put all big logs here)
- BASE_EXT = BASE/build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension, BASE_CLI = BASE/build/release/duckdb
- NEW_EXT = MAIN/build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension, NEW_CLI = MAIN/build/release/duckdb

Build state:
- Pre-existing CMake bug (CMakeLists.txt lines 69-73): `Rust_CARGO_TARGET` is forced to wasm32-unknown-emscripten whenever that rustup target is merely installed. It is installed on this machine, so native builds compile Rust to wasm, and the link then fails with `libanofox_fcst_ffi.a: file format not recognized`. The orchestrator ALREADY applied the identical fix (uncommitted) in BASE and RESTARTED the baseline build. BASE/build-release.log now shows `Rust_CARGO_TARGET: x86_64-unknown-linux-gnu` and ends with an `EXIT=<code>` line when done. Do NOT start, kill, or touch the baseline build. Wait for its EXIT line.
- MAIN has no build/ dir yet, so the 2.0 build is cold. Native and wasm use separate dirs (build/release vs build/wasm_eh).
- Submodules now: duckdb @ v1.5.5 (d8cdaa33), extension-ci-tools @ b777c70 (v1.5-variegata). Remote heads at planning time: duckdb `v2.0-cyanoptera` = 5070de35, extension-ci-tools `v2.0-cyanoptera` = 969bc762. Branches may have moved, so pin whatever SHA you actually fetch and record it.
- Machine: 20 cores, 64 GB RAM (~52 GB free). Two concurrent cold builds are safe with `CMAKE_BUILD_PARALLEL_LEVEL=10` each.
- `src/include/anofox_fcst_ffi.h` is modified in MAIN (pre-existing user work, and cbindgen regenerates it on every build). It must NEVER be staged or committed.

Benchmark harness facts:
- The benchmark venv locks python `duckdb==1.5.1`. A locally built extension must exactly match the engine version, so NEITHER the v1.5.5 nor the 2.0 extension can LOAD in-process via the Python package. Only `TS_FORECAST_PANEL_BY` currently goes through the build's own CLI (`_run_panel_query_via_cli`, subprocess timeout hardcoded 600 s). Per-series `TS_FORECAST_BY` uses `con.execute`, which will fail. That is why Task 1 and Task 3 add a CLI toggle.
- `ANOFOX_EXTENSION_PATH` env and the `extension_path` arg select the extension; `_find_duckdb_cli(extension_path)` derives `<build>/release/duckdb` from it.
- Family wrappers (`benchmark/m4/*_benchmark/run.py`) write to a fixed `results/` dir, which already holds untracked user results. Do NOT use the wrappers. The driver calls `run_anofox_benchmark(...)` and `evaluate_forecasts(...)` directly with its own `output_dir`.
- `run_anofox_benchmark(benchmark_name, train_df, horizon, seasonality, models_config, output_dir, group, freq, extension_path, function_name, ...)` writes `anofox-<name>-<group>.parquet` (forecasts) and `anofox-<name>-<group>-metrics.parquet` (columns include `model`, `time_seconds`). `evaluate_forecasts(benchmark_name, test_df_pd, train_df_pd, seasonality, results_dir, group)` writes `<name>-evaluation-<group>.parquet` (mase/mae/rmse per model). Data comes from `src.common.data.get_data('m4', 'Daily', train=True|False)` under benchmark/data. Configs are in `benchmark/configs/{baseline,ets,theta,arima,mfles,mstl}.py` (`BENCHMARK_NAME`, `MODELS`, optional `MAX_SERIES`, `FUNCTION_NAME`). These six configs contain 19 models.
- `benchmark/.venv` does not exist yet. Run `uv sync` in benchmark/. pyproject requires Python >=3.11,<3.13, and system python is 3.14, so uv provisions a managed 3.12.
- M4 parquet `benchmark/data/m4_daily_train_long.parquet`: columns unique_id VARCHAR, ds DATE, y DOUBLE; 9,964,658 rows, 4,227 series.
- `benchmark/sql/10k_series_synthetic_test.sql` LOADs the RELATIVE path `build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension`, so run it with cwd = the tree root whose build you are measuring. It uses random() data, so compare timing only.
- Macro signatures (src/macros/ts_macros.cpp): ts_forecast_by(source, group_col, date_col, target_col, method, horizon, frequency, params := MAP{}); ts_cv_folds_by(source, group_col, date_col, target_col, n_folds, horizon, params := MAP{}); ts_cv_forecast_by(ml_folds, group_col, date_col, target_col, method, params := MAP{}) where ml_folds is a TABLE NAME holding ts_cv_folds_by output; ts_fill_gaps_by(source, group_col, date_col, value_col, frequency); ts_stats_by(source, group_col, date_col, value_col, frequency); ts_features_by(source, group_col, date_col, value_col). Examples also pass a trailing MAP{}; confirm the working arity on the baseline CLI first. ts_cv_forecast_by with 'AutoEnsemble' SEGFAULTS (known crate 0.15.3 bug), so never use it.
- Telemetry: the extension sends PostHog telemetry on LOAD unless `DATAZOO_DISABLE_TELEMETRY=1`. Export it for EVERY CLI, unittest, and benchmark run on BOTH builds. This keeps dev-build telemetry from leaking and removes network latency from timings.

WASM facts (coordinator, verified on v1.5.5):
- Recipe: `source ~/emsdk/emsdk_env.sh` (emsdk 3.1.71 active; current v1.5 ci-tools pin it in `.github/workflows/_extension_distribution.yml` via `setup-emsdk` `version: 3.1.71`). Then `rustup target add wasm32-unknown-emscripten` (already installed). The emsdk-bundled wasm-opt is broken (rejects --enable-bulk-memory-opt). Use official binaryen `version_123` from github.com/WebAssembly/binaryen releases, installed to ~/tools/binaryen-version_123 (does not exist yet), and build with `EM_BINARYEN_ROOT=~/tools/binaryen-version_123` plus its `bin` prepended to PATH. Then run `make wasm_eh` (~30 min cold).
- The ci-tools wasm targets run `emcmake cmake $(GENERATOR) ...` and then `emmake make -j8 -Cbuild/wasm_eh`. Do NOT set GEN=ninja for wasm, because a Ninja-generated tree cannot be driven by `make -C`. Re-check this recipe in the 2.0 makefile before running.
- Artifact: MAIN/build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm. extension_config.cmake passes `LINKED_LIBS "$<TARGET_FILE:anofox_fcst_ffi-static>"`. In v1.5.5, duckdb/extension/extension_build_tools.cmake consumes it as `DUCKDB_EXTENSION_${NAME}_LINKED_LIBS` (lines 186, 293). Verify that this survives in 2.0.
- Runtime harness: `node test/wasm/run.mjs --ext <wasm>` (curated 8-file subset by default). @duckdb/duckdb-wasm is pinned to 1.33.1-dev64.0 (engine v1.5.5) in test/wasm/package.json. npm dist-tags: latest 1.33.1-dev57.0, next 1.33.1-dev65.0 (2026-09-29). A v1.5.5 wasm baseline build is NOT required. Compare against the curated subset, which is green on v1.5.5 in CI. ~23 files are known pre-existing `--all` failures (stale-test debt).
</context>

<tasks>

<task type="tracer">
  <name>Task 1 (tracer): CMake fix, 2.0 submodule bump, native 2.0 build plus compile fixes, AutoETS forecast parity vs v1.5.5 on 20 M4 series</name>
  <files>CMakeLists.txt, .gitmodules, duckdb (submodule pointer), extension-ci-tools (submodule pointer), src/ (only files the 2.0 compiler rejects), .gitignore, benchmark/duckdb2_compare.py</files>
  <action>
Step A, CMake fix (first, before the submodule bump; per coordinator). In MAIN/CMakeLists.txt, change the WASM target check at lines 69-73 from `if (NOT WASM_TARGET_FOUND EQUAL -1)` to `if (EMSCRIPTEN AND NOT WASM_TARGET_FOUND EQUAL -1)`. EMSCRIPTEN is set by the Emscripten toolchain file that emcmake injects for every wasm_* build and for the extension_configuration_wasm step. Native builds then fall through to the OS/arch branch or the rustc-host fallback. Change nothing else in that file. Stage ONLY CMakeLists.txt and commit with the message `fix(build): only target wasm32-unknown-emscripten when building with Emscripten`, followed by a blank line and the Co-Authored-By trailer (see constraints). This is the same patch the orchestrator applied uncommitted in BASE, so baseline numbers use the same fix. The report must say this fix should also go to main as its own PR.

Step B, submodule bump. In MAIN/duckdb: fetch origin `v2.0-cyanoptera` and check out FETCH_HEAD (detached). In MAIN/extension-ci-tools: same. Set `.gitmodules` `submodule.extension-ci-tools.branch` to `v2.0-cyanoptera` via `git config -f .gitmodules`. Record into EVID/versions.txt: both submodule SHAs, `git -C duckdb describe --tags --always`, and the old SHAs (d8cdaa33 / b777c70). Never edit files inside the duckdb or extension-ci-tools submodules. They are the code under test. Stage exactly `.gitmodules duckdb extension-ci-tools` and commit `chore(deps): track duckdb + extension-ci-tools v2.0-cyanoptera` (plus trailer).

Step C, native 2.0 build. Before building, diff the 2.0 ci-tools makefile (`extension-ci-tools/makefiles/duckdb_extension.Makefile`) against the old pin for `release`, `test_release_internal`, and `wasm_eh` recipe changes, and note anything relevant in EVID/ci-tools-diff.txt. Create LOGS. Start the build in the BACKGROUND (Bash run_in_background) from MAIN as one command: `GEN=ninja CMAKE_BUILD_PARALLEL_LEVEL=10 make release` with stdout and stderr redirected to LOGS/build-release-v20.log, then append `EXIT=$?`. Poll for the `EXIT=` line with tail or grep (Monitor until-loop or periodic checks). Never guess completion and never read the full log. Confirm the log shows `Rust_CARGO_TARGET: x86_64-unknown-linux-gnu` and does NOT show `Building for WASM platform`.

Then fix compile errors in a loop: grep the log for `error:`, fix, and re-run the build in the background (incremental). Expected breakage areas are the extension entry point (ExtensionLoader / DUCKDB_CPP_EXTENSION_ENTRY), TableFunction bind/init/execute signatures, FunctionData Copy/Equals, aggregate state callbacks, ListVector/FlatVector/Vector APIs, Value/LogicalType APIs, macro registration in src/macros/ts_macros.cpp, and C++ standard requirements (CMakeLists forces C++17; raise it only if 2.0 headers demand it, and record why).

Fix rules:
- Fix only what the compiler rejects. Changes must be behavior-preserving; no forecasting or SQL-semantics changes.
- Prefer APIs that exist in BOTH 1.5.5 and 2.0. Where impossible, guard with `#if` on a version macro DuckDB really exposes (grep duckdb/src/include first and do not invent a macro name).
- Rust/FFI should be untouched.
- If the DataZooDE submodules posthog-telemetry or datazoo-banner fail under 2.0, you MAY apply a local uncommitted patch inside them purely to unblock testing. List it in the report as a required upstream change. Do not commit inside them.
- Log every change (file, API that changed, fix applied, guarded or not) in EVID/code-changes.md.

When it builds, stage only the touched src/ (and CMakeLists.txt, if changed again) paths and commit `fix(duckdb2): adapt extension to DuckDB 2.0 C++ API` (plus trailer). Skip this commit if no src changes were needed.

BLOCKED path: if the build cannot succeed without patching duckdb or extension-ci-tools sources (for example, the 2.0 branch itself fails to compile, or the ci-tools makefile is broken), stop. Commit what exists, write 261006-ujg-BENCHMARK.md with BLOCKED status and the exact first errors, mark the downstream native sections BLOCKED, and return.

Step D, tracer driver. Wait for BASE/build-release.log to end with `EXIT=0`. If it is non-zero, report it and stop: the baseline is the orchestrator's, so do not rebuild it yourself. Then run `uv sync` in MAIN/benchmark.

Add the line `benchmark/results/duckdb2_compare/` to MAIN/.gitignore (raw parquet outputs live there).

Create MAIN/benchmark/duckdb2_compare.py as an argparse CLI with subcommands. It must insert the benchmark root into sys.path the same way the m4 run.py wrappers do. It must also force `DATAZOO_DISABLE_TELEMETRY=1` into the environment of every subprocess it starts. Implement now ONLY the `tracer` subcommand (Task 3 adds the rest):
- Arguments: `--base-ext`, `--new-ext`.
- For each ext, derive the CLI as ext.parent.parent.parent / 'duckdb' and fail clearly if it is missing.
- Run that CLI with `-unsigned` and a temporary SQL script. The script does the following: LOAD the ext; create table s from the 20 lexicographically first unique_id series of the M4 parquet (absolute path); COPY ts_forecast_by('s', unique_id, ds, y, 'AutoETS', 14, '1d') ordered by all output columns to a temp parquet; COPY `SELECT library_version, source_id FROM pragma_version()` to a second temp parquet.
- Load both forecast parquets with pandas, sort by the group and date key columns, and require exact equality: same columns, same row count (expect 280), and every value bitwise equal (`pandas.testing.assert_frame_equal` with `check_exact=True`).
- Print both engine versions and source_ids, then the single line `TRACER PARITY OK rows=<n>`. Exit 1 with a diff summary on any mismatch.

Append both version lines to EVID/versions.txt. Stage `.gitignore benchmark/duckdb2_compare.py` and commit `test(bench): duckdb2 compare driver with tracer parity check` (plus trailer).

Commit hygiene for every commit in this plan: stage explicit paths only (never `git add -A` or `git add .`). Before each commit, confirm `git diff --cached --name-only` lists only intended paths. The modified FFI header and untracked .cache/, .gsd/, benchmark/data/, and benchmark results must never be staged.
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast && grep -q "Rust_CARGO_TARGET: x86_64-unknown-linux-gnu" build/logs/build-release-v20.log && tail -1 build/logs/build-release-v20.log | grep -q "^EXIT=0" && test "$(git config -f .gitmodules submodule.extension-ci-tools.branch)" = "v2.0-cyanoptera" && grep -q "EMSCRIPTEN AND NOT WASM_TARGET_FOUND" CMakeLists.txt && CHANGED="$(git diff --name-only main..HEAD)" && test "$(printf '%s\n' "$CHANGED" | grep -c 'anofox_fcst_ffi.h')" = "0" && cd benchmark && DATAZOO_DISABLE_TELEMETRY=1 uv run python duckdb2_compare.py tracer --base-ext /home/simonm/projects/duckdb/anofox-forecast-baseline-v155/build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension --new-ext /home/simonm/projects/duckdb/anofox-forecast/build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension | grep -q "TRACER PARITY OK"</automated>
  </verify>
  <done>CMake fix, submodule bump, and (if needed) 2.0 compat fixes are committed on test/duckdb-2.0 as separate commits. The native 2.0 build exits 0 with an x86_64 Rust target. Both CLIs LOAD their extension, and AutoETS forecasts on the 20-series subset are bitwise identical. EVID/versions.txt and EVID/code-changes.md exist. Alternatively, the BLOCKED report was written and the plan stopped here.</done>
</task>

<task type="auto">
  <name>Task 2: sqllogictest suite on both builds + wasm_eh 2.0 build, artifact checks, and runtime harness</name>
  <files>.planning/quick/261006-ujg-test-anofox-forecast-against-duckdb-2-0-/evidence/ (test-summary-v155.txt, test-summary-v20.txt, test-failures-v155.txt, test-failures-v20.txt, test-2.0-only.md, wasm.txt), src/ (only if a 2.0-only failure is an extension-side API bug)</files>
  <precondition>Task 1 verify passed: build/release (2.0) and BASE build/release (v1.5.5) both exist and the tracer parity check printed TRACER PARITY OK.</precondition>
  <action>
Ordering: start the wasm_eh build FIRST in the background, then run both native test suites while it compiles. Test timing is not measured, so CPU sharing here is fine. Benchmarks (Task 3) must wait until everything in this task has finished.

WASM build (per coordinator requirement):
1. emsdk pin: grep the 2.0 ci-tools `.github/workflows/_extension_distribution.yml` (and the makefiles) for the `setup-emsdk` version, the EMSDK version, and any emscripten version. If it differs from 3.1.71, run `~/emsdk/emsdk install <ver>` and `~/emsdk/emsdk activate <ver>` and note it in EVID/wasm.txt. After the wasm build finishes, re-activate 3.1.71 so the user's global emsdk is restored.
2. binaryen: download `binaryen-version_123-x86_64-linux.tar.gz` and its `.sha256` asset from the official WebAssembly/binaryen GitHub release `version_123`. Verify the checksum BEFORE extracting, then extract to ~/tools so that ~/tools/binaryen-version_123/bin/wasm-opt exists. Abort the WASM part on a checksum mismatch.
3. Before building, confirm extension_config.cmake still passes LINKED_LIBS, and that the 2.0 duckdb/extension/extension_build_tools.cmake (or wherever `duckdb_extension_load` / `register_extension` now live) still accepts LINKED_LIBS and feeds `DUCKDB_EXTENSION_<NAME>_LINKED_LIBS` into the wasm post-build emcc link. Record the evidence lines in EVID/wasm.txt. If the mechanism was renamed, adapt extension_config.cmake minimally and log it in EVID/code-changes.md.
4. Re-read the 2.0 `wasm_eh` makefile recipe. Then, in the BACKGROUND from MAIN in a single shell: source ~/emsdk/emsdk_env.sh; export EM_BINARYEN_ROOT=~/tools/binaryen-version_123; prepend its bin to PATH; run `make wasm_eh` (WITHOUT GEN=ninja) to LOGS/build-wasm_eh-v20.log; append `EXIT=$?`. Poll for EXIT.
5. Confirm the log shows `Rust_CARGO_TARGET: wasm32-unknown-emscripten`. This proves the Task 1 CMake fix keeps wasm detection.
6. Fix wasm-only compile errors under the same rules as Task 1 Step C, then commit any src fix as `fix(duckdb2): wasm_eh build fixes for DuckDB 2.0`.

Native test suites. Read the test command from each tree's ci-tools `test_release_internal` recipe; for v1.5.5 it is `./build/release/test/unittest "test/*"`. In the BACKGROUND with DATAZOO_DISABLE_TELEMETRY=1, run the suite in BASE (log BASE/test-v155.log) and in MAIN (log LOGS/test-v20.log), each followed by an `EXIT=$?` line. Both use the same test files, since the branch does not modify test/. Poll for EXIT.

From each log, extract:
- the catch2 summary line (`test cases: ... passed ... failed`, plus assertions) into EVID/test-summary-<label>.txt;
- the sorted unique set of failing `test/sql/*.test` files into EVID/test-failures-<label>.txt.

Cross-check the failing-file count against the catch2 failed count. If they disagree, re-run suspect files individually (`unittest "test/sql/<file>"`) to settle it.

Compute 2.0-only failures as the set difference (v20 minus v155) and 2.0-fixed as (v155 minus v20). For each 2.0-only file, re-run it alone on the 2.0 build and classify it in EVID/test-2.0-only.md with the first failing statement and the expected vs actual excerpt:
- (a) extension-side bug under the 2.0 API: fix in src/, behavior-preserving, re-run the file plus the tracer, and commit `fix(duckdb2): <short>`;
- (b) intended DuckDB 2.0 behavior change (error text, type names, ordering, etc.): record only;
- (c) suspected upstream 2.0 bug: record with a minimal SQL repro.

Never edit test expectations to hide a 2.0 difference, because the branch must keep the v1.5.5 expectations comparable. Pre-existing failures (present in both builds) are listed by count only.

WASM artifact checks (once the wasm build EXIT is known), written to EVID/wasm.txt as `KEY=value` lines plus notes:
- `BUILD=ok|fail`, with the first error if it failed.
- `SIGNATURE=present|absent`: use a node one-liner with `WebAssembly.Module.customSections(new WebAssembly.Module(bytes), 'duckdb_signature')`. Fallback: list sections with ~/emsdk/upstream/bin/llvm-objdump -h.
- `FFI_IMPORTS=<n>`: count of `WebAssembly.Module.imports` entries whose name starts with `anofox_`. Expected 0. Any >0 means LINKED_LIBS did not survive.
- `SIZE_V20=<bytes>`.
- `SIZE_V155=<bytes or n/a + reason>`. Try `gh run download` of the newest successful main-branch MainDistributionPipeline run's artifact `anofox_forecast-v1.5.5-extension-wasm_eh` into the scratchpad. If it has expired, fall back to the content-length of the community-extensions v1.5.5 wasm_eh URL and label it as the community build.
- `RUNTIME=PASS|FAIL|BLOCKED`. Determine which @duckdb/duckdb-wasm build (if any) bundles the engine the 2.0 extension targets. Run `npm view @duckdb/duckdb-wasm dist-tags time --json`. For the `next` tag and any newer dev builds, `npm pack` into the scratchpad and inspect `package/dist/duckdb-eh.wasm` strings for the embedded version and storage-version list. Compare against the 2.0 CLI `library_version`/`source_id` from EVID/versions.txt and against the version string stamped into the extension metadata.
  - Only if an exact engine match exists: run `npm install --no-save @duckdb/duckdb-wasm@<ver>` in MAIN/test/wasm, then `node test/wasm/run.mjs --ext <artifact>` (curated subset), record per-file results vs the v1.5.5 curated expectation (8/8 green), then run `npm ci` in test/wasm to restore the pinned version. Do not commit a package.json bump unless a 2.0 engine build actually passed.
  - If no match exists: `RUNTIME=BLOCKED`, with the list of versions inspected and their embedded engine versions. Never fake a runtime result.

Keep log reads to tail and grep.
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast/.planning/quick/261006-ujg-test-anofox-forecast-against-duckdb-2-0-/evidence && grep -q "test cases" test-summary-v155.txt && grep -q "test cases" test-summary-v20.txt && test -f test-failures-v155.txt && test -f test-failures-v20.txt && test -f test-2.0-only.md && grep -Eq "^BUILD=(ok|fail)" wasm.txt && grep -Eq "^RUNTIME=(PASS|FAIL|BLOCKED)" wasm.txt && (grep -q "^BUILD=fail" wasm.txt || (grep -Eq "^SIGNATURE=(present|absent)" wasm.txt && grep -Eq "^FFI_IMPORTS=[0-9]+" wasm.txt)) && test -z "$(pgrep -f 'unittest|emmake|ninja' || true)"</automated>
  </verify>
  <done>Both native suites ran to completion with recorded counts. 2.0-only failures are each classified (a/b/c), and extension-side ones are fixed and committed. The wasm_eh 2.0 build result, signature, FFI-import, size, and runtime (PASS/FAIL/BLOCKED with evidence) are recorded in EVID/wasm.txt. test/wasm is restored to its pinned duckdb-wasm, emsdk is re-activated to 3.1.71 if it was changed, and no build or test processes are still running.</done>
</task>

<task type="auto">
  <name>Task 3: Benchmarks on both builds (M4 Daily 6 families, 10k synthetic, SQL micro-bench), sequential, plus 261006-ujg-BENCHMARK.md</name>
  <files>benchmark/src/common/anofox_runner.py, benchmark/duckdb2_compare.py, benchmark/sql/duckdb2_compare.sql, .planning/quick/261006-ujg-test-anofox-forecast-against-duckdb-2-0-/261006-ujg-BENCHMARK.md, .planning/quick/261006-ujg-test-anofox-forecast-against-duckdb-2-0-/evidence/</files>
  <precondition>No build, test, or other benchmark process is running on the machine for either tree (pgrep for ninja, cargo, emmake, unittest, duckdb CLI is empty) so timings are free of CPU contention.</precondition>
  <action>
Runner toggle in benchmark/src/common/anofox_runner.py (minimal, default behavior unchanged):
- Set `use_cli_subprocess` true when function_name is TS_FORECAST_PANEL_BY OR env `ANOFOX_USE_CLI` equals "1". In that mode, do not open an in-process connection.
- Make the panel-specific error messages generic, since they now apply to both modes.
- In the per-series branch, when `use_cli_subprocess` is true, send the existing TS_FORECAST_BY query through `_run_panel_query_via_cli` instead of `con.execute`.
- Replace the hardcoded subprocess timeout 600 with `int(os.environ.get('ANOFOX_CLI_TIMEOUT', '600'))`.
- Note in a docstring that measured time then includes a constant parquet round-trip and CLI start overhead, identical for both builds.

SQL micro-benchmark, new file benchmark/sql/duckdb2_compare.sql:
- Header comment: purpose; run from MAIN root; the extension is supplied by the caller via the CLI flag -cmd with a LOAD statement, never hardcoded; telemetry env off; the driver runs the file 3x per CLI and takes the per-label median.
- Untimed setup with `.timer off`: `SET preserve_insertion_order = false`. Create table m4s from the first N lexicographic unique_ids of benchmark/data/m4_daily_train_long.parquet (start with N=500). Create m4gap as m4s minus rows whose day-of-month is 15 (deterministic gaps).
- Then `.timer on`. Each timed statement is preceded by a `.print BENCH <label>` line. Labels and queries:
  - forecast_naive, forecast_autoets, forecast_theta, forecast_mfles: count plus rounded avg of yhat over ts_forecast_by on m4s with horizon 14 and '1d'.
  - stats_by: count over ts_stats_by on m4s with '1d'.
  - features_by: count over ts_features_by on m4s, using the arity that works on the baseline.
  - cv_folds: CREATE OR REPLACE TEMP TABLE folds AS ts_cv_folds_by on m4s with 3 folds and horizon 14.
  - cv_forecast: count plus rounded avg of the forecast column over ts_cv_forecast_by('folds', ..., 'AutoETS'). Never use the ensemble model that segfaults.
  - fill_gaps: count over ts_fill_gaps_by on m4gap with '1d'.
- Sizing: run the file once on the BASELINE CLI. Every statement must succeed and each timed statement should take roughly 1-60 s. Adjust N (and drop a model only if it is unworkable, recording why) BEFORE any measurement. Once measuring starts, the file is frozen and identical for both builds.

Driver, extend benchmark/duckdb2_compare.py with these subcommands (all subprocesses get DATAZOO_DISABLE_TELEMETRY=1; the CLI is derived from the ext path; fail loudly on missing data and never silently drop a label or model):
- `m4 --label <v155|v20> --ext <path> --families baseline,ets,theta,arima,mfles,mstl`: sets ANOFOX_USE_CLI=1 and ANOFOX_CLI_TIMEOUT=7200. For each family, imports `configs.<family>`, loads train and test via get_data('m4', 'Daily'), and applies MAX_SERIES exactly as `create_benchmark_functions.anofox` does. Calls run_anofox_benchmark with output_dir=benchmark/results/duckdb2_compare/<label>/<family>, extension_path=<ext>, and function_name=getattr(cfg, 'FUNCTION_NAME', 'TS_FORECAST_BY'). Then calls evaluate_forecasts on the same dir.
- `sqlbench --label <name> --ext <path> --reps 3`: runs the CLI with `-unsigned -cmd "LOAD '<ext>'"`, stdin = the SQL file, cwd = MAIN, `--reps` times. Parses `BENCH <label>` followed by the next `Run Time (s): real <x>` line. Errors if any label has no time, in case 2.0 changed the timer output format. Captures the printed checksums and writes per-rep JSON under benchmark/results/duckdb2_compare/sqlbench/<name>.json.
- `synth10k --label <name> --root <tree root>`: runs `<root>/build/release/duckdb -unsigned -cmd ".timer on"` with stdin = MAIN/benchmark/sql/10k_series_synthetic_test.sql and cwd = root (so the relative LOAD resolves to that tree's build). Records process wall time and the sum of `Run Time (s): real` values to JSON. Records timing only, because the data is random.
- `compare`: writes three markdown fragments to EVID:
  - m4-compare.md: rows family | model | v1.5.5 s | 2.0 s | delta % | MASE/MAE/RMSE identical | max abs yhat diff | rows differing. Forecasts are joined on the key columns. Any non-identical value is flagged FINDING. Exit non-zero if any of the 19 expected models is missing for either label.
  - sqlbench-compare.md: label | median v1.5.5 | median 2.0 | delta % | baseline drift % (run 1 vs run 2) | checksum match.
  - synth10k-compare.md: label | wall s | summed statement s | delta %.
  - Delta % is (t_2.0 minus t_v1.5.5) / t_v1.5.5 x 100.

Run order, strictly SEQUENTIAL, each step in the background with a log in LOGS and an EXIT line, polled. No two benchmark steps may overlap:
1. sqlbench v155a (BASE_EXT)
2. sqlbench v20 (NEW_EXT)
3. sqlbench v155b (BASE_EXT; drift check)
4. synth10k v155 (root BASE)
5. synth10k v20 (root MAIN)
6. m4 v155 (BASE_EXT)
7. m4 v20 (NEW_EXT)
8. compare

A single model failing on one build is a finding: record it and keep going.

Dual-compile check (only if Task 1 or Task 2 committed src/ changes; run after ALL measurements): in BASE, check out the branch versions of exactly the changed src files (`git -C BASE checkout test/duckdb-2.0 -- <files>`), then run an incremental `GEN=ninja make release` in the background with a log. Record whether the 2.0-adapted source compiles against v1.5.5 and whether its tracer-style forecast still LOADs and runs. Then restore those files to 7e23980 with `git -C BASE checkout 7e23980 -- <files>`, leaving the orchestrator's uncommitted CMakeLists patch untouched.

Report: write QDIR/261006-ujg-BENCHMARK.md with these sections, every number traceable to an EVID file or a named log:
- `## Environment`: CPU/cores/RAM, gcc, rustc, node, emsdk, and both engine library_version/source_id plus submodule SHAs from EVID/versions.txt.
- `## Build`: the CMake wasm-target fix (pre-existing bug, applies to both builds, should go to main as its own PR).
- `## Code Changes for DuckDB 2.0`: from EVID/code-changes.md, with a "compiles on v1.5.5: yes/no/guarded" column from the dual-compile check, and any required upstream changes in posthog-telemetry or datazoo-banner.
- `## Test Suite`: counts both builds, the 2.0-only list with categories, 2.0-fixed files, and the pre-existing failure count.
- `## WASM`: from EVID/wasm.txt.
- `## M4 Daily`: m4-compare.md table, noting that harness time includes constant CLI/parquet overhead.
- `## 10k Synthetic`.
- `## SQL Micro-benchmark`.
- `## Findings`: regressions or improvements beyond baseline drift, accuracy differences, blocked items, and a recommendation.

Commit: stage `benchmark/src/common/anofox_runner.py benchmark/duckdb2_compare.py benchmark/sql/duckdb2_compare.sql` and commit `test(bench): DuckDB 2.0 vs v1.5.5 benchmark harness` (plus trailer). Stage the report and the evidence folder and commit `docs(quick-261006-ujg): DuckDB 2.0 test + benchmark report` (plus trailer). Never stage raw parquet, logs, or the FFI header. Verify each commit hash with `git log --oneline -n 8` before quoting it.
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast && R=.planning/quick/261006-ujg-test-anofox-forecast-against-duckdb-2-0-/261006-ujg-BENCHMARK.md && for h in "Environment" "Build" "Code Changes for DuckDB 2.0" "Test Suite" "WASM" "M4 Daily" "10k Synthetic" "SQL Micro-benchmark" "Findings"; do grep -q "^## $h" "$R" || { echo "missing section $h"; exit 1; }; done && cd benchmark && uv run python duckdb2_compare.py compare && CHANGED="$(git diff --name-only main..HEAD)" && test "$(printf '%s\n' "$CHANGED" | grep -c 'anofox_fcst_ffi.h')" = "0" && LOG="$(git log --oneline main..HEAD)" && printf '%s\n' "$LOG" | grep -q "DuckDB 2.0 test + benchmark report"</automated>
  </verify>
  <done>All 8 benchmark steps ran sequentially on an otherwise idle machine. `compare` exits 0 with all 19 M4 models present for both labels. Accuracy identity is stated per model, and any difference is flagged FINDING. The report has every required section, with numbers traceable to evidence files. The dual-compile result is recorded (or marked not applicable). The harness and report are committed without the FFI header, logs, or parquet files. BASE is restored to 7e23980 plus the orchestrator's CMake patch.</done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| GitHub (duckdb, extension-ci-tools unreleased branches) -> local build | Unreleased upstream source is compiled and executed locally |
| GitHub release (WebAssembly/binaryen) -> ~/tools | Prebuilt binary placed on PATH for the wasm build |
| npm registry (@duckdb/duckdb-wasm dev builds), PyPI via uv.lock -> local | Third-party packages executed by the harnesses |
| Extension on LOAD -> PostHog | Telemetry egress from dev builds |
| Executor -> git history / user working tree | Commits on a shared branch next to pre-existing uncommitted user work |

## STRIDE Threat Register

| Threat ID | Category | Component | Severity | Disposition | Mitigation Plan |
|-----------|----------|-----------|----------|-------------|-----------------|
| T-ujg-01 | Tampering | duckdb / extension-ci-tools submodule fetch | medium | mitigate | Fetch only from the official github.com/duckdb URLs already in .gitmodules. Pin and record the exact SHAs in EVID/versions.txt. Never patch files inside these submodules. |
| T-ujg-02 | Tampering | binaryen version_123 tarball | high | mitigate | Download only from the official WebAssembly/binaryen release. Verify against the published .sha256 asset before extracting, and abort the WASM part on mismatch. Install under ~/tools only, with PATH prepended only in the wasm build shell. |
| T-ujg-SC | Tampering | uv sync / npm installs | high | mitigate | No new package names are introduced. `uv sync` restores from the committed uv.lock, and test/wasm uses `npm ci` from the committed lockfile. Alternate @duckdb/duckdb-wasm versions (official @duckdb scope, already a pinned dependency) are inspected via `npm pack` in the scratchpad or installed with `--no-save`, then restored with `npm ci`. No package.json bump is committed unless a 2.0 engine build passes. |
| T-ujg-03 | Information Disclosure | PostHog telemetry on extension LOAD | medium | mitigate | Export DATAZOO_DISABLE_TELEMETRY=1 for every CLI, unittest, and benchmark subprocess on both builds (the driver forces it), so dev-build loads do not emit telemetry and timings exclude network latency. |
| T-ujg-04 | Tampering | User's uncommitted src/include/anofox_fcst_ffi.h and untracked dirs | high | mitigate | Stage explicit paths only and check `git diff --cached --name-only` before each commit. Automated verify asserts that the FFI header is absent from main..HEAD. |
| T-ujg-05 | Repudiation | Report numbers and commit hashes | medium | mitigate | Every report number must come from an EVID file or a named log. Commit hashes are confirmed with `git log` before being quoted (a known executor fabrication risk). |
| T-ujg-06 | Denial of Service | Orchestrator's baseline build / BASE worktree | medium | mitigate | Never kill, restart, or rebuild the baseline build. Only wait on its EXIT line. The dual-compile check runs after all measurements and restores the touched src files to 7e23980. |
| T-ujg-07 | Elevation of Privilege | Global emsdk activation change | low | mitigate | If the 2.0 ci-tools pin a different emsdk, activate it only for the build and re-activate 3.1.71 afterwards. Record the change in EVID/wasm.txt. |
</threat_model>

<verification>
- Task verifies pass in order (tracer parity, then suites and WASM evidence, then report and compare).
- `git log --oneline main..test/duckdb-2.0` shows: the CMake fix, the submodule bump, optional duckdb2 src fixes, the tracer driver, the benchmark harness, and the report commit. No commit touches the FFI header.
- `git -C duckdb rev-parse HEAD` and `git -C extension-ci-tools rev-parse HEAD` match EVID/versions.txt.
- The report states, for each M4 model, whether accuracy is identical. Any difference is listed under Findings.
- WASM runtime is reported as PASS, FAIL, or BLOCKED, with the inspected duckdb-wasm versions as evidence.
</verification>

<success_criteria>
- Native anofox_forecast builds and runs on DuckDB v2.0-cyanoptera (or a BLOCKED report precisely documents why not).
- The test-suite delta between v1.5.5 and 2.0 is known file-by-file with root-cause categories.
- The wasm_eh 2.0 build status and artifact integrity are known, and the runtime status is honest.
- Performance deltas for M4 Daily (19 models), 10k synthetic, and the SQL micro-benchmark are tabulated with baseline drift for context.
- The full list of code changes needed for 2.0 is documented, including whether each still compiles on v1.5.5.
</success_criteria>

<output>
Write `.planning/quick/261006-ujg-test-anofox-forecast-against-duckdb-2-0-/261006-ujg-SUMMARY.md` when done (per execute-plan workflow). The primary deliverable is `261006-ujg-BENCHMARK.md` in the same directory.
</output>
