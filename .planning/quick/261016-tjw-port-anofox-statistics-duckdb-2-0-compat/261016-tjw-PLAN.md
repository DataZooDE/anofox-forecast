---
phase: quick-261016-tjw
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - src/include/duckdb_compat.hpp
  - src/include/anofox_forecast_extension.hpp
  - src/include/*.hpp
  - src/anofox_forecast_extension.cpp
  - src/aggregate_functions/*.cpp
  - src/scalar_functions/*.cpp
  - src/table_functions/*.cpp
  - src/macros/ts_macros.cpp
  - scripts/audit_allowlist.txt
  - CMakeLists.txt
  - .github/workflows/MainDistributionPipeline.yml
  - test/sql/**/*.test
  - examples/**/*.sql
  - docs/**/*.md
  - plugins/anofox-forecast/skills/**/SKILL.md
autonomous: true
requirements: [QUICK-261016-tjw]

estimate:
  tokens: 320000
  raw_tokens: 320000
  tasks: 3
  confidence: low

must_haves:
  truths:
    - "The extension builds against DuckDB v1.5.6 and the clean-HOME suite passes unchanged: 5591 assertions in 94 test cases, 0 failures"
    - "Example-script output on v1.5.6 is identical before and after the call-site rewrite (behaviour is unchanged on 1.x)"
    - "Every extension source file compiles (syntax-only) against the DuckDB v1.4.5 headers and against the DuckDB 2.0-dev b15358c headers"
    - "A local DuckDB 2.0-dev (b15358c) build of the extension passes the full clean-HOME suite (94 cases, 0 failures)"
    - "On 2.0-dev, filtered COUNT(*) over the in-out *_native functions returns the same row count on every one of 20 repeated runs and matches v1.5.6 (no SetCardinality row loss)"
    - "Named table-function parameters stay optional on 2.0-dev, and throwing scalar functions surface user errors, not InternalException"
    - "A wasm_eh build uses Rust 1.86.0 even when the default rustup toolchain is newer, and links with the emsdk-bundled binaryen; native builds keep the 1.89 floor"
    - "CI has a non-blocking duckdb-next-build job (DuckDB b15358c, ci-tools v2.0-cyanoptera, linux_amd64 + wasm_eh)"
    - "test/sql, examples, docs and skills use keyword list lambdas only (arrow form gone)"
  artifacts:
    - path: "src/include/duckdb_compat.hpp"
      provides: "Shape-detected (SFINAE) helpers for 1.4 / 1.5 / 2.0 API differences; forwards to the old API on 1.x"
      min_lines: 380
    - path: "CMakeLists.txt"
      provides: "WASM-only Rust 1.86.0 toolchain pin via Corrosion Rust_TOOLCHAIN"
      contains: "ANOFOX_WASM_RUST_TOOLCHAIN"
    - path: ".github/workflows/MainDistributionPipeline.yml"
      provides: "duckdb-next-build job"
      contains: "duckdb-next-build"
  key_links:
    - from: "src/**/*.cpp (every TU that uses a helper)"
      to: "src/include/duckdb_compat.hpp"
      via: "#include (directly or via its own header)"
      pattern: "duckdb_compat.hpp"
    - from: "*_native in-out finalize callbacks"
      to: "SetOutputCardinality helper"
      via: "every DataChunk cardinality write on the output chunk"
      pattern: "SetOutputCardinality"
    - from: "CMakeLists.txt WASM_BUILD branch"
      to: "Corrosion FindRust"
      via: "Rust_TOOLCHAIN cache var set before FetchContent_MakeAvailable(Corrosion)"
      pattern: "Rust_TOOLCHAIN"
---

<objective>
Port anofox-statistics' DuckDB 2.0 compatibility layer to anofox_forecast (handover tasks 1-6 of
`.planning/HANDOVER-duckdb2-compat.md`): one shape-detected compat header, a mechanical call-site
rewrite, the known 2.0 runtime fixes, a WASM-only Rust 1.86 pin, a non-blocking DuckDB 2.0-dev CI
job, and keyword lambda syntax. Then verify locally on v1.5.6 and on a DuckDB 2.0-dev build.

Purpose: duckdb/community-extensions now builds the registry default against DuckDB 2.0-dev
(b15358c, ci-tools v2.0-cyanoptera). Today our code does not build there, and if it did it could
silently drop rows. v1.4.5 and v1.5.6 behaviour must stay identical.

Output: `src/include/duckdb_compat.hpp`, rewritten `src/`, CMake WASM pin, CI job, lambda-syntax
test/docs/example updates, a pushed `chore/duckdb2-compat` branch, and a DRAFT PR so CI runs.

OUT OF SCOPE (handover task 7, handled by the orchestrator with the user): CHANGELOG, release
notes, merge, `gh release`, community-extensions PR #3112. Do not touch those. Do not merge.
</objective>

<execution_context>
@~/.claude/gsd-core/workflows/execute-plan.md
@~/.claude/gsd-core/templates/summary.md
</execution_context>

<context>
@.planning/HANDOVER-duckdb2-compat.md
@.planning/STATE.md
@.claude/CLAUDE.md

Facts the planner checked on 2026-10-16. Re-check them; do not trust them blindly.
- Branch `chore/duckdb2-compat` (at main 30c86e9). Commit here. Worktrees are disabled for GSD, so
  the executor runs in the main tree. Push after each verified task (memory: commit + push regularly).
- duckdb submodule is v1.5.6 (069cc9f9b5b). The 2.0 commit `b15358c371daf10dad799a43b6167f6ccdce84d8`
  is NOT in the local submodule object store yet; fetch it. The submodule pointer must NOT change.
- Reference implementation: `git -C ~/projects/duckdb/anofox-statistics show origin/main:src/include/duckdb_compat.hpp`
  (391 lines, already provides AsDuckName/MakeChild/NameString, ReturnTypeOf/SetReturnTypeOf,
  ArgumentsOf, SetNullHandlingOf, SetOrderDependentOf, NameOf, SetFunctionCallbackOf, MakeParser,
  SetCreateInfoName, BindInfoOf, FlatDataMut/FlatValidityMut, StructEntry/StructEntries,
  ANOFOX_AGG_INIT / ANOFOX_AGG_FINALIZE / ANOFOX_AGG_BIND / ANOFOX_SCALAR_BIND,
  AggregateBindFunction/ScalarBindFunction, MarkFallible plus a static_assert). Commits to read with
  `git -C ~/projects/duckdb/anofox-statistics show <sha>`: 6682340, 4fbfc67, 8e9147d, 85ad173,
  473688f, 9be3298, 9a63905. The statistics port converted almost every flat-data access to
  FlatDataMut (739 sites) and every struct-child access to StructEntry (859 sites).
- Our earlier narrower port: `origin/test/duckdb-2.0` (fa44c2b, f90fa32, 7f7a821, 03925ca), with an
  API catalog at `git show origin/test/duckdb-2.0:.planning/quick/261006-ujg-test-anofox-forecast-against-duckdb-2-0-/evidence/code-changes.md`.
  It used a header-presence version gate. This task replaces that with shape detection, so do NOT
  cherry-pick it (main has also moved on a lot). Use it only to see which call sites needed changes.
  It also built against an older 2.0 snapshot (5070de35), not b15358c.
- Live grep counts over `src/` at plan time (re-derive them at execution; MUTABLE-SCOPE AUTHORITY,
  where the live grep wins over these numbers): DataChunk cardinality writes 60 hits / 20 files;
  `FlatVector::GetData` 429 / 24; `StructVector::GetEntries` 70 / 20; `child_list_t` 75 / 23;
  `null_handling` 30 / 9; `.return_type =` 7 / 5; `bind_info` 4 / 3; `AggregateInputData &`
  (custom finalizers) 36 / 7; table-bind `vector<string> &names` 25 / 20; `input_table_names` 39 / 17;
  `named_parameters[` 3 sites in ts_split_keys.cpp + ts_validate_separator.cpp;
  `Parser::ParseExpressionList` 1 site plus CreateInfo `schema`/`name` assignments in
  src/macros/ts_macros.cpp (~line 2690-2735). 46 .cpp + 22 .hpp under src/. Several table-function
  .cpp files do NOT include anofox_forecast_extension.hpp, so include duckdb_compat.hpp directly
  where you need it.
- API presence by version: on v1.5.6, `DataChunk::SetChildCardinality` exists as a documented
  pure forwarder to SetCardinality ("NOTE(backport)" in data_chunk.hpp:66-71). v1.4.5 lacks it.
  `ScalarFunction::SetFallible` exists on v1.5.6 (function.hpp:259) but is NOT a no-op there, so
  MarkFallible must stay gated on the 2.0 bind shape, as in statistics. v1.5.6 and v1.4.5 both lack
  AddKeywordOnly, GetSignature(), GetDataMutable, ValidityMutable and GetBuiltinParser.
- Local build facts: build/release (v1.5.6, Ninja, json linked via extension_config.cmake) has
  `build/release/compile_commands.json` with 92 entries for our src/ files, which is the basis for
  syntax-only cross-version checks. Corrosion v0.6.1 FindRust honours a `Rust_TOOLCHAIN` cache var
  set before Corrosion is fetched. It auto-adds a missing *target* but errors if the *toolchain* is not
  installed. rustup toolchains installed: stable (default, 1.98), 1.86.0 (has
  wasm32-unknown-emscripten), 1.89.0, others. emsdk 3.1.71 lives at ~/emsdk.
- CI Rust job commands: `cargo fmt --all -- --check`, `cargo clippy --all-targets --all-features -- -D warnings`,
  `cargo test --all-features`, `cargo test --workspace --locked`.
- `make audit` uses line-specific regex allowlist entries (scripts/audit_allowlist.txt). Rewriting
  an allowlisted line can make its entry stale, and stale entries fail the audit.
- Arrow lambdas at plan time: 47 code lines across test/sql, examples, docs and
  plugins/anofox-forecast/skills (after dropping `#`/`--` comments and `.print` lines). No arrow
  lambdas in src/ raw SQL, but re-check src/macros/ts_macros.cpp. Keyword lambda syntax has been
  supported since DuckDB 1.3, so it is safe on the 1.4.5 LTS line.
</context>

<tasks>

<task type="tracer">
  <name>Task 1: Compat header + full call-site rewrite, proven to compile on 1.4.5 / 1.5.6 / 2.0 headers and unchanged on v1.5.6</name>
  <files>src/include/duckdb_compat.hpp, src/include/anofox_forecast_extension.hpp, src/include/*.hpp, src/anofox_forecast_extension.cpp, src/aggregate_functions/*.cpp, src/scalar_functions/*.cpp, src/table_functions/*.cpp, src/macros/ts_macros.cpp, scripts/audit_allowlist.txt</files>
  <action>
Step 0. Baseline, before any edit:
- Rebuild v1.5.6 incrementally with `GEN=ninja make release`.
- Run the clean-HOME suite with `HOME=$(mktemp -d) DATAZOO_DISABLE_TELEMETRY=1 build/release/test/unittest "test/sql/*"`. Save the log to build/logs/tjw-baseline-v156.log and record the totals. Expect 5591 assertions / 94 cases.
- Snapshot the output of every examples/**/*.sql run through build/release/duckdb into build/tjw-examples-before/ (one file per example, stdout+stderr). It is the "behaviour unchanged" oracle for step 6.

Step 1. Cross-version header trees, outside the repo, without moving the submodule pointer:
- `git -C duckdb fetch origin b15358c371daf10dad799a43b6167f6ccdce84d8`. If fetching a bare sha is refused, fetch branch v2.0-cyanoptera and confirm the sha is reachable.
- `git -C duckdb worktree add --detach /home/simonm/projects/duckdb/duckdb-2.0-b15358c b15358c371daf10dad799a43b6167f6ccdce84d8`
- `git -C duckdb worktree add --detach /home/simonm/projects/duckdb/duckdb-1.4.5 v1.4.5`
- Confirm `git status --short duckdb extension-ci-tools` is empty afterwards.

Step 2. Create src/include/duckdb_compat.hpp. Start from the anofox-statistics origin/main header verbatim (keep its ANOFOX_* macro names, the static_assert, and its rule: detect each API by shape, never by a DuckDB version macro; the `__has_include` of the split vector headers is the one allowed header test, exactly as statistics does it). Add the forecast-specific extras below. Each one is shape-detected, forwards to the old API on 1.x, and gets a one-line comment naming the API difference:
  (a) `SetOutputCardinality(DataChunk &, idx_t)`. Prefer the member SetChildCardinality when it exists, else the plain cardinality setter. This carries over the 2.0 in-out-finalize row-loss fix from test/duckdb-2.0 03925ca. On v1.5.6 the preferred member is the documented forwarder, so behaviour is identical. On 1.4.5 the fallback is chosen.
  (b) The table-bind names type, taken from the shape of table_function_bind_t (argument 3 via the header's FunctionTraits): alias `TableBindNames` and element `TableBindName`, plus a builder from std::string. Reads of `input.input_table_names[i]` go through NameString.
  (c) `AddNamedParameter(TableFunction &, const std::string &, LogicalType)`. When the keyword-only signature API exists (2.0), register with a typed-NULL default `Value(type)` so the parameter stays optional. Otherwise use the legacy named_parameters map. Also add a constexpr trait, true only when the keyword-only path is selected, that bind loops consult to skip NULL values. Explicit `:= NULL` behaviour on 1.x then stays exactly as it is today.
  (d) An expression-list parse helper. Use the static Parser::ParseExpressionList when it is callable statically, else MakeParser() plus the instance method.
  (e) Add any further helper the syntax-only 2.0 pass (step 5) proves necessary, for example the macro `default_parameters` key type or a table-function callback typedef change. Keep the same pattern.

Step 3. Mechanical rewrite across src/. Re-derive the list with grep first. Script the high-volume patterns (perl -pi with targeted regexes, then review `git diff` per file). Hand-fix the rest:
  - Every DataChunk cardinality write: route it through SetOutputCardinality (all ~60 sites, not only finalize callbacks).
  - Writes through flat-vector data/validity on output/result vectors and their children: use FlatDataMut / FlatValidityMut. INPUT vectors (scalar args.data[i], aggregate Update inputs[i], in-out input chunk columns) must be read through UnifiedVectorFormat (ToUnifiedFormat, sel->get_index, validity), never as flat data. 2.0 hands dictionary vectors, so this applies especially to list_entry_t reads in every aggregate Update and in every scalar execute that reads a LIST input (handover task 2).
  - StructVector::GetEntries indexing: use StructEntry(entries[i]). Runtime-string STRUCT child names: use MakeChild. Reading struct child names: use NameString.
  - `.return_type =` in bind callbacks: use SetReturnTypeOf. `.bind_info`: use BindInfoOf. `null_handling` assignments: use SetNullHandlingOf.
  - Custom aggregate finalizers: register via ANOFOX_AGG_FINALIZE(fn). Custom init: ANOFOX_AGG_INIT. Aggregate bind callbacks: ANOFOX_AGG_BIND with an AggregateBindFunction& parameter. Scalar bind callbacks: ANOFOX_SCALAR_BIND with a ScalarBindFunction& parameter. DuckDB's own templates (AggregateFunction::StateInitialize etc.) stay as they are.
  - Table bind callbacks: `names` out-param typed as TableBindNames, pushes via the builder, input_table_names reads via NameString.
  - ts_split_keys.cpp / ts_validate_separator.cpp: register via AddNamedParameter. Bind loops skip NULL values only when the trait from 2(c) is true.
  - src/macros/ts_macros.cpp: CreateInfo schema/name via SetCreateInfoName, the expression-list parse via helper 2(d), and the macro default_parameters key via the DuckName/AsDuckName helpers.
  - MarkFallible(fn) on every ScalarFunction this extension registers whose execute path can throw. That is every FFI-backed scalar in src/scalar_functions/*.cpp, plus the scalars registered from src/table_functions/*.cpp (e.g. ts_forecast_ensemble_native, ts_ensemble_inspect_native, ts_decomposition, ts_features, ts_changepoints, ts_peaks, ts_periods, ts_seasonality, ts_detrend). Call it on each overload before it is added to its set. When in doubt, mark it; it is a no-op on 1.x.
  Include duckdb_compat.hpp in every TU (or its own .hpp) that uses a helper. Do not change any statistical logic; this is marshalling only.

Step 4. Rebuild v1.5.6 (`GEN=ninja make release`) until there are 0 errors.

Step 5. Syntax-only cross-version proof:
- Write a throwaway script, build/tjw-syntax-check.sh (build/ is gitignored). It reads build/release/compile_commands.json, keeps the entries whose file is under this repo's src/, and, for a given header root R, rewrites every `-I<repo>/duckdb/` include prefix to R. It strips the -o/-MD/-MF/-MT output flags, appends -fsyntax-only, and runs all entries in parallel (xargs -P20). It exits non-zero on any error.
- Run it with R=/home/simonm/projects/duckdb/duckdb-1.4.5/ and with R=/home/simonm/projects/duckdb/duckdb-2.0-b15358c/. If 2.0 needs an include directory that v1.5.6 did not have, add it in the script.
- Iterate on the header and call sites until both runs report 0 errors. On the 2.0 run, also count `deprecated` warnings that point into this repo's src/ and drive them to 0. 2.0 deprecations hide silent behaviour changes (memory duckdb2-port-gotchas).

Step 6. Behaviour-unchanged proof on v1.5.6:
- Rebuild, then rerun the clean-HOME suite. It must be 5591 assertions / 94 cases, all passed.
- Re-run every example into build/tjw-examples-after/. `diff -r build/tjw-examples-before build/tjw-examples-after` must be empty. Lines carrying wall-clock timings are the only allowed differences; list any you exclude.

Step 7. Run `make audit`. If entries went stale because an allowlisted line was rewritten (e.g. FlatDataMut replacing flat-data access), update only the regex column and keep the justification. Add no new statistics code and no new entries without a marshalling justification.

Step 8. Commit in two commits: `feat(compat): add shape-detected duckdb_compat.hpp for DuckDB 1.4/1.5/2.0` and `refactor(compat): route src/ DuckDB API calls through duckdb_compat helpers`. Each ends with the Co-Authored-By trailer from the conversation. Then `git push -u origin chore/duckdb2-compat`.
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast && HOME=$(mktemp -d) DATAZOO_DISABLE_TELEMETRY=1 build/release/test/unittest "test/sql/*" 2>&1 | tail -2   # expect: All tests passed (5591 assertions in 94 test cases)</automated>
    <automated>bash build/tjw-syntax-check.sh /home/simonm/projects/duckdb/duckdb-1.4.5/ && bash build/tjw-syntax-check.sh /home/simonm/projects/duckdb/duckdb-2.0-b15358c/ && echo SYNTAX-OK   # expect SYNTAX-OK, 0 errors on both header roots</automated>
    <automated>diff -r build/tjw-examples-before build/tjw-examples-after && echo EXAMPLES-IDENTICAL</automated>
    <automated>grep -rnE '\.SetCardinality\(' src | grep -v '^src/include/duckdb_compat.hpp:' | grep -vE '^[^:]+:[0-9]+:\s*//' | wc -l   # expect 0</automated>
    <automated>grep -rnE 'DUCKDB_(MAJOR_|MINOR_|PATCH_)?VERSION' src | grep -vE '^[^:]+:[0-9]+:\s*//' | wc -l   # expect 0</automated>
    <automated>make audit && git diff --quiet HEAD -- duckdb extension-ci-tools Cargo.lock && echo AUDIT-AND-PINS-OK</automated>
  </verify>
  <done>
- src/include/duckdb_compat.hpp exists, is based on the statistics header, and adds the forecast extras (a)-(d) plus any needed (e). All are shape-detected and use no version macro.
- Every src/ call site that the 2.0 API changes goes through a helper.
- The 1.4.5 and 2.0 header syntax checks are clean, with 0 src/ deprecation warnings on 2.0.
- The v1.5.6 suite is unchanged at 5591/94 and the example outputs are identical. `make audit` passes.
- The submodule pointers and Cargo.lock are unchanged. Both commits are pushed.
  </done>
</task>

<task type="auto">
  <name>Task 2: Local DuckDB 2.0-dev (b15358c) build; full suite + row-count probe green; runtime fixes</name>
  <files>src/**/*.cpp, src/include/duckdb_compat.hpp, extension_config.cmake (only if 2.0 rejects the json load, and only with a fix that is a no-op on 1.x), test/sql/**/*.test (only for 2.0 error-wording drift, kept passing on 1.x)</files>
  <precondition>Task 1 is committed. /home/simonm/projects/duckdb/duckdb-2.0-b15358c is checked out at b15358c. build/release (v1.5.6) passes 5591/94.</precondition>
  <action>
1. Configure a separate 2.0 build dir, so build/release is never clobbered. Mirror the non-default cache entries of build/release/CMakeCache.txt: `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DEXTENSION_STATIC_BUILD=1 -DDUCKDB_EXTENSION_CONFIGS=/home/simonm/projects/duckdb/anofox-forecast/extension_config.cmake -DUNITTEST_ROOT_DIRECTORY=/home/simonm/projects/duckdb/anofox-forecast/ -DBENCHMARK_ROOT_DIRECTORY=/home/simonm/projects/duckdb/anofox-forecast/ -DENABLE_UNITTEST_CPP_TESTS=FALSE -DENABLE_EXTENSION_AUTOLOADING=0 -DENABLE_EXTENSION_AUTOINSTALL=0 -S /home/simonm/projects/duckdb/duckdb-2.0-b15358c -B build/release-dd2`. Then run `cmake --build build/release-dd2 --config Release` in the background, logging to build/logs/tjw-dd2-build.log. Poll the log mtime; never sleep blind (memory: subagents stall silently).
   Fallback, only if 2.0's CMake insists on the new-style extension build: add an extension-ci-tools worktree at v2.0-cyanoptera (`git -C extension-ci-tools worktree add --detach <path> origin/v2.0-cyanoptera` after a fetch). Build from a scratch project dir that symlinks src, test, crates, Cargo.toml, Cargo.lock, CMakeLists.txt, extension_config.cmake, vcpkg.json and posthog-telemetry back to this repo, and contains `duckdb` -> the 2.0 worktree. Document which path you used. Committed submodule pointers stay untouched either way.
2. Fix compile/link errors the syntax pass could not see (template instantiation, linking). Use the compat-header style; on 1.x everything must keep forwarding to the old API. Count deprecation warnings into this repo's src/ in the build log and drive them to 0.
3. Run the full suite on 2.0 with `HOME=$(mktemp -d) DATAZOO_DISABLE_TELEMETRY=1 build/release-dd2/test/unittest "test/sql/*"`, logged to build/logs/tjw-dd2-suite.log. The target is 94 cases, 0 failures. Classify every failure and fix the root cause:
   - InternalException where a user error was expected: the function needs MarkFallible.
   - Missing required named parameter: register through AddNamedParameter.
   - Wrong, missing or nondeterministic rows: the output cardinality helper is missing, or a dictionary input is read as flat. Use UnifiedVectorFormat.
   - Changed 2.0 error wording in a `statement error` expectation: change the expected text to a substring common to 1.5.6 and 2.0. Never use 2.0-only or 1.5-only SQL; tests must stay green on 1.4.5 and 1.5.6.
   - json: 2.0 links json natively. If the existing `duckdb_extension_load(json)` breaks the 2.0 configure, fix it so that 1.x still loads json.
4. Row-count probe (SetCardinality trap). Write build/tjw-rowcount-probe.sh. It runs the same SQL on build/release/duckdb (v1.5.6) and build/release-dd2/duckdb (2.0), 20 repetitions each, with `SET threads=20`. Data: a deterministic 1000-series x 60-day table. Queries:
   - `SELECT COUNT(*) FROM ts_forecast_by('t', id, ds, y, 'NAIVE', 12, '1d', MAP([], [])) WHERE yhat IS NOT NULL` (expect 12000).
   - One filtered COUNT(*) for each other in-out *_native macro family exercised in test/sql: cv folds + cv forecast, fill gaps, fill forward, metrics, features, aggregate hierarchy / combine / split keys. Copy each call shape from the existing test files.
   - Pass condition: each query returns one identical count across all 20 runs and across both engines. The script prints PROBE OK or the diverging query.
   - Also export the AutoETS forecast for the first 20 series from both engines to CSV and diff them. They must be identical (the earlier port saw bitwise parity).
5. After every fix, rebuild 2.0 AND v1.5.6. The v1.5.6 suite must remain 5591/94 and the T1 syntax check must stay clean for 1.4.5.
6. Commit each fix separately as `fix(duckdb2): <what>`, with the trailer. Push.
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast && HOME=$(mktemp -d) DATAZOO_DISABLE_TELEMETRY=1 build/release-dd2/test/unittest "test/sql/*" 2>&1 | tail -2   # expect: All tests passed (... assertions in 94 test cases)</automated>
    <automated>bash build/tjw-rowcount-probe.sh   # expect final line PROBE OK (20/20 identical counts per query, both engines; AutoETS CSV diff empty)</automated>
    <automated>HOME=$(mktemp -d) DATAZOO_DISABLE_TELEMETRY=1 build/release/test/unittest "test/sql/*" 2>&1 | tail -2 && bash build/tjw-syntax-check.sh /home/simonm/projects/duckdb/duckdb-1.4.5/   # expect 5591/94 on v1.5.6 and 1.4.5 syntax clean</automated>
    <automated>grep -E '/anofox-forecast/src/.*deprecated' build/logs/tjw-dd2-build.log | wc -l   # expect 0</automated>
  </verify>
  <done>
- build/release-dd2 (DuckDB b15358c) builds the extension with 0 src/ deprecation warnings.
- Its clean-HOME suite passes 94/94.
- The row-count probe prints PROBE OK and the AutoETS outputs are identical across engines.
- v1.5.6 is still 5591/94 and 1.4.5 is still syntax-clean.
- Fixes are committed atomically and pushed. The submodule pointers are unchanged.
  </done>
</task>

<task type="auto">
  <name>Task 3: WASM-only Rust 1.86 pin, duckdb-next CI job, keyword lambdas, final DoD + draft PR</name>
  <files>CMakeLists.txt, .github/workflows/MainDistributionPipeline.yml, test/sql/**/*.test, examples/**/*.sql, docs/**/*.md, plugins/anofox-forecast/skills/**/SKILL.md, src/macros/ts_macros.cpp (only if it holds arrow lambdas)</files>
  <precondition>Tasks 1-2 are committed. ~/emsdk/emsdk_env.sh exists (emsdk 3.1.71). The rustup default toolchain is newer than 1.86.</precondition>
  <action>
1. WASM Rust pin, scoped to WASM only (handover task 3). Native keeps the workspace floor `rust-version = 1.89` and the rust-msrv CI job. Therefore add NO rust-toolchain.toml, and do not port statistics' musl-detection change 9a63905, which is only needed with such a file. In CMakeLists.txt, after WASM_BUILD is determined and before `FetchContent_MakeAvailable(Corrosion)`:
   - Add a cache STRING `ANOFOX_WASM_RUST_TOOLCHAIN`, default "1.86.0". An empty value disables the pin.
   - Comment the reason: Rust >= 1.87 / LLVM 20 marks wasm objects with bulk-memory-opt and call-indirect-overlong. DuckDB CI's emsdk 3.1.71 / binaryen v120 `wasm-opt -O3` rejects them. The registry's 2.0 build uses ci-tools Rust 1.97+. Remove the pin once DuckDB's emsdk understands those features.
   - When WASM_BUILD is set and the value is non-empty: find rustup and run `rustup toolchain install <ver> --profile minimal --target wasm32-unknown-emscripten` via execute_process (idempotent). FATAL_ERROR with an actionable message if that fails, or a WARNING plus no pin if rustup is absent. Then set `Rust_TOOLCHAIN` as a CACHE STRING with FORCE.
   - The existing WASM cargo-target selection keys on the *default* toolchain's installed targets. Make sure an Emscripten build still selects wasm32-unknown-emscripten when only the pinned toolchain carries that target. Gate on EMSCRIPTEN; the install step guarantees the target.
   - Keep the existing WASM `--ignore-rust-version` flag. The non-WASM path stays byte-identical.
2. Verify the pin locally:
   - `source ~/emsdk/emsdk_env.sh && cargo +1.86.0 build --release --ignore-rust-version --target wasm32-unknown-emscripten -p anofox-fcst-ffi` must exit 0.
   - Then the newer-Rust CI scenario: default toolchain stable, emsdk-bundled binaryen (NO EM_BINARYEN_ROOT override). Run `rm -rf build/wasm_eh && source ~/emsdk/emsdk_env.sh && GEN=ninja make wasm_eh` in the background, logged to build/logs/tjw-wasm_eh.log (cold build ~30 min).
   - Pass condition: exit 0; the configure log shows Corrosion's `Rust Toolchain: 1.86.0`; build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm exists, carries a `duckdb_signature` custom section, and imports 0 symbols named anofox_*. Check with ~/emsdk/upstream/bin/llvm-objdump or a short python wasm-section parser.
   - Reconfigure native (`GEN=ninja make release`) and confirm `grep Rust_TOOLCHAIN build/release/CMakeCache.txt` does not show 1.86.
3. CI (handover task 4). Add a `duckdb-next-build` job to .github/workflows/MainDistributionPipeline.yml that mirrors anofox-statistics 85ad173 + 473688f:
   - `uses: duckdb/extension-ci-tools/.github/workflows/_extension_distribution.yml@v2.0-cyanoptera`
   - with: duckdb_version b15358c371daf10dad799a43b6167f6ccdce84d8, ci_tools_version v2.0-cyanoptera, extension_name anofox_forecast, enable_rust true, reduced_ci_mode disabled, artifact_postfix "-duckdb-next", and statistics' exclude_archs list, so that only linux_amd64 (full sqllogictest) and wasm_eh build.
   - Comment: non-blocking, deploys nothing, keep the duckdb commit in sync with duckdb/community-extensions, wasm_eh mirrors the registry PR build.
   - No other job may `needs` it, it gets no `secrets: inherit`, and no deploy job depends on it.
   - Validate with `python3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/MainDistributionPipeline.yml'))"`.
4. Keyword lambdas (handover task 5). Rewrite every arrow-form list lambda to keyword form in test/sql, examples, docs, plugins/anofox-forecast/skills, and in any SQL raw string under src/:
   - A single parameter becomes `lambda x: body`. A parenthesised multi-parameter list becomes `lambda a, b: body` (drop the parentheses).
   - Do NOT touch JSON arrow operators (an arrow followed by a string literal or number), or prose arrows in `#` / `--` comments and `.print` lines.
   - Re-run the v1.5.6 suite (5591/94) and the 2.0 suite (94/94). Run each changed example through build/release/duckdb with no errors. Run `make check-docs`.
5. Final Definition of Done (regression gates; Rust is not expected to change):
   - `cargo fmt --all -- --check`, `cargo clippy --all-targets --all-features -- -D warnings`, `cargo test --workspace --locked`
   - `make audit`, `make check-docs`, `python3 scripts/check_correctness_inventory.py`
   - `git diff --quiet origin/main -- duckdb extension-ci-tools Cargo.lock`
6. Commit (`fix(wasm): pin Rust 1.86 for WASM builds only`, `ci: add non-blocking DuckDB 2.0-dev build job`, `test: use keyword lambda syntax in list lambdas`, each with the trailer) and push.
7. Open a DRAFT PR to main: `gh pr create --draft --base main --head chore/duckdb2-compat`, title "DuckDB 2.0-dev compatibility (compat header, WASM Rust 1.86 pin, duckdb-next CI)". The body summarises tasks 1-6 and the local evidence, and ends with the PR attribution line from the conversation.
   - Poll CI (gh run list/watch, in the background). Record the conclusion of every leg in the SUMMARY: v1.4.5 LTS (Linux + WASM), v1.5.6 (all), duckdb-next (linux_amd64 + wasm_eh).
   - Fix and push failures caused by our code. Record infra/runner failures without looping.
   - Do NOT merge, release, edit CHANGELOG, or touch community-extensions.
  </action>
  <verify>
    <automated>cd /home/simonm/projects/duckdb/anofox-forecast && R='\b[a-z_][a-z0-9_]*\)? ?-> ?[a-zA-Z(]'; grep -rnE "$R" test/sql examples docs plugins/anofox-forecast/skills | grep -vE ':[0-9]+:\s*(#|--|\.print)' | sed -E 's/--.*$//' | grep -cE "$R"   # expect 0</automated>
    <automated>grep -q 'Rust Toolchain: 1.86.0' build/logs/tjw-wasm_eh.log && test -f build/wasm_eh/extension/anofox_forecast/anofox_forecast.duckdb_extension.wasm && echo WASM-PIN-OK</automated>
    <automated>python3 -c "import yaml; d=yaml.safe_load(open('.github/workflows/MainDistributionPipeline.yml')); j=d['jobs']['duckdb-next-build']; assert 'v2.0-cyanoptera' in j['uses'] and j['with']['duckdb_version'].startswith('b15358c'); assert all('duckdb-next-build' not in str(v.get('needs','')) for v in d['jobs'].values()); print('CI-JOB-OK')"</automated>
    <automated>cargo fmt --all -- --check && cargo clippy --all-targets --all-features -- -D warnings && cargo test --workspace --locked && make audit && make check-docs && python3 scripts/check_correctness_inventory.py && echo DOD-OK</automated>
    <automated>HOME=$(mktemp -d) DATAZOO_DISABLE_TELEMETRY=1 build/release/test/unittest "test/sql/*" 2>&1 | tail -1; HOME=$(mktemp -d) DATAZOO_DISABLE_TELEMETRY=1 build/release-dd2/test/unittest "test/sql/*" 2>&1 | tail -1   # expect 5591/94 on v1.5.6 and 94/94 on 2.0</automated>
  </verify>
  <done>
- A wasm_eh build with default Rust 1.98 plus the emsdk-bundled binaryen links using Rust 1.86.0. The artifact is signed and has 0 anofox_* imports. Native still resolves a toolchain of 1.89 or newer.
- duckdb-next-build is in the CI workflow and is non-blocking.
- 0 arrow lambdas remain in tests/examples/docs/skills. Both suites are green.
- fmt, clippy, cargo test, audit, check-docs and the inventory all pass.
- Branch pushed, DRAFT PR open. CI leg outcomes are recorded in the SUMMARY. No merge or release.
  </done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| SQL caller -> extension functions | Untrusted SQL arguments reach C++/FFI. On 2.0, an error thrown from a function that is not marked fallible becomes an InternalException, which can invalidate the database instance. |
| DuckDB engine -> in-out finalize output | Output row counts depend on vector sizes. 2.0 does not resync them after in_out_function_final. |
| CMake configure -> rustup dist server | A WASM configure now downloads a pinned Rust toolchain at build time. |
| Repo CI -> third-party reusable workflow | The new job runs duckdb/extension-ci-tools@v2.0-cyanoptera, which is a branch ref. |

## STRIDE Threat Register

| Threat ID | Category | Component | Severity | Disposition | Mitigation Plan |
|-----------|----------|-----------|----------|-------------|-----------------|
| T-tjw-01 | Tampering (data integrity) | *_native in-out finalize output on 2.0 | high | mitigate | All output cardinality writes go through SetOutputCardinality (SetChildCardinality on 2.0). The T2 probe proves 20/20 identical filtered counts and identical AutoETS output across engines. |
| T-tjw-02 | Denial of Service | Throwing scalar functions on 2.0 | high | mitigate | MarkFallible on every FFI-backed scalar (no-op on 1.x). The 2.0 suite's `statement error` cases prove user errors stay user errors. |
| T-tjw-03 | Tampering | Dictionary-encoded list inputs on 2.0 | medium | mitigate | Aggregate Update / scalar execute read inputs via UnifiedVectorFormat; covered by the 2.0 suite plus the probe. |
| T-tjw-04 | Denial of Service | Named params turning required on 2.0 | low | mitigate | AddNamedParameter with a typed-NULL default; NULL skipped only on the keyword-only API path, so 1.x is unchanged. |
| T-tjw-05 | Tampering | rustup toolchain install in WASM configure | medium | mitigate | Exact version pin 1.86.0 through `rustup toolchain install` only (rustup verifies dist checksums over TLS). No curl-pipe-shell. WASM builds only. Can be disabled via an empty ANOFOX_WASM_RUST_TOOLCHAIN. |
| T-tjw-06 | Elevation of Privilege | duckdb-next-build reusable workflow on a branch ref | low | accept | Same practice as the existing @v1.4-andium / @v1.5-variegata jobs. The new job gets no `secrets: inherit`, nothing `needs` it, and nothing deploys from it. |
| T-tjw-SC | Tampering | npm/pip/cargo installs | low | accept | No new packages or crates. The gate `git diff --quiet origin/main -- Cargo.lock` proves the lockfile is unchanged. |
</threat_model>

<verification>
- v1.5.6: clean-HOME suite 5591 assertions / 94 cases (T1, T2, T3 re-runs) and identical example outputs before/after (T1).
- 1.4.5 headers: syntax-only compile of all src/ is clean (T1, re-checked T2). The LTS CI leg is the full proof.
- 2.0-dev b15358c: local build plus full clean-HOME suite 94/94, row-count probe PROBE OK, 0 src/ deprecation warnings (T2).
- WASM: 1.86 pin proven with the newer default toolchain and the bundled binaryen (T3).
- CI: draft PR legs recorded (LTS Linux/WASM, v1.5.6 all, duckdb-next linux_amd64 + wasm_eh).
- Pins: the duckdb / extension-ci-tools submodule pointers and Cargo.lock are unchanged against origin/main.

Source coverage (handover `.planning/HANDOVER-duckdb2-compat.md`):

| Source item | Covered by |
|---|---|
| Goal: builds + passes on 2.0-dev and still on 1.5.6 / 1.4.5 | T1 (1.4.5/1.5.6), T2 (2.0), T3 (CI legs) |
| Task 1: compat header incl. SetChildCardinality + AddKeywordOnly extras | T1 step 2 |
| Task 2: call-site rewrite, unified-format list reads, MarkFallible | T1 step 3, T2 runtime fixes |
| Task 3: WASM Rust 1.86 without breaking native | T3 steps 1-2 |
| Task 4: non-blocking duckdb-next CI job | T3 step 3 |
| Task 5: keyword lambda syntax; 2.0 links json | T3 step 4; T2 step 3 (json) |
| Task 6: verification matrix | T1/T2/T3 verify + T3 step 7 |
| Task 7: release + registry | EXCLUDED by orchestrator (handled with the user afterwards) |
</verification>

<success_criteria>
- `src/include/duckdb_compat.hpp` is the single, shape-detected compat layer. There is no DuckDB version macro anywhere in src/.
- The extension passes the full suite on v1.5.6 (5591/94, unchanged) and on a local DuckDB 2.0-dev b15358c build (94/94), with no row-count drift.
- WASM builds pin Rust 1.86.0 while native keeps the 1.89 floor.
- duckdb-next-build runs in CI without blocking anything. Keyword lambdas are used throughout.
- All work is committed atomically on chore/duckdb2-compat and pushed, a DRAFT PR is open, and nothing is merged or released.
</success_criteria>

<output>
Create `.planning/quick/261016-tjw-port-anofox-statistics-duckdb-2-0-compat/261016-tjw-SUMMARY.md` when done. Include:
- Per-task commit hashes; the orchestrator verifies these against git.
- The final helper list in duckdb_compat.hpp, with the 1.x path each one selects.
- Call-site counts before/after from the live grep.
- The v1.5.6 and 2.0 suite totals with their log paths.
- Probe output and the wasm_eh evidence: toolchain line, signature section, import count.
- The draft PR URL and every CI leg's conclusion.
- Worktree paths left in place: /home/simonm/projects/duckdb/duckdb-2.0-b15358c and /home/simonm/projects/duckdb/duckdb-1.4.5, plus build/release-dd2.
- Any deviations, including any test expectation reworded for 2.0 error-message drift.
</output>
