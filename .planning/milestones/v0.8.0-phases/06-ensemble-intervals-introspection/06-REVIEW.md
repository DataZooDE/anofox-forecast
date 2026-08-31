---
phase: 06-ensemble-intervals-introspection
reviewed: 2026-08-31
depth: standard (targeted manual review by orchestrator)
status: clean
critical: 0
warning: 0
info: 2
reviewer_note: "The spawned gsd-code-reviewer agent died three times on transient API 'connection closed' errors during long runs without writing REVIEW.md. Because Phase 6 correctness was already independently verified (gsd-verifier 7/7 + orchestrator re-ran both examples: all INSP-01 DoD assertions 0, EPI-01 bad_rows 0, no regression, no runtime memory errors — DuckDB would crash on UB), the orchestrator performed a focused manual review of the exact highest-risk items the reviewer was tasked with (variable-count result marshalling, free-function memory safety, NULL encoding, panic safety, weights/members alignment). No Critical or Warning issues found."
---

# Phase 06: Ensemble Intervals & Introspection — Code Review (INSP-01)

**Scope reviewed:** the INSP-01 introspection source (EPI-01 is example+docs only, no source):
`crates/anofox-fcst-core/src/forecast.rs` (`inspect_explicit_ensemble`, `inspect_auto_ensemble`), `crates/anofox-fcst-ffi/src/lib.rs` (`EnsembleInspectResult`, `anofox_ts_ensemble_inspect`, `anofox_ts_auto_ensemble_inspect`, `anofox_free_ensemble_inspect_result`), `src/table_functions/ts_ensemble_inspect_native.cpp` (+ `.hpp`), registration, macros, CMakeLists.

## Highest-risk checks — all CLEAN

### 1. Result marshalling + memory ownership — CLEAN
- FFI allocates `member_names_buf` (C `malloc`), `weights`, `scores` (`alloc_or_error`) only on the full-success path; on any alloc failure it frees the previously-allocated buffers before returning `false` (lib.rs ~8244-8259, ~8409-8426).
- `anofox_free_ensemble_inspect_result` (lib.rs 8470-8492): null-checks the result pointer; frees each of the three pointers **only if non-null** (handles explicit-member `scores=null` and AutoEnsemble non-Mean `weights=null`); **nulls each pointer after free** (idempotent → safe against accidental double-free); `free` matches `malloc`; zeroes `count`/`member_names_buf_len`.
- C++ calls `anofox_free_ensemble_inspect_result(&insp_result)` once per unpack (ts_ensemble_inspect_native.cpp:261, :443); inspect:free call ratio is 2:2.
- Error paths do not leak: `insp_result` is `memset`-zeroed before the FFI call; on `!success` the C++ throws or `SetNull`+`continue` — nothing was allocated (FFI sets pointers only on full success), so skipping free is correct.

### 2. Bounded buffer parse — CLEAN
- Rust recovers member names via `from_raw_parts(members_buf, members_buf_len)` + split-on-NUL (bounded by length, not an unbounded scan).
- C++ parses the returned names buffer with `strnlen(p, remaining - (p - buf))` bounded by `member_names_buf_len` (no over-read past the buffer). Reads exactly `insp_result.count` entries; the name index is guarded (`i < names_out.size()`).

### 3. NULL encoding — CORRECT
- C++ maps a null `weights`/`scores` pointer to a genuine SQL NULL: `insp_result.weights != nullptr ? Value::DOUBLE(...) : Value(LogicalType::DOUBLE)` (:245-252, :427-433). No 0.0/NaN leaks as a value. AutoEnsemble non-Mean weight and explicit-member score both surface as SQL NULL.

### 4. Panic safety — CLEAN
- Both FFI exports wrap the body in `catch_unwind`; names decoded with `String::from_utf8_lossy` (no panic on non-UTF8); `Ensemble::fit`/`AutoEnsemble::fit` errors are mapped to `AnofoxError`, not `unwrap`ped. A defensive `members_buf`/`members_count` mismatch check returns `InvalidParameter`.

### 5. Weights/members alignment + slice bounds — CLEAN
- `inspect_explicit_ensemble` builds each member via `build_forecaster` inside a loop that propagates any build error with `?` BEFORE constructing the ensemble — so the ensemble always has exactly `member_names.len()` models and `ens.weights().len()` == member count; the final `.zip(weights.iter())` can never misalign. `.weights()` is read AFTER `.fit()` (pre-fit weights are uniform — correctly noted in a code comment).
- `inspect_auto_ensemble` uses `all_scores().iter().take(model_count())` — `.take(k)` is safe even if `k > len` (yields fewer, never panics). Weight = `Some(1/k)` for Mean, `None` otherwise (documented crate 0.15.3 limitation).

### 6. Reuse — CLEAN
- Reuses Phase 5 `build_forecaster` + `parse_combination_method` verbatim; ≥2-member guard and `<3` min-length/NULL-interpolation guards mirror Phase 5's `forecast_explicit_ensemble` (the WR-01/WR-02 fixes carry over).

## Info (non-blocking)
- **IN-01:** the two introspection functions duplicate a fair amount of the FFI marshalling boilerplate between the explicit and auto exports; a shared helper for "build EnsembleInspectResult from (names, weights, scores)" would reduce duplication. Cosmetic.
- **IN-02:** the manual `EnsembleInspectResult` malloc/copy for `member_names_buf` (vs the `alloc_or_error` helper used for the numeric arrays) is a slight asymmetry; harmless but could be unified. Cosmetic.

## Verdict
**clean** — 0 Critical, 0 Warning. The variable-count result marshalling, companion free function, NULL encoding, panic safety, and weights/members alignment are all correctly implemented. Consistent with the passing verifier (7/7) and the independently re-run examples (all DoD assertions 0, both EPI-01 `bad_rows` 0, no regression).
