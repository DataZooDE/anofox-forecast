---
phase: quick-261016-tjw
plan: 01
subsystem: infra
tags: [duckdb-2.0, compat, cpp, cmake, wasm, rust-toolchain, ci, sqllogictest]

requires:
  - phase: v0.10.0 / v2026.10.16 release (main 30c86e9)
    provides: v1.5.6-green codebase + HANDOVER-duckdb2-compat.md
provides:
  - src/include/duckdb_compat.hpp (shape-detected 1.4/1.5/2.0 compat layer)
  - every src/ DuckDB API call site routed through the compat helpers
  - WASM-only Rust 1.86.0 pin (CMake, Corrosion Rust_TOOLCHAIN)
  - non-blocking duckdb-next-build CI job (2.0-dev b15358c, linux_amd64 + wasm_eh)
  - keyword lambda syntax in tests/examples/docs
  - 2.0-only static link of the extension into DuckDB's own test binaries
affects: [release v2026.10.x, community-extensions PR #3112, anofox-statistics compat parity]

actuals:
  tokens: 122550        # chars/4 over the realized diff (git diff 30c86e9..HEAD = 490201 chars)
  tasks: 3
  commits: 7            # MEASURED: git rev-list --count 30c86e9..HEAD
plan_head_before: 30c86e90b84507ad60a8eb562e96cae8db9892d4

tech-stack:
  added: []
  patterns:
    - "Detect DuckDB API by shape (SFINAE / callback-typedef arity / `if(COMMAND ...)` in CMake), never by version macro"
    - "Output chunk cardinality only via SetOutputCardinality (SetChildCardinality where it exists)"
    - "Every registered ScalarFunction is marked fallible (Fallible()/MarkFallible, no-op on 1.x)"

key-files:
  created:
    - src/include/duckdb_compat.hpp
  modified:
    - src/**/*.cpp (44 TUs), src/include/anofox_forecast_extension.hpp, src/include/ts_list_marshal.hpp, src/include/ts_forecast_params.hpp
    - CMakeLists.txt
    - extension_config.cmake
    - .github/workflows/MainDistributionPipeline.yml
    - test/sql (8 files), examples/data_preparation/null_handling_examples.sql, docs/api/05-period-detection.md, docs/api/05a-decomposition.md

key-decisions:
  - "Blanket FlatVector::GetData -> FlatDataMut (424 sites, identical on 1.x); input vectors are read through the unified format"
  - "All registered scalar functions are marked fallible, not just the known throwers (no-op on 1.x; 2.0 only loses dictionary caching / pushdown on them)"
  - "2.0 error-wording drift is handled with a <REGEX>: alternation that matches both the 1.x and the 2.0 message, not a weaker common substring"
  - "extension_config.cmake requests duckdb_extension_statically_link(anofox_forecast) when that command exists, so the 2.0 CI sqllogictest actually runs (it skipped 94/94 before)"

requirements-completed: [QUICK-261016-tjw]

duration: ~2h30m
completed: 2026-10-16
status: complete
---

# Quick 261016-tjw: Port anofox-statistics' DuckDB 2.0 compat layer — Summary

**anofox_forecast now builds and passes the full test suite (5591 assertions / 94 files) on DuckDB 2.0-dev b15358c, v1.5.6 and (in CI) v1.4.5 LTS. It does this through one shape-detected `duckdb_compat.hpp`, a WASM-only Rust 1.86 pin and a non-blocking duckdb-next CI job whose sqllogictest now actually runs. Draft PR #277 is green on every leg.**

## Commits (branch `chore/duckdb2-compat`, pushed)

| # | Hash | Message | Task |
|---|------|---------|------|
| 1 | `29b8b85` | feat(compat): add shape-detected duckdb_compat.hpp for DuckDB 1.4/1.5/2.0 | T1 |
| 2 | `dcbb8a9` | refactor(compat): route src/ DuckDB API calls through duckdb_compat helpers | T1 |
| 3 | `3b61bf1` | fix(duckdb2): accept the 2.0 error for bare table-name metrics macro calls | T2 |
| 4 | `5f21323` | test: use keyword lambda syntax in list lambdas | T3 |
| 5 | `33a8fff` | ci: add non-blocking DuckDB 2.0-dev build job | T3 |
| 6 | `b9a82bc` | fix(wasm): pin Rust 1.86 for WASM builds only | T3 |
| 7 | `720c7c7` | fix(duckdb2): statically link the extension into the 2.0 test binaries | T3 (deviation) |

Draft PR: https://github.com/DataZooDE/anofox-forecast/pull/277. It is not merged, there is no release, and community-extensions is untouched.

## duckdb_compat.hpp: helper list and the path each one takes on 1.x

The file has 560 lines. The anofox-statistics base is included verbatim:
- `DuckName`/`AsDuckName`/`MakeChild`/`NameString` use the `std::string` key.
- `ReturnTypeOf`/`SetReturnTypeOf`/`ArgumentsOf`/`SetArgumentTypeOf`/`SetNullHandlingOf`/`SetOrderDependentOf`/`NameOf`/`SetFunctionCallbackOf`/`BindInfoOf` use the public fields.
- `MakeParser` calls `Parser()`. `SetCreateInfoName` uses the `schema`/`name` fields.
- `FlatDataMut`/`FlatValidityMut` call `FlatVector::GetData`/`Validity`. `StructEntry` dereferences the `unique_ptr`.
- `ANOFOX_AGG_INIT/FINALIZE/BIND` and `ANOFOX_SCALAR_BIND` are identity adapters. `MarkFallible` is a no-op, guarded by the static_assert.

Forecast extras, all shape-detected:

| Helper | 2.0 path | 1.5.6 path | 1.4.5 path |
|---|---|---|---|
| `SetOutputCardinality(chunk, n)` | `SetChildCardinality` (resizes child vectors) | `SetChildCardinality` (documented forwarder to `SetCardinality`) | `SetCardinality` |
| `ToUnified(v, count, fmt)` | `ToUnifiedFormat(fmt)` | `ToUnifiedFormat(count, fmt)` | same as 1.5.6 |
| `FlattenVector(v, count)` | `Flatten()` | `Flatten(count)` | same as 1.5.6 |
| `ListChild` / `ListChildMut` | `ListVector::GetChild` / `GetChildMutable` | `ListVector::GetEntry` | same as 1.5.6 |
| `SetStabilityOf(fn, s)` | `SetStability` | `SetStability` | `fn.stability = s` |
| `TableBindNames` / `TableBindName` / `MakeBindName` | `vector<Identifier>`, taken from `table_function_bind_t` arg 3 | `vector<string>` | same as 1.5.6 |
| `AddNamedParameter` + `kNamedParamsHaveNullDefault` | `GetSignature().AddKeywordOnly(name, type, Value(type))`, trait true | `named_parameters[name] = type`, trait false | same as 1.5.6 |
| `ParseExpressionList(str)` | `MakeParser().ParseExpressionList` | static `Parser::ParseExpressionList` | same as 1.5.6 |
| `QueryMaterialized(ctx, q)` | `Query(q, nullptr)` (default params: blocking, retained) | `Query(q, false)` | same as 1.5.6 |
| `Fallible(ScalarFunction)` | `MarkFallible` + return | no-op | no-op |

The repo has no DuckDB version macro anywhere in src/: the grep returns 0. The only header test is the statistics `__has_include` of the split vector headers.

## Call-site counts (live grep, src/)

Before → after; the "after" count is the helper occurrences on `+` lines of the T1 diff:
- `.SetCardinality(` 60 → 0 (`SetOutputCardinality` 60)
- `FlatVector::GetData` 429 → 0 code sites (`FlatDataMut` 424, plus 4 input reads moved to `UnifiedVectorFormat`; 1 comment left)
- `FlatVector::Validity` 6 → 0 (`FlatValidityMut` 6)
- `StructVector::GetEntries` indexing → `StructEntry` 355
- `.ToUnifiedFormat(` 95 → 0 (`ToUnified` 100)
- `.Flatten(count)` 15 → `FlattenVector` 15
- `ListVector::GetEntry` 64 → `ListChildMut` 64
- `output.SetValue(c,r,v)` 40 → `output.data[c].SetValue(r,v)` 40
- `.stability =` 46 → `SetStabilityOf` 46
- `null_handling =` 30 → `SetNullHandlingOf` 30
- `.return_type =` 7 → `SetReturnTypeOf` 7. `->return_type` reads → `ReturnTypeOf` 5
- `bind_info` 4 → `BindInfoOf` 4
- Aggregate finalizers → `ANOFOX_AGG_FINALIZE` 10. Aggregate binds → `ANOFOX_AGG_BIND` 3. Scalar binds → `ANOFOX_SCALAR_BIND` 7
- Table-bind `vector<string> &names` 25 → `TableBindNames &names` 25. Name pushes → `MakeBindName` 131. `input_table_names` / STRUCT child names → `NameString` 102
- Runtime STRUCT child names → `MakeChild` 5. `named_parameters[` 3 → `AddNamedParameter` 3. `CreateInfo` → `SetCreateInfoName` 2
- Scalars marked fallible: 154 by-value `Fallible(ScalarFunction(...))` plus 35 named `MarkFallible(x)` (19 files)

## Verification evidence

**v1.5.6** (`build/release`):
- Baseline clean-HOME suite: 5591 assertions / 94 files, all passed (`build/logs/tjw-baseline-v156.log`).
- After T1: 5591/94 (`build/logs/tjw-t1-v156.log`).
- Final: 5591/94 (`build/logs/tjw-final-v156.log`).

**Example oracle:** 25 examples were run single-threaded with `setseed(0.42)`. Output before and after the rewrite is identical (`diff -r build/tjw-examples-before build/tjw-examples-after` is empty). Final run: the only difference is that the arrow-lambda WARNING block in `null_handling_examples.sql` is gone, as expected.

**Syntax-only checks** (`build/tjw-syntax-check.sh`, 46 TUs, header roots swapped):

| Header root | Errors | src/ deprecation warnings |
|---|---|---|
| v1.4.5 | 0 | 0 |
| v1.5.6 | 0 | 0 |
| 2.0-dev b15358c | 0 | 0 |

**2.0-dev b15358c local build** (`build/release-dd2`, plain cmake configure, json linked natively):
- Build logs: `build/logs/tjw-dd2-build.log`, `build/logs/tjw-dd2-build2.log`. Both have 0 src/ deprecation warnings.
- Suite: 5591 assertions / 94 files, all passed (`build/logs/tjw-dd2-suite.log`).
- The CI runner `scripts/ci/run_tests.py` gives 94 passed, 0 skipped (`build/logs/tjw-dd2-runtests.log`).
- No InternalException and no "not marked as fallible" errors in the log.

**Row-count probe** (`build/tjw-rowcount-probe.sh`, `build/logs/tjw-rowcount-probe.log`): 1000 series × 60 days, `threads=20`, 20 runs per engine. Each query returned one count, identical across all 20 runs and across v1.5.6 and 2.0:

| Query | Rows |
|---|---|
| forecast_naive | 12000 |
| cv_folds | 162000 |
| cv_split | 102000 |
| cv_forecast | 18000 |
| fill_gaps | 59600 |
| fill_forward | 63200 |
| metrics_mae | 1000 |
| metrics_rmse | 1000 |
| features | 1000 |
| stats_by | 1000 |
| mstl | 1000 |
| changepoints | 60000 |
| aggregate_hierarchy | 62040 |
| combine_keys | 60000 |
| split_keys | 60000 |

AutoETS on the first 20 series: 240 rows, byte-identical CSV on both engines. The probe prints `PROBE OK`.

**WASM** (`build/logs/tjw-wasm_eh.log`):
- `cargo +1.86.0 build --release --ignore-rust-version --target wasm32-unknown-emscripten -p anofox-fcst-ffi` exits 0 (`build/logs/tjw-cargo186-wasm.log`).
- `make wasm_eh` cold build: default toolchain stable 1.98, emsdk 3.1.71 bundled binaryen, no `EM_BINARYEN_ROOT`. It exits 0. Configure prints `-- WASM build: pinned Rust toolchain 1.86.0` and `-- Rust Toolchain: 1.86.0`. All 160 rustc invocations are 1.86.0.
- Artifact `build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm` (5.7 MB): custom sections `dylink.0` and `duckdb_signature`. 2265 imports, 0 named `anofox_*`.
- Native reconfigure: `Rust_TOOLCHAIN` in `build/release/CMakeCache.txt` is `1.89.0-x86_64-unknown-linux-gnu`, not 1.86.

**DoD gates:**
- `cargo fmt --all -- --check` passes. `cargo clippy --all-targets --all-features -- -D warnings` passes. `cargo test --workspace --locked`: 772 passed, 0 failed.
- `make audit`: 34 hits, 34 allow-listed, 0 violations, 0 stale entries.
- `make check-docs`: 344 PASS, 0 FAIL.
- `check_correctness_inventory.py` passes.
- `git diff --quiet origin/main -- duckdb extension-ci-tools Cargo.lock` is true: the pins and the lockfile are unchanged.

## CI legs (draft PR #277)

**Run 38963163930** (head b9a82bc) was all green, but the duckdb-next `linux_amd64` sqllogictest skipped all 94 files: `require anofox_forecast: 94`. anofox-statistics' duckdb-next job shows the same thing (230/230 skipped). This led to deviation 1.

**Run 38969012711** (head 720c7c7) concluded **success**:

| Line | Leg | Result |
|---|---|---|
| v1.4.5 LTS | Linux amd64 / arm64 / amd64_musl | success |
| v1.4.5 LTS | wasm mvp / eh / threads | success |
| v1.4.5 LTS | smoke tests (linux_amd64, osx_arm64, windows_amd64) | success |
| v1.4.5 LTS | macOS, Windows builds | skipped (excluded on LTS, pre-existing) |
| v1.5.6 | Linux amd64 / arm64 | success |
| v1.5.6 | macOS amd64 / arm64 | success |
| v1.5.6 | Windows amd64 | success |
| v1.5.6 | wasm mvp / eh / threads | success |
| v1.5.6 | smoke tests ×3 | success |
| v1.5.6 | WASM runtime test (Node harness) | success |
| — | Rust Tests | success |
| — | Rust MSRV 1.89.0 | success |
| duckdb-next | `linux_amd64` | success, "ran tests: 94 passed, 0 skipped"; configure shows "Linked into DuckDB: extensions [anofox_forecast, ...]" |
| duckdb-next | `wasm_eh` | success; ci-tools default rustc 1.97.1, and the build ran on `-- Rust Toolchain: 1.86.0` |

The static-audits workflow was green on every push and on the PR.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing functionality] The 2.0 CI leg ran no tests**
- **Found during:** T3 step 7 (CI polling).
- **Issue:** DuckDB 2.0 removed default linking: extensions are only linked when a config calls `duckdb_extension_statically_link()`. The duckdb-next `linux_amd64` sqllogictest therefore skipped 94/94 test files.
- **Fix:** `extension_config.cmake` now calls `if(COMMAND duckdb_extension_statically_link) duckdb_extension_statically_link(anofox_forecast) endif()`.
  - This is a no-op on 1.x, where the command doesn't exist and every loaded extension is linked already.
  - It does not change the loadable artifact.
- **Verification:**
  - Local 2.0 reconfigure shows the extension linked, `run_tests.py` gives 94 passed / 0 skipped, and the suite is 5591/94.
  - v1.5.6 rebuild is still 5591/94.
  - CI 2.0 leg: 94 passed.
- **Commit:** `720c7c7`. The plan allowed `extension_config.cmake` edits only for json; this goes beyond that, but the edit is gated on the command existing.

**2. [Rule 1 - Bug / 2.0 runtime] Some inputs were read as flat vectors**
- **Issue:** `ts_estimate_backtest_memory` (metrics.cpp) and `ts_features_config_from_json` (ts_features.cpp) read their input args as flat vectors. 2.0 throws InternalException on dictionary inputs, and on 1.x reading a constant input with count > 1 was out of bounds.
- **Fix:** both now read inputs through `UnifiedVectorFormat`. This is included in `dcbb8a9`.

**3. [Rule 3 - Blocking] 2.0 function sets are read-only**
- **Issue:** On 2.0, `ScalarFunctionSet::functions` holds `shared_ptr<const ScalarFunction>`. `ts_forecast_ensemble_native` set null handling by looping over the set after the functions were added.
- **Fix:** the null handling (and fallible marking) is now set on each overload before `AddFunction`. In `dcbb8a9`.

**4. [Rule 3] Plan's lambda regex missed some lambdas**
- **Issue:** The plan's regex `-> ?[a-zA-Z(]` missed lambdas whose body starts with a digit (`i -> 1.0`, `t -> 0.5 * t ...`, 28 lines). The first 2.0 suite run caught them (2 failures).
- **Fix:** All arrow lambdas are now converted: 75 lines in 10 files. The broader check `(\w+|\(..\))\s*->\s*[^>]` on non-comment lines finds 0.

**5. [Test expectation, 2.0 error-wording drift] `test/sql/correctness/metrics.test` (3 statements)**
- **Change:** The expectation `Binder Error` is now `<REGEX>:.*(Binder Error|Column 'ds' not found).*`.
- **Reason:** 1.x rejects a VARCHAR passed to a `TABLE` parameter at bind. 2.0 binds it, and our metrics bind then reports the missing column.
- **Compatibility:** sqllogictest `<REGEX>:` exists on 1.4.5 (`result_helper.cpp:318`). This is the only test expectation that changed.
- **Commit:** `3b61bf1`.

**6. Example oracle had to be made deterministic**
- **Issue:** The synthetic examples use `RANDOM()` and unordered output, so two runs of the same binary differed.
- **Fix:** `build/tjw-run-examples.sh` now prepends `SET threads=1; SELECT setseed(0.42);`.
- **Exclusions:** `m5_*` examples are excluded because they download the full M5 dataset over the network and were OOM-killed (exit 137).
- **How the baseline was taken:**
  1. Copied the WIP `src/` to scratch.
  2. Ran `git checkout HEAD -- src/`.
  3. Rebuilt and captured the baseline output.
  4. Restored the WIP and touched the files to force a rebuild. ninja had skipped the mtime-preserved restore on the first try, and that was caught.

**7. Local `make wasm_eh` was run without `GEN=ninja`**
- **Reason:** The ci-tools recipe runs `emmake make -C build/wasm_eh`, which does not work with a Ninja generator.

## Deferred Issues (out of scope, pre-existing)

- `examples/backtesting/synthetic_backtest_examples.sql` has about 20 pre-existing stale-API errors, e.g. `ts_cv_split_by` signature and `group_id`. The same errors appear before and after this change.
- **SQL compat warning:** 2.0 deprecates implicit string→identifier conversion in table-function args. Our macros pass quoted strings, so the tests were unaffected, but user SQL using unquoted table names will warn on 2.0.
- **Disk:**
  - `/home` dropped to 29 GB free by the end of the session.
  - The repo's `target/` is about 60 GB: a cargo cache, partly from the DoD `cargo clippy`/`cargo test` runs. Other sessions' builds share the disk.
  - `build/tjw-cargo-wasm186` was deleted.

## Worktrees and build dirs left in place

- `/home/simonm/projects/duckdb/duckdb-2.0-b15358c`: detached at b15358c, a git worktree of the duckdb submodule (281 MB).
- `/home/simonm/projects/duckdb/duckdb-1.4.5`: detached at v1.4.5 (277 MB).
- `build/release-dd2`: DuckDB 2.0-dev build with the extension statically linked (2.7 GB).
- `build/wasm_eh`: fresh 1.86-pinned build.
- **Scripts** (throwaway, gitignored `build/`): `build/tjw-syntax-check.sh`, `build/tjw-run-examples.sh`, `build/tjw-rowcount-probe.sh`.
- **Logs:** `build/logs/tjw-*.log`.

Submodule pointers (duckdb, extension-ci-tools, posthog-telemetry, datazoo-banner) are unchanged.

## Known Stubs

None.

## Threat Flags

None beyond the plan's register. T-tjw-05 (rustup install in the WASM configure) is implemented as planned: exact version, `rustup toolchain install` only, WASM only, and it can be disabled with `-DANOFOX_WASM_RUST_TOOLCHAIN=`.

## Self-Check: PASSED

- FOUND: src/include/duckdb_compat.hpp (560 lines; contains SetOutputCardinality, AddNamedParameter, kNamedParamsHaveNullDefault)
- FOUND: CMakeLists.txt contains ANOFOX_WASM_RUST_TOOLCHAIN and Rust_TOOLCHAIN
- FOUND: .github/workflows/MainDistributionPipeline.yml contains duckdb-next-build
- FOUND commits: 29b8b85, dcbb8a9, 3b61bf1, 5f21323, 33a8fff, b9a82bc, 720c7c7 (all on origin/chore/duckdb2-compat)
