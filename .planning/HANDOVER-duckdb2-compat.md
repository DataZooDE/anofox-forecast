# Handover: DuckDB 2.0 compatibility for anofox_forecast

**Created:** 2026-10-16, end of the v0.10.0 / v2026.10.16 release session
**Branch:** `chore/duckdb2-compat` (start work here, from `main` @ 78ccdd1 or later)
**Goal:** anofox_forecast builds and passes its tests against DuckDB 2.0-dev (as the duckdb/community-extensions registry builds it) and still against v1.5.6 and v1.4.5 LTS. Then cut a CalVer release and point the community PR at it.

## Why now

- duckdb/community-extensions PR **#3112** (bump anofox_forecast to v2026.10.16, ref `2620690`) is open. The registry's **default** build (`ref:`) now targets DuckDB **2.0-dev** (commit `b15358c371daf10dad799a43b6167f6ccdce84d8`, extension-ci-tools `v2.0-cyanoptera`). Our code isn't 2.0-ported, so that build is expected to fail. If it compiles, it may silently drop rows (see memory `duckdb2-port-gotchas`).
- `variegata:` (v1.5.6) and `andium:` (v1.4.5) keys in description.yml only redirect the 1.5.6 and 1.4.5 builds. They cannot make the default 2.0 build pass. `scripts/build.py` in community-extensions picks the ref per DUCKDB_VERSION; `ref_next` covers prereleases.

## Reference implementation: anofox-statistics (already done there)

Local clone: `~/projects/duckdb/anofox-statistics` (read files via `git show origin/main:<path>`; the local checkout is on another branch).

| Commit (anofox-statistics) | What |
|---|---|
| `6682340` PR #209 | `src/include/duckdb_compat.hpp` (391 lines), plus a mechanical rewrite of 126 files onto its helpers. It detects each changed API by shape (no DuckDB version macro): aggregate init/finalize/bind callback typedefs via template adapters; `child_list_t` key `std::string` vs `Identifier` (`MakeChild`, `AsDuckName`, `NameString`); SFINAE for members that became accessors (`return_type`, `arguments`, `null_handling`, `order_dependent`, `bind_info`, `function`, CreateInfo name/schema, `FlatVector::GetDataMutable/ValidityMutable` → `FlatDataMut`/`FlatValidityMut`, `StructVector::GetEntries` Vector vs `unique_ptr<Vector>` → `StructEntry`, `Parser::GetBuiltinParser` → `MakeParser`). Runtime 2.0 fixes: aggregate Update reads list inputs via the **unified vector format** (2.0 passes dictionary vectors); throwing scalar functions call `MarkFallible` (no-op on 1.x). |
| `4fbfc67` | assert `SetFallible` exists on 2.0; lambda keyword syntax in examples (`lambda x: ...`) |
| `8e9147d` | tests: lambda keyword syntax in list lambdas |
| `85ad173`, `473688f` | CI: non-blocking `duckdb-next-build` job (`_extension_distribution.yml@v2.0-cyanoptera`, duckdb_version `b15358c…`, `artifact_postfix: -duckdb-next`, `reduced_ci_mode: disabled`, builds only linux_amd64 + wasm_eh). Keep the duckdb commit in sync with the community repo. |
| `9be3298` | WASM: `rust-toolchain.toml` pins Rust **1.86.0**. Rust ≥1.87 (LLVM 20) marks wasm objects with `bulk-memory-opt`/`call-indirect-overlong`; the emsdk 3.1.71 / binaryen v120 that DuckDB CI pins runs `wasm-opt -O3`, which rejects them (`Unknown option '--enable-bulk-memory-opt'`). The registry's 2.0 PR build uses ci-tools Rust 1.97+, so it breaks without the pin. |
| `9a63905` | CMake: detect musl from `DUCKDB_PLATFORM`, not from installed rustup targets (needed once rust-toolchain.toml lists many targets) |

Also see our own earlier, narrower port on branch `origin/test/duckdb-2.0` (quick task 261006-ujg): `ANOFOX_SET_CARDINALITY` → `SetChildCardinality` (2.0 no longer resizes vectors in `in_out_function_final`; row loss otherwise), `AddKeywordOnly` typed-NULL defaults (named params became REQUIRED), and 2.0 links json. Reuse those two fixes; prefer the statistics compat-header style for everything else.

## Tasks

1. **Port the compat header.** Bring in anofox-statistics' `duckdb_compat.hpp` as `src/include/duckdb_compat.hpp`. Add our extras from `test/duckdb-2.0`: the SetCardinality→SetChildCardinality helper and AddKeywordOnly defaults. Keep 1.4/1.5 behaviour byte-identical (helpers forward to the old API).
2. **Mechanical call-site rewrite** across `src/**/*.cpp` (~43 table functions, 8 aggregates, scalar functions, macros, `anofox_forecast_extension.cpp`). Grep first for `SetCardinality(`, `.return_type`, `.arguments`, `null_handling`, `FlatVector::GetData`, `StructVector::GetEntries`, `child_list_t`, aggregate callbacks, `AddKeywordOnly` / named params. Aggregates: read list inputs through `ToUnifiedFormat` (dictionary vectors on 2.0). Throwing scalars: `MarkFallible`.
3. **WASM Rust 1.86 without breaking native.** We CANNOT pin 1.86 repo-wide: native needs ≥1.89 (trueno AVX-512 → workspace `rust-version = 1.89`, rust-msrv CI job). Pin only for WASM, e.g. in `CMakeLists.txt` set corrosion's `Rust_TOOLCHAIN 1.86.0` when `WASM_BUILD` (before FetchContent/corrosion), and make sure the toolchain plus `wasm32-unknown-emscripten` get installed in CI (rustup auto-install, or an explicit `rustup toolchain install 1.86.0 --target wasm32-unknown-emscripten` in a CMake/cargo pre-step). The `--ignore-rust-version` WASM flag (commit `c68376b`) and the no-`is_multiple_of` rule already make the crates compile on 1.86. Verify locally: `cargo +1.86.0 build --release --ignore-rust-version --target wasm32-unknown-emscripten -p anofox-fcst-ffi`, plus a wasm_eh build with the newer-Rust scenario (recipe: memory `wasm_eh local build`).
4. **CI:** add the non-blocking `duckdb-next-build` job to `.github/workflows/MainDistributionPipeline.yml` (mirror anofox-statistics 85ad173/473688f; DuckDB `b15358c…`, ci-tools `v2.0-cyanoptera`; linux_amd64 full sqllogictest + wasm_eh).
5. **Tests:** lambda keyword syntax (`lambda x: …`) wherever `x -> …` list lambdas appear in test/sql, docs and examples (2.0 deprecates the arrow form). The 2.0 build links json natively.
6. **Verify:**
   - native v1.5.6 local build: full suite with a clean HOME (`HOME=$(mktemp -d) build/release/test/unittest "test/sql/*"`, baseline 5591 assertions / 94 cases);
   - `cargo test --workspace`, fmt/clippy, `make audit`, `make check-docs`, inventory;
   - a DuckDB 2.0-dev build (duckdb submodule at `b15358c` in a separate build dir or via CI): full suite, watching for row-count drops (SetCardinality trap) and the required-named-param trap;
   - PR CI green on v1.4.5 LTS (Linux/WASM), v1.5.6 (all) and the new 2.0 job.
7. **Release + registry:**
   - CHANGELOG `## [YYYY.MM.DD]` section ("DuckDB 2.0-dev support") and `docs/release-notes/vYYYY.MM.DD.md`;
   - merge with a merge commit, then `gh release create vYYYY.MM.DD --target <merge sha>` (memory `calver-release-process`);
   - update community PR #3112 (branch `DataZooDE:bump-anofox_forecast-v2026.10.16` on `DataZooDE/community-extensions`) with the new `version` and `ref`; if a 1.5.6/1.4.5-only difference is needed, use `variegata:` / `andium:`.

## Known constraints and gotchas (from this session)

- Release CI pitfalls hidden locally: run tests with a clean HOME (json was installed in ~/.duckdb); no DuckDB-1.5-only SQL functions in tests (`struct_keys` is missing on 1.4.5); WASM-only negative tests must not be `*.test` files.
- LTS v1.4.5 macOS is excluded in our CI (GitHub macos-26 image, MacOSX27.0.sdk breaks the v1.4-andium linker); re-enable later (memory `lts-macos-runner-sdk-break`).
- Worktrees stay disabled for GSD executors in this repo; verify executor reports against git and disk.
- Merges and releases are fine (the user authorised the release flow), but confirm before opening further community-extensions PRs.

## Suggested start (after /clear)

`/gsd-quick` (or `/gsd-new-milestone` if you prefer a tracked phase) with: "Port anofox-statistics' DuckDB 2.0 compat layer to anofox_forecast per .planning/HANDOVER-duckdb2-compat.md".
