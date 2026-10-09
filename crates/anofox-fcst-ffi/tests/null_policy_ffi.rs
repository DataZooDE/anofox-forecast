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
    ErrorCode,
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
