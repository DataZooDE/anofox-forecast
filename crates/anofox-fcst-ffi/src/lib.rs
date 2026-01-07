//! FFI boundary layer for anofox-forecast DuckDB extension.
//!
//! This crate provides C-compatible functions that can be called from the
//! C++ DuckDB extension wrapper.
//!
//! # Module Organization
//!
//! - `types` - FFI-compatible data structures
//! - `error_handling` - Standardized error handling utilities
//! - `conversion` - Parameter conversion helpers
//! - `allocation` - Memory allocation helpers
//! - `telemetry` - Usage telemetry (native only)

pub mod allocation;
pub mod conversion;
pub mod error_handling;
#[cfg(not(target_family = "wasm"))]
pub mod telemetry;
pub mod types;

// Use core::ffi types which work on all platforms including WASM
use core::ffi::{c_char, c_double, c_int};
use std::ffi::CStr;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

// Re-export helper functions from submodules for internal use
use allocation::{alloc_double_array, vec_to_c_double_array};
use conversion::to_option_usize;
use error_handling::{check_null_pointers, init_error, set_error};

// size_t is not in core::ffi, use usize instead
#[allow(non_camel_case_types)]
type size_t = usize;

// Memory allocation - use libc on native, std::alloc on WASM
#[cfg(not(target_family = "wasm"))]
use libc::{free, malloc};

#[cfg(target_family = "wasm")]
unsafe fn malloc(size: usize) -> *mut core::ffi::c_void {
    use std::alloc::{alloc, Layout};
    let layout = Layout::from_size_align(size, 8).unwrap();
    alloc(layout) as *mut core::ffi::c_void
}

#[cfg(target_family = "wasm")]
unsafe fn free(ptr: *mut core::ffi::c_void) {
    use std::alloc::{dealloc, Layout};
    if !ptr.is_null() {
        // Note: We don't know the actual size, so we use a minimal layout
        // This is safe because DuckDB manages the actual memory
        let layout = Layout::from_size_align(1, 8).unwrap();
        dealloc(ptr as *mut u8, layout);
    }
}

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

// Helper: vec_to_c_array delegates to allocation module
#[inline]
unsafe fn vec_to_c_array(vec: &[f64]) -> *mut c_double {
    vec_to_c_double_array(vec)
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
    init_error(out_error);

    let ptrs = &[
        values as *const core::ffi::c_void,
        out_result as *const core::ffi::c_void,
    ];
    if check_null_pointers(out_error, ptrs) {
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
            set_error(out_error, ErrorCode::ComputationError, &e.to_string());
            false
        }
        Err(_) => {
            set_error(out_error, ErrorCode::PanicCaught, "Panic in Rust code");
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
    init_error(out_error);

    let ptrs = &[
        actual as *const core::ffi::c_void,
        forecast as *const core::ffi::c_void,
        out_result as *const core::ffi::c_void,
    ];
    if check_null_pointers(out_error, ptrs) {
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
            set_error(out_error, ErrorCode::ComputationError, &e.to_string());
            false
        }
        Err(_) => {
            set_error(out_error, ErrorCode::PanicCaught, "Panic in Rust code");
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
    init_error(out_error);

    let ptrs = &[
        values as *const core::ffi::c_void,
        out_periods as *const core::ffi::c_void,
        out_n_periods as *const core::ffi::c_void,
    ];
    if check_null_pointers(out_error, ptrs) {
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let values_vec = std::slice::from_raw_parts(values, length).to_vec();
        anofox_fcst_core::detect_seasonality(&values_vec, to_option_usize(max_period))
    }));

    match result {
        Ok(Ok(periods)) => {
            let n = periods.len();
            *out_n_periods = n;

            if n > 0 {
                let ptr = malloc(n * std::mem::size_of::<c_int>()) as *mut c_int;
                if ptr.is_null() {
                    set_error(out_error, ErrorCode::AllocationError, "Memory allocation failed");
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
            set_error(out_error, ErrorCode::ComputationError, &e.to_string());
            false
        }
        Err(_) => {
            set_error(out_error, ErrorCode::PanicCaught, "Panic in Rust code");
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
    init_error(out_error);

    let ptrs = &[
        values as *const core::ffi::c_void,
        out_result as *const core::ffi::c_void,
    ];
    if check_null_pointers(out_error, ptrs) {
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let values_vec = std::slice::from_raw_parts(values, length).to_vec();
        anofox_fcst_core::analyze_seasonality(&values_vec, to_option_usize(max_period))
    }));

    match result {
        Ok(Ok(analysis)) => {
            let n = analysis.periods.len();

            // Allocate and copy detected_periods
            if n > 0 {
                let periods_ptr = malloc(n * std::mem::size_of::<c_int>()) as *mut c_int;

                if periods_ptr.is_null() {
                    set_error(out_error, ErrorCode::AllocationError, "Memory allocation failed");
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
            set_error(out_error, ErrorCode::ComputationError, &e.to_string());
            false
        }
        Err(_) => {
            set_error(out_error, ErrorCode::PanicCaught, "Panic in Rust code");
            false
        }
    }
}

// ============================================================================
// Period Detection Functions (fdars-core integration)
// ============================================================================

/// Detect periods using specified method.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_detect_periods(
    values: *const c_double,
    length: size_t,
    method: *const c_char,
    out_result: *mut types::MultiPeriodResult,
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
        let method_str = if method.is_null() {
            "fft"
        } else {
            CStr::from_ptr(method).to_str().unwrap_or("fft")
        };
        let period_method: anofox_fcst_core::PeriodMethod = method_str.parse().unwrap_or_default();
        anofox_fcst_core::detect_periods(&values_vec, period_method)
    }));

    match result {
        Ok(Ok(multi_result)) => {
            let n = multi_result.periods.len();
            (*out_result).n_periods = n;
            (*out_result).primary_period = multi_result.primary_period;
            copy_string_to_buffer(&multi_result.method, &mut (*out_result).method);

            if n > 0 {
                let periods_ptr = malloc(n * std::mem::size_of::<types::DetectedPeriodFFI>())
                    as *mut types::DetectedPeriodFFI;
                for (i, dp) in multi_result.periods.iter().enumerate() {
                    (*periods_ptr.add(i)).period = dp.period;
                    (*periods_ptr.add(i)).confidence = dp.confidence;
                    (*periods_ptr.add(i)).strength = dp.strength;
                    (*periods_ptr.add(i)).amplitude = dp.amplitude;
                    (*periods_ptr.add(i)).phase = dp.phase;
                    (*periods_ptr.add(i)).iteration = dp.iteration;
                }
                (*out_result).periods = periods_ptr;
            } else {
                (*out_result).periods = ptr::null_mut();
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

/// Estimate period using FFT.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_estimate_period_fft(
    values: *const c_double,
    length: size_t,
    out_result: *mut types::SinglePeriodResult,
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
        anofox_fcst_core::estimate_period_fft_ts(&values_vec)
    }));

    match result {
        Ok(Ok(single_result)) => {
            (*out_result).period = single_result.period;
            (*out_result).frequency = single_result.frequency;
            (*out_result).power = single_result.power;
            (*out_result).confidence = single_result.confidence;
            copy_string_to_buffer(&single_result.method, &mut (*out_result).method);
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

/// Estimate period using ACF.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_estimate_period_acf(
    values: *const c_double,
    length: size_t,
    max_lag: c_int,
    out_result: *mut types::SinglePeriodResult,
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
        let max_lag_opt = if max_lag > 0 {
            Some(max_lag as usize)
        } else {
            None
        };
        anofox_fcst_core::estimate_period_acf_ts(&values_vec, max_lag_opt)
    }));

    match result {
        Ok(Ok(single_result)) => {
            (*out_result).period = single_result.period;
            (*out_result).frequency = single_result.frequency;
            (*out_result).power = single_result.power;
            (*out_result).confidence = single_result.confidence;
            copy_string_to_buffer(&single_result.method, &mut (*out_result).method);
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

/// Detect multiple periods in time series.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_detect_multiple_periods(
    values: *const c_double,
    length: size_t,
    max_periods: c_int,
    min_confidence: c_double,
    min_strength: c_double,
    out_result: *mut types::MultiPeriodResult,
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
        let max_p = if max_periods > 0 {
            Some(max_periods as usize)
        } else {
            None
        };
        let min_c = if min_confidence > 0.0 {
            Some(min_confidence)
        } else {
            None
        };
        let min_s = if min_strength > 0.0 {
            Some(min_strength)
        } else {
            None
        };
        anofox_fcst_core::detect_multiple_periods_ts(&values_vec, max_p, min_c, min_s)
    }));

    match result {
        Ok(Ok(multi_result)) => {
            let n = multi_result.periods.len();
            (*out_result).n_periods = n;
            (*out_result).primary_period = multi_result.primary_period;
            copy_string_to_buffer(&multi_result.method, &mut (*out_result).method);

            if n > 0 {
                let periods_ptr = malloc(n * std::mem::size_of::<types::DetectedPeriodFFI>())
                    as *mut types::DetectedPeriodFFI;
                for (i, dp) in multi_result.periods.iter().enumerate() {
                    (*periods_ptr.add(i)).period = dp.period;
                    (*periods_ptr.add(i)).confidence = dp.confidence;
                    (*periods_ptr.add(i)).strength = dp.strength;
                    (*periods_ptr.add(i)).amplitude = dp.amplitude;
                    (*periods_ptr.add(i)).phase = dp.phase;
                    (*periods_ptr.add(i)).iteration = dp.iteration;
                }
                (*out_result).periods = periods_ptr;
            } else {
                (*out_result).periods = ptr::null_mut();
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
// Peak Detection Functions (fdars-core integration)
// ============================================================================

/// Detect peaks in time series.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_detect_peaks(
    values: *const c_double,
    length: size_t,
    min_distance: c_double,
    min_prominence: c_double,
    smooth_first: bool,
    out_result: *mut types::PeakDetectionResultFFI,
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
        let min_dist = if min_distance > 0.0 {
            Some(min_distance)
        } else {
            None
        };
        let min_prom = if min_prominence > 0.0 {
            Some(min_prominence)
        } else {
            None
        };
        anofox_fcst_core::detect_peaks(&values_vec, min_dist, min_prom, smooth_first, None)
    }));

    match result {
        Ok(Ok(peak_result)) => {
            let n = peak_result.n_peaks;
            (*out_result).n_peaks = n;
            (*out_result).mean_period = peak_result.mean_period;

            if n > 0 {
                let peaks_ptr =
                    malloc(n * std::mem::size_of::<types::PeakFFI>()) as *mut types::PeakFFI;
                for (i, peak) in peak_result.peaks.iter().enumerate() {
                    (*peaks_ptr.add(i)).index = peak.index;
                    (*peaks_ptr.add(i)).time = peak.time;
                    (*peaks_ptr.add(i)).value = peak.value;
                    (*peaks_ptr.add(i)).prominence = peak.prominence;
                }
                (*out_result).peaks = peaks_ptr;
            } else {
                (*out_result).peaks = ptr::null_mut();
            }

            let n_dist = peak_result.inter_peak_distances.len();
            (*out_result).n_distances = n_dist;
            if n_dist > 0 {
                (*out_result).inter_peak_distances =
                    vec_to_c_array(&peak_result.inter_peak_distances);
            } else {
                (*out_result).inter_peak_distances = ptr::null_mut();
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

/// Analyze peak timing variability.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_analyze_peak_timing(
    values: *const c_double,
    length: size_t,
    period: c_double,
    out_result: *mut types::PeakTimingResultFFI,
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

    if period <= 0.0 {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::InvalidInput, "Period must be positive");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let values_vec = std::slice::from_raw_parts(values, length).to_vec();
        anofox_fcst_core::analyze_peak_timing(&values_vec, period, None)
    }));

    match result {
        Ok(Ok(timing_result)) => {
            let n = timing_result.peak_times.len();
            (*out_result).n_peaks = n;
            (*out_result).mean_timing = timing_result.mean_timing;
            (*out_result).std_timing = timing_result.std_timing;
            (*out_result).range_timing = timing_result.range_timing;
            (*out_result).variability_score = timing_result.variability_score;
            (*out_result).timing_trend = timing_result.timing_trend;
            (*out_result).is_stable = timing_result.is_stable;

            if n > 0 {
                (*out_result).peak_times = vec_to_c_array(&timing_result.peak_times);
                (*out_result).peak_values = vec_to_c_array(&timing_result.peak_values);
                (*out_result).normalized_timing = vec_to_c_array(&timing_result.normalized_timing);
            } else {
                (*out_result).peak_times = ptr::null_mut();
                (*out_result).peak_values = ptr::null_mut();
                (*out_result).normalized_timing = ptr::null_mut();
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
// Detrending Functions (fdars-core integration)
// ============================================================================

/// Detrend time series using specified method.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_detrend(
    values: *const c_double,
    length: size_t,
    method: *const c_char,
    out_result: *mut types::DetrendResultFFI,
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
        let method_str = if method.is_null() {
            "auto"
        } else {
            CStr::from_ptr(method).to_str().unwrap_or("auto")
        };
        let detrend_method: anofox_fcst_core::DetrendMethod =
            method_str.parse().unwrap_or_default();
        anofox_fcst_core::detrend(&values_vec, detrend_method)
    }));

    match result {
        Ok(Ok(detrend_result)) => {
            let n = detrend_result.trend.len();
            (*out_result).length = n;
            (*out_result).rss = detrend_result.rss;
            (*out_result).n_params = detrend_result.n_params;
            copy_string_to_buffer(&detrend_result.method, &mut (*out_result).method);

            (*out_result).trend = vec_to_c_array(&detrend_result.trend);
            (*out_result).detrended = vec_to_c_array(&detrend_result.detrended);

            if let Some(ref coeffs) = detrend_result.coefficients {
                (*out_result).n_coefficients = coeffs.len();
                (*out_result).coefficients = vec_to_c_array(coeffs);
            } else {
                (*out_result).n_coefficients = 0;
                (*out_result).coefficients = ptr::null_mut();
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

/// Decompose time series into trend, seasonal, and remainder.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_decompose(
    values: *const c_double,
    length: size_t,
    period: c_double,
    method: *const c_char,
    out_result: *mut types::DecomposeResultFFI,
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

    if period <= 0.0 {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::InvalidInput, "Period must be positive");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let values_vec = std::slice::from_raw_parts(values, length).to_vec();
        let method_str = if method.is_null() {
            "additive"
        } else {
            CStr::from_ptr(method).to_str().unwrap_or("additive")
        };
        let decompose_method: anofox_fcst_core::DecomposeMethod =
            method_str.parse().unwrap_or_default();
        anofox_fcst_core::decompose(&values_vec, period, decompose_method)
    }));

    match result {
        Ok(Ok(decompose_result)) => {
            let n = decompose_result.trend.len();
            (*out_result).length = n;
            (*out_result).period = decompose_result.period;
            copy_string_to_buffer(&decompose_result.method, &mut (*out_result).method);

            (*out_result).trend = vec_to_c_array(&decompose_result.trend);
            (*out_result).seasonal = vec_to_c_array(&decompose_result.seasonal);
            (*out_result).remainder = vec_to_c_array(&decompose_result.remainder);

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
// Extended Seasonality Functions (fdars-core integration)
// ============================================================================

/// Compute seasonal strength using specified method.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_seasonal_strength(
    values: *const c_double,
    length: size_t,
    period: c_double,
    method: *const c_char,
    out_strength: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if values.is_null() || out_strength.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    if period <= 0.0 {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::InvalidInput, "Period must be positive");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let values_vec = std::slice::from_raw_parts(values, length).to_vec();
        let method_str = if method.is_null() {
            "variance"
        } else {
            CStr::from_ptr(method).to_str().unwrap_or("variance")
        };
        let strength_method: anofox_fcst_core::StrengthMethod =
            method_str.parse().unwrap_or_default();
        anofox_fcst_core::seasonal_strength(&values_vec, period, strength_method)
    }));

    match result {
        Ok(Ok(strength)) => {
            *out_strength = strength;
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

/// Compute windowed seasonal strength.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_seasonal_strength_windowed(
    values: *const c_double,
    length: size_t,
    period: c_double,
    window_size: c_double,
    method: *const c_char,
    out_strengths: *mut *mut c_double,
    out_n_windows: *mut size_t,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if values.is_null() || out_strengths.is_null() || out_n_windows.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    if period <= 0.0 {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::InvalidInput, "Period must be positive");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let values_vec = std::slice::from_raw_parts(values, length).to_vec();
        let window_opt = if window_size > 0.0 {
            Some(window_size)
        } else {
            None
        };
        let method_str = if method.is_null() {
            None
        } else {
            let s = CStr::from_ptr(method).to_str().unwrap_or("variance");
            Some(
                s.parse::<anofox_fcst_core::StrengthMethod>()
                    .unwrap_or_default(),
            )
        };
        anofox_fcst_core::seasonal_strength_windowed(&values_vec, period, window_opt, method_str)
    }));

    match result {
        Ok(Ok(strengths)) => {
            *out_n_windows = strengths.len();
            if !strengths.is_empty() {
                *out_strengths = vec_to_c_array(&strengths);
            } else {
                *out_strengths = ptr::null_mut();
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

/// Classify seasonality type and pattern.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_classify_seasonality(
    values: *const c_double,
    length: size_t,
    period: c_double,
    strength_threshold: c_double,
    timing_threshold: c_double,
    out_result: *mut types::SeasonalityClassificationFFI,
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

    if period <= 0.0 {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::InvalidInput, "Period must be positive");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let values_vec = std::slice::from_raw_parts(values, length).to_vec();
        let strength_opt = if strength_threshold > 0.0 {
            Some(strength_threshold)
        } else {
            None
        };
        let timing_opt = if timing_threshold > 0.0 {
            Some(timing_threshold)
        } else {
            None
        };
        anofox_fcst_core::classify_seasonality(&values_vec, period, strength_opt, timing_opt)
    }));

    match result {
        Ok(Ok(classification)) => {
            (*out_result).is_seasonal = classification.is_seasonal;
            (*out_result).has_stable_timing = classification.has_stable_timing;
            (*out_result).timing_variability = classification.timing_variability;
            (*out_result).seasonal_strength = classification.seasonal_strength;
            copy_string_to_buffer(
                classification.classification.as_str(),
                &mut (*out_result).classification,
            );

            // Copy cycle strengths
            let n_cycles = classification.cycle_strengths.len();
            (*out_result).n_cycle_strengths = n_cycles;
            if n_cycles > 0 {
                (*out_result).cycle_strengths = vec_to_c_array(&classification.cycle_strengths);
            } else {
                (*out_result).cycle_strengths = ptr::null_mut();
            }

            // Copy weak seasons
            let n_weak = classification.weak_seasons.len();
            (*out_result).n_weak_seasons = n_weak;
            if n_weak > 0 {
                let weak_ptr = malloc(n_weak * std::mem::size_of::<size_t>()) as *mut size_t;
                for (i, idx) in classification.weak_seasons.iter().enumerate() {
                    *weak_ptr.add(i) = *idx;
                }
                (*out_result).weak_seasons = weak_ptr;
            } else {
                (*out_result).weak_seasons = ptr::null_mut();
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

/// Detect seasonality changes over time.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_detect_seasonality_changes(
    values: *const c_double,
    length: size_t,
    period: c_double,
    threshold: c_double,
    window_size: c_double,
    min_duration: c_double,
    out_result: *mut types::ChangeDetectionResultFFI,
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

    if period <= 0.0 {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::InvalidInput, "Period must be positive");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let values_vec = std::slice::from_raw_parts(values, length).to_vec();
        let threshold_opt = if threshold > 0.0 {
            Some(threshold)
        } else {
            None
        };
        let window_opt = if window_size > 0.0 {
            Some(window_size)
        } else {
            None
        };
        let min_dur_opt = if min_duration > 0.0 {
            Some(min_duration)
        } else {
            None
        };
        anofox_fcst_core::detect_seasonality_changes(
            &values_vec,
            period,
            threshold_opt,
            window_opt,
            min_dur_opt,
        )
    }));

    match result {
        Ok(Ok(change_result)) => {
            let n = change_result.change_points.len();
            (*out_result).n_changes = n;

            if n > 0 {
                let changes_ptr =
                    malloc(n * std::mem::size_of::<types::SeasonalityChangePointFFI>())
                        as *mut types::SeasonalityChangePointFFI;
                for (i, cp) in change_result.change_points.iter().enumerate() {
                    (*changes_ptr.add(i)).index = cp.index;
                    (*changes_ptr.add(i)).time = cp.time;
                    copy_string_to_buffer(
                        cp.change_type.as_str(),
                        &mut (*changes_ptr.add(i)).change_type,
                    );
                    (*changes_ptr.add(i)).strength_before = cp.strength_before;
                    (*changes_ptr.add(i)).strength_after = cp.strength_after;
                }
                (*out_result).change_points = changes_ptr;
            } else {
                (*out_result).change_points = ptr::null_mut();
            }

            // Copy strength curve
            let n_curve = change_result.strength_curve.len();
            (*out_result).n_strength_curve = n_curve;
            if n_curve > 0 {
                (*out_result).strength_curve = vec_to_c_array(&change_result.strength_curve);
            } else {
                (*out_result).strength_curve = ptr::null_mut();
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

/// Compute instantaneous period using Hilbert transform.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_instantaneous_period(
    values: *const c_double,
    length: size_t,
    out_result: *mut types::InstantaneousPeriodResultFFI,
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
        anofox_fcst_core::instantaneous_period(&values_vec)
    }));

    match result {
        Ok(Ok(inst_result)) => {
            let n = inst_result.period.len();
            (*out_result).length = n;

            if n > 0 {
                (*out_result).periods = vec_to_c_array(&inst_result.period);
                (*out_result).frequencies = vec_to_c_array(&inst_result.frequency);
                (*out_result).amplitudes = vec_to_c_array(&inst_result.amplitude);
            } else {
                (*out_result).periods = ptr::null_mut();
                (*out_result).frequencies = ptr::null_mut();
                (*out_result).amplitudes = ptr::null_mut();
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

/// Detect amplitude modulation in seasonal time series.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_detect_amplitude_modulation(
    values: *const c_double,
    length: size_t,
    period: c_double,
    modulation_threshold: c_double,
    seasonality_threshold: c_double,
    out_result: *mut types::AmplitudeModulationResultFFI,
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

    if period <= 0.0 {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::InvalidInput, "Period must be positive");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let values_vec = std::slice::from_raw_parts(values, length).to_vec();
        let mod_thresh = if modulation_threshold > 0.0 {
            Some(modulation_threshold)
        } else {
            None
        };
        let seas_thresh = if seasonality_threshold > 0.0 {
            Some(seasonality_threshold)
        } else {
            None
        };
        anofox_fcst_core::detect_amplitude_modulation(&values_vec, period, mod_thresh, seas_thresh)
    }));

    match result {
        Ok(Ok(am_result)) => {
            (*out_result).is_seasonal = am_result.is_seasonal;
            (*out_result).seasonal_strength = am_result.seasonal_strength;
            (*out_result).has_modulation = am_result.has_modulation;
            copy_string_to_buffer(
                am_result.modulation_type.as_str(),
                &mut (*out_result).modulation_type,
            );
            (*out_result).modulation_score = am_result.modulation_score;
            (*out_result).amplitude_trend = am_result.amplitude_trend;
            (*out_result).scale = am_result.scale;

            let n = am_result.wavelet_amplitude.len();
            (*out_result).n_points = n;
            if n > 0 {
                (*out_result).wavelet_amplitude = vec_to_c_array(&am_result.wavelet_amplitude);
                (*out_result).time_points = vec_to_c_array(&am_result.time_points);
            } else {
                (*out_result).wavelet_amplitude = ptr::null_mut();
                (*out_result).time_points = ptr::null_mut();
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
// Decomposition Functions
// ============================================================================

/// MSTL decomposition.
///
/// # Arguments
/// * `insufficient_data_mode` - How to handle insufficient data:
///   - 0 (Fail): Error on insufficient data (default)
///   - 1 (Trend): Apply trend-only decomposition, seasonal components are empty
///   - 2 (None): Skip decomposition entirely, return empty result
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_mstl_decomposition(
    values: *const c_double,
    length: size_t,
    periods: *const c_int,
    n_periods: size_t,
    insufficient_data_mode: c_int,
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

    let mode = anofox_fcst_core::InsufficientDataMode::from_int(insufficient_data_mode);

    let result = catch_unwind(AssertUnwindSafe(|| {
        let values_vec = std::slice::from_raw_parts(values, length).to_vec();
        let periods_vec: Vec<i32> = if periods.is_null() || n_periods == 0 {
            vec![]
        } else {
            std::slice::from_raw_parts(periods, n_periods).to_vec()
        };
        anofox_fcst_core::mstl_decompose(&values_vec, &periods_vec, mode)
    }));

    match result {
        Ok(Ok(decomp)) => {
            (*out_result).decomposition_applied = decomp.decomposition_applied;
            (*out_result).n_seasonal = decomp.seasonal.len();

            // Copy trend (may be None if decomposition was skipped)
            if let Some(ref trend) = decomp.trend {
                (*out_result).n_observations = trend.len();
                (*out_result).trend = vec_to_c_array(trend);
            } else {
                (*out_result).n_observations = length;
                (*out_result).trend = ptr::null_mut();
            }

            // Copy remainder (may be None if decomposition was skipped)
            if let Some(ref remainder) = decomp.remainder {
                (*out_result).remainder = vec_to_c_array(remainder);
            } else {
                (*out_result).remainder = ptr::null_mut();
            }

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

/// Validate feature parameter keys and return warnings for unknown keys.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_validate_feature_params(
    param_names: *const *const c_char,
    n_params: size_t,
    out_warnings: *mut *mut *mut c_char,
    out_n_warnings: *mut size_t,
) -> bool {
    if param_names.is_null() || out_warnings.is_null() || out_n_warnings.is_null() {
        return false;
    }

    // Convert C strings to Rust strings
    let mut param_vec = Vec::with_capacity(n_params);
    for i in 0..n_params {
        let param_ptr = *param_names.add(i);
        if !param_ptr.is_null() {
            if let Ok(param_str) = CStr::from_ptr(param_ptr).to_str() {
                param_vec.push(param_str.to_string());
            }
        }
    }

    // Call the validation function
    let warnings = anofox_fcst_core::validate_feature_params(&param_vec);
    let n = warnings.len();

    *out_n_warnings = n;

    if n > 0 {
        let warnings_ptr = malloc(n * std::mem::size_of::<*mut c_char>()) as *mut *mut c_char;
        if warnings_ptr.is_null() {
            return false;
        }

        for (i, warning) in warnings.into_iter().enumerate() {
            let warning_len = warning.len() + 1;
            let warning_cstr = malloc(warning_len) as *mut c_char;
            if !warning_cstr.is_null() {
                ptr::copy_nonoverlapping(
                    warning.as_ptr() as *const c_char,
                    warning_cstr,
                    warning.len(),
                );
                *warning_cstr.add(warning.len()) = 0;
            }
            *warnings_ptr.add(i) = warning_cstr;
        }

        *out_warnings = warnings_ptr;
    } else {
        *out_warnings = ptr::null_mut();
    }

    true
}

/// Free warnings array returned by validate_feature_params.
///
/// # Safety
/// The pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_warnings(warnings: *mut *mut c_char, n_warnings: size_t) {
    if warnings.is_null() {
        return;
    }

    for i in 0..n_warnings {
        let warning_ptr = *warnings.add(i);
        if !warning_ptr.is_null() {
            free(warning_ptr as *mut core::ffi::c_void);
        }
    }
    free(warnings as *mut core::ffi::c_void);
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
        free(r.dates as *mut core::ffi::c_void);
        r.dates = ptr::null_mut();
    }
    if !r.values.is_null() {
        free(r.values as *mut core::ffi::c_void);
        r.values = ptr::null_mut();
    }
    if !r.validity.is_null() {
        free(r.validity as *mut core::ffi::c_void);
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
        free(r.values as *mut core::ffi::c_void);
        r.values = ptr::null_mut();
    }
    if !r.validity.is_null() {
        free(r.validity as *mut core::ffi::c_void);
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
        free(r.point_forecasts as *mut core::ffi::c_void);
        r.point_forecasts = ptr::null_mut();
    }
    if !r.lower_bounds.is_null() {
        free(r.lower_bounds as *mut core::ffi::c_void);
        r.lower_bounds = ptr::null_mut();
    }
    if !r.upper_bounds.is_null() {
        free(r.upper_bounds as *mut core::ffi::c_void);
        r.upper_bounds = ptr::null_mut();
    }
    if !r.fitted_values.is_null() {
        free(r.fitted_values as *mut core::ffi::c_void);
        r.fitted_values = ptr::null_mut();
    }
    if !r.residuals.is_null() {
        free(r.residuals as *mut core::ffi::c_void);
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
        free(r.changepoints as *mut core::ffi::c_void);
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
        free(r.is_changepoint as *mut core::ffi::c_void);
        r.is_changepoint = ptr::null_mut();
    }
    if !r.changepoint_probability.is_null() {
        free(r.changepoint_probability as *mut core::ffi::c_void);
        r.changepoint_probability = ptr::null_mut();
    }
    if !r.changepoint_indices.is_null() {
        free(r.changepoint_indices as *mut core::ffi::c_void);
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
        free(r.features as *mut core::ffi::c_void);
        r.features = ptr::null_mut();
    }

    if !r.feature_names.is_null() {
        for i in 0..r.n_features {
            let name_ptr = *r.feature_names.add(i);
            if !name_ptr.is_null() {
                free(name_ptr as *mut core::ffi::c_void);
            }
        }
        free(r.feature_names as *mut core::ffi::c_void);
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
        free(r.detected_periods as *mut core::ffi::c_void);
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
        free(r.trend as *mut core::ffi::c_void);
        r.trend = ptr::null_mut();
    }
    if !r.remainder.is_null() {
        free(r.remainder as *mut core::ffi::c_void);
        r.remainder = ptr::null_mut();
    }
    if !r.seasonal_periods.is_null() {
        free(r.seasonal_periods as *mut core::ffi::c_void);
        r.seasonal_periods = ptr::null_mut();
    }

    if !r.seasonal_components.is_null() {
        for i in 0..r.n_seasonal {
            let comp_ptr = *r.seasonal_components.add(i);
            if !comp_ptr.is_null() {
                free(comp_ptr as *mut core::ffi::c_void);
            }
        }
        free(r.seasonal_components as *mut core::ffi::c_void);
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
        free(ptr as *mut core::ffi::c_void);
    }
}

/// Free an int array.
///
/// # Safety
/// The pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_int_array(ptr: *mut c_int) {
    if !ptr.is_null() {
        free(ptr as *mut core::ffi::c_void);
    }
}

/// Free a MultiPeriodResult.
///
/// # Safety
/// The result pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_multi_period_result(result: *mut types::MultiPeriodResult) {
    if result.is_null() {
        return;
    }
    let r = &mut *result;

    if !r.periods.is_null() {
        free(r.periods as *mut core::ffi::c_void);
        r.periods = ptr::null_mut();
    }
}

/// Free a PeakDetectionResultFFI.
///
/// # Safety
/// The result pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_peak_detection_result(
    result: *mut types::PeakDetectionResultFFI,
) {
    if result.is_null() {
        return;
    }
    let r = &mut *result;

    if !r.peaks.is_null() {
        free(r.peaks as *mut core::ffi::c_void);
        r.peaks = ptr::null_mut();
    }
    if !r.inter_peak_distances.is_null() {
        free(r.inter_peak_distances as *mut core::ffi::c_void);
        r.inter_peak_distances = ptr::null_mut();
    }
}

/// Free a PeakTimingResultFFI.
///
/// # Safety
/// The result pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_peak_timing_result(result: *mut types::PeakTimingResultFFI) {
    if result.is_null() {
        return;
    }
    let r = &mut *result;

    if !r.peak_times.is_null() {
        free(r.peak_times as *mut core::ffi::c_void);
        r.peak_times = ptr::null_mut();
    }
    if !r.peak_values.is_null() {
        free(r.peak_values as *mut core::ffi::c_void);
        r.peak_values = ptr::null_mut();
    }
    if !r.normalized_timing.is_null() {
        free(r.normalized_timing as *mut core::ffi::c_void);
        r.normalized_timing = ptr::null_mut();
    }
}

/// Free a DetrendResultFFI.
///
/// # Safety
/// The result pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_detrend_result(result: *mut types::DetrendResultFFI) {
    if result.is_null() {
        return;
    }
    let r = &mut *result;

    if !r.trend.is_null() {
        free(r.trend as *mut core::ffi::c_void);
        r.trend = ptr::null_mut();
    }
    if !r.detrended.is_null() {
        free(r.detrended as *mut core::ffi::c_void);
        r.detrended = ptr::null_mut();
    }
    if !r.coefficients.is_null() {
        free(r.coefficients as *mut core::ffi::c_void);
        r.coefficients = ptr::null_mut();
    }
}

/// Free a DecomposeResultFFI.
///
/// # Safety
/// The result pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_decompose_result(result: *mut types::DecomposeResultFFI) {
    if result.is_null() {
        return;
    }
    let r = &mut *result;

    if !r.trend.is_null() {
        free(r.trend as *mut core::ffi::c_void);
        r.trend = ptr::null_mut();
    }
    if !r.seasonal.is_null() {
        free(r.seasonal as *mut core::ffi::c_void);
        r.seasonal = ptr::null_mut();
    }
    if !r.remainder.is_null() {
        free(r.remainder as *mut core::ffi::c_void);
        r.remainder = ptr::null_mut();
    }
}

/// Free a SeasonalityClassificationFFI.
///
/// # Safety
/// The result pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_seasonality_classification_result(
    result: *mut types::SeasonalityClassificationFFI,
) {
    if result.is_null() {
        return;
    }
    let r = &mut *result;

    if !r.cycle_strengths.is_null() {
        free(r.cycle_strengths as *mut core::ffi::c_void);
        r.cycle_strengths = ptr::null_mut();
    }
    if !r.weak_seasons.is_null() {
        free(r.weak_seasons as *mut core::ffi::c_void);
        r.weak_seasons = ptr::null_mut();
    }
}

/// Free a ChangeDetectionResultFFI.
///
/// # Safety
/// The result pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_change_detection_result(
    result: *mut types::ChangeDetectionResultFFI,
) {
    if result.is_null() {
        return;
    }
    let r = &mut *result;

    if !r.change_points.is_null() {
        free(r.change_points as *mut core::ffi::c_void);
        r.change_points = ptr::null_mut();
    }
    if !r.strength_curve.is_null() {
        free(r.strength_curve as *mut core::ffi::c_void);
        r.strength_curve = ptr::null_mut();
    }
}

/// Free an InstantaneousPeriodResultFFI.
///
/// # Safety
/// The result pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_instantaneous_period_result(
    result: *mut types::InstantaneousPeriodResultFFI,
) {
    if result.is_null() {
        return;
    }
    let r = &mut *result;

    if !r.periods.is_null() {
        free(r.periods as *mut core::ffi::c_void);
        r.periods = ptr::null_mut();
    }
    if !r.frequencies.is_null() {
        free(r.frequencies as *mut core::ffi::c_void);
        r.frequencies = ptr::null_mut();
    }
    if !r.amplitudes.is_null() {
        free(r.amplitudes as *mut core::ffi::c_void);
        r.amplitudes = ptr::null_mut();
    }
}

/// Free an AmplitudeModulationResultFFI.
///
/// # Safety
/// The result pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_amplitude_modulation_result(
    result: *mut types::AmplitudeModulationResultFFI,
) {
    if result.is_null() {
        return;
    }
    let r = &mut *result;

    if !r.wavelet_amplitude.is_null() {
        free(r.wavelet_amplitude as *mut core::ffi::c_void);
        r.wavelet_amplitude = ptr::null_mut();
    }
    if !r.time_points.is_null() {
        free(r.time_points as *mut core::ffi::c_void);
        r.time_points = ptr::null_mut();
    }
}

// ============================================================================
// Version
// ============================================================================

#[no_mangle]
pub extern "C" fn anofox_fcst_version() -> *const c_char {
    static VERSION: &[u8] = b"0.1.0\0";
    VERSION.as_ptr() as *const c_char
}
