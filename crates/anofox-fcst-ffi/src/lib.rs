//! FFI boundary layer for anofox-forecast DuckDB extension.
//!
//! This crate provides C-compatible functions that can be called from the
//! C++ DuckDB extension wrapper.

pub mod types;

use libc::{c_char, c_double, c_int, free, malloc, size_t};
use std::ffi::CStr;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

pub use types::*;

// ============================================================================
// Helper Functions
// ============================================================================

/// Build a series with NULL handling from raw pointers.
unsafe fn build_series(
    data: *const c_double,
    validity: *const u64,
    length: size_t,
) -> Vec<Option<f64>> {
    let data_slice = std::slice::from_raw_parts(data, length);

    if validity.is_null() {
        data_slice.iter().map(|&v| Some(v)).collect()
    } else {
        let validity_len = length.div_ceil(64);
        let validity_slice = std::slice::from_raw_parts(validity, validity_len);

        (0..length)
            .map(|i| {
                let word = validity_slice[i / 64];
                let is_valid = (word >> (i % 64)) & 1 == 1;
                if is_valid {
                    Some(data_slice[i])
                } else {
                    None
                }
            })
            .collect()
    }
}

/// Build a Vec<f64> from raw pointers, treating NULLs as NaN.
#[allow(dead_code)]
unsafe fn build_values(data: *const c_double, validity: *const u64, length: size_t) -> Vec<f64> {
    let data_slice = std::slice::from_raw_parts(data, length);

    if validity.is_null() {
        data_slice.to_vec()
    } else {
        let validity_len = length.div_ceil(64);
        let validity_slice = std::slice::from_raw_parts(validity, validity_len);

        (0..length)
            .map(|i| {
                let word = validity_slice[i / 64];
                let is_valid = (word >> (i % 64)) & 1 == 1;
                if is_valid {
                    data_slice[i]
                } else {
                    f64::NAN
                }
            })
            .collect()
    }
}

/// Allocate a double array using libc malloc.
unsafe fn alloc_double_array(n: size_t) -> *mut c_double {
    if n == 0 {
        return ptr::null_mut();
    }
    malloc(n * std::mem::size_of::<c_double>()) as *mut c_double
}

/// Copy a Rust Vec<f64> to a malloc'd C array.
unsafe fn vec_to_c_array(vec: &[f64]) -> *mut c_double {
    let ptr = alloc_double_array(vec.len());
    if !ptr.is_null() && !vec.is_empty() {
        ptr::copy_nonoverlapping(vec.as_ptr(), ptr, vec.len());
    }
    ptr
}

/// Copy a string to a fixed-size char buffer.
fn copy_string_to_buffer(s: &str, buffer: &mut [c_char]) {
    let bytes = s.as_bytes();
    let len = bytes.len().min(buffer.len() - 1);
    for (i, &b) in bytes[..len].iter().enumerate() {
        buffer[i] = b as c_char;
    }
    buffer[len] = 0;
}

// ============================================================================
// Statistics Functions
// ============================================================================

/// Compute time series statistics.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_stats(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    out_result: *mut TsStatsResult,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if values.is_null() || out_result.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    if length == 0 {
        *out_result = TsStatsResult::default();
        return true;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let series = build_series(values, validity, length);
        anofox_fcst_core::compute_ts_stats(&series)
    }));

    match result {
        Ok(Ok(stats)) => {
            *out_result = stats.into();
            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::ComputationError, &e.to_string());
            }
            false
        }
        Err(_) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::PanicCaught, "Panic in Rust code");
            }
            false
        }
    }
}

// ============================================================================
// Metric Functions
// ============================================================================

// Helper for 2-argument metric functions
unsafe fn impl_metric_2arg<F>(
    actual: *const c_double,
    actual_len: size_t,
    forecast: *const c_double,
    forecast_len: size_t,
    out_result: *mut c_double,
    out_error: *mut AnofoxError,
    core_fn: F,
) -> bool
where
    F: FnOnce(&[f64], &[f64]) -> Result<f64, anofox_fcst_core::ForecastError>
        + std::panic::UnwindSafe,
{
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if actual.is_null() || forecast.is_null() || out_result.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let actual_vec = std::slice::from_raw_parts(actual, actual_len).to_vec();
        let forecast_vec = std::slice::from_raw_parts(forecast, forecast_len).to_vec();
        core_fn(&actual_vec, &forecast_vec)
    }));

    match result {
        Ok(Ok(value)) => {
            *out_result = value;
            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::ComputationError, &e.to_string());
            }
            false
        }
        Err(_) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::PanicCaught, "Panic in Rust code");
            }
            false
        }
    }
}

/// Mean Absolute Error
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_mae(
    actual: *const c_double,
    actual_len: size_t,
    forecast: *const c_double,
    forecast_len: size_t,
    out_result: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    impl_metric_2arg(
        actual,
        actual_len,
        forecast,
        forecast_len,
        out_result,
        out_error,
        anofox_fcst_core::mae,
    )
}

/// Mean Squared Error
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_mse(
    actual: *const c_double,
    actual_len: size_t,
    forecast: *const c_double,
    forecast_len: size_t,
    out_result: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    impl_metric_2arg(
        actual,
        actual_len,
        forecast,
        forecast_len,
        out_result,
        out_error,
        anofox_fcst_core::mse,
    )
}

/// Root Mean Squared Error
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_rmse(
    actual: *const c_double,
    actual_len: size_t,
    forecast: *const c_double,
    forecast_len: size_t,
    out_result: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    impl_metric_2arg(
        actual,
        actual_len,
        forecast,
        forecast_len,
        out_result,
        out_error,
        anofox_fcst_core::rmse,
    )
}

/// Mean Absolute Percentage Error
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_mape(
    actual: *const c_double,
    actual_len: size_t,
    forecast: *const c_double,
    forecast_len: size_t,
    out_result: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    impl_metric_2arg(
        actual,
        actual_len,
        forecast,
        forecast_len,
        out_result,
        out_error,
        anofox_fcst_core::mape,
    )
}

/// Symmetric Mean Absolute Percentage Error
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_smape(
    actual: *const c_double,
    actual_len: size_t,
    forecast: *const c_double,
    forecast_len: size_t,
    out_result: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    impl_metric_2arg(
        actual,
        actual_len,
        forecast,
        forecast_len,
        out_result,
        out_error,
        anofox_fcst_core::smape,
    )
}

/// R-squared (Coefficient of Determination)
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_r2(
    actual: *const c_double,
    actual_len: size_t,
    forecast: *const c_double,
    forecast_len: size_t,
    out_result: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    impl_metric_2arg(
        actual,
        actual_len,
        forecast,
        forecast_len,
        out_result,
        out_error,
        anofox_fcst_core::r2,
    )
}

/// Bias (Mean Error)
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_bias(
    actual: *const c_double,
    actual_len: size_t,
    forecast: *const c_double,
    forecast_len: size_t,
    out_result: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    impl_metric_2arg(
        actual,
        actual_len,
        forecast,
        forecast_len,
        out_result,
        out_error,
        anofox_fcst_core::bias,
    )
}

/// Relative MAE comparing two models.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_rmae(
    actual: *const c_double,
    actual_len: size_t,
    pred1: *const c_double,
    pred1_len: size_t,
    pred2: *const c_double,
    pred2_len: size_t,
    out_result: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if actual.is_null() || pred1.is_null() || pred2.is_null() || out_result.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let actual_vec = std::slice::from_raw_parts(actual, actual_len).to_vec();
        let pred1_vec = std::slice::from_raw_parts(pred1, pred1_len).to_vec();
        let pred2_vec = std::slice::from_raw_parts(pred2, pred2_len).to_vec();
        anofox_fcst_core::rmae(&actual_vec, &pred1_vec, &pred2_vec)
    }));

    match result {
        Ok(Ok(value)) => {
            *out_result = value;
            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::ComputationError, &e.to_string());
            }
            false
        }
        Err(_) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::PanicCaught, "Panic in Rust code");
            }
            false
        }
    }
}

/// Mean Absolute Scaled Error.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_mase(
    actual: *const c_double,
    actual_len: size_t,
    forecast: *const c_double,
    forecast_len: size_t,
    baseline: *const c_double,
    baseline_len: size_t,
    out_result: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if actual.is_null() || forecast.is_null() || baseline.is_null() || out_result.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let actual_vec = std::slice::from_raw_parts(actual, actual_len).to_vec();
        let forecast_vec = std::slice::from_raw_parts(forecast, forecast_len).to_vec();
        let baseline_vec = std::slice::from_raw_parts(baseline, baseline_len).to_vec();
        anofox_fcst_core::mase(&actual_vec, &forecast_vec, &baseline_vec)
    }));

    match result {
        Ok(Ok(value)) => {
            *out_result = value;
            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::ComputationError, &e.to_string());
            }
            false
        }
        Err(_) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::PanicCaught, "Panic in Rust code");
            }
            false
        }
    }
}

/// Quantile loss function.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_quantile_loss(
    actual: *const c_double,
    actual_len: size_t,
    forecast: *const c_double,
    forecast_len: size_t,
    quantile: c_double,
    out_result: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if actual.is_null() || forecast.is_null() || out_result.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let actual_vec = std::slice::from_raw_parts(actual, actual_len).to_vec();
        let forecast_vec = std::slice::from_raw_parts(forecast, forecast_len).to_vec();
        anofox_fcst_core::quantile_loss(&actual_vec, &forecast_vec, quantile)
    }));

    match result {
        Ok(Ok(value)) => {
            *out_result = value;
            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::ComputationError, &e.to_string());
            }
            false
        }
        Err(_) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::PanicCaught, "Panic in Rust code");
            }
            false
        }
    }
}

/// Multi-quantile loss function.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
/// quantiles is a 2D array: n_levels arrays, each of length actual_len.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_mqloss(
    actual: *const c_double,
    actual_len: size_t,
    quantiles: *const *const c_double, // Array of pointers to quantile forecast arrays
    n_levels: size_t,
    levels: *const c_double,
    out_result: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if actual.is_null() || quantiles.is_null() || levels.is_null() || out_result.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    if n_levels == 0 {
        if !out_error.is_null() {
            (*out_error).set_error(
                ErrorCode::InvalidInput,
                "Must have at least one quantile level",
            );
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let actual_vec = std::slice::from_raw_parts(actual, actual_len).to_vec();
        let levels_vec = std::slice::from_raw_parts(levels, n_levels).to_vec();

        // Build Vec<Vec<f64>> from the 2D array
        let mut forecasts_vec: Vec<Vec<f64>> = Vec::with_capacity(n_levels);
        for i in 0..n_levels {
            let quantile_ptr = *quantiles.add(i);
            if quantile_ptr.is_null() {
                return Err(anofox_fcst_core::ForecastError::InvalidInput(format!(
                    "Null pointer at quantile index {}",
                    i
                )));
            }
            let quantile_vec = std::slice::from_raw_parts(quantile_ptr, actual_len).to_vec();
            forecasts_vec.push(quantile_vec);
        }

        anofox_fcst_core::mqloss(&actual_vec, &forecasts_vec, &levels_vec)
    }));

    match result {
        Ok(Ok(value)) => {
            *out_result = value;
            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::ComputationError, &e.to_string());
            }
            false
        }
        Err(_) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::PanicCaught, "Panic in Rust code");
            }
            false
        }
    }
}

/// Coverage of prediction intervals.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_coverage(
    actual: *const c_double,
    actual_len: size_t,
    lower: *const c_double,
    upper: *const c_double,
    out_result: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if actual.is_null() || lower.is_null() || upper.is_null() || out_result.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let actual_vec = std::slice::from_raw_parts(actual, actual_len).to_vec();
        let lower_vec = std::slice::from_raw_parts(lower, actual_len).to_vec();
        let upper_vec = std::slice::from_raw_parts(upper, actual_len).to_vec();
        anofox_fcst_core::coverage(&actual_vec, &lower_vec, &upper_vec)
    }));

    match result {
        Ok(Ok(value)) => {
            *out_result = value;
            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::ComputationError, &e.to_string());
            }
            false
        }
        Err(_) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::PanicCaught, "Panic in Rust code");
            }
            false
        }
    }
}

// ============================================================================
// Seasonality Functions
// ============================================================================

/// Detect seasonal periods in a time series.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_detect_seasonality(
    values: *const c_double,
    length: size_t,
    max_period: c_int,
    out_periods: *mut *mut c_int,
    out_n_periods: *mut size_t,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if values.is_null() || out_periods.is_null() || out_n_periods.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let values_vec = std::slice::from_raw_parts(values, length).to_vec();
        let max_p = if max_period > 0 {
            Some(max_period as usize)
        } else {
            None
        };
        anofox_fcst_core::detect_seasonality(&values_vec, max_p)
    }));

    match result {
        Ok(Ok(periods)) => {
            let n = periods.len();
            *out_n_periods = n;

            if n > 0 {
                let ptr = malloc(n * std::mem::size_of::<c_int>()) as *mut c_int;
                if ptr.is_null() {
                    if !out_error.is_null() {
                        (*out_error)
                            .set_error(ErrorCode::AllocationError, "Memory allocation failed");
                    }
                    return false;
                }
                for (i, &p) in periods.iter().enumerate() {
                    *ptr.add(i) = p;
                }
                *out_periods = ptr;
            } else {
                *out_periods = ptr::null_mut();
            }
            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::ComputationError, &e.to_string());
            }
            false
        }
        Err(_) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::PanicCaught, "Panic in Rust code");
            }
            false
        }
    }
}

/// Analyze seasonality in a time series.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
/// Note: timestamps parameter is for API compatibility but is ignored internally.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_analyze_seasonality(
    _timestamps: *const i64, // Ignored, for C++ API compatibility
    _timestamps_len: size_t, // Ignored
    values: *const c_double,
    length: size_t,
    max_period: c_int,
    out_result: *mut SeasonalityResult,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if values.is_null() || out_result.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let values_vec = std::slice::from_raw_parts(values, length).to_vec();
        let max_p = if max_period > 0 {
            Some(max_period as usize)
        } else {
            None
        };
        anofox_fcst_core::analyze_seasonality(&values_vec, max_p)
    }));

    match result {
        Ok(Ok(analysis)) => {
            let n = analysis.periods.len();

            // Allocate and copy detected_periods
            if n > 0 {
                let periods_ptr = malloc(n * std::mem::size_of::<c_int>()) as *mut c_int;

                if periods_ptr.is_null() {
                    if !out_error.is_null() {
                        (*out_error)
                            .set_error(ErrorCode::AllocationError, "Memory allocation failed");
                    }
                    return false;
                }

                for (i, &p) in analysis.periods.iter().enumerate() {
                    *periods_ptr.add(i) = p;
                }

                (*out_result).detected_periods = periods_ptr;
            } else {
                (*out_result).detected_periods = ptr::null_mut();
            }

            (*out_result).n_periods = n;
            (*out_result).primary_period = analysis.primary_period;
            (*out_result).seasonal_strength = analysis.seasonal_strength;
            (*out_result).trend_strength = analysis.trend_strength;

            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::ComputationError, &e.to_string());
            }
            false
        }
        Err(_) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::PanicCaught, "Panic in Rust code");
            }
            false
        }
    }
}

// ============================================================================
// Decomposition Functions
// ============================================================================

/// MSTL decomposition.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_mstl_decomposition(
    values: *const c_double,
    length: size_t,
    periods: *const c_int,
    n_periods: size_t,
    out_result: *mut MstlResult,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if values.is_null() || out_result.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let values_vec = std::slice::from_raw_parts(values, length).to_vec();
        let periods_vec: Vec<i32> = if periods.is_null() || n_periods == 0 {
            vec![]
        } else {
            std::slice::from_raw_parts(periods, n_periods).to_vec()
        };
        anofox_fcst_core::mstl_decompose(&values_vec, &periods_vec)
    }));

    match result {
        Ok(Ok(decomp)) => {
            let n = decomp.trend.len();
            (*out_result).n_observations = n;
            (*out_result).n_seasonal = decomp.seasonal.len();

            // Copy trend
            (*out_result).trend = vec_to_c_array(&decomp.trend);

            // Copy remainder
            (*out_result).remainder = vec_to_c_array(&decomp.remainder);

            // Copy seasonal periods
            if !decomp.periods.is_empty() {
                let periods_ptr =
                    malloc(decomp.periods.len() * std::mem::size_of::<c_int>()) as *mut c_int;
                for (i, &p) in decomp.periods.iter().enumerate() {
                    *periods_ptr.add(i) = p;
                }
                (*out_result).seasonal_periods = periods_ptr;
            } else {
                (*out_result).seasonal_periods = ptr::null_mut();
            }

            // Copy seasonal components
            if !decomp.seasonal.is_empty() {
                let comps_ptr = malloc(decomp.seasonal.len() * std::mem::size_of::<*mut c_double>())
                    as *mut *mut c_double;
                for (i, comp) in decomp.seasonal.iter().enumerate() {
                    *comps_ptr.add(i) = vec_to_c_array(comp);
                }
                (*out_result).seasonal_components = comps_ptr;
            } else {
                (*out_result).seasonal_components = ptr::null_mut();
            }

            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::ComputationError, &e.to_string());
            }
            false
        }
        Err(_) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::PanicCaught, "Panic in Rust code");
            }
            false
        }
    }
}

// ============================================================================
// Changepoint Functions
// ============================================================================

/// Detect changepoints using PELT algorithm.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_detect_changepoints(
    values: *const c_double,
    length: size_t,
    min_size: c_int,
    penalty: c_double,
    out_result: *mut ChangepointResult,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if values.is_null() || out_result.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let values_vec = std::slice::from_raw_parts(values, length).to_vec();
        let pen = if penalty > 0.0 { Some(penalty) } else { None };
        anofox_fcst_core::detect_changepoints(
            &values_vec,
            min_size.max(1) as usize,
            pen,
            anofox_fcst_core::CostFunction::L2,
        )
    }));

    match result {
        Ok(Ok(cp)) => {
            let n = cp.changepoints.len();
            (*out_result).n_changepoints = n;
            (*out_result).cost = cp.cost;

            if n > 0 {
                let ptr = malloc(n * std::mem::size_of::<size_t>()) as *mut size_t;
                for (i, &c) in cp.changepoints.iter().enumerate() {
                    *ptr.add(i) = c;
                }
                (*out_result).changepoints = ptr;
            } else {
                (*out_result).changepoints = ptr::null_mut();
            }

            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::ComputationError, &e.to_string());
            }
            false
        }
        Err(_) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::PanicCaught, "Panic in Rust code");
            }
            false
        }
    }
}

/// BOCPD changepoint detection.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_detect_changepoints_bocpd(
    values: *const c_double,
    length: size_t,
    hazard_lambda: c_double,
    include_probabilities: bool,
    out_result: *mut types::BocpdResult,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if values.is_null() || out_result.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let values_vec = std::slice::from_raw_parts(values, length).to_vec();
        let lambda = if hazard_lambda > 0.0 {
            hazard_lambda
        } else {
            250.0
        };
        anofox_fcst_core::detect_changepoints_bocpd(&values_vec, lambda, include_probabilities)
    }));

    match result {
        Ok(Ok(bocpd)) => {
            let n = bocpd.is_changepoint.len();
            (*out_result).n_points = n;

            // Allocate and copy is_changepoint
            let is_cp_ptr = malloc(n * std::mem::size_of::<bool>()) as *mut bool;
            for (i, &cp) in bocpd.is_changepoint.iter().enumerate() {
                *is_cp_ptr.add(i) = cp;
            }
            (*out_result).is_changepoint = is_cp_ptr;

            // Allocate and copy changepoint_probability
            let prob_ptr = malloc(n * std::mem::size_of::<c_double>()) as *mut c_double;
            for (i, &p) in bocpd.changepoint_probability.iter().enumerate() {
                *prob_ptr.add(i) = p;
            }
            (*out_result).changepoint_probability = prob_ptr;

            // Copy changepoint indices
            let n_cp = bocpd.changepoints.len();
            (*out_result).n_changepoints = n_cp;
            if n_cp > 0 {
                let idx_ptr = malloc(n_cp * std::mem::size_of::<size_t>()) as *mut size_t;
                for (i, &c) in bocpd.changepoints.iter().enumerate() {
                    *idx_ptr.add(i) = c;
                }
                (*out_result).changepoint_indices = idx_ptr;
            } else {
                (*out_result).changepoint_indices = ptr::null_mut();
            }

            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::ComputationError, &e.to_string());
            }
            false
        }
        Err(_) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::PanicCaught, "Panic in Rust code");
            }
            false
        }
    }
}

// ============================================================================
// Feature Functions
// ============================================================================

/// Extract time series features.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_features(
    values: *const c_double,
    length: size_t,
    out_result: *mut FeaturesResult,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if values.is_null() || out_result.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let values_vec = std::slice::from_raw_parts(values, length).to_vec();
        anofox_fcst_core::extract_features(&values_vec)
    }));

    match result {
        Ok(Ok(features)) => {
            let n = features.len();
            (*out_result).n_features = n;

            if n > 0 {
                let values_ptr = malloc(n * std::mem::size_of::<c_double>()) as *mut c_double;
                let names_ptr = malloc(n * std::mem::size_of::<*mut c_char>()) as *mut *mut c_char;

                let mut sorted: Vec<_> = features.into_iter().collect();
                sorted.sort_by(|a, b| a.0.cmp(&b.0));

                for (i, (name, value)) in sorted.into_iter().enumerate() {
                    *values_ptr.add(i) = value;

                    let name_len = name.len() + 1;
                    let name_ptr = malloc(name_len) as *mut c_char;
                    ptr::copy_nonoverlapping(name.as_ptr() as *const c_char, name_ptr, name.len());
                    *name_ptr.add(name.len()) = 0;
                    *names_ptr.add(i) = name_ptr;
                }

                (*out_result).features = values_ptr;
                (*out_result).feature_names = names_ptr;
            } else {
                (*out_result).features = ptr::null_mut();
                (*out_result).feature_names = ptr::null_mut();
            }

            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::ComputationError, &e.to_string());
            }
            false
        }
        Err(_) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::PanicCaught, "Panic in Rust code");
            }
            false
        }
    }
}

/// List available feature names.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_features_list(
    out_names: *mut *mut c_char,
    out_count: *mut size_t,
) -> bool {
    let names = anofox_fcst_core::list_features();
    let n = names.len();

    *out_count = n;

    if n > 0 {
        let names_ptr = malloc(n * std::mem::size_of::<*mut c_char>()) as *mut *mut c_char;

        for (i, name) in names.into_iter().enumerate() {
            let name_len = name.len() + 1;
            let name_ptr = malloc(name_len) as *mut c_char;
            ptr::copy_nonoverlapping(name.as_ptr() as *const c_char, name_ptr, name.len());
            *name_ptr.add(name.len()) = 0;
            *names_ptr.add(i) = name_ptr;
        }

        *out_names = names_ptr as *mut c_char;
    }

    true
}

// ============================================================================
// Forecast Functions
// ============================================================================

/// Generate forecasts for a time series.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_forecast(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    options: *const ForecastOptions,
    out_result: *mut ForecastResult,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if values.is_null() || options.is_null() || out_result.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let series = build_series(values, validity, length);
        let opts = &*options;

        // Parse model name
        let model_str = CStr::from_ptr(opts.model.as_ptr())
            .to_str()
            .unwrap_or("auto");

        let model_type: anofox_fcst_core::ModelType = model_str
            .parse()
            .unwrap_or(anofox_fcst_core::ModelType::AutoETS);

        let core_opts = anofox_fcst_core::ForecastOptions {
            model: model_type,
            horizon: opts.horizon as usize,
            confidence_level: opts.confidence_level,
            seasonal_period: opts.seasonal_period as usize,
            auto_detect_seasonality: opts.auto_detect_seasonality,
            include_fitted: opts.include_fitted,
            include_residuals: opts.include_residuals,
        };

        anofox_fcst_core::forecast(&series, &core_opts)
    }));

    match result {
        Ok(Ok(forecast)) => {
            let n_forecasts = forecast.point.len();
            (*out_result).n_forecasts = n_forecasts;

            // Copy point forecasts
            (*out_result).point_forecasts = vec_to_c_array(&forecast.point);
            (*out_result).lower_bounds = vec_to_c_array(&forecast.lower);
            (*out_result).upper_bounds = vec_to_c_array(&forecast.upper);

            // Copy fitted values
            if let Some(ref fitted) = forecast.fitted {
                (*out_result).fitted_values = vec_to_c_array(fitted);
                (*out_result).n_fitted = fitted.len();
            } else {
                (*out_result).fitted_values = ptr::null_mut();
                (*out_result).n_fitted = 0;
            }

            // Copy residuals
            if let Some(ref resid) = forecast.residuals {
                (*out_result).residuals = vec_to_c_array(resid);
            } else {
                (*out_result).residuals = ptr::null_mut();
            }

            // Copy model name
            copy_string_to_buffer(&forecast.model_name, &mut (*out_result).model_name);

            (*out_result).aic = forecast.aic.unwrap_or(f64::NAN);
            (*out_result).bic = forecast.bic.unwrap_or(f64::NAN);
            (*out_result).mse = forecast.mse.unwrap_or(f64::NAN);

            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::ComputationError, &e.to_string());
            }
            false
        }
        Err(_) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::PanicCaught, "Panic in Rust code");
            }
            false
        }
    }
}

// ============================================================================
// Data Quality Functions
// ============================================================================

/// Compute data quality metrics.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_data_quality(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    out_result: *mut DataQualityResult,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if values.is_null() || out_result.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let series = build_series(values, validity, length);
        anofox_fcst_core::compute_data_quality(&series, None)
    }));

    match result {
        Ok(Ok(quality)) => {
            (*out_result).structural_score = quality.structural_score;
            (*out_result).temporal_score = quality.temporal_score;
            (*out_result).magnitude_score = quality.magnitude_score;
            (*out_result).behavioral_score = quality.behavioral_score;
            (*out_result).overall_score = quality.overall_score;
            (*out_result).n_gaps = quality.n_gaps;
            (*out_result).n_missing = quality.n_missing;
            (*out_result).is_constant = quality.is_constant;
            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::ComputationError, &e.to_string());
            }
            false
        }
        Err(_) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::PanicCaught, "Panic in Rust code");
            }
            false
        }
    }
}

// ============================================================================
// Imputation Functions
// ============================================================================

/// Fill NULL values with a constant.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_fill_nulls_const(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    fill_value: c_double,
    out_values: *mut *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if values.is_null() || out_values.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    let series = build_series(values, validity, length);
    let filled = anofox_fcst_core::fill_nulls_const(&series, fill_value);
    *out_values = vec_to_c_array(&filled);

    true
}

/// Fill NULL values with the series mean.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_fill_nulls_mean(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    out_values: *mut *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if values.is_null() || out_values.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    let series = build_series(values, validity, length);
    let filled = anofox_fcst_core::fill_nulls_mean(&series);
    *out_values = vec_to_c_array(&filled);

    true
}

// ============================================================================
// Filter Functions
// ============================================================================

/// Compute differences of a time series.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_diff(
    values: *const c_double,
    length: size_t,
    order: c_int,
    out_values: *mut *mut c_double,
    out_length: *mut size_t,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if values.is_null() || out_values.is_null() || out_length.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let values_vec = std::slice::from_raw_parts(values, length).to_vec();
        anofox_fcst_core::diff(&values_vec, order.max(0) as usize)
    }));

    match result {
        Ok(Ok(diffed)) => {
            *out_length = diffed.len();
            *out_values = vec_to_c_array(&diffed);
            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::ComputationError, &e.to_string());
            }
            false
        }
        Err(_) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::PanicCaught, "Panic in Rust code");
            }
            false
        }
    }
}

// ============================================================================
// Gap Filling Functions
// ============================================================================

/// Allocate a validity bitmask for n elements.
unsafe fn alloc_validity(n: size_t) -> *mut u64 {
    let n_words = n.div_ceil(64);
    if n_words == 0 {
        return ptr::null_mut();
    }
    let ptr = malloc(n_words * std::mem::size_of::<u64>()) as *mut u64;
    if !ptr.is_null() {
        // Initialize all bits to 1 (all valid)
        for i in 0..n_words {
            *ptr.add(i) = !0u64;
        }
    }
    ptr
}

/// Set validity bit for index.
unsafe fn set_validity_bit(validity: *mut u64, index: usize, is_valid: bool) {
    let word_idx = index / 64;
    let bit_idx = index % 64;
    if is_valid {
        *validity.add(word_idx) |= 1u64 << bit_idx;
    } else {
        *validity.add(word_idx) &= !(1u64 << bit_idx);
    }
}

/// Allocate an i64 array using libc malloc.
unsafe fn alloc_i64_array(n: size_t) -> *mut i64 {
    if n == 0 {
        return ptr::null_mut();
    }
    malloc(n * std::mem::size_of::<i64>()) as *mut i64
}

/// Fill gaps in a time series with new timestamps.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_fill_gaps(
    dates: *const i64,
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    frequency_seconds: i64,
    out_result: *mut GapFillResult,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if dates.is_null() || values.is_null() || out_result.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    if frequency_seconds <= 0 {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::InvalidFrequency, "Frequency must be positive");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let dates_vec: Vec<i64> = std::slice::from_raw_parts(dates, length).to_vec();
        let series = build_series(values, validity, length);
        anofox_fcst_core::fill_gaps(&dates_vec, &series, frequency_seconds)
    }));

    match result {
        Ok(Ok((filled_dates, filled_values))) => {
            let n = filled_dates.len();
            (*out_result).length = n;

            if n > 0 {
                // Allocate and copy dates
                (*out_result).dates = alloc_i64_array(n);
                for (i, &d) in filled_dates.iter().enumerate() {
                    *(*out_result).dates.add(i) = d;
                }

                // Allocate values and validity
                (*out_result).values = alloc_double_array(n);
                (*out_result).validity = alloc_validity(n);

                for (i, v) in filled_values.iter().enumerate() {
                    match v {
                        Some(val) => {
                            *(*out_result).values.add(i) = *val;
                            set_validity_bit((*out_result).validity, i, true);
                        }
                        None => {
                            *(*out_result).values.add(i) = f64::NAN;
                            set_validity_bit((*out_result).validity, i, false);
                        }
                    }
                }
            } else {
                (*out_result).dates = ptr::null_mut();
                (*out_result).values = ptr::null_mut();
                (*out_result).validity = ptr::null_mut();
            }

            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::ComputationError, &e.to_string());
            }
            false
        }
        Err(_) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::PanicCaught, "Panic in Rust code");
            }
            false
        }
    }
}

/// Fill forward to a target date.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_fill_forward_dates(
    dates: *const i64,
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    target_date: i64,
    frequency_seconds: i64,
    out_result: *mut GapFillResult,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if dates.is_null() || values.is_null() || out_result.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    if frequency_seconds <= 0 {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::InvalidFrequency, "Frequency must be positive");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let dates_vec: Vec<i64> = std::slice::from_raw_parts(dates, length).to_vec();
        let series = build_series(values, validity, length);
        anofox_fcst_core::fill_forward(&dates_vec, &series, target_date, frequency_seconds)
    }));

    match result {
        Ok(Ok((filled_dates, filled_values))) => {
            let n = filled_dates.len();
            (*out_result).length = n;

            if n > 0 {
                // Allocate and copy dates
                (*out_result).dates = alloc_i64_array(n);
                for (i, &d) in filled_dates.iter().enumerate() {
                    *(*out_result).dates.add(i) = d;
                }

                // Allocate values and validity
                (*out_result).values = alloc_double_array(n);
                (*out_result).validity = alloc_validity(n);

                for (i, v) in filled_values.iter().enumerate() {
                    match v {
                        Some(val) => {
                            *(*out_result).values.add(i) = *val;
                            set_validity_bit((*out_result).validity, i, true);
                        }
                        None => {
                            *(*out_result).values.add(i) = f64::NAN;
                            set_validity_bit((*out_result).validity, i, false);
                        }
                    }
                }
            } else {
                (*out_result).dates = ptr::null_mut();
                (*out_result).values = ptr::null_mut();
                (*out_result).validity = ptr::null_mut();
            }

            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::ComputationError, &e.to_string());
            }
            false
        }
        Err(_) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::PanicCaught, "Panic in Rust code");
            }
            false
        }
    }
}

/// Detect the frequency of a time series.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_detect_frequency(
    dates: *const i64,
    length: size_t,
    out_frequency: *mut i64,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if dates.is_null() || out_frequency.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let dates_vec: Vec<i64> = std::slice::from_raw_parts(dates, length).to_vec();
        anofox_fcst_core::detect_frequency(&dates_vec)
    }));

    match result {
        Ok(Ok(freq)) => {
            *out_frequency = freq;
            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::ComputationError, &e.to_string());
            }
            false
        }
        Err(_) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::PanicCaught, "Panic in Rust code");
            }
            false
        }
    }
}

/// Fill NULL values with forward fill.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_fill_nulls_forward(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    out_result: *mut FilledValuesResult,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if values.is_null() || out_result.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    let series = build_series(values, validity, length);
    let filled = anofox_fcst_core::fill_nulls_forward(&series);

    (*out_result).length = filled.len();

    if !filled.is_empty() {
        (*out_result).values = alloc_double_array(filled.len());
        (*out_result).validity = alloc_validity(filled.len());

        for (i, v) in filled.iter().enumerate() {
            match v {
                Some(val) => {
                    *(*out_result).values.add(i) = *val;
                    set_validity_bit((*out_result).validity, i, true);
                }
                None => {
                    *(*out_result).values.add(i) = f64::NAN;
                    set_validity_bit((*out_result).validity, i, false);
                }
            }
        }
    } else {
        (*out_result).values = ptr::null_mut();
        (*out_result).validity = ptr::null_mut();
    }

    true
}

/// Fill NULL values with backward fill.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_fill_nulls_backward(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    out_result: *mut FilledValuesResult,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if values.is_null() || out_result.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    let series = build_series(values, validity, length);
    let filled = anofox_fcst_core::fill_nulls_backward(&series);

    (*out_result).length = filled.len();

    if !filled.is_empty() {
        (*out_result).values = alloc_double_array(filled.len());
        (*out_result).validity = alloc_validity(filled.len());

        for (i, v) in filled.iter().enumerate() {
            match v {
                Some(val) => {
                    *(*out_result).values.add(i) = *val;
                    set_validity_bit((*out_result).validity, i, true);
                }
                None => {
                    *(*out_result).values.add(i) = f64::NAN;
                    set_validity_bit((*out_result).validity, i, false);
                }
            }
        }
    } else {
        (*out_result).values = ptr::null_mut();
        (*out_result).validity = ptr::null_mut();
    }

    true
}

/// Fill NULL values with linear interpolation.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_fill_nulls_interpolate(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    out_values: *mut *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if values.is_null() || out_values.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    let series = build_series(values, validity, length);
    let filled = anofox_fcst_core::fill_nulls_interpolate(&series);
    *out_values = vec_to_c_array(&filled);

    true
}

// ============================================================================
// Memory Management Functions
// ============================================================================

/// Free a TsStatsResult.
///
/// # Safety
/// The result pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_ts_stats_result(_result: *mut TsStatsResult) {
    // No heap allocations
}

/// Free a GapFillResult.
///
/// # Safety
/// The result pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_gap_fill_result(result: *mut GapFillResult) {
    if result.is_null() {
        return;
    }
    let r = &mut *result;

    if !r.dates.is_null() {
        free(r.dates as *mut libc::c_void);
        r.dates = ptr::null_mut();
    }
    if !r.values.is_null() {
        free(r.values as *mut libc::c_void);
        r.values = ptr::null_mut();
    }
    if !r.validity.is_null() {
        free(r.validity as *mut libc::c_void);
        r.validity = ptr::null_mut();
    }
}

/// Free a FilledValuesResult.
///
/// # Safety
/// The result pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_filled_values_result(result: *mut FilledValuesResult) {
    if result.is_null() {
        return;
    }
    let r = &mut *result;

    if !r.values.is_null() {
        free(r.values as *mut libc::c_void);
        r.values = ptr::null_mut();
    }
    if !r.validity.is_null() {
        free(r.validity as *mut libc::c_void);
        r.validity = ptr::null_mut();
    }
}

/// Free a ForecastResult.
///
/// # Safety
/// The result pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_forecast_result(result: *mut ForecastResult) {
    if result.is_null() {
        return;
    }
    let r = &mut *result;

    if !r.point_forecasts.is_null() {
        free(r.point_forecasts as *mut libc::c_void);
        r.point_forecasts = ptr::null_mut();
    }
    if !r.lower_bounds.is_null() {
        free(r.lower_bounds as *mut libc::c_void);
        r.lower_bounds = ptr::null_mut();
    }
    if !r.upper_bounds.is_null() {
        free(r.upper_bounds as *mut libc::c_void);
        r.upper_bounds = ptr::null_mut();
    }
    if !r.fitted_values.is_null() {
        free(r.fitted_values as *mut libc::c_void);
        r.fitted_values = ptr::null_mut();
    }
    if !r.residuals.is_null() {
        free(r.residuals as *mut libc::c_void);
        r.residuals = ptr::null_mut();
    }
}

/// Free a ChangepointResult.
///
/// # Safety
/// The result pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_changepoint_result(result: *mut ChangepointResult) {
    if result.is_null() {
        return;
    }
    let r = &mut *result;

    if !r.changepoints.is_null() {
        free(r.changepoints as *mut libc::c_void);
        r.changepoints = ptr::null_mut();
    }
}

/// Free a BocpdResult.
///
/// # Safety
/// The result pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_bocpd_result(result: *mut types::BocpdResult) {
    if result.is_null() {
        return;
    }
    let r = &mut *result;

    if !r.is_changepoint.is_null() {
        free(r.is_changepoint as *mut libc::c_void);
        r.is_changepoint = ptr::null_mut();
    }
    if !r.changepoint_probability.is_null() {
        free(r.changepoint_probability as *mut libc::c_void);
        r.changepoint_probability = ptr::null_mut();
    }
    if !r.changepoint_indices.is_null() {
        free(r.changepoint_indices as *mut libc::c_void);
        r.changepoint_indices = ptr::null_mut();
    }
}

/// Free a FeaturesResult.
///
/// # Safety
/// The result pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_features_result(result: *mut FeaturesResult) {
    if result.is_null() {
        return;
    }
    let r = &mut *result;

    if !r.features.is_null() {
        free(r.features as *mut libc::c_void);
        r.features = ptr::null_mut();
    }

    if !r.feature_names.is_null() {
        for i in 0..r.n_features {
            let name_ptr = *r.feature_names.add(i);
            if !name_ptr.is_null() {
                free(name_ptr as *mut libc::c_void);
            }
        }
        free(r.feature_names as *mut libc::c_void);
        r.feature_names = ptr::null_mut();
    }
}

/// Free a SeasonalityResult.
///
/// # Safety
/// The result pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_seasonality_result(result: *mut SeasonalityResult) {
    if result.is_null() {
        return;
    }
    let r = &mut *result;

    if !r.detected_periods.is_null() {
        free(r.detected_periods as *mut libc::c_void);
        r.detected_periods = ptr::null_mut();
    }
}

/// Free a MstlResult.
///
/// # Safety
/// The result pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_mstl_result(result: *mut MstlResult) {
    if result.is_null() {
        return;
    }
    let r = &mut *result;

    if !r.trend.is_null() {
        free(r.trend as *mut libc::c_void);
        r.trend = ptr::null_mut();
    }
    if !r.remainder.is_null() {
        free(r.remainder as *mut libc::c_void);
        r.remainder = ptr::null_mut();
    }
    if !r.seasonal_periods.is_null() {
        free(r.seasonal_periods as *mut libc::c_void);
        r.seasonal_periods = ptr::null_mut();
    }

    if !r.seasonal_components.is_null() {
        for i in 0..r.n_seasonal {
            let comp_ptr = *r.seasonal_components.add(i);
            if !comp_ptr.is_null() {
                free(comp_ptr as *mut libc::c_void);
            }
        }
        free(r.seasonal_components as *mut libc::c_void);
        r.seasonal_components = ptr::null_mut();
    }
}

/// Free a double array.
///
/// # Safety
/// The pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_double_array(ptr: *mut c_double) {
    if !ptr.is_null() {
        free(ptr as *mut libc::c_void);
    }
}

/// Free an int array.
///
/// # Safety
/// The pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_int_array(ptr: *mut c_int) {
    if !ptr.is_null() {
        free(ptr as *mut libc::c_void);
    }
}

// ============================================================================
// Version
// ============================================================================

#[no_mangle]
pub extern "C" fn anofox_fcst_version() -> *const libc::c_char {
    static VERSION: &[u8] = b"0.1.0\0";
    VERSION.as_ptr() as *const libc::c_char
}
