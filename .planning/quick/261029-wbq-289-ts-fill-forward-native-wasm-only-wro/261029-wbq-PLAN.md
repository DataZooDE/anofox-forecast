---
phase: quick-261029-wbq
plan: 01
type: execute
requirements: [GH-289]
---

# 261029-wbq: _ts_fill_forward_native WASM-only wrong results (#289)

Executed inline (no sub-agents). Investigation-led: the plan was revised as evidence came in.

**Goal:** root-cause and fix the open Phase 19 deferral in `deferred-items.md`: `_ts_fill_forward_native` gives correct results natively but wrong ones in DuckDB-Wasm (calendar and hourly frequencies, plus short groups). Then restore its WASM test coverage.

**Tasks**
1. Reproduce on a local v1.5.6 `wasm_eh` build in the Node harness (`261029-wbq-repro.test`, with native-verified expected values).
2. Bisect the cause with temporary debug hooks, never committed, on both sides of the FFI boundary.
3. Fix at the cause, then verify with a CI-like build: emsdk 3.1.71 with its bundled binaryen v120. Run the curated harness and the native full suite.
4. Close the deferral: un-skip `ts_fill_forward_native.test` on WASM, add a permanent regression test, update the CHANGELOG.
