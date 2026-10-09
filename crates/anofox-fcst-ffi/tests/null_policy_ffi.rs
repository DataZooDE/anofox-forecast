//! FFI-ABI proof that metric validity bitmasks drive the pairwise-complete
//! missing-data policy (Phase 12 D-02, plan 12-01/12-02).
//!
//! Each test calls the exact `extern "C"` FFI function with the exact bitmask
//! layout the C++ `ListDoubleReader` produces: one `u64` word per 64 elements,
//! bit `i` (LSB-first within the word) set iff element `i` is valid.

use anofox_fcst_ffi::types::AnofoxError;
use anofox_fcst_ffi::{anofox_ts_mae, ErrorCode};

/// Build a validity bitmask word (LSB-first) from a slice of bools.
fn bitmask(valid: &[bool]) -> u64 {
    let mut word: u64 = 0;
    for (i, &v) in valid.iter().enumerate() {
        if v {
            word |= 1u64 << i;
        }
    }
    word
}

#[test]
fn mae_drops_incomplete_pairs_via_validity_bitmask() {
    // actual = [1, NULL, 3, 4] -> validity 0b1101 (bit1=0)
    // forecast = [1, 2, NULL, 4] -> validity 0b1011 (bit2=0)
    let actual = [1.0, 0.0, 3.0, 4.0];
    let actual_validity = bitmask(&[true, false, true, true]);
    let forecast = [1.0, 2.0, 0.0, 4.0];
    let forecast_validity = bitmask(&[true, true, false, true]);

    let mut result = 0.0f64;
    let mut error = AnofoxError::default();
    let success = unsafe {
        anofox_ts_mae(
            actual.as_ptr(),
            &actual_validity,
            actual.len(),
            forecast.as_ptr(),
            &forecast_validity,
            forecast.len(),
            &mut result,
            &mut error,
        )
    };
    assert!(success, "expected success, error: {:?}", error.code);
    assert!((result - 0.0).abs() < 1e-12, "expected 0.0, got {result}");
}

#[test]
fn mae_nan_for_null_null_validity_pointers_matches_bitmask_path() {
    // ts_metrics_native.cpp path: values already NaN-for-NULL, validity pointers null.
    let actual = [1.0, f64::NAN, 3.0, 4.0];
    let forecast = [1.0, 2.0, f64::NAN, 4.0];

    let mut result = 0.0f64;
    let mut error = AnofoxError::default();
    let success = unsafe {
        anofox_ts_mae(
            actual.as_ptr(),
            std::ptr::null(),
            actual.len(),
            forecast.as_ptr(),
            std::ptr::null(),
            forecast.len(),
            &mut result,
            &mut error,
        )
    };
    assert!(success, "expected success, error: {:?}", error.code);
    assert!((result - 0.0).abs() < 1e-12, "expected 0.0, got {result}");
}

#[test]
fn mae_no_complete_pair_is_insufficient_data() {
    // actual = [NULL, 1], forecast = [1, NULL] -> no index where both are valid.
    let actual = [0.0, 1.0];
    let actual_validity = bitmask(&[false, true]);
    let forecast = [1.0, 0.0];
    let forecast_validity = bitmask(&[true, false]);

    let mut result = 0.0f64;
    let mut error = AnofoxError::default();
    let success = unsafe {
        anofox_ts_mae(
            actual.as_ptr(),
            &actual_validity,
            actual.len(),
            forecast.as_ptr(),
            &forecast_validity,
            forecast.len(),
            &mut result,
            &mut error,
        )
    };
    assert!(!success);
    assert_eq!(error.code, ErrorCode::InsufficientData);
}

#[test]
fn mae_length_mismatch_is_rejected() {
    let actual = [1.0, 2.0, 3.0];
    let forecast = [1.0, 2.0];

    let mut result = 0.0f64;
    let mut error = AnofoxError::default();
    let success = unsafe {
        anofox_ts_mae(
            actual.as_ptr(),
            std::ptr::null(),
            actual.len(),
            forecast.as_ptr(),
            std::ptr::null(),
            forecast.len(),
            &mut result,
            &mut error,
        )
    };
    assert!(!success);
}
