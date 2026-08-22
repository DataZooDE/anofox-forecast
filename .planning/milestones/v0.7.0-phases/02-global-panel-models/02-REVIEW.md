---
phase: 02-global-panel-models
reviewed: 2026-08-21T00:00:00Z
depth: standard
files_reviewed: 5
files_reviewed_list:
  - crates/anofox-fcst-ffi/src/lib.rs
  - src/table_functions/ts_forecast_panel_native.cpp
  - src/include/anofox_fcst_ffi.h
  - benchmark/src/common/anofox_runner.py
  - benchmark/src/common/benchmark_runner.py
findings:
  critical: 0
  warning: 0
  info: 0
  total: 0
status: clean
---

# Phase 02: Code Review Report (Iteration 3 — Fix Verification)

**Reviewed:** 2026-08-21T00:00:00Z
**Depth:** standard
**Files Reviewed:** 5
**Status:** clean

## Summary

This is the final (iteration 3) pass of a three-pass fix loop. The three fixes targeted by iteration 2 (CR-01 deferred error for n_kept<3, WR-01 overflow match in lib.rs, WR-02 freq-aware date re-computation) were verified in full. All three are correct and introduce no new regressions. Status is clean.

---

## Fix Verification

### CR-01 — `deferred_error_message` control flow (`src/table_functions/ts_forecast_panel_native.cpp`)

**Verdict: Correct.**

Control flow traced across all `n_kept` branches in `TsForecastPanelNativeFinalize`:

**`n_kept == 0` (all series dropped):** Sets `processed = true`, falls through to the output-batching block. `deferred_error_message` remains `""`. Any DROPPED rows in `gstate.results` flush via successive `HAVE_MORE_OUTPUT` returns. When `remaining == 0` the `!empty()` guard is false, so FINISHED returns cleanly. Correct.

**`n_kept == 1 or 2` (too-short panel):** Sets `processed = true`; sets `deferred_error_message` (line 573). Falls through. DROPPED sentinel rows flush first. The deferred throw is checked at both FINISHED return sites:
- Line 693–695: the `remaining == 0` early-return path — fires when there are zero rows to emit at all (e.g., zero DROPPED rows accumulated alongside the two short series).
- Lines 747–750: the `output_offset >= results.size()` post-batch path — fires after the last DROPPED-row batch has been emitted.

These two sites are mutually exclusive per invocation: if `remaining == 0` fires first, execution never reaches the second; if DROPPED rows require at least one output batch, the second fires after the final batch. No double-throw is possible. No path through the code returns FINISHED on a too-short panel without either forecasting (n_kept>=3) or throwing the deferred error (n_kept<3). Correct.

**`n_kept >= 3` (happy path):** `deferred_error_message` is never assigned. Both FINISHED sites check `!empty()` which evaluates to false; no spurious throw. Correct.

---

### WR-01 — lib.rs overflow `match` + `horizon == 0` guard (`crates/anofox-fcst-ffi/src/lib.rs` ~lines 7013, 7027–7044)

**Verdict: Correct.**

**First multiplication (`n_series * series_len`, line 7013):** Uses `checked_mul(series_len).ok_or_else(|| PanelForecastError::InvalidModel(...))` followed by `?` inside the `catch_unwind` closure. The closure return type is `Result<(Vec<Vec<f64>>, String), PanelForecastError>`, so `?` propagates `Err` to the `Ok(Err(e))` arm of the outer `match result`. That arm sets `out_error` and returns `false`. Error is reported cleanly; no UB.

**Second multiplication (`n_series * horizon`, lines 7033–7044):** Uses an explicit `match` with a `None` arm that calls `(*out_error).set_error(...)` and `return false`. This is in the `Ok(Ok(...))` arm, outside the closure, so direct `return false` is correct — no `?` is needed and no intermediate Result wrapping is involved. Both `Some` and `None` arms are handled; no overflow goes silent.

**`horizon == 0` guard (lines 7027–7031):** Placed in the `Ok(Ok(...))` arm, before the `checked_mul` on `horizon`, after `forecast_panel_impl` has returned. Calling `forecast_panel_impl` with `horizon=0` is harmless — it produces empty prediction vectors per series. The guard intercepts allocation of a zero-element buffer and returns a clear error. Correct and safe.

---

### WR-02 — `_FREQ_DELTA` dict (`benchmark/src/common/anofox_runner.py` lines 306–312)

**Verdict: Correct.**

The `_FREQ_DELTA` dict maps each M4 frequency letter to the correct pandas offset:

| Key | Value | Correct? |
|-----|-------|----------|
| `'D'` | `pd.Timedelta(days=1)` | Yes |
| `'h'` | `pd.Timedelta(hours=1)` | Yes |
| `'W'` | `pd.Timedelta(weeks=1)` | Yes |
| `'M'` | `pd.DateOffset(months=1)` | Yes — a fixed Timedelta cannot represent a calendar month |

The `.get(freq, pd.Timedelta(days=1))` fallback is sensible: an unknown frequency code degrades to daily rather than raising `KeyError`. No crash on unknown freq.

One non-blocking observation: `pd.DateOffset(months=1) * Series[int]` produces an `object`-dtype intermediate in pandas (DateOffsets are not vectorisable over Series in the same way Timedelta is), but the subsequent `last_ds + step_delta * fcst_df['forecast_step']` still produces correct date values. This is benchmark code and the result is functionally correct. Not classified as a finding.

---

## Conclusion

All three fixes from iteration 2 are correct. The `deferred_error_message` control flow is sound across every reachable code path. The overflow guards in `lib.rs` cover both multiplications with correct error propagation. The `_FREQ_DELTA` dict uses correct units for all supported frequencies and has a safe default. No new critical or warning issues were introduced.

---

_Reviewed: 2026-08-21T00:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
