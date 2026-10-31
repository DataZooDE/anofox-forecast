---
quick_id: 261029-wbq
status: complete
issue: 289
commits: [01da744c]
---

# 261029-wbq: _ts_fill_forward_native WASM-only wrong results (#289)

## Root cause
The WASM build pinned Rust 1.86.0. Up to at least 1.92, Rust compiles `wasm32-unknown-emscripten` code with **JS-based exception handling**: the extension imported 113 `invoke_*` trampolines. Inside the native-exception `wasm_eh` side module, ordinary Rust code then computed wrong results. The same source built with Rust 1.96 has 0 `invoke_*` imports and is correct.

## Evidence (v1.5.6 `wasm_eh`, Node harness, `261029-wbq-repro.test`)
- **Before:** 8 of 13 fail.
  - The `'1d'` fill returns the input unfilled.
  - `'1mo'` returns 652 rows stepping from 1970-02-28, where 4 are expected.
  - `'1h'` aborts with a Rust `capacity overflow` panic, then "Rust panics must be rethrown".
- **Debug hooks** (temporary, in the v156 worktree only):
  - C++ passes correct dates, target and step.
  - Rust receives them intact.
  - Inside core, `micros_to_datetime` fails for 198 of 400 days. A verbatim copy compiled in the FFI crate gets all of them right, and so does chrono's out-of-line function called through a function pointer.
- **Ruled out:** `std::regex` (a replacement changed nothing; reverted); `wasm-opt` (same failure linked at `-O0`); static-data corruption (the live chrono table is byte-identical to the file); a stale Rust lib (see the build-ordering note below).
- **Toolchain bisection:** 1.86, 1.88 and 1.92 fail 8 of 13; 1.96 and 1.98 pass 13 of 13. The pristine branch code with 1.96 also passes 13 of 13, so the toolchain alone explains it.

## Fix (01da744c)
- `CMakeLists.txt`: `wasm_eh` pins Rust **1.96.0**. `wasm_mvp` and `wasm_threads` keep 1.86.0: JS exceptions match the mvp platform.
- `extension_config.cmake`: for `wasm_eh`, `LINKED_LIBS` carries `-Xlinker --no-check-features -Xlinker --features=…`. The list omits `bulk-memory-opt` and `call-indirect-overlong`, which emsdk 3.1.71's binaryen v120 rejects. Their instructions are covered by `bulk-memory` and `reference-types`.
- WASM allocator, in both `lib.rs` and `allocation.rs`: the size header is 8 bytes, not `size_of::<usize>()` (4 on wasm32). Before, every returned i64/f64 array was misaligned; debug Rust traps with `misaligned pointer dereference`.
- Tests: `ts_fill_forward_native.test` (45 assertions) is off the WASM skip list. The new `ts_fill_forward_wasm_regression.test` (13) pins the bisected cases. Both are in CURATED.

## Verification
- **CI-like `wasm_eh` build:** bundled binaryen v120 (EM_BINARYEN_ROOT unset), with the CMake default picking 1.96.0. It links, has 0 `invoke_*` imports, and loads in DuckDB-Wasm 1.33.1-dev65.0 (engine v1.5.6).
- **Curated WASM harness:** 540 passed, 0 failed, across 12 files.
- **`--all`:** 4193 passed and 8 failed, across 110 files. Phase 19 recorded 23 failing files. All 8 remaining failures are environmental: `SET threads` in single-threaded WASM, unexpanded `${pio}` loop variables in the harness, a 2.0-only `v2_or` path, and one exact-vs-approximate `R` comparison. The latest is `extension_comparison` mean 159.4 vs 159.0, where 159.4 is the true value.
- **Native v2.0.0:** `cargo fmt --check` and clippy (FFI crate) are clean. The full sqllogictest suite has 6975 assertions in 110 test cases, all passing.
- Not done locally: `wasm_mvp`, `wasm_threads`, and the DuckDB 2.0 (cyanoptera) WASM leg. CI covers them; `wasm_mvp`'s toolchain is unchanged.

## Notes
- Local incremental WASM builds can link the `.wasm` before cargo finishes, so the artifact carries the previous Rust lib. Re-run make, or `touch` a C++ source and relink, before testing.
