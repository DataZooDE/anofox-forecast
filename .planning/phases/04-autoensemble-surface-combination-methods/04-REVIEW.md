---
phase: 04-autoensemble-surface-combination-methods
reviewed: 2026-08-30T12:00:00Z
depth: standard
files_reviewed: 7
files_reviewed_list:
  - crates/anofox-fcst-core/src/forecast.rs
  - crates/anofox-fcst-ffi/src/types.rs
  - crates/anofox-fcst-ffi/src/lib.rs
  - src/include/anofox_fcst_ffi.h
  - src/scalar_functions/ts_forecast_scalar.cpp
  - src/table_functions/ts_forecast_native.cpp
  - examples/forecasting/autoensemble.sql
findings:
  critical: 0
  warning: 2
  info: 2
  total: 4
status: issues_found
---

# Phase 4: Code Review Report — AutoEnsemble Surface

**Reviewed:** 2026-08-30T12:00:00Z
**Depth:** standard
**Files Reviewed:** 7
**Status:** issues_found

## Summary

Phase 4 adds `ModelType::AutoEnsemble` through the full stack: Rust core (`forecast.rs`), FFI struct additive extension (`types.rs`, `lib.rs`), cbindgen-generated header (`anofox_fcst_ffi.h`), and two C++ dispatch sites (`ts_forecast_scalar.cpp`, `ts_forecast_native.cpp`). The runnable cross-check example (`autoensemble.sql`) exercises all six combination methods.

The ABI additive change is correctly executed: both `ForecastOptions` and `ForecastOptionsExog` have the new fields appended at the end, the cbindgen header matches the Rust struct layout exactly, `From<ForecastOptions> for ForecastOptionsExog` copies both new fields, and both C++ sites use `memset(&opts, 0, sizeof(opts))` before use — so the `ensemble_method` buffer is zero-initialised (NUL-terminated) even when the string is empty. The `CStr::from_ptr` pattern is safe under these conditions. The `strncpy`/explicit-NUL guard at `[sizeof - 1]` on the C++ side is correct. `parse_combination_method` rejects unknown strings (including `"custom"`) with a clean `InvalidParameter` error and handles case/whitespace correctly. No panicking `unwrap`/`expect` in any of the new non-test code paths.

Two warnings concern latent correctness gaps that do not affect any current exercised path but will silently misbehave under plausible future use or adversarial input. Two info items note documentation and test gaps.

---

## Warnings

### WR-01: Negative `top_k` silently wraps to `usize::MAX`, bypasses intended validation

**File:** `crates/anofox-fcst-ffi/src/lib.rs:3483`, `crates/anofox-fcst-ffi/src/lib.rs:3784`, `crates/anofox-fcst-ffi/src/lib.rs:4201`

**Issue:** All three FFI sites cast `opts.ensemble_top_k` (type `c_int`) to `usize` with a bare `as usize`. On 64-bit targets, a negative value such as `-1i32` becomes `18446744073709551615usize` (`usize::MAX`). The check in `forecast.rs:780` only guards `== 0`, so the giant value flows into `AutoEnsembleConfig { top_k: usize::MAX }`.

The crate's `auto.rs:204` saves the program: `let top_k = self.config.top_k.min(candidates.len())` clamps to `candidates.len()` (at most 3), so no panic or memory unsafety occurs. However the user receives no diagnostic — a negative `top_k` silently acts identically to "use all available models." The pre-existing `garch_p`/`garch_q` fields share this pattern but the GARCH crate similarly guards the order internally; the difference here is that `top_k` is a user-visible tuning parameter whose meaning (count) makes a negative value unambiguously wrong.

Note: this is a bit-identical Rust-defined operation (not undefined behaviour), but the silent wrong-result behaviour constitutes a correctness issue.

**Fix:** Guard in the Rust FFI layer before the `as usize` conversion:

```rust
// In each of the three FFI sites (anofox_ts_forecast, anofox_ts_forecast_exog, build_core_options):
let ensemble_top_k = if opts.ensemble_top_k < 0 {
    return Err(ForecastError::InvalidParameter {
        param: "ensemble_top_k".to_string(),
        value: opts.ensemble_top_k.to_string(),
        reason: "top_k must be >= 0 (0 = default 3)".to_string(),
    });
} else {
    opts.ensemble_top_k as usize
};
```

Alternatively, clamp silently (consistent with existing `garch_p` precedent): `opts.ensemble_top_k.max(0) as usize`.

---

### WR-02: `forecast_with_model` silently ignores `ensemble_top_k`/`ensemble_method` for `AutoEnsemble`

**File:** `crates/anofox-fcst-core/src/forecast.rs:1147`

**Issue:** `forecast_with_model` is a shared dispatcher that does **not** receive `ensemble_top_k` or `ensemble_method` as parameters. Its `AutoEnsemble` arm hardcodes both:

```rust
// Line 1147 — params NOT forwarded from caller's options:
ModelType::AutoEnsemble => forecast_auto_ensemble(values, horizon, 3, None, period),
```

`forecast_with_model` is currently called from two places:

1. The fallback branch of `forecast_with_exog` (line 961) when the model does not support exogenous variables — which is the case for `AutoEnsemble` (not in the `supports_exog` match). If a caller passes a `ForecastOptionsExog` with `ensemble_top_k=2` and `combination_method="median"`, the exog path silently uses `top_k=3, method=mean` instead. The current C++ callers of `anofox_ts_forecast_exog` (`ts_forecast.cpp`) do not exercise this (they set `model="auto"`), so no current code path hits this, but any future exog-aware call with `AutoEnsemble` would silently use the wrong parameters.

2. The `forecast_inspect` function does NOT call `forecast_with_model`; it falls through to an `InvalidModel` error for `AutoEnsemble`. So no issue there.

The risk is that the bug is invisible until a user passes exog data with an `AutoEnsemble` call from the lower-level `anofox_ts_forecast_exog` FFI entry point.

**Fix:** Thread the ensemble parameters through `forecast_with_model`, or split the `AutoEnsemble` dispatch out of `forecast_with_model` and handle it directly in `forecast_with_exog`'s fallback branch using the caller's `options`:

```rust
// In forecast_with_exog's else branch (lines 958–972), replace the shared call:
} else {
    match options.model {
        ModelType::AutoEnsemble => forecast_auto_ensemble(
            &clean_values,
            options.horizon,
            if options.ensemble_top_k == 0 { 3 } else { options.ensemble_top_k },
            options.ensemble_method.as_deref(),
            period,
        ),
        _ => forecast_with_model(
            &clean_values,
            options.horizon,
            options.model,
            period,
            options.window,
            &options.seasonal_periods,
            options.model_pool.as_deref(),
            options.laplace_variant.unwrap_or_default(),
            options.laplace_seasonal_batch_init,
            options.confidence_level,
        ),
    }
```

---

## Info

### IN-01: `Stacking { folds: 2 }` is hardcoded and undocumented to the user

**File:** `crates/anofox-fcst-core/src/forecast.rs:2523`

**Issue:** `parse_combination_method` maps `"stacking"` to `CombinationMethod::Stacking { folds: 2 }`, but this choice is invisible to the user. Neither the SQL example's parameter table (lines 8–17 of `autoensemble.sql`), the doc comment on `parse_combination_method`, nor the `ForecastOptions.ensemble_method` field doc mentions that stacking uses 2-fold cross-validation internally. Users who expect standard 5-fold or leave-one-out stacking have no way to know or change this.

**Fix:** Add a parenthetical note in the user-visible parameter documentation:

```sql
-- 'stacking'        — ridge-stacking weights (2-fold CV, fixed; no config)
```

And in `parse_combination_method`'s doc comment:

```rust
/// `"stacking"` maps to `CombinationMethod::Stacking { folds: 2 }`.
/// The fold count is not user-configurable in Phase 4.
```

---

### IN-02: No unit tests for `parse_combination_method` or `forecast_auto_ensemble` in `forecast.rs`

**File:** `crates/anofox-fcst-core/src/forecast.rs` (test module, lines 3424+)

**Issue:** The new private functions `parse_combination_method` and `forecast_auto_ensemble` have no inline tests in `forecast.rs`. Upstream crate tests cover `AutoEnsemble::fit` directly (in `anofox-forecast-0.15.3`), but the following critical paths in the FFI layer's own code are untested:

- Unknown method string rejection (e.g., `parse_combination_method(Some("custom"))` returns `Err`; `parse_combination_method(Some("unknown_xyz"))` returns `Err`)
- `"stacking"` and `"horizon_adaptive"` aliases parse correctly (they are the most likely aliases to drift if the upstream crate renames variants)
- `ensemble_top_k == 0` → default-3 logic in `forecast()` (line 780)
- Integration: `forecast()` with `ModelType::AutoEnsemble` returns `Ok` on a valid series

The SQL example exercises these end-to-end, but the Rust unit test layer provides faster feedback during `cargo test`.

**Fix:** Add a `#[cfg(test)]` block in `forecast.rs` testing at minimum:

```rust
#[test]
fn parse_combination_method_rejects_unknown() {
    assert!(parse_combination_method(Some("custom")).is_err());
    assert!(parse_combination_method(Some("bad_value")).is_err());
}

#[test]
fn parse_combination_method_accepts_all_methods() {
    for m in &["mean", "median", "weighted_mse", "inverse_aic", "stacking", "horizon_adaptive"] {
        assert!(parse_combination_method(Some(m)).is_ok(), "failed for: {}", m);
    }
}

#[test]
fn forecast_auto_ensemble_basic() {
    let values: Vec<Option<f64>> = (0..60).map(|i| Some(10.0 + i as f64 * 0.5)).collect();
    let result = forecast(&values, &ForecastOptions {
        model: ModelType::AutoEnsemble,
        horizon: 5,
        ensemble_top_k: 0,  // → default 3
        ..Default::default()
    });
    assert!(result.is_ok());
    assert_eq!(result.unwrap().point.len(), 5);
}
```

---

_Reviewed: 2026-08-30T12:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
