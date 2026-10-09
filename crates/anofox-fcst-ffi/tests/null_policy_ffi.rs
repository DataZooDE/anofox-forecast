//! FFI-ABI proof that metric validity bitmasks drive the pairwise-complete
//! missing-data policy (Phase 12 D-02, plan 12-01/12-02).
//!
//! Each test calls the exact `extern "C"` FFI function with the exact bitmask
//! layout the C++ `ListDoubleReader` produces: one `u64` word per 64 elements,
//! bit `i` (LSB-first within the word) set iff element `i` is valid.

use anofox_fcst_ffi::types::AnofoxError;
use anofox_fcst_ffi::{
    anofox_ts_bias, anofox_ts_coverage, anofox_ts_mae, anofox_ts_mase, anofox_ts_mqloss,
    anofox_ts_mse, anofox_ts_quantile_loss, anofox_ts_r2, anofox_ts_rmae, anofox_ts_rmse,
    anofox_ts_series_gate, ErrorCode,
};

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

/// Decode an `AnofoxError::message` fixed `[c_char; 256]` buffer to a `String`
/// (NUL-terminated, ASCII/UTF-8 message text set by `set_error`).
fn error_message(error: &AnofoxError) -> String {
    let bytes: Vec<u8> = error
        .message
        .iter()
        .take_while(|&&b| b != 0)
        .map(|&b| b as u8)
        .collect();
    String::from_utf8_lossy(&bytes).into_owned()
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
fn mse_rmse_bias_drop_incomplete_pairs() {
    // mse([1,N,3,4],[2,2,N,6]) = 2.5; rmse = sqrt(2.5); bias = 1.5 (forecast - actual).
    let actual = [1.0, 0.0, 3.0, 4.0];
    let actual_validity = bitmask(&[true, false, true, true]);
    let forecast = [2.0, 2.0, 0.0, 6.0];
    let forecast_validity = bitmask(&[true, true, false, true]);

    let mut mse_result = 0.0f64;
    let mut error = AnofoxError::default();
    assert!(unsafe {
        anofox_ts_mse(
            actual.as_ptr(),
            &actual_validity,
            actual.len(),
            forecast.as_ptr(),
            &forecast_validity,
            forecast.len(),
            &mut mse_result,
            &mut error,
        )
    });
    assert!((mse_result - 2.5).abs() < 1e-12, "mse={mse_result}");

    let mut rmse_result = 0.0f64;
    assert!(unsafe {
        anofox_ts_rmse(
            actual.as_ptr(),
            &actual_validity,
            actual.len(),
            forecast.as_ptr(),
            &forecast_validity,
            forecast.len(),
            &mut rmse_result,
            &mut error,
        )
    });
    assert!(
        (rmse_result - 2.5f64.sqrt()).abs() < 1e-12,
        "rmse={rmse_result}"
    );

    let mut bias_result = 0.0f64;
    assert!(unsafe {
        anofox_ts_bias(
            actual.as_ptr(),
            &actual_validity,
            actual.len(),
            forecast.as_ptr(),
            &forecast_validity,
            forecast.len(),
            &mut bias_result,
            &mut error,
        )
    });
    assert!((bias_result - 1.5).abs() < 1e-12, "bias={bias_result}");
}

#[test]
fn mape_and_r2_drop_incomplete_pairs() {
    // mape([1,N,4,2],[2,2,N,3]) = 75.0
    let actual = [1.0, 0.0, 4.0, 2.0];
    let actual_validity = bitmask(&[true, false, true, true]);
    let forecast = [2.0, 2.0, 0.0, 3.0];
    let forecast_validity = bitmask(&[true, true, false, true]);

    let mut mape_result = 0.0f64;
    let mut error = AnofoxError::default();
    assert!(unsafe {
        anofox_fcst_ffi::anofox_ts_mape(
            actual.as_ptr(),
            &actual_validity,
            actual.len(),
            forecast.as_ptr(),
            &forecast_validity,
            forecast.len(),
            &mut mape_result,
            &mut error,
        )
    });
    assert!((mape_result - 75.0).abs() < 1e-9, "mape={mape_result}");

    // r2([1,2,3,N],[1,2,4,9]) = 0.5
    let actual2 = [1.0, 2.0, 3.0, 0.0];
    let actual2_validity = bitmask(&[true, true, true, false]);
    let forecast2 = [1.0, 2.0, 4.0, 9.0];

    let mut r2_result = 0.0f64;
    assert!(unsafe {
        anofox_ts_r2(
            actual2.as_ptr(),
            &actual2_validity,
            actual2.len(),
            forecast2.as_ptr(),
            std::ptr::null(),
            forecast2.len(),
            &mut r2_result,
            &mut error,
        )
    });
    assert!((r2_result - 0.5).abs() < 1e-12, "r2={r2_result}");
}

#[test]
fn mase_and_rmae_use_complete_cases_across_three_columns() {
    // mase([1,2,3,4],[1,2,3,5],[2,N,4,5]) = 1/3; rmae same arrays = 1/3.
    let actual = [1.0, 2.0, 3.0, 4.0];
    let forecast = [1.0, 2.0, 3.0, 5.0];
    let baseline = [2.0, 0.0, 4.0, 5.0];
    let baseline_validity = bitmask(&[true, false, true, true]);

    let mut mase_result = 0.0f64;
    let mut error = AnofoxError::default();
    assert!(unsafe {
        anofox_ts_mase(
            actual.as_ptr(),
            std::ptr::null(),
            actual.len(),
            forecast.as_ptr(),
            std::ptr::null(),
            forecast.len(),
            baseline.as_ptr(),
            &baseline_validity,
            baseline.len(),
            &mut mase_result,
            &mut error,
        )
    });
    assert!(
        (mase_result - 1.0 / 3.0).abs() < 1e-12,
        "mase={mase_result}"
    );

    let mut rmae_result = 0.0f64;
    assert!(unsafe {
        anofox_ts_rmae(
            actual.as_ptr(),
            std::ptr::null(),
            actual.len(),
            forecast.as_ptr(),
            std::ptr::null(),
            forecast.len(),
            baseline.as_ptr(),
            &baseline_validity,
            baseline.len(),
            &mut rmae_result,
            &mut error,
        )
    });
    assert!(
        (rmae_result - 1.0 / 3.0).abs() < 1e-12,
        "rmae={rmae_result}"
    );
}

#[test]
fn coverage_and_quantile_loss_drop_incomplete_cases() {
    // coverage([1,2,3],[0,N,4],[2,3,5]) = 0.5
    let actual = [1.0, 2.0, 3.0];
    let lower = [0.0, 0.0, 4.0];
    let lower_validity = bitmask(&[true, false, true]);
    let upper = [2.0, 3.0, 5.0];

    let mut coverage_result = 0.0f64;
    let mut error = AnofoxError::default();
    assert!(unsafe {
        anofox_ts_coverage(
            actual.as_ptr(),
            std::ptr::null(),
            lower.as_ptr(),
            &lower_validity,
            upper.as_ptr(),
            std::ptr::null(),
            actual.len(),
            &mut coverage_result,
            &mut error,
        )
    });
    assert!(
        (coverage_result - 0.5).abs() < 1e-12,
        "coverage={coverage_result}"
    );

    // quantile_loss([1,N,3],[2,2,2],0.5) = 0.5
    let actual2 = [1.0, 0.0, 3.0];
    let actual2_validity = bitmask(&[true, false, true]);
    let forecast2 = [2.0, 2.0, 2.0];

    let mut ql_result = 0.0f64;
    assert!(unsafe {
        anofox_ts_quantile_loss(
            actual2.as_ptr(),
            &actual2_validity,
            actual2.len(),
            forecast2.as_ptr(),
            std::ptr::null(),
            forecast2.len(),
            0.5,
            &mut ql_result,
            &mut error,
        )
    });
    assert!((ql_result - 0.5).abs() < 1e-12, "quantile_loss={ql_result}");
}

#[test]
fn mqloss_drops_incomplete_cases_per_level() {
    // mqloss(actual [1,2,N], quantiles [[0,1,2],[2,3,4]], levels [0.1,0.9]) ~ 0.1
    let actual = [1.0, 2.0, 0.0];
    let actual_validity = bitmask(&[true, true, false]);
    let level0 = [0.0, 1.0, 2.0];
    let level1 = [2.0, 3.0, 4.0];
    let levels = [0.1, 0.9];
    let quantiles: [*const f64; 2] = [level0.as_ptr(), level1.as_ptr()];

    let mut mqloss_result = 0.0f64;
    let mut error = AnofoxError::default();
    assert!(unsafe {
        anofox_ts_mqloss(
            actual.as_ptr(),
            &actual_validity,
            actual.len(),
            quantiles.as_ptr(),
            std::ptr::null(),
            2,
            levels.as_ptr(),
            &mut mqloss_result,
            &mut error,
        )
    });
    assert!((mqloss_result - 0.1).abs() < 1e-9, "mqloss={mqloss_result}");
}

#[test]
fn every_metric_with_null_validity_and_complete_data_matches_core_slice() {
    // No behaviour change for complete data: null validity pointers everywhere.
    let actual = [1.0, 2.0, 3.0, 4.0];
    let forecast = [1.1, 2.1, 2.9, 4.1];

    let mut mae_result = 0.0f64;
    let mut error = AnofoxError::default();
    assert!(unsafe {
        anofox_ts_mae(
            actual.as_ptr(),
            std::ptr::null(),
            actual.len(),
            forecast.as_ptr(),
            std::ptr::null(),
            forecast.len(),
            &mut mae_result,
            &mut error,
        )
    });
    let expected: f64 = actual
        .iter()
        .zip(forecast.iter())
        .map(|(a, f)| (a - f).abs())
        .sum::<f64>()
        / actual.len() as f64;
    assert!((mae_result - expected).abs() < 1e-12);
}

#[test]
fn gate_trims_leading_and_trailing_missing() {
    // [0,0,1,2,0] validity 0b01100 -> valid at indices 2,3 -> start 2, length 2.
    let values = [0.0, 0.0, 1.0, 2.0, 0.0];
    let validity = bitmask(&[false, false, true, true, false]);

    let mut out_start: usize = 0;
    let mut out_length: usize = 0;
    let mut error = AnofoxError::default();
    let success = unsafe {
        anofox_ts_series_gate(
            values.as_ptr(),
            &validity,
            values.len(),
            &mut out_start,
            &mut out_length,
            &mut error,
        )
    };
    assert!(success, "expected success, error: {:?}", error.code);
    assert_eq!(out_start, 2);
    assert_eq!(out_length, 2);
}

#[test]
fn gate_interior_null_errors_with_one_based_position() {
    // [1,2,0,4] validity 0b1011 (bit2=0) -> interior NULL at original position 3.
    let values = [1.0, 2.0, 0.0, 4.0];
    let validity = bitmask(&[true, true, false, true]);

    let mut out_start: usize = 0;
    let mut out_length: usize = 0;
    let mut error = AnofoxError::default();
    let success = unsafe {
        anofox_ts_series_gate(
            values.as_ptr(),
            &validity,
            values.len(),
            &mut out_start,
            &mut out_length,
            &mut error,
        )
    };
    assert!(!success);
    assert_eq!(error.code, ErrorCode::InvalidInput);
    let msg = error_message(&error);
    assert!(
        msg.contains("interior NULL at position 3"),
        "message: {msg}"
    );
}

#[test]
fn gate_all_invalid_or_zero_length_returns_empty_not_error() {
    // All-invalid, length 3 -> success, length 0.
    let values = [0.0, 0.0, 0.0];
    let validity = bitmask(&[false, false, false]);
    let mut out_start: usize = 0;
    let mut out_length: usize = 0;
    let mut error = AnofoxError::default();
    let success = unsafe {
        anofox_ts_series_gate(
            values.as_ptr(),
            &validity,
            values.len(),
            &mut out_start,
            &mut out_length,
            &mut error,
        )
    };
    assert!(success, "expected success, error: {:?}", error.code);
    assert_eq!(out_length, 0);

    // Zero length, values pointer null -> success, length 0.
    let mut out_start2: usize = 0;
    let mut out_length2: usize = 0;
    let success2 = unsafe {
        anofox_ts_series_gate(
            std::ptr::null(),
            std::ptr::null(),
            0,
            &mut out_start2,
            &mut out_length2,
            &mut error,
        )
    };
    assert!(success2, "expected success, error: {:?}", error.code);
    assert_eq!(out_length2, 0);
}

#[test]
fn gate_null_validity_with_nan_interior_errors_with_position() {
    // [5, NaN, 7], null validity pointer -> NaN at index 1 is "interior NULL at position 2".
    let values = [5.0, f64::NAN, 7.0];
    let mut out_start: usize = 0;
    let mut out_length: usize = 0;
    let mut error = AnofoxError::default();
    let success = unsafe {
        anofox_ts_series_gate(
            values.as_ptr(),
            std::ptr::null(),
            values.len(),
            &mut out_start,
            &mut out_length,
            &mut error,
        )
    };
    assert!(!success);
    assert_eq!(error.code, ErrorCode::InvalidInput);
    let msg = error_message(&error);
    assert!(
        msg.contains("interior NULL at position 2"),
        "message: {msg}"
    );
}

#[test]
fn gate_null_validity_no_nan_returns_full_range() {
    let values = [1.0, 2.0, 3.0, 4.0];
    let mut out_start: usize = 0;
    let mut out_length: usize = 0;
    let mut error = AnofoxError::default();
    let success = unsafe {
        anofox_ts_series_gate(
            values.as_ptr(),
            std::ptr::null(),
            values.len(),
            &mut out_start,
            &mut out_length,
            &mut error,
        )
    };
    assert!(success, "expected success, error: {:?}", error.code);
    assert_eq!(out_start, 0);
    assert_eq!(out_length, values.len());
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
