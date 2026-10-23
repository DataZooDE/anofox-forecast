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

// Newer clippy lints, firing after a stable-toolchain bump past this repo's last
// green CI. Suppressed rather than rewritten: the `.is_multiple_of()`/`.div_ceil()`
// method forms are newer than the project MSRV (Rust 1.86; `is_multiple_of`
// stabilized in 1.87), the many-arg exports are idiomatic at the C FFI boundary,
// and the aligned doc-comment argument lists are intentional for readability.
#![allow(clippy::doc_overindented_list_items)]
#![allow(clippy::too_many_arguments)]
#![allow(clippy::manual_div_ceil)]
// unknown_lints: clippy::manual_is_multiple_of doesn't exist yet on this
// workspace's pinned 1.89 toolchain (added in a later clippy); keeps
// `cargo clippy -D warnings` green on both.
#![allow(unknown_lints)]
#![allow(clippy::manual_is_multiple_of)]

pub mod allocation;
pub mod conversion;
pub mod error_handling;
pub mod types;

// Use core::ffi types which work on all platforms including WASM
use core::ffi::{c_char, c_double, c_int};
use std::ffi::CStr;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

// Re-export helper functions from submodules for internal use
use allocation::{alloc_double_array, alloc_or_error, free_ptr, vec_to_c_double_array};
use conversion::to_option_usize;
use error_handling::{check_null_pointers, init_error, set_error};

// size_t is not in core::ffi, use usize instead
#[allow(non_camel_case_types)]
type size_t = usize;

// Memory allocation - use libc on native, std::alloc on WASM
#[cfg(not(target_family = "wasm"))]
use libc::{free, malloc};

// WASM allocator — size-prefixed to satisfy Rust's dealloc(Layout) contract.
//
// Rust's global allocator requires that `dealloc` is called with the exact same `Layout` as
// `alloc`. On WASM there is no libc free() that carries its own size metadata, so a hardcoded
// layout would be UB. We solve this by prepending a `usize` header that stores the requested
// allocation size; `free` reads it back to reconstruct the correct Layout.
//
// Memory layout per allocation:
//   [ usize header (size of data region) | data ... ]
//                                         ^-- returned pointer
//
// Total allocation: size_of::<usize>() + size bytes, aligned to 8.
// This is compatible across all alloc/free pairings in both lib.rs and allocation.rs
// as long as both files use the identical header format (they do — see allocation.rs).
#[cfg(target_family = "wasm")]
unsafe fn malloc(size: usize) -> *mut core::ffi::c_void {
    use std::alloc::{alloc, Layout};
    use std::mem::size_of;
    // Allocate header + data. Alignment 8 covers both usize (≤8 bytes) and f64 (8 bytes).
    let total = size_of::<usize>()
        .checked_add(size)
        .expect("allocation size overflow");
    let layout = Layout::from_size_align(total, 8).expect("8-byte alignment is always valid");
    let base = alloc(layout);
    if base.is_null() {
        return base as *mut core::ffi::c_void;
    }
    // Store the data size (not the total) in the header for dealloc.
    *(base as *mut usize) = size;
    // Return pointer to the data region (after the header).
    base.add(size_of::<usize>()) as *mut core::ffi::c_void
}

#[cfg(target_family = "wasm")]
unsafe fn free(ptr: *mut core::ffi::c_void) {
    use std::alloc::{dealloc, Layout};
    use std::mem::size_of;
    if ptr.is_null() {
        return;
    }
    // Recover base pointer and stored size from the header.
    let base = (ptr as *mut u8).sub(size_of::<usize>());
    let size = *(base as *const usize);
    let total = size_of::<usize>()
        .checked_add(size)
        .expect("allocation size overflow");
    let layout = Layout::from_size_align(total, 8).expect("8-byte alignment is always valid");
    dealloc(base, layout);
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
    seasonal_period: size_t,
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
        anofox_fcst_core::compute_ts_stats(&series, seasonal_period)
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

/// Compute time series statistics with date information and frequency type for gap detection.
///
/// This version properly handles calendar frequencies (monthly, quarterly, yearly) which
/// have variable durations that cannot be accurately represented in microseconds.
///
/// `period_count` is the user-declared calendar multiplier for Monthly/Quarterly/Yearly
/// frequencies (e.g. `2` for `'2mo'`, `3` for `'3q'`, `5` for `'5y'`; `1` for the
/// single-unit strings); ignored for `Fixed` (CR-01 fix, 13-03 deferred item).
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
/// The dates array must have the same length as the values array.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_stats_with_dates_and_type(
    values: *const c_double,
    validity: *const u64,
    dates: *const i64,
    length: size_t,
    frequency_micros: i64,
    frequency_type: FrequencyType,
    seasonal_period: size_t,
    period_count: size_t,
    out_result: *mut TsStatsResult,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);

    let ptrs = &[
        values as *const core::ffi::c_void,
        dates as *const core::ffi::c_void,
        out_result as *const core::ffi::c_void,
    ];
    if check_null_pointers(out_error, ptrs) {
        return false;
    }

    if length == 0 {
        *out_result = TsStatsResult::default();
        return true;
    }

    let period_count_u32 = u32::try_from(period_count).unwrap_or(u32::MAX);

    let result = catch_unwind(AssertUnwindSafe(|| {
        let series = build_series(values, validity, length);
        let dates_slice = std::slice::from_raw_parts(dates, length);
        let freq_type: anofox_fcst_core::FrequencyType = frequency_type.into();
        anofox_fcst_core::compute_ts_stats_with_dates_and_type(
            &series,
            dates_slice,
            frequency_micros,
            freq_type,
            seasonal_period,
            period_count_u32,
        )
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

// Helper for 2-argument metric functions. `actual_validity`/`forecast_validity` may each
// be null (all valid). Pairs are dropped via `anofox_fcst_core::complete_cases` (D-02)
// before the core metric is computed; an empty filtered result flows into the core
// metric's own InsufficientData error, which we remap to ErrorCode::InsufficientData.
unsafe fn impl_metric_2arg<F>(
    actual: *const c_double,
    actual_validity: *const u64,
    actual_len: size_t,
    forecast: *const c_double,
    forecast_validity: *const u64,
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
        let actual_series = build_series(actual, actual_validity, actual_len);
        let forecast_series = build_series(forecast, forecast_validity, forecast_len);
        if actual_series.len() != forecast_series.len() {
            return Err(anofox_fcst_core::ForecastError::InvalidInput(format!(
                "arrays must have the same length: {} vs {}",
                actual_series.len(),
                forecast_series.len()
            )));
        }
        let filtered = anofox_fcst_core::complete_cases(&[&actual_series, &forecast_series])?;
        core_fn(&filtered[0], &filtered[1])
    }));

    match result {
        Ok(Ok(value)) => {
            *out_result = value;
            true
        }
        Ok(Err(e)) => {
            let code = if matches!(e, anofox_fcst_core::ForecastError::InsufficientData { .. }) {
                ErrorCode::InsufficientData
            } else {
                ErrorCode::ComputationError
            };
            set_error(out_error, code, &e.to_string());
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
/// All pointer arguments must be valid and non-null (except the two `*_validity`
/// pointers, which may be null to mean "all valid"). Arrays must have the specified
/// lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_mae(
    actual: *const c_double,
    actual_validity: *const u64,
    actual_len: size_t,
    forecast: *const c_double,
    forecast_validity: *const u64,
    forecast_len: size_t,
    out_result: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    impl_metric_2arg(
        actual,
        actual_validity,
        actual_len,
        forecast,
        forecast_validity,
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
    actual_validity: *const u64,
    actual_len: size_t,
    forecast: *const c_double,
    forecast_validity: *const u64,
    forecast_len: size_t,
    out_result: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    impl_metric_2arg(
        actual,
        actual_validity,
        actual_len,
        forecast,
        forecast_validity,
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
    actual_validity: *const u64,
    actual_len: size_t,
    forecast: *const c_double,
    forecast_validity: *const u64,
    forecast_len: size_t,
    out_result: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    impl_metric_2arg(
        actual,
        actual_validity,
        actual_len,
        forecast,
        forecast_validity,
        forecast_len,
        out_result,
        out_error,
        anofox_fcst_core::rmse,
    )
}

/// Mean Absolute Percentage Error. `*out_is_null` is set `true` (call still
/// returns `true` for "no error") when any pairwise-complete actual is
/// exactly zero -- MAPE is undefined there (D-07); callers must check
/// `*out_is_null` before trusting `*out_result`.
///
/// # Safety
/// All pointer arguments must be valid and non-null (except the `*_validity`
/// pointers). Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_mape(
    actual: *const c_double,
    actual_validity: *const u64,
    actual_len: size_t,
    forecast: *const c_double,
    forecast_validity: *const u64,
    forecast_len: size_t,
    out_result: *mut c_double,
    out_is_null: *mut bool,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);

    let ptrs = &[
        actual as *const core::ffi::c_void,
        forecast as *const core::ffi::c_void,
        out_result as *const core::ffi::c_void,
        out_is_null as *const core::ffi::c_void,
    ];
    if check_null_pointers(out_error, ptrs) {
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let actual_series = build_series(actual, actual_validity, actual_len);
        let forecast_series = build_series(forecast, forecast_validity, forecast_len);
        if actual_series.len() != forecast_series.len() {
            return Err(anofox_fcst_core::ForecastError::InvalidInput(format!(
                "arrays must have the same length: {} vs {}",
                actual_series.len(),
                forecast_series.len()
            )));
        }
        let filtered = anofox_fcst_core::complete_cases(&[&actual_series, &forecast_series])?;
        anofox_fcst_core::mape(&filtered[0], &filtered[1])
    }));

    match result {
        Ok(Ok(Some(value))) => {
            *out_result = value;
            *out_is_null = false;
            true
        }
        Ok(Ok(None)) => {
            *out_is_null = true;
            true
        }
        Ok(Err(e)) => {
            let code = if matches!(e, anofox_fcst_core::ForecastError::InsufficientData { .. }) {
                ErrorCode::InsufficientData
            } else {
                ErrorCode::ComputationError
            };
            set_error(out_error, code, &e.to_string());
            false
        }
        Err(_) => {
            set_error(out_error, ErrorCode::PanicCaught, "Panic in Rust code");
            false
        }
    }
}

/// Symmetric Mean Absolute Percentage Error
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_smape(
    actual: *const c_double,
    actual_validity: *const u64,
    actual_len: size_t,
    forecast: *const c_double,
    forecast_validity: *const u64,
    forecast_len: size_t,
    out_result: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    impl_metric_2arg(
        actual,
        actual_validity,
        actual_len,
        forecast,
        forecast_validity,
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
    actual_validity: *const u64,
    actual_len: size_t,
    forecast: *const c_double,
    forecast_validity: *const u64,
    forecast_len: size_t,
    out_result: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    impl_metric_2arg(
        actual,
        actual_validity,
        actual_len,
        forecast,
        forecast_validity,
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
    actual_validity: *const u64,
    actual_len: size_t,
    forecast: *const c_double,
    forecast_validity: *const u64,
    forecast_len: size_t,
    out_result: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    impl_metric_2arg(
        actual,
        actual_validity,
        actual_len,
        forecast,
        forecast_validity,
        forecast_len,
        out_result,
        out_error,
        anofox_fcst_core::bias,
    )
}

/// Maps a ForecastError to the FFI error code, picking InsufficientData for the
/// "nothing left after complete_cases" case (D-04) instead of ComputationError.
fn metric_error_code(e: &anofox_fcst_core::ForecastError) -> ErrorCode {
    if matches!(e, anofox_fcst_core::ForecastError::InsufficientData { .. }) {
        ErrorCode::InsufficientData
    } else {
        ErrorCode::ComputationError
    }
}

/// Relative MAE comparing two models. `*_validity` pointers may each be null
/// (all valid); rows missing in ANY of actual/pred1/pred2 are dropped (D-02).
///
/// # Safety
/// All pointer arguments must be valid and non-null (except the `*_validity`
/// pointers). Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_rmae(
    actual: *const c_double,
    actual_validity: *const u64,
    actual_len: size_t,
    pred1: *const c_double,
    pred1_validity: *const u64,
    pred1_len: size_t,
    pred2: *const c_double,
    pred2_validity: *const u64,
    pred2_len: size_t,
    out_result: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);

    let ptrs = &[
        actual as *const core::ffi::c_void,
        pred1 as *const core::ffi::c_void,
        pred2 as *const core::ffi::c_void,
        out_result as *const core::ffi::c_void,
    ];
    if check_null_pointers(out_error, ptrs) {
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let actual_series = build_series(actual, actual_validity, actual_len);
        let pred1_series = build_series(pred1, pred1_validity, pred1_len);
        let pred2_series = build_series(pred2, pred2_validity, pred2_len);
        if actual_series.len() != pred1_series.len() || actual_series.len() != pred2_series.len() {
            return Err(anofox_fcst_core::ForecastError::InvalidInput(
                "actual, pred1 and pred2 arrays must have the same length".to_string(),
            ));
        }
        let filtered =
            anofox_fcst_core::complete_cases(&[&actual_series, &pred1_series, &pred2_series])?;
        anofox_fcst_core::rmae(&filtered[0], &filtered[1], &filtered[2])
    }));

    match result {
        Ok(Ok(value)) => {
            *out_result = value;
            true
        }
        Ok(Err(e)) => {
            set_error(out_error, metric_error_code(&e), &e.to_string());
            false
        }
        Err(_) => {
            set_error(out_error, ErrorCode::PanicCaught, "Panic in Rust code");
            false
        }
    }
}

/// Mean Absolute Scaled Error (Hyndman & Koehler 2006, D-08). `actual_validity`/
/// `forecast_validity` may each be null (all valid); rows missing in EITHER
/// actual or forecast are dropped before computing the evaluation MAE (D-02,
/// same `complete_cases` policy as every other metric). `insample_validity`
/// may also be null (all valid); unlike `actual`/`forecast`, missing in-sample
/// positions are NOT dropped/complete-cased -- they are preserved exactly so
/// the lag-`seasonal_period` scale only skips the specific pairs touching a
/// missing value (never splices across one).
///
/// `*out_is_null` is set `true` (with the call still returning `true` for
/// "no error") when the in-sample scale is numerically zero -- MASE is
/// undefined there (R's `forecast::accuracy()` returns `Inf`; this product
/// returns SQL NULL instead). Always write to `*out_is_null` on a `true`
/// return; callers must check it before trusting `*out_result`.
///
/// # Safety
/// All pointer arguments must be valid and non-null (except the `*_validity`
/// pointers). Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_mase(
    actual: *const c_double,
    actual_validity: *const u64,
    actual_len: size_t,
    forecast: *const c_double,
    forecast_validity: *const u64,
    forecast_len: size_t,
    insample: *const c_double,
    insample_validity: *const u64,
    insample_len: size_t,
    seasonal_period: i64,
    out_result: *mut c_double,
    out_is_null: *mut bool,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);

    let ptrs = &[
        actual as *const core::ffi::c_void,
        forecast as *const core::ffi::c_void,
        insample as *const core::ffi::c_void,
        out_result as *const core::ffi::c_void,
        out_is_null as *const core::ffi::c_void,
    ];
    if check_null_pointers(out_error, ptrs) {
        return false;
    }

    if seasonal_period < 1 {
        set_error(
            out_error,
            ErrorCode::InvalidInput,
            &format!("seasonal_period must be >= 1, got {seasonal_period}"),
        );
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let actual_series = build_series(actual, actual_validity, actual_len);
        let forecast_series = build_series(forecast, forecast_validity, forecast_len);
        if actual_series.len() != forecast_series.len() {
            return Err(anofox_fcst_core::ForecastError::InvalidInput(format!(
                "actual and forecast arrays must have the same length: {} vs {}",
                actual_series.len(),
                forecast_series.len()
            )));
        }
        let filtered = anofox_fcst_core::complete_cases(&[&actual_series, &forecast_series])?;
        let insample_series = build_series(insample, insample_validity, insample_len);
        anofox_fcst_core::mase(
            &filtered[0],
            &filtered[1],
            &insample_series,
            seasonal_period as usize,
        )
    }));

    match result {
        Ok(Ok(Some(value))) => {
            *out_result = value;
            *out_is_null = false;
            true
        }
        Ok(Ok(None)) => {
            *out_is_null = true;
            true
        }
        Ok(Err(e)) => {
            set_error(out_error, metric_error_code(&e), &e.to_string());
            false
        }
        Err(_) => {
            set_error(out_error, ErrorCode::PanicCaught, "Panic in Rust code");
            false
        }
    }
}

/// Quantile loss function. `*_validity` pointers may each be null (all valid);
/// pairs missing in either actual or forecast are dropped (D-02).
///
/// # Safety
/// All pointer arguments must be valid and non-null (except the `*_validity`
/// pointers). Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_quantile_loss(
    actual: *const c_double,
    actual_validity: *const u64,
    actual_len: size_t,
    forecast: *const c_double,
    forecast_validity: *const u64,
    forecast_len: size_t,
    quantile: c_double,
    out_result: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
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
        let actual_series = build_series(actual, actual_validity, actual_len);
        let forecast_series = build_series(forecast, forecast_validity, forecast_len);
        if actual_series.len() != forecast_series.len() {
            return Err(anofox_fcst_core::ForecastError::InvalidInput(format!(
                "arrays must have the same length: {} vs {}",
                actual_series.len(),
                forecast_series.len()
            )));
        }
        let filtered = anofox_fcst_core::complete_cases(&[&actual_series, &forecast_series])?;
        anofox_fcst_core::quantile_loss(&filtered[0], &filtered[1], quantile)
    }));

    match result {
        Ok(Ok(value)) => {
            *out_result = value;
            true
        }
        Ok(Err(e)) => {
            set_error(out_error, metric_error_code(&e), &e.to_string());
            false
        }
        Err(_) => {
            set_error(out_error, ErrorCode::PanicCaught, "Panic in Rust code");
            false
        }
    }
}

/// Multi-quantile loss function. `quantiles is a 2D array: n_levels arrays,
/// each of length actual_len. `quantiles_validity` is a nullable array of
/// `n_levels` nullable per-level bitmasks (a null entry means "all valid" for
/// that level); `actual_validity` may be null (all valid). `levels` itself is
/// a PARAMETER list — NULL levels are rejected before the FFI call
/// (`ListDoubleReader::ReadNoNulls` on the C++ side), so there is no
/// `levels_validity` parameter. Each quantile level independently drops rows
/// missing in either `actual` or that level's forecast (D-02).
///
/// # Safety
/// All pointer arguments must be valid and non-null (except `actual_validity`
/// and `quantiles_validity`, and each per-level pointer inside
/// `quantiles_validity`). Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_mqloss(
    actual: *const c_double,
    actual_validity: *const u64,
    actual_len: size_t,
    quantiles: *const *const c_double, // Array of pointers to quantile forecast arrays
    quantiles_validity: *const *const u64, // nullable array of nullable per-level bitmasks
    n_levels: size_t,
    levels: *const c_double,
    out_result: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);

    let ptrs = &[
        actual as *const core::ffi::c_void,
        quantiles as *const core::ffi::c_void,
        levels as *const core::ffi::c_void,
        out_result as *const core::ffi::c_void,
    ];
    if check_null_pointers(out_error, ptrs) {
        return false;
    }

    if n_levels == 0 {
        set_error(
            out_error,
            ErrorCode::InvalidInput,
            "Must have at least one quantile level",
        );
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let actual_series = build_series(actual, actual_validity, actual_len);
        let levels_vec = std::slice::from_raw_parts(levels, n_levels).to_vec();

        let mut total_loss = 0.0;
        // `i` indexes raw pointer arithmetic (`quantiles.add(i)`,
        // `quantiles_validity.add(i)`) alongside `levels_vec[i]`; an iterator
        // can't express the pointer-offset access, so a range loop is correct here.
        #[allow(clippy::needless_range_loop)]
        for i in 0..n_levels {
            let quantile_ptr = *quantiles.add(i);
            if quantile_ptr.is_null() {
                return Err(anofox_fcst_core::ForecastError::InvalidInput(format!(
                    "Null pointer at quantile index {}",
                    i
                )));
            }
            let level_validity = if quantiles_validity.is_null() {
                std::ptr::null()
            } else {
                *quantiles_validity.add(i)
            };
            let forecast_series = build_series(quantile_ptr, level_validity, actual_len);
            if actual_series.len() != forecast_series.len() {
                return Err(anofox_fcst_core::ForecastError::InvalidInput(format!(
                    "arrays must have the same length: {} vs {}",
                    actual_series.len(),
                    forecast_series.len()
                )));
            }
            let filtered = anofox_fcst_core::complete_cases(&[&actual_series, &forecast_series])?;
            total_loss +=
                anofox_fcst_core::quantile_loss(&filtered[0], &filtered[1], levels_vec[i])?;
        }

        Ok(total_loss / n_levels as f64)
    }));

    match result {
        Ok(Ok(value)) => {
            *out_result = value;
            true
        }
        Ok(Err(e)) => {
            set_error(out_error, metric_error_code(&e), &e.to_string());
            false
        }
        Err(_) => {
            set_error(out_error, ErrorCode::PanicCaught, "Panic in Rust code");
            false
        }
    }
}

/// Coverage of prediction intervals. `*_validity` pointers may each be null
/// (all valid); rows missing in ANY of actual/lower/upper are dropped (D-02).
///
/// # Safety
/// All pointer arguments must be valid and non-null (except the `*_validity`
/// pointers). Arrays must have the specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_coverage(
    actual: *const c_double,
    actual_validity: *const u64,
    lower: *const c_double,
    lower_validity: *const u64,
    upper: *const c_double,
    upper_validity: *const u64,
    length: size_t,
    out_result: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);

    let ptrs = &[
        actual as *const core::ffi::c_void,
        lower as *const core::ffi::c_void,
        upper as *const core::ffi::c_void,
        out_result as *const core::ffi::c_void,
    ];
    if check_null_pointers(out_error, ptrs) {
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let actual_series = build_series(actual, actual_validity, length);
        let lower_series = build_series(lower, lower_validity, length);
        let upper_series = build_series(upper, upper_validity, length);
        let filtered =
            anofox_fcst_core::complete_cases(&[&actual_series, &lower_series, &upper_series])?;
        anofox_fcst_core::coverage(&filtered[0], &filtered[1], &filtered[2])
    }));

    match result {
        Ok(Ok(value)) => {
            *out_result = value;
            true
        }
        Ok(Err(e)) => {
            set_error(out_error, metric_error_code(&e), &e.to_string());
            false
        }
        Err(_) => {
            set_error(out_error, ErrorCode::PanicCaught, "Panic in Rust code");
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
                    set_error(
                        out_error,
                        ErrorCode::AllocationError,
                        "Memory allocation failed",
                    );
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
                    set_error(
                        out_error,
                        ErrorCode::AllocationError,
                        "Memory allocation failed",
                    );
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
        // Use defaults: max_period (None = 365), min_confidence (None = method-specific default)
        anofox_fcst_core::detect_periods(&values_vec, period_method, None, None)
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

/// Detect multiple periods in a time series using the specified method.
///
/// Returns a flattened result with parallel arrays for safer FFI.
/// This version avoids memory issues when used through R's DuckDB bindings.
///
/// # Arguments
/// * `max_period` - Maximum period to search (0 = use default of 365)
/// * `min_confidence` - Minimum confidence threshold; periods below this are filtered out.
///   Use negative value (e.g., -1.0) to use method-specific default.
///   Use 0.0 to disable filtering. Use positive value for custom threshold.
/// * `expected_periods` - Optional array of expected periods to validate against (NULL = no validation)
/// * `n_expected` - Number of expected periods (0 if expected_periods is NULL)
/// * `tolerance` - Relative tolerance for matching (negative = use default 0.1)
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_detect_periods_flat(
    values: *const c_double,
    length: size_t,
    method: *const c_char,
    max_period: size_t,
    min_confidence: c_double,
    expected_periods: *const c_double,
    n_expected: size_t,
    tolerance: c_double,
    out_result: *mut types::FlatMultiPeriodResult,
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
        // Convert 0 to None (use default), otherwise Some(max_period)
        let max_period_opt = if max_period == 0 {
            None
        } else {
            Some(max_period)
        };
        // Convert min_confidence: negative = use default, 0 or positive = use value
        let min_confidence_opt = if min_confidence < 0.0 || min_confidence.is_nan() {
            None // Use method-specific default
        } else {
            Some(min_confidence) // 0.0 disables filtering, positive value is threshold
        };

        // Convert expected_periods: NULL or n_expected=0 means no validation
        let expected_vec: Option<Vec<f64>> = if expected_periods.is_null() || n_expected == 0 {
            None
        } else {
            Some(std::slice::from_raw_parts(expected_periods, n_expected).to_vec())
        };

        // Convert tolerance: negative = use default
        let tolerance_opt = if tolerance < 0.0 || tolerance.is_nan() {
            None
        } else {
            Some(tolerance)
        };

        anofox_fcst_core::detect_periods_with_validation(
            &values_vec,
            period_method,
            max_period_opt,
            min_confidence_opt,
            expected_vec.as_deref(),
            tolerance_opt,
        )
    }));

    match result {
        Ok(Ok(multi_result)) => {
            let n = multi_result.periods.len();
            (*out_result).n_periods = n;
            (*out_result).primary_period = multi_result.primary_period;
            copy_string_to_buffer(&multi_result.method, &mut (*out_result).method);

            if n > 0 {
                // Allocate parallel arrays instead of struct array
                let period_ptr = malloc(n * std::mem::size_of::<c_double>()) as *mut c_double;
                let confidence_ptr = malloc(n * std::mem::size_of::<c_double>()) as *mut c_double;
                let strength_ptr = malloc(n * std::mem::size_of::<c_double>()) as *mut c_double;
                let amplitude_ptr = malloc(n * std::mem::size_of::<c_double>()) as *mut c_double;
                let phase_ptr = malloc(n * std::mem::size_of::<c_double>()) as *mut c_double;
                let iteration_ptr = malloc(n * std::mem::size_of::<size_t>()) as *mut size_t;
                let matches_expected_ptr = malloc(n * std::mem::size_of::<bool>()) as *mut bool;
                let matched_expected_ptr =
                    malloc(n * std::mem::size_of::<c_double>()) as *mut c_double;
                let match_deviation_ptr =
                    malloc(n * std::mem::size_of::<c_double>()) as *mut c_double;

                for (i, dp) in multi_result.periods.iter().enumerate() {
                    *period_ptr.add(i) = dp.period;
                    *confidence_ptr.add(i) = dp.confidence;
                    *strength_ptr.add(i) = dp.strength;
                    *amplitude_ptr.add(i) = dp.amplitude;
                    *phase_ptr.add(i) = dp.phase;
                    *iteration_ptr.add(i) = dp.iteration;
                    *matches_expected_ptr.add(i) = dp.matches_expected;
                    *matched_expected_ptr.add(i) = dp.matched_expected_period.unwrap_or(f64::NAN);
                    *match_deviation_ptr.add(i) = dp.match_deviation.unwrap_or(f64::NAN);
                }

                (*out_result).period_values = period_ptr;
                (*out_result).confidence_values = confidence_ptr;
                (*out_result).strength_values = strength_ptr;
                (*out_result).amplitude_values = amplitude_ptr;
                (*out_result).phase_values = phase_ptr;
                (*out_result).iteration_values = iteration_ptr;
                (*out_result).matches_expected_values = matches_expected_ptr;
                (*out_result).matched_expected_values = matched_expected_ptr;
                (*out_result).match_deviation_values = match_deviation_ptr;
            } else {
                (*out_result).period_values = ptr::null_mut();
                (*out_result).confidence_values = ptr::null_mut();
                (*out_result).strength_values = ptr::null_mut();
                (*out_result).amplitude_values = ptr::null_mut();
                (*out_result).phase_values = ptr::null_mut();
                (*out_result).iteration_values = ptr::null_mut();
                (*out_result).matches_expected_values = ptr::null_mut();
                (*out_result).matched_expected_values = ptr::null_mut();
                (*out_result).match_deviation_values = ptr::null_mut();
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

/// Detect multiple periods in time series.
///
/// Returns a flattened result with parallel arrays for safer FFI.
/// This version avoids memory issues when used through R's DuckDB bindings.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_detect_multiple_periods_flat(
    values: *const c_double,
    length: size_t,
    max_periods: c_int,
    min_confidence: c_double,
    min_strength: c_double,
    out_result: *mut types::FlatMultiPeriodResult,
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
                // Allocate parallel arrays instead of struct array
                let period_ptr = malloc(n * std::mem::size_of::<c_double>()) as *mut c_double;
                let confidence_ptr = malloc(n * std::mem::size_of::<c_double>()) as *mut c_double;
                let strength_ptr = malloc(n * std::mem::size_of::<c_double>()) as *mut c_double;
                let amplitude_ptr = malloc(n * std::mem::size_of::<c_double>()) as *mut c_double;
                let phase_ptr = malloc(n * std::mem::size_of::<c_double>()) as *mut c_double;
                let iteration_ptr = malloc(n * std::mem::size_of::<size_t>()) as *mut size_t;
                let matches_expected_ptr = malloc(n * std::mem::size_of::<bool>()) as *mut bool;
                let matched_expected_ptr =
                    malloc(n * std::mem::size_of::<c_double>()) as *mut c_double;
                let match_deviation_ptr =
                    malloc(n * std::mem::size_of::<c_double>()) as *mut c_double;

                for (i, dp) in multi_result.periods.iter().enumerate() {
                    *period_ptr.add(i) = dp.period;
                    *confidence_ptr.add(i) = dp.confidence;
                    *strength_ptr.add(i) = dp.strength;
                    *amplitude_ptr.add(i) = dp.amplitude;
                    *phase_ptr.add(i) = dp.phase;
                    *iteration_ptr.add(i) = dp.iteration;
                    *matches_expected_ptr.add(i) = dp.matches_expected;
                    *matched_expected_ptr.add(i) = dp.matched_expected_period.unwrap_or(f64::NAN);
                    *match_deviation_ptr.add(i) = dp.match_deviation.unwrap_or(f64::NAN);
                }

                (*out_result).period_values = period_ptr;
                (*out_result).confidence_values = confidence_ptr;
                (*out_result).strength_values = strength_ptr;
                (*out_result).amplitude_values = amplitude_ptr;
                (*out_result).phase_values = phase_ptr;
                (*out_result).iteration_values = iteration_ptr;
                (*out_result).matches_expected_values = matches_expected_ptr;
                (*out_result).matched_expected_values = matched_expected_ptr;
                (*out_result).match_deviation_values = match_deviation_ptr;
            } else {
                (*out_result).period_values = ptr::null_mut();
                (*out_result).confidence_values = ptr::null_mut();
                (*out_result).strength_values = ptr::null_mut();
                (*out_result).amplitude_values = ptr::null_mut();
                (*out_result).phase_values = ptr::null_mut();
                (*out_result).iteration_values = ptr::null_mut();
                (*out_result).matches_expected_values = ptr::null_mut();
                (*out_result).matched_expected_values = ptr::null_mut();
                (*out_result).match_deviation_values = ptr::null_mut();
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

/// Autoperiod: FFT period detection with ACF validation.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_autoperiod(
    values: *const c_double,
    length: size_t,
    acf_threshold: c_double,
    out_result: *mut types::AutoperiodResultFFI,
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
        let threshold = if acf_threshold > 0.0 {
            Some(acf_threshold)
        } else {
            None
        };
        anofox_fcst_core::autoperiod(&values_vec, threshold)
    }));

    match result {
        Ok(Ok(ap_result)) => {
            (*out_result).period = ap_result.period;
            (*out_result).fft_confidence = ap_result.fft_confidence;
            (*out_result).acf_validation = ap_result.acf_validation;
            (*out_result).detected = ap_result.detected;
            copy_string_to_buffer(&ap_result.method, &mut (*out_result).method);
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

/// CFD Autoperiod: First-differenced FFT with ACF validation.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_cfd_autoperiod(
    values: *const c_double,
    length: size_t,
    acf_threshold: c_double,
    out_result: *mut types::AutoperiodResultFFI,
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
        let threshold = if acf_threshold > 0.0 {
            Some(acf_threshold)
        } else {
            None
        };
        anofox_fcst_core::cfd_autoperiod(&values_vec, threshold)
    }));

    match result {
        Ok(Ok(ap_result)) => {
            (*out_result).period = ap_result.period;
            (*out_result).fft_confidence = ap_result.fft_confidence;
            (*out_result).acf_validation = ap_result.acf_validation;
            (*out_result).detected = ap_result.detected;
            copy_string_to_buffer(&ap_result.method, &mut (*out_result).method);
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

/// Lomb-Scargle periodogram for period detection.
///
/// Optimal for detecting periodic signals in unevenly sampled data.
/// More robust than FFT for noisy data and provides statistical significance.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_lomb_scargle(
    values: *const c_double,
    length: size_t,
    min_period: c_double,
    max_period: c_double,
    n_frequencies: size_t,
    out_result: *mut types::LombScargleResultFFI,
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
        let min_p = if min_period > 0.0 {
            Some(min_period)
        } else {
            None
        };
        let max_p = if max_period > 0.0 {
            Some(max_period)
        } else {
            None
        };
        let n_freq = if n_frequencies > 0 {
            Some(n_frequencies)
        } else {
            None
        };
        anofox_fcst_core::lomb_scargle(&values_vec, None, min_p, max_p, n_freq)
    }));

    match result {
        Ok(Ok(ls_result)) => {
            (*out_result).period = ls_result.period;
            (*out_result).frequency = ls_result.frequency;
            (*out_result).power = ls_result.power;
            (*out_result).false_alarm_prob = ls_result.false_alarm_prob;
            copy_string_to_buffer(&ls_result.method, &mut (*out_result).method);
            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                (*out_error).set_error(ffi_error_code(&e), &e.to_string());
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

/// AIC-based period comparison.
///
/// Fits sinusoidal models with different candidate periods and selects
/// the one with the lowest AIC.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_aic_period(
    values: *const c_double,
    length: size_t,
    min_period: c_double,
    max_period: c_double,
    n_candidates: size_t,
    out_result: *mut types::AicPeriodResultFFI,
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
        let min_p = if min_period > 0.0 {
            Some(min_period)
        } else {
            None
        };
        let max_p = if max_period > 0.0 {
            Some(max_period)
        } else {
            None
        };
        let n_cand = if n_candidates > 0 {
            Some(n_candidates)
        } else {
            None
        };
        anofox_fcst_core::aic_comparison(&values_vec, min_p, max_p, n_cand, None)
    }));

    match result {
        Ok(Ok(aic_result)) => {
            (*out_result).period = aic_result.period;
            (*out_result).aic = aic_result.aic;
            (*out_result).bic = aic_result.bic;
            (*out_result).rss = aic_result.rss;
            (*out_result).r_squared = aic_result.r_squared;
            copy_string_to_buffer(&aic_result.method, &mut (*out_result).method);
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

/// SSA (Singular Spectrum Analysis) for period detection.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_ssa_period(
    values: *const c_double,
    length: size_t,
    window_size: size_t,
    n_components: size_t,
    out_result: *mut types::SsaPeriodResultFFI,
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
        let win_size = if window_size > 0 {
            Some(window_size)
        } else {
            None
        };
        let n_comp = if n_components > 0 {
            Some(n_components)
        } else {
            None
        };
        anofox_fcst_core::ssa_period(&values_vec, win_size, n_comp)
    }));

    match result {
        Ok(Ok(ssa_result)) => {
            (*out_result).period = ssa_result.period;
            (*out_result).variance_explained = ssa_result.variance_explained;
            (*out_result).n_eigenvalues = ssa_result.eigenvalues.len();
            copy_string_to_buffer(&ssa_result.method, &mut (*out_result).method);
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

/// STL-based period detection via seasonal strength optimization.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_stl_period(
    values: *const c_double,
    length: size_t,
    min_period: size_t,
    max_period: size_t,
    n_candidates: size_t,
    out_result: *mut types::StlPeriodResultFFI,
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
        let min_p = if min_period > 0 {
            Some(min_period)
        } else {
            None
        };
        let max_p = if max_period > 0 {
            Some(max_period)
        } else {
            None
        };
        let n_cand = if n_candidates > 0 {
            Some(n_candidates)
        } else {
            None
        };
        anofox_fcst_core::stl_period(&values_vec, min_p, max_p, n_cand)
    }));

    match result {
        Ok(Ok(stl_result)) => {
            (*out_result).period = stl_result.period;
            (*out_result).seasonal_strength = stl_result.seasonal_strength;
            (*out_result).trend_strength = stl_result.trend_strength;
            copy_string_to_buffer(&stl_result.method, &mut (*out_result).method);
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

/// Matrix Profile period detection.
///
/// Uses Matrix Profile to find motifs and estimate periodicity from
/// the distribution of motif distances.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_matrix_profile_period(
    values: *const c_double,
    length: size_t,
    subsequence_length: size_t,
    n_best: size_t,
    out_result: *mut types::MatrixProfilePeriodResultFFI,
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
        let subseq_len = if subsequence_length > 0 {
            Some(subsequence_length)
        } else {
            None
        };
        let n_b = if n_best > 0 { Some(n_best) } else { None };
        anofox_fcst_core::matrix_profile_period(&values_vec, subseq_len, n_b)
    }));

    match result {
        Ok(Ok(mp_result)) => {
            (*out_result).period = mp_result.period;
            (*out_result).confidence = mp_result.confidence;
            (*out_result).n_motifs = mp_result.n_motifs;
            (*out_result).subsequence_length = mp_result.subsequence_length;
            copy_string_to_buffer(&mp_result.method, &mut (*out_result).method);
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

/// SAZED period detection using spectral analysis with zero-padding.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_sazed_period(
    values: *const c_double,
    length: size_t,
    min_period: size_t,
    max_period: size_t,
    zero_pad_factor: size_t,
    out_result: *mut types::SazedPeriodResultFFI,
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
        let zp_factor = if zero_pad_factor > 0 {
            Some(zero_pad_factor)
        } else {
            None
        };
        let min_p = if min_period > 0 {
            Some(min_period)
        } else {
            None
        };
        let max_p = if max_period > 0 {
            Some(max_period)
        } else {
            None
        };
        // sazed_period signature: padding_factor, min_period, max_period
        anofox_fcst_core::sazed_period(&values_vec, zp_factor, min_p, max_p)
    }));

    match result {
        Ok(Ok(sazed_result)) => {
            (*out_result).period = sazed_result.period;
            (*out_result).power = sazed_result.power;
            (*out_result).snr = sazed_result.snr;
            copy_string_to_buffer(&sazed_result.method, &mut (*out_result).method);
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

    // WR-03: the stale duplicate `period <= 0.0` pre-check that used to live
    // here was removed. It never rejected NaN (`nan <= 0.0` is false) and
    // rejected -Infinity with the old "Period must be positive" wording
    // instead of the core-level check's "Period must be a positive finite
    // number" -- a second source of truth that could desync from the
    // core-level `!period.is_finite() || period <= 0.0` check (CR-03) this
    // call falls through to below. The core-level check is strictly a
    // superset, so there is now exactly one source of truth.
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
            // CR-05: this used to hardcode ErrorCode::ComputationError
            // regardless of the underlying error, so the C++ wrapper's
            // `error.code == INVALID_INPUT` check (added to fix CR-05) could
            // never see INVALID_INPUT here and always fell through to a
            // silent NULL instead of raising the CR-03 InvalidParameter
            // error (e.g. a NaN/non-finite/non-positive period) as a SQL
            // error. Use the same ffi_error_code mapping every other
            // period-validating function in this file already uses.
            if !out_error.is_null() {
                (*out_error).set_error(ffi_error_code(&e), &e.to_string());
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

/// Map a core `ForecastError` to the FFI `ErrorCode` a caller should see.
///
/// `InvalidParameter` (D-02: strict method-name parsing) and `InvalidInput`
/// are caller mistakes -- the C++ layer re-throws these as a DuckDB
/// `InvalidInputException` (precedent: `ts_data_quality.cpp`). Every other
/// `ForecastError` variant is an internal computation failure. Phase 15
/// plans reuse this helper for `anofox_ts_decompose` and future strict
/// parameter validation.
fn ffi_error_code(e: &anofox_fcst_core::ForecastError) -> ErrorCode {
    match e {
        anofox_fcst_core::ForecastError::InvalidParameter { .. }
        | anofox_fcst_core::ForecastError::InvalidInput(_) => ErrorCode::InvalidInput,
        _ => ErrorCode::ComputationError,
    }
}

/// Parse an optional `regression` C string into a `StationarityRegression`
/// (D-05): `anofox_ts_adf` / `anofox_ts_kpss` / `anofox_ts_stationarity`
/// call this OUTSIDE their `catch_unwind` closure so an invalid value is
/// reported as `ErrorCode::InvalidInput` (via [`ffi_error_code`]) rather
/// than caught as a panic. A null pointer means the default (`Constant`,
/// "c"); invalid UTF-8 is `ForecastError::InvalidInput`; any other value is
/// delegated to `StationarityRegression::from_str`'s strict, case-insensitive
/// parsing (`InvalidParameter`, message "expected one of: c, ct").
///
/// # Safety
///
/// `ptr` must be either null or a valid NUL-terminated C string.
unsafe fn parse_regression(
    ptr: *const c_char,
) -> Result<anofox_fcst_core::StationarityRegression, anofox_fcst_core::ForecastError> {
    if ptr.is_null() {
        return Ok(anofox_fcst_core::StationarityRegression::Constant);
    }
    let s = CStr::from_ptr(ptr).to_str().map_err(|_| {
        anofox_fcst_core::ForecastError::InvalidInput("regression is not valid UTF-8".to_string())
    })?;
    s.parse()
}

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
        // D-07/T-10-07: invalid UTF-8 is an error, never a silent "auto"
        // fallback. D-02 (Phase 15): the method-NAME parse itself is now
        // strict too -- an unknown method name is a caller error, not a
        // silent fallback to "auto".
        let method_str = if method.is_null() {
            "auto"
        } else {
            match CStr::from_ptr(method).to_str() {
                Ok(s) => s,
                Err(_) => {
                    return Err(anofox_fcst_core::ForecastError::InvalidInput(
                        "method is not valid UTF-8".to_string(),
                    ))
                }
            }
        };
        let detrend_method: anofox_fcst_core::DetrendMethod = method_str.parse()?;
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
                (*out_error).set_error(ffi_error_code(&e), &e.to_string());
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

    // WR-03: the stale duplicate `period <= 0.0` pre-check that used to live
    // here was removed. It never rejected NaN (`nan <= 0.0` is false) and
    // rejected -Infinity with the old "Period must be positive" wording
    // instead of the core-level check's "Period must be a positive finite
    // number" -- a second source of truth that could desync from the
    // core-level `!period.is_finite() || period <= 0.0` check (CR-03) this
    // call falls through to below. The core-level check is strictly a
    // superset, so there is now exactly one source of truth.
    let result = catch_unwind(AssertUnwindSafe(|| {
        let values_vec = std::slice::from_raw_parts(values, length).to_vec();
        // D-02 (Phase 15, Task 3): strict method-name parsing, same
        // discipline as anofox_ts_detrend -- an unknown method name is a
        // caller error, not a silent fallback to "additive".
        let method_str = if method.is_null() {
            "additive"
        } else {
            match CStr::from_ptr(method).to_str() {
                Ok(s) => s,
                Err(_) => {
                    return Err(anofox_fcst_core::ForecastError::InvalidInput(
                        "method is not valid UTF-8".to_string(),
                    ))
                }
            }
        };
        let decompose_method: anofox_fcst_core::DecomposeMethod = method_str.parse()?;
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
                (*out_error).set_error(ffi_error_code(&e), &e.to_string());
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

    // WR-03: the stale duplicate `period <= 0.0` pre-check that used to live
    // here was removed. It never rejected NaN (`nan <= 0.0` is false) and
    // rejected -Infinity with the old "Period must be positive" wording
    // instead of the core-level check's "Period must be a positive finite
    // number" -- a second source of truth that could desync from the
    // core-level `!period.is_finite() || period <= 0.0` check (CR-03) this
    // call falls through to below. The core-level check is strictly a
    // superset, so there is now exactly one source of truth.
    let result = catch_unwind(AssertUnwindSafe(|| {
        let values_vec = std::slice::from_raw_parts(values, length).to_vec();
        let method_str = if method.is_null() {
            "variance"
        } else {
            CStr::from_ptr(method).to_str().unwrap_or("variance")
        };
        // D-12: strict parse -- an unknown method (or the removed
        // "wavelet") now returns an error instead of silently falling
        // back to Variance.
        let strength_method: anofox_fcst_core::StrengthMethod = method_str.parse()?;
        anofox_fcst_core::seasonal_strength(&values_vec, period, strength_method)
    }));

    match result {
        Ok(Ok(strength)) => {
            *out_strength = strength;
            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                (*out_error).set_error(ffi_error_code(&e), &e.to_string());
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

    // WR-03: the stale duplicate `period <= 0.0` pre-check that used to live
    // here was removed. It never rejected NaN (`nan <= 0.0` is false) and
    // rejected -Infinity with the old "Period must be positive" wording
    // instead of the core-level check's "Period must be a positive finite
    // number" -- a second source of truth that could desync from the
    // core-level `!period.is_finite() || period <= 0.0` check (CR-03) this
    // call falls through to below. The core-level check is strictly a
    // superset, so there is now exactly one source of truth.
    let result = catch_unwind(AssertUnwindSafe(|| {
        let values_vec = std::slice::from_raw_parts(values, length).to_vec();
        let window_opt = if window_size > 0.0 {
            Some(window_size)
        } else {
            None
        };
        let method_str: Option<anofox_fcst_core::StrengthMethod> = if method.is_null() {
            None
        } else {
            let s = CStr::from_ptr(method).to_str().unwrap_or("variance");
            // D-12: strict parse -- see anofox_ts_seasonal_strength above.
            Some(s.parse::<anofox_fcst_core::StrengthMethod>()?)
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
                (*out_error).set_error(ffi_error_code(&e), &e.to_string());
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

    // WR-03: the stale duplicate `period <= 0.0` pre-check that used to live
    // here was removed. It never rejected NaN (`nan <= 0.0` is false) and
    // rejected -Infinity with the old "Period must be positive" wording
    // instead of the core-level check's "Period must be a positive finite
    // number" -- a second source of truth that could desync from the
    // core-level `!period.is_finite() || period <= 0.0` check (CR-03) this
    // call falls through to below. The core-level check is strictly a
    // superset, so there is now exactly one source of truth.
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
                (*out_error).set_error(ffi_error_code(&e), &e.to_string());
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

    // WR-03: the stale duplicate `period <= 0.0` pre-check that used to live
    // here was removed. It never rejected NaN (`nan <= 0.0` is false) and
    // rejected -Infinity with the old "Period must be positive" wording
    // instead of the core-level check's "Period must be a positive finite
    // number" -- a second source of truth that could desync from the
    // core-level `!period.is_finite() || period <= 0.0` check (CR-03) this
    // call falls through to below. The core-level check is strictly a
    // superset, so there is now exactly one source of truth.
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
            // CR-05: this used to hardcode ErrorCode::ComputationError
            // regardless of the underlying error, so the C++ wrapper's
            // `error.code == INVALID_INPUT` check (added to fix CR-05) could
            // never see INVALID_INPUT here and always fell through to a
            // silent NULL instead of raising the CR-03 InvalidParameter
            // error (e.g. a NaN/non-finite/non-positive period) as a SQL
            // error. Use the same ffi_error_code mapping every other
            // period-validating function in this file already uses.
            if !out_error.is_null() {
                (*out_error).set_error(ffi_error_code(&e), &e.to_string());
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

    // WR-03: the stale duplicate `period <= 0.0` pre-check that used to live
    // here was removed. It never rejected NaN (`nan <= 0.0` is false) and
    // rejected -Infinity with the old "Period must be positive" wording
    // instead of the core-level check's "Period must be a positive finite
    // number" -- a second source of truth that could desync from the
    // core-level `!period.is_finite() || period <= 0.0` check (CR-03) this
    // call falls through to below. The core-level check is strictly a
    // superset, so there is now exactly one source of truth.
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
            // CR-05: this used to hardcode ErrorCode::ComputationError
            // regardless of the underlying error, so the C++ wrapper's
            // `error.code == INVALID_INPUT` check (added to fix CR-05) could
            // never see INVALID_INPUT here and always fell through to a
            // silent NULL instead of raising the CR-03 InvalidParameter
            // error (e.g. a NaN/non-finite/non-positive period) as a SQL
            // error. Use the same ffi_error_code mapping every other
            // period-validating function in this file already uses.
            if !out_error.is_null() {
                (*out_error).set_error(ffi_error_code(&e), &e.to_string());
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
                match alloc_or_error(trend, out_error, "Failed to allocate trend") {
                    Ok(ptr) => (*out_result).trend = ptr,
                    Err(()) => return false,
                }
            } else {
                (*out_result).n_observations = length;
                (*out_result).trend = ptr::null_mut();
            }

            // Copy remainder (may be None if decomposition was skipped)
            if let Some(ref remainder) = decomp.remainder {
                match alloc_or_error(remainder, out_error, "Failed to allocate remainder") {
                    Ok(ptr) => (*out_result).remainder = ptr,
                    Err(()) => {
                        free_ptr((*out_result).trend as *mut core::ffi::c_void);
                        (*out_result).trend = ptr::null_mut();
                        return false;
                    }
                }
            } else {
                (*out_result).remainder = ptr::null_mut();
            }

            // Copy seasonal periods
            if !decomp.periods.is_empty() {
                let periods_ptr =
                    malloc(decomp.periods.len() * std::mem::size_of::<c_int>()) as *mut c_int;
                if periods_ptr.is_null() {
                    if !out_error.is_null() {
                        (*out_error).set_error(
                            ErrorCode::AllocationError,
                            "Failed to allocate seasonal periods",
                        );
                    }
                    free_ptr((*out_result).trend as *mut core::ffi::c_void);
                    free_ptr((*out_result).remainder as *mut core::ffi::c_void);
                    (*out_result).trend = ptr::null_mut();
                    (*out_result).remainder = ptr::null_mut();
                    return false;
                }
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
                if comps_ptr.is_null() {
                    if !out_error.is_null() {
                        (*out_error).set_error(
                            ErrorCode::AllocationError,
                            "Failed to allocate seasonal components array",
                        );
                    }
                    free_ptr((*out_result).trend as *mut core::ffi::c_void);
                    free_ptr((*out_result).remainder as *mut core::ffi::c_void);
                    free_ptr((*out_result).seasonal_periods as *mut core::ffi::c_void);
                    (*out_result).trend = ptr::null_mut();
                    (*out_result).remainder = ptr::null_mut();
                    (*out_result).seasonal_periods = ptr::null_mut();
                    return false;
                }
                for (i, comp) in decomp.seasonal.iter().enumerate() {
                    match alloc_or_error(comp, out_error, "Failed to allocate seasonal component") {
                        Ok(ptr) => *comps_ptr.add(i) = ptr,
                        Err(()) => {
                            // Clean up already allocated seasonal components
                            for j in 0..i {
                                free_ptr(*comps_ptr.add(j) as *mut core::ffi::c_void);
                            }
                            free_ptr(comps_ptr as *mut core::ffi::c_void);
                            free_ptr((*out_result).trend as *mut core::ffi::c_void);
                            free_ptr((*out_result).remainder as *mut core::ffi::c_void);
                            free_ptr((*out_result).seasonal_periods as *mut core::ffi::c_void);
                            (*out_result).trend = ptr::null_mut();
                            (*out_result).remainder = ptr::null_mut();
                            (*out_result).seasonal_periods = ptr::null_mut();
                            return false;
                        }
                    }
                }
                (*out_result).seasonal_components = comps_ptr;
            } else {
                (*out_result).seasonal_components = ptr::null_mut();
            }

            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                (*out_error).set_error(ffi_error_code(&e), &e.to_string());
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

/// BOCPD changepoint detection with an explicit `is_changepoint` decision
/// rule (#287, BUG-06, user decision D2-ii; `deseasonalize`/
/// `seasonal_period` added for BUG-05, D1-b): shared by both
/// `ts_detect_changepoints_by` and `ts_detect_changepoints_agg` so they
/// agree on hazard_lambda, threshold, deseasonalize and seasonal_period for
/// identical inputs.
///
/// `opts->threshold` NaN means "unset" (the default heuristic applies,
/// reproducing [`anofox_ts_detect_changepoints_bocpd`] bit for bit).
/// `opts->hazard_lambda <= 0.0` means "unset" (defaults to 250.0, same
/// convention as the sibling export above). A set, out-of-range
/// `opts->threshold` (not finite, or outside `[0, 1]`) returns an
/// `INVALID_INPUT`-coded error naming `threshold` in the message.
/// `opts->deseasonalize == false` (default) leaves `values` unchanged.
/// `opts->seasonal_period == 0` means "auto-detect the period"; a negative
/// value returns an `INVALID_INPUT`-coded error naming `seasonal_period`;
/// a positive value is the explicit period.
///
/// # Safety
/// All pointer arguments must be valid and non-null. Arrays must have the
/// specified lengths.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_detect_changepoints_bocpd_opts(
    values: *const c_double,
    length: size_t,
    opts: *const types::ChangepointOptionsFFI,
    out_result: *mut types::BocpdResult,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if values.is_null() || opts.is_null() || out_result.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let values_vec = std::slice::from_raw_parts(values, length).to_vec();
        let hazard_lambda = if (*opts).hazard_lambda > 0.0 {
            (*opts).hazard_lambda
        } else {
            250.0
        };
        let threshold = if (*opts).threshold.is_nan() {
            None
        } else {
            Some((*opts).threshold)
        };
        let seasonal_period_raw = (*opts).seasonal_period;
        if seasonal_period_raw < 0 {
            return Err(anofox_fcst_core::ForecastError::InvalidParameter {
                param: "seasonal_period".to_string(),
                value: seasonal_period_raw.to_string(),
                reason: "must be a non-negative integer".to_string(),
            });
        }
        let seasonal_period = if seasonal_period_raw == 0 {
            None
        } else {
            Some(seasonal_period_raw as usize)
        };
        anofox_fcst_core::detect_changepoints_bocpd_with_options(
            &values_vec,
            &anofox_fcst_core::ChangepointOptions {
                hazard_lambda,
                threshold,
                deseasonalize: (*opts).deseasonalize,
                seasonal_period,
            },
        )
    }));

    match result {
        Ok(Ok(bocpd)) => {
            let n = bocpd.is_changepoint.len();
            (*out_result).n_points = n;

            let is_cp_ptr = malloc(n * std::mem::size_of::<bool>()) as *mut bool;
            for (i, &cp) in bocpd.is_changepoint.iter().enumerate() {
                *is_cp_ptr.add(i) = cp;
            }
            (*out_result).is_changepoint = is_cp_ptr;

            let prob_ptr = malloc(n * std::mem::size_of::<c_double>()) as *mut c_double;
            for (i, &p) in bocpd.changepoint_probability.iter().enumerate() {
                *prob_ptr.add(i) = p;
            }
            (*out_result).changepoint_probability = prob_ptr;

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
                let error_code = match e.to_code() {
                    1 => ErrorCode::NullPointer,
                    2 => ErrorCode::InvalidInput,
                    3 => ErrorCode::ComputationError,
                    4 => ErrorCode::AllocationError,
                    5 => ErrorCode::InvalidModel,
                    6 => ErrorCode::InsufficientData,
                    7 => ErrorCode::InvalidDateFormat,
                    8 => ErrorCode::InvalidFrequency,
                    9 => ErrorCode::InvalidInput, // InvalidParameter -> InvalidInput at FFI boundary
                    _ => ErrorCode::InternalError,
                };
                (*out_error).set_error(error_code, &e.to_string());
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
    } else {
        *out_names = ptr::null_mut();
    }

    true
}

/// Free the feature-name array returned by `anofox_ts_features_list`.
///
/// # Safety
/// `names` and `n_names` must be exactly the pointer and count that
/// `anofox_ts_features_list` wrote into `*out_names`/`*out_count` for a
/// single call — this releases every name and the array itself through the
/// same allocator shim that allocated them (the size-prefixed WASM shim, or
/// libc `malloc`/`free` on native). Manually `free()`-ing WASM-shim-allocated
/// memory instead of calling this function reads/writes 8 bytes before the
/// real allocation and corrupts the Emscripten heap (#289). `names` may be
/// null (no-op). Call at most once per array returned by
/// `anofox_ts_features_list`.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_ts_features_list(names: *mut *mut c_char, n_names: size_t) {
    if names.is_null() {
        return;
    }

    for i in 0..n_names {
        let name_ptr = *names.add(i);
        if !name_ptr.is_null() {
            free(name_ptr as *mut core::ffi::c_void);
        }
    }
    free(names as *mut core::ffi::c_void);
}

// ============================================================================
// Forecast Functions
// ============================================================================

/// Generate forecasts for a time series.
///
/// Bounded, safe read of a fixed-size `[c_char; N]` FFI string field (D-07,
/// T-10-04/T-10-07).
///
/// Uses `CStr::from_bytes_until_nul` over the array's own bytes — never
/// `CStr::from_ptr`, which has no bound and would read past the end of a
/// buffer the caller forgot to NUL-terminate. A missing terminator or
/// invalid UTF-8 is an `InvalidInput` naming `field`, never a silent
/// `"auto"`/empty-string fallback.
///
/// # Safety
/// `buf` must be a valid, readable `[c_char; N]` array (true for every
/// `ForecastOptions`/`ForecastOptionsExog` field it is called on — they are
/// plain fixed-size arrays embedded in a `#[repr(C)]` struct dereferenced
/// from a caller-supplied pointer already validated non-null).
unsafe fn c_field_str<'a>(
    buf: &'a [c_char],
    field: &str,
) -> std::result::Result<&'a str, anofox_fcst_core::ForecastError> {
    let bytes = std::slice::from_raw_parts(buf.as_ptr() as *const u8, buf.len());
    let cstr = CStr::from_bytes_until_nul(bytes).map_err(|_| {
        anofox_fcst_core::ForecastError::InvalidInput(format!("{field} is not NUL-terminated"))
    })?;
    cstr.to_str().map_err(|_| {
        anofox_fcst_core::ForecastError::InvalidInput(format!("{field} is not valid UTF-8"))
    })
}

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
        let core_opts = build_core_options(&*options)?;
        anofox_fcst_core::forecast(&series, &core_opts)
    }));

    match result {
        Ok(Ok(forecast)) => {
            let n_forecasts = forecast.point.len();
            (*out_result).n_forecasts = n_forecasts;

            // Copy point forecasts with allocation error checking
            (*out_result).point_forecasts = match alloc_or_error(
                &forecast.point,
                out_error,
                "Failed to allocate point forecasts",
            ) {
                Ok(ptr) => ptr,
                Err(()) => return false,
            };

            (*out_result).lower_bounds = match alloc_or_error(
                &forecast.lower,
                out_error,
                "Failed to allocate lower bounds",
            ) {
                Ok(ptr) => ptr,
                Err(()) => {
                    // Clean up already allocated memory
                    free_ptr((*out_result).point_forecasts as *mut _);
                    (*out_result).point_forecasts = ptr::null_mut();
                    return false;
                }
            };

            (*out_result).upper_bounds = match alloc_or_error(
                &forecast.upper,
                out_error,
                "Failed to allocate upper bounds",
            ) {
                Ok(ptr) => ptr,
                Err(()) => {
                    free_ptr((*out_result).point_forecasts as *mut _);
                    free_ptr((*out_result).lower_bounds as *mut _);
                    (*out_result).point_forecasts = ptr::null_mut();
                    (*out_result).lower_bounds = ptr::null_mut();
                    return false;
                }
            };

            // Copy fitted values
            if let Some(ref fitted) = forecast.fitted {
                (*out_result).fitted_values =
                    match alloc_or_error(fitted, out_error, "Failed to allocate fitted values") {
                        Ok(ptr) => ptr,
                        Err(()) => {
                            free_ptr((*out_result).point_forecasts as *mut _);
                            free_ptr((*out_result).lower_bounds as *mut _);
                            free_ptr((*out_result).upper_bounds as *mut _);
                            (*out_result).point_forecasts = ptr::null_mut();
                            (*out_result).lower_bounds = ptr::null_mut();
                            (*out_result).upper_bounds = ptr::null_mut();
                            return false;
                        }
                    };
                (*out_result).n_fitted = fitted.len();
            } else {
                (*out_result).fitted_values = ptr::null_mut();
                (*out_result).n_fitted = 0;
            }

            // Copy residuals. n_residuals is its OWN length field (plan 10-02
            // fix) — it must never be aliased to n_fitted, since fitted and
            // residuals can legitimately have different presence/length
            // (GARCH always has fitted=None, residuals=Some; see
            // ForecastResult::n_residuals doc comment).
            if let Some(ref resid) = forecast.residuals {
                (*out_result).residuals =
                    match alloc_or_error(resid, out_error, "Failed to allocate residuals") {
                        Ok(ptr) => ptr,
                        Err(()) => {
                            free_ptr((*out_result).point_forecasts as *mut _);
                            free_ptr((*out_result).lower_bounds as *mut _);
                            free_ptr((*out_result).upper_bounds as *mut _);
                            free_ptr((*out_result).fitted_values as *mut _);
                            (*out_result).point_forecasts = ptr::null_mut();
                            (*out_result).lower_bounds = ptr::null_mut();
                            (*out_result).upper_bounds = ptr::null_mut();
                            (*out_result).fitted_values = ptr::null_mut();
                            return false;
                        }
                    };
                (*out_result).n_residuals = resid.len();
            } else {
                (*out_result).residuals = ptr::null_mut();
                (*out_result).n_residuals = 0;
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
                // Map ForecastError to appropriate ErrorCode
                let error_code = match e.to_code() {
                    1 => ErrorCode::NullPointer,
                    2 => ErrorCode::InvalidInput,
                    3 => ErrorCode::ComputationError,
                    4 => ErrorCode::AllocationError,
                    5 => ErrorCode::InvalidModel,
                    6 => ErrorCode::InsufficientData,
                    7 => ErrorCode::InvalidDateFormat,
                    8 => ErrorCode::InvalidFrequency,
                    9 => ErrorCode::InvalidInput, // InvalidParameter → InvalidInput at FFI boundary
                    _ => ErrorCode::InternalError,
                };
                (*out_error).set_error(error_code, &e.to_string());
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

/// Generate time series forecasts with exogenous variables.
///
/// This function extends `anofox_ts_forecast` to support external regressors (xreg).
/// Exogenous variables can improve forecast accuracy when external factors (e.g.,
/// promotions, holidays, weather) influence the target variable.
///
/// # Arguments
/// * `values` - Pointer to time series values (target variable y)
/// * `validity` - Pointer to validity bitmask (NULL means all valid)
/// * `length` - Number of observations
/// * `options` - Forecast options including exogenous data
/// * `out_result` - Output forecast result
/// * `out_error` - Output error (optional)
///
/// # Supported Models
/// The following models support exogenous variables:
/// - AutoARIMA, ARIMA (ARIMAX)
/// - OptimizedTheta, DynamicTheta
/// - MFLES
///
/// Other models will ignore the exogenous data and produce a standard forecast.
///
/// # Safety
/// All pointer arguments must be valid. Arrays must have the specified lengths.
/// For exogenous data:
/// - Each regressor's `n_values` must equal `length`
/// - Each regressor's `n_future` must equal `options.horizon`
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_forecast_exog(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    options: *const ForecastOptionsExog,
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

        let mut core_opts = build_core_options_exog(opts)?;

        // Build exogenous data if provided
        let exog_data = if !opts.exog.is_null() {
            let exog = &*opts.exog;
            if !exog.is_empty() {
                let mut historical: Vec<Vec<f64>> = Vec::with_capacity(exog.n_regressors);
                let mut future: Vec<Vec<f64>> = Vec::with_capacity(exog.n_regressors);

                for i in 0..exog.n_regressors {
                    let reg = &*exog.regressors.add(i);

                    // Validate lengths
                    if reg.n_values != length {
                        return Err(anofox_fcst_core::error::ForecastError::InvalidInput(
                            format!(
                                "Exogenous regressor {} has {} values but y has {} values",
                                i, reg.n_values, length
                            ),
                        ));
                    }
                    if reg.n_future != opts.horizon as usize {
                        return Err(anofox_fcst_core::error::ForecastError::InvalidInput(
                            format!(
                                "Exogenous regressor {} has {} future values but horizon is {}",
                                i, reg.n_future, opts.horizon
                            ),
                        ));
                    }

                    // Copy historical values, mapping any invalid entry (per
                    // `reg.validity`, D-12/FCST-08) to NaN — the core's own
                    // NaN-is-missing convention — instead of the unmarshalled
                    // (possibly uninitialised-looking, e.g. 0-filled by the
                    // caller) raw double. `null` validity = every entry valid.
                    historical.push(build_values(reg.values, reg.validity, reg.n_values));

                    // Same for the future window (`reg.future_validity`).
                    future.push(build_values(
                        reg.future_values,
                        reg.future_validity,
                        reg.n_future,
                    ));
                }

                Some(anofox_fcst_core::ExogenousData { historical, future })
            } else {
                None
            }
        } else {
            None
        };
        core_opts.exog = exog_data;

        anofox_fcst_core::forecast_with_exog(&series, &core_opts)
    }));

    match result {
        Ok(Ok(forecast)) => {
            let n_forecasts = forecast.point.len();
            (*out_result).n_forecasts = n_forecasts;

            // Copy point forecasts with allocation error checking
            (*out_result).point_forecasts = match alloc_or_error(
                &forecast.point,
                out_error,
                "Failed to allocate point forecasts",
            ) {
                Ok(ptr) => ptr,
                Err(()) => return false,
            };

            (*out_result).lower_bounds = match alloc_or_error(
                &forecast.lower,
                out_error,
                "Failed to allocate lower bounds",
            ) {
                Ok(ptr) => ptr,
                Err(()) => {
                    free_ptr((*out_result).point_forecasts as *mut _);
                    (*out_result).point_forecasts = ptr::null_mut();
                    return false;
                }
            };

            (*out_result).upper_bounds = match alloc_or_error(
                &forecast.upper,
                out_error,
                "Failed to allocate upper bounds",
            ) {
                Ok(ptr) => ptr,
                Err(()) => {
                    free_ptr((*out_result).point_forecasts as *mut _);
                    free_ptr((*out_result).lower_bounds as *mut _);
                    (*out_result).point_forecasts = ptr::null_mut();
                    (*out_result).lower_bounds = ptr::null_mut();
                    return false;
                }
            };

            // Copy fitted values
            if let Some(ref fitted) = forecast.fitted {
                (*out_result).fitted_values =
                    match alloc_or_error(fitted, out_error, "Failed to allocate fitted values") {
                        Ok(ptr) => ptr,
                        Err(()) => {
                            free_ptr((*out_result).point_forecasts as *mut _);
                            free_ptr((*out_result).lower_bounds as *mut _);
                            free_ptr((*out_result).upper_bounds as *mut _);
                            (*out_result).point_forecasts = ptr::null_mut();
                            (*out_result).lower_bounds = ptr::null_mut();
                            (*out_result).upper_bounds = ptr::null_mut();
                            return false;
                        }
                    };
                (*out_result).n_fitted = fitted.len();
            } else {
                (*out_result).fitted_values = ptr::null_mut();
                (*out_result).n_fitted = 0;
            }

            // Copy residuals. n_residuals is its OWN length field (plan
            // 10-02 fix) — never aliased to n_fitted; see
            // ForecastResult::n_residuals doc comment.
            if let Some(ref resid) = forecast.residuals {
                (*out_result).residuals =
                    match alloc_or_error(resid, out_error, "Failed to allocate residuals") {
                        Ok(ptr) => ptr,
                        Err(()) => {
                            free_ptr((*out_result).point_forecasts as *mut _);
                            free_ptr((*out_result).lower_bounds as *mut _);
                            free_ptr((*out_result).upper_bounds as *mut _);
                            free_ptr((*out_result).fitted_values as *mut _);
                            (*out_result).point_forecasts = ptr::null_mut();
                            (*out_result).lower_bounds = ptr::null_mut();
                            (*out_result).upper_bounds = ptr::null_mut();
                            (*out_result).fitted_values = ptr::null_mut();
                            return false;
                        }
                    };
                (*out_result).n_residuals = resid.len();
            } else {
                (*out_result).residuals = ptr::null_mut();
                (*out_result).n_residuals = 0;
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
                let error_code = match e.to_code() {
                    1 => ErrorCode::NullPointer,
                    2 => ErrorCode::InvalidInput,
                    3 => ErrorCode::ComputationError,
                    4 => ErrorCode::AllocationError,
                    5 => ErrorCode::InvalidModel,
                    6 => ErrorCode::InsufficientData,
                    7 => ErrorCode::InvalidDateFormat,
                    8 => ErrorCode::InvalidFrequency,
                    9 => ErrorCode::InvalidInput, // InvalidParameter → InvalidInput at FFI boundary
                    _ => ErrorCode::InternalError,
                };
                (*out_error).set_error(error_code, &e.to_string());
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
// Forecast Explainability (Inspectable + Explainable)
// ============================================================================

/// Allocate a NUL-terminated C string on the FFI heap and hand ownership to the
/// caller. Returns null if allocation fails.
///
/// # Safety
/// The caller must eventually free the returned pointer via `anofox_free_string`.
unsafe fn alloc_c_string(s: &str) -> *mut c_char {
    let bytes = s.as_bytes();
    let len = bytes.len();
    let buf = malloc(len + 1) as *mut c_char;
    if buf.is_null() {
        return ptr::null_mut();
    }
    ptr::copy_nonoverlapping(bytes.as_ptr() as *const c_char, buf, len);
    *buf.add(len) = 0;
    buf
}

/// Free a C string previously allocated by an inspect / explain call.
///
/// # Safety
/// `ptr` must be either null or a pointer previously returned by one of
/// `anofox_ts_forecast_inspect` / `anofox_ts_forecast_explain` via their
/// `out_json` out-argument.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_string(ptr: *mut c_char) {
    if !ptr.is_null() {
        free(ptr as *mut _);
    }
}

/// Fit the requested model and serialise its `Inspectable::explanation()`
/// snapshot as JSON.
///
/// The JSON structure is the serde-serialised
/// `anofox_forecast::models::inspect::Explanation` enum: an object with a
/// single key equal to the model family (e.g. `AutoETS`, `Arima`,
/// `Laplace`) whose value is the per-family payload struct.
///
/// # Arguments
/// * `values`, `validity`, `length` — series (validity may be null for
///   fully-valid data)
/// * `options` — the same `ForecastOptions` used by `anofox_ts_forecast`;
///   the `horizon` field is ignored (inspect is a fit-state snapshot)
/// * `out_json` — receives ownership of a newly-allocated NUL-terminated
///   JSON string; must be freed with `anofox_free_string`
/// * `out_error` — optional error slot
///
/// # Supported models
/// AutoETS, AutoARIMA, AutoTheta, AutoTBATS, MFLES, AutoMFLES, MSTL,
/// AutoMSTL, Laplace. Any other model returns an `InvalidModel` error.
///
/// # Safety
/// Pointer arguments must be valid; `out_json` and `options` must be non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_forecast_inspect(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    options: *const ForecastOptions,
    out_json: *mut *mut c_char,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if values.is_null() || options.is_null() || out_json.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }
    *out_json = ptr::null_mut();

    let result = catch_unwind(AssertUnwindSafe(|| {
        let series = build_series(values, validity, length);
        let core_opts = build_core_options(&*options)?;
        anofox_fcst_core::forecast_inspect(&series, &core_opts)
    }));

    match result {
        Ok(Ok(json)) => {
            let ptr = alloc_c_string(&json);
            if ptr.is_null() {
                if !out_error.is_null() {
                    (*out_error).set_error(
                        ErrorCode::AllocationError,
                        "Failed to allocate JSON output buffer",
                    );
                }
                return false;
            }
            *out_json = ptr;
            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                let code = match e.to_code() {
                    1 => ErrorCode::NullPointer,
                    2 => ErrorCode::InvalidInput,
                    3 => ErrorCode::ComputationError,
                    4 => ErrorCode::AllocationError,
                    5 => ErrorCode::InvalidModel,
                    6 => ErrorCode::InsufficientData,
                    7 => ErrorCode::InvalidDateFormat,
                    8 => ErrorCode::InvalidFrequency,
                    9 => ErrorCode::InvalidInput,
                    _ => ErrorCode::InternalError,
                };
                (*out_error).set_error(code, &e.to_string());
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

/// Fit the requested model and serialise its per-horizon
/// `Explainable::explain(horizon)` decomposition as JSON.
///
/// The JSON has keys `level`, `trend`, `seasonal`, `residual` (each a
/// Vec\<f64\> of length `horizon`, absent when the family has no such
/// component) and `named_components` (a map of arbitrary named additive
/// components).
///
/// # Arguments
/// * `values`, `validity`, `length` — historical series
/// * `horizon` — number of periods ahead to explain (must equal the
///   horizon you'll subsequently forecast with; internally we call
///   `.explain(horizon)`)
/// * `options` — `ForecastOptions`; `horizon` field is ignored (uses the
///   explicit `horizon` argument instead)
/// * `out_json`, `out_error` — see `anofox_ts_forecast_inspect`
///
/// # Supported models
/// ETS (fixed spec), MSTL / AutoMSTL, Theta. Any other model returns an
/// `InvalidModel` error.
///
/// # Safety
/// Pointer arguments must be valid; `out_json` and `options` must be non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_forecast_explain(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    horizon: size_t,
    options: *const ForecastOptions,
    out_json: *mut *mut c_char,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if values.is_null() || options.is_null() || out_json.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }
    *out_json = ptr::null_mut();

    let result = catch_unwind(AssertUnwindSafe(|| {
        let series = build_series(values, validity, length);
        let core_opts = build_core_options(&*options)?;
        anofox_fcst_core::forecast_explain(&series, horizon, &core_opts)
    }));

    match result {
        Ok(Ok(json)) => {
            let ptr = alloc_c_string(&json);
            if ptr.is_null() {
                if !out_error.is_null() {
                    (*out_error).set_error(
                        ErrorCode::AllocationError,
                        "Failed to allocate JSON output buffer",
                    );
                }
                return false;
            }
            *out_json = ptr;
            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                let code = match e.to_code() {
                    1 => ErrorCode::NullPointer,
                    2 => ErrorCode::InvalidInput,
                    3 => ErrorCode::ComputationError,
                    4 => ErrorCode::AllocationError,
                    5 => ErrorCode::InvalidModel,
                    6 => ErrorCode::InsufficientData,
                    7 => ErrorCode::InvalidDateFormat,
                    8 => ErrorCode::InvalidFrequency,
                    9 => ErrorCode::InvalidInput,
                    _ => ErrorCode::InternalError,
                };
                (*out_error).set_error(code, &e.to_string());
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

/// Shared FFI → core `ForecastOptions` conversion used by `anofox_ts_forecast`,
/// `anofox_ts_forecast_inspect`, and `anofox_ts_forecast_explain`.
///
/// Every buffered string field (`model`, `ets_model`, `seasonal_periods_str`,
/// `model_pool`, `laplace_variant`, `kalman_model`, `ensemble_method`,
/// `arima_order_str`, `seasonal_order_str`) is read via [`c_field_str`] (D-07,
/// T-10-04/T-10-07) — a missing NUL terminator or invalid UTF-8 is an
/// `InvalidInput` error, never a silent `"auto"`/empty-string fallback.
unsafe fn build_core_options(
    opts: &ForecastOptions,
) -> Result<anofox_fcst_core::ForecastOptions, anofox_fcst_core::ForecastError> {
    let model_str = c_field_str(&opts.model, "model")?;
    let model_type: anofox_fcst_core::ModelType = model_str.parse().map_err(|_| {
        anofox_fcst_core::ForecastError::InvalidModel(format!("Unknown model: '{}'", model_str))
    })?;

    let ets_spec = c_field_str(&opts.ets_model, "ets_model")?
        .to_owned()
        .into_non_empty();

    let sp_str = c_field_str(&opts.seasonal_periods_str, "seasonal_periods")?;
    let seasonal_periods = anofox_fcst_core::parse_seasonal_periods(sp_str)?;

    let model_pool = c_field_str(&opts.model_pool, "model_pool")?
        .to_owned()
        .into_non_empty();

    let laplace_variant = c_field_str(&opts.laplace_variant, "laplace_variant")?
        .to_owned()
        .into_non_empty()
        .map(|s| anofox_fcst_core::LaplaceVariant::parse(&s))
        .transpose()?;

    // Parse kalman_model spec (empty → None → local_level default)
    let kalman_model = c_field_str(&opts.kalman_model, "kalman_model")?
        .to_owned()
        .into_non_empty();

    // Parse ensemble_method (empty → None → "mean" default in core)
    let ensemble_method = c_field_str(&opts.ensemble_method, "ensemble_method")?
        .to_owned()
        .into_non_empty();

    // ARIMA order strings (D-08/D-09) — strict parse, bounds-checked in core.
    let arima_order_str = c_field_str(&opts.arima_order_str, "arima_order")?;
    let arima_order = anofox_fcst_core::parse_order_triplet("arima_order", arima_order_str)?;
    let seasonal_order_str = c_field_str(&opts.seasonal_order_str, "seasonal_order")?;
    let seasonal_order =
        anofox_fcst_core::parse_order_triplet("seasonal_order", seasonal_order_str)?;

    Ok(anofox_fcst_core::ForecastOptions {
        model: model_type,
        ets_spec,
        horizon: opts.horizon as usize,
        confidence_level: opts.confidence_level,
        seasonal_period: opts.seasonal_period as usize,
        auto_detect_seasonality: opts.auto_detect_seasonality,
        include_fitted: opts.include_fitted,
        include_residuals: opts.include_residuals,
        window: opts.window.max(0) as usize,
        seasonal_periods,
        model_pool,
        laplace_variant,
        laplace_seasonal_batch_init: opts.laplace_seasonal_batch_init,
        garch_p: opts.garch_p as usize,
        garch_q: opts.garch_q as usize,
        kalman_model,
        ensemble_top_k: opts.ensemble_top_k.max(0) as usize,
        ensemble_method,
        arima_order,
        seasonal_order,
    })
}

/// Shared FFI → core `ForecastOptionsExog` conversion used by
/// `anofox_ts_forecast_exog`, minus the exog payload itself (the caller
/// fills in `.exog` separately, since that requires the raw pointer/length
/// marshalling that has nothing to do with the buffered string fields).
unsafe fn build_core_options_exog(
    opts: &ForecastOptionsExog,
) -> Result<anofox_fcst_core::ForecastOptionsExog, anofox_fcst_core::ForecastError> {
    let model_str = c_field_str(&opts.model, "model")?;
    let model_type: anofox_fcst_core::ModelType = model_str.parse().map_err(|_| {
        anofox_fcst_core::ForecastError::InvalidModel(format!("Unknown model: '{}'", model_str))
    })?;

    let ets_spec = c_field_str(&opts.ets_model, "ets_model")?
        .to_owned()
        .into_non_empty();

    let sp_str = c_field_str(&opts.seasonal_periods_str, "seasonal_periods")?;
    let seasonal_periods = anofox_fcst_core::parse_seasonal_periods(sp_str)?;

    let model_pool = c_field_str(&opts.model_pool, "model_pool")?
        .to_owned()
        .into_non_empty();

    let laplace_variant = c_field_str(&opts.laplace_variant, "laplace_variant")?
        .to_owned()
        .into_non_empty()
        .map(|s| anofox_fcst_core::LaplaceVariant::parse(&s))
        .transpose()?;

    let kalman_model = c_field_str(&opts.kalman_model, "kalman_model")?
        .to_owned()
        .into_non_empty();

    let ensemble_method = c_field_str(&opts.ensemble_method, "ensemble_method")?
        .to_owned()
        .into_non_empty();

    let arima_order_str = c_field_str(&opts.arima_order_str, "arima_order")?;
    let arima_order = anofox_fcst_core::parse_order_triplet("arima_order", arima_order_str)?;
    let seasonal_order_str = c_field_str(&opts.seasonal_order_str, "seasonal_order")?;
    let seasonal_order =
        anofox_fcst_core::parse_order_triplet("seasonal_order", seasonal_order_str)?;

    Ok(anofox_fcst_core::ForecastOptionsExog {
        model: model_type,
        ets_spec,
        horizon: opts.horizon as usize,
        confidence_level: opts.confidence_level,
        seasonal_period: opts.seasonal_period as usize,
        auto_detect_seasonality: opts.auto_detect_seasonality,
        include_fitted: opts.include_fitted,
        include_residuals: opts.include_residuals,
        exog: None,
        window: opts.window.max(0) as usize,
        seasonal_periods,
        model_pool,
        laplace_variant,
        laplace_seasonal_batch_init: opts.laplace_seasonal_batch_init,
        garch_p: opts.garch_p as usize,
        garch_q: opts.garch_q as usize,
        kalman_model,
        ensemble_top_k: opts.ensemble_top_k.max(0) as usize,
        ensemble_method,
        arima_order,
        seasonal_order,
    })
}

/// Tiny `""` → `None` helper so the repeated
/// `.to_owned().filter(|s| !s.is_empty()).map(...)` chain reads as one call.
trait IntoNonEmpty {
    fn into_non_empty(self) -> Option<String>;
}

impl IntoNonEmpty for String {
    fn into_non_empty(self) -> Option<String> {
        if self.is_empty() {
            None
        } else {
            Some(self)
        }
    }
}

// ============================================================================
// Data Quality Functions
// ============================================================================

/// Compute data quality metrics (D-05, WRAP-02): dates/frequency/n_short-aware,
/// using the shared missing-period gap counter from plan 13-01.
///
/// `dates` may be null, meaning "no date information" -- `n_gaps`/`n_gap_runs` are
/// `0` and `temporal_score` is `1.0`. When non-null it must have exactly `length`
/// entries. `frequency_type == Fixed` with `frequency_micros <= 0` auto-detects the
/// step from `dates`. `n_short` must be `>= 1` (`InvalidInput` otherwise, surfaced as
/// `ErrorCode::InvalidInput`). `period_count` is the user-declared calendar
/// multiplier for Monthly/Quarterly/Yearly frequencies (e.g. `2` for `'2mo'`); ignored
/// for `Fixed` (CR-01 fix, 13-03 deferred item).
///
/// # Safety
/// `values` and `out_result` must be valid and non-null. `validity` and `dates` may
/// be null (meaning all-valid / no dates respectively). `values` (and `dates`, when
/// non-null) must have at least `length` elements.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_data_quality(
    values: *const c_double,
    validity: *const u64,
    dates: *const i64,
    length: size_t,
    frequency_micros: i64,
    frequency_type: FrequencyType,
    n_short: size_t,
    period_count: size_t,
    out_result: *mut DataQualityResult,
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
        *out_result = DataQualityResult::default();
        return true;
    }

    let period_count_u32 = u32::try_from(period_count).unwrap_or(u32::MAX);

    let result = catch_unwind(AssertUnwindSafe(|| {
        let series = build_series(values, validity, length);
        let dates_vec: Option<Vec<i64>> = if dates.is_null() {
            None
        } else {
            Some(std::slice::from_raw_parts(dates, length).to_vec())
        };
        let freq_type: anofox_fcst_core::FrequencyType = frequency_type.into();
        anofox_fcst_core::compute_data_quality(
            &series,
            dates_vec.as_deref(),
            frequency_micros,
            freq_type,
            n_short,
            period_count_u32,
        )
    }));

    match result {
        Ok(Ok(quality)) => {
            *out_result = quality.into();
            true
        }
        Ok(Err(e)) => {
            let code = match e {
                anofox_fcst_core::ForecastError::InvalidInput(_) => ErrorCode::InvalidInput,
                _ => ErrorCode::ComputationError,
            };
            set_error(out_error, code, &e.to_string());
            false
        }
        Err(_) => {
            set_error(out_error, ErrorCode::PanicCaught, "Panic in Rust code");
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

    match alloc_or_error(&filled, out_error, "Failed to allocate filled values") {
        Ok(ptr) => {
            *out_values = ptr;
            true
        }
        Err(()) => false,
    }
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

    match alloc_or_error(&filled, out_error, "Failed to allocate filled values") {
        Ok(ptr) => {
            *out_values = ptr;
            true
        }
        Err(()) => false,
    }
}

// ============================================================================
// Filter Functions
// ============================================================================

/// Compute d-th order, lag-`seasonal_lag` differences of a time series,
/// row-aligned: `out_values`/`out_validity` have exactly `length` entries,
/// with the first `order * seasonal_lag` entries NULL (D-05/D-06) and any
/// difference touching a missing (invalid) input NULL (SQL row-wise policy).
///
/// # Safety
/// `values`, `out_values`, `out_validity` and `out_error` must be valid and
/// non-null. `values` must have `length` elements; `validity` must have
/// `length.div_ceil(64)` words or be null (meaning all-valid). `out_values`
/// must have room for `length` doubles; `out_validity` must have room for
/// `length.div_ceil(64)` words — every word is overwritten by this call.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_diff(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    order: c_int,
    seasonal_lag: c_int,
    out_values: *mut c_double,
    out_validity: *mut u64,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if values.is_null() || out_values.is_null() || out_validity.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    if order < 0 {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::InvalidInput, "diff_order must be >= 0");
        }
        return false;
    }

    if seasonal_lag < 1 {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::InvalidInput, "seasonal_lag must be >= 1");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let series = build_series(values, validity, length);
        anofox_fcst_core::diff_aligned(&series, order as usize, seasonal_lag as usize)
    }));

    match result {
        Ok(Ok(diffed)) => {
            let out_values_slice = std::slice::from_raw_parts_mut(out_values, length);
            let n_words = length.div_ceil(64);
            let out_validity_slice = std::slice::from_raw_parts_mut(out_validity, n_words);
            for word in out_validity_slice.iter_mut() {
                *word = 0;
            }
            for (i, v) in diffed.into_iter().enumerate() {
                match v {
                    Some(x) => {
                        out_values_slice[i] = x;
                        out_validity_slice[i / 64] |= 1u64 << (i % 64);
                    }
                    None => {
                        out_values_slice[i] = 0.0;
                    }
                }
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

/// Checks whether a series is constant (Phase 12 D-09 single source of truth
/// for `ts_drop_constant_by`): groups with fewer than two non-missing values
/// (including an all-missing group) classify as constant.
///
/// # Safety
/// `values` and `out_result` must be valid and non-null. `validity` may be
/// null (meaning all-valid). `length` must equal the number of `f64`
/// elements at `values`.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_is_constant(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    out_result: *mut bool,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if out_result.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    // An empty (but non-NULL-pointer) list classifies as constant per this
    // function's own documented contract ("fewer than two non-missing values
    // ... classifies as constant"). Mirror `anofox_ts_series_gate`: check
    // `length == 0` before the `values` null check, since the caller may
    // legitimately pass `nullptr` for `values` precisely when `length == 0`.
    if length == 0 {
        *out_result = true;
        return true;
    }

    if values.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let series = build_series(values, validity, length);
        anofox_fcst_core::is_constant(&series)
    }));

    match result {
        Ok(is_const) => {
            *out_result = is_const;
            true
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
///
/// # Arguments
/// * `dates` - Array of timestamps in microseconds
/// * `values` - Array of values
/// * `validity` - Validity bitmask (NULL means all valid)
/// * `length` - Number of elements
/// * `frequency_micros` - Frequency in microseconds (used for Fixed type)
/// * `frequency_type` - Type of frequency (0=Fixed, 1=Monthly, 2=Quarterly, 3=Yearly)
/// * `period_count` - User-declared calendar multiplier for Monthly/Quarterly/Yearly
///   frequencies (e.g. `2` for `'2mo'`, `3` for `'3q'`, `5` for `'5y'`; `1` for the
///   single-unit strings); ignored for Fixed (CR-01 fix, 13-03 deferred item)
/// * `out_result` - Output result structure
/// * `out_error` - Output error structure
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_fill_gaps(
    dates: *const i64,
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    frequency_micros: i64,
    frequency_type: FrequencyType,
    period_count: size_t,
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

    // For Fixed frequency, require positive frequency_micros
    // For calendar frequencies, the value is ignored
    if frequency_type == FrequencyType::Fixed && frequency_micros <= 0 {
        if !out_error.is_null() {
            (*out_error).set_error(
                ErrorCode::InvalidFrequency,
                "Frequency must be positive for fixed intervals",
            );
        }
        return false;
    }

    let freq_type_core: anofox_fcst_core::FrequencyType = frequency_type.into();
    let period_count_u32 = u32::try_from(period_count).unwrap_or(u32::MAX);

    let result = catch_unwind(AssertUnwindSafe(|| {
        let dates_vec: Vec<i64> = std::slice::from_raw_parts(dates, length).to_vec();
        let series = build_series(values, validity, length);
        anofox_fcst_core::fill_gaps(
            &dates_vec,
            &series,
            frequency_micros,
            freq_type_core,
            period_count_u32,
        )
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
///
/// # Arguments
/// * `dates` - Array of timestamps in microseconds
/// * `values` - Array of values
/// * `validity` - Validity bitmask (NULL means all valid)
/// * `length` - Number of elements
/// * `target_date` - Target date to extend to (microseconds since epoch)
/// * `frequency_micros` - Frequency in microseconds (used for Fixed type)
/// * `frequency_type` - Type of frequency (0=Fixed, 1=Monthly, 2=Quarterly, 3=Yearly)
/// * `period_count` - User-declared calendar multiplier for Monthly/Quarterly/Yearly
///   frequencies (e.g. `2` for `'2mo'`, `3` for `'3q'`, `5` for `'5y'`; `1` for the
///   single-unit strings); ignored for Fixed (CR-01 fix, 13-03 deferred item)
/// * `out_result` - Output result structure
/// * `out_error` - Output error structure
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_fill_forward_dates(
    dates: *const i64,
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    target_date: i64,
    frequency_micros: i64,
    frequency_type: FrequencyType,
    period_count: size_t,
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

    // For Fixed frequency, require positive frequency_micros
    // For calendar frequencies, the value is ignored
    if frequency_type == FrequencyType::Fixed && frequency_micros <= 0 {
        if !out_error.is_null() {
            (*out_error).set_error(
                ErrorCode::InvalidFrequency,
                "Frequency must be positive for fixed intervals",
            );
        }
        return false;
    }

    let freq_type_core: anofox_fcst_core::FrequencyType = frequency_type.into();
    let period_count_u32 = u32::try_from(period_count).unwrap_or(u32::MAX);

    let result = catch_unwind(AssertUnwindSafe(|| {
        let dates_vec: Vec<i64> = std::slice::from_raw_parts(dates, length).to_vec();
        let series = build_series(values, validity, length);
        anofox_fcst_core::fill_forward(
            &dates_vec,
            &series,
            target_date,
            frequency_micros,
            freq_type_core,
            period_count_u32,
        )
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
/// `out_frequency` is written in MICROSECONDS (the same unit every `dates` array in
/// this FFI boundary uses) -- previously undocumented at this boundary (Phase 12
/// deferred item (e), G23/WRAP-03). See `anofox_fcst_core::detect_frequency`'s doc
/// comment for the tie-break rule.
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

/// Compute `horizon` future dates (microseconds) from an observed date
/// series, using the D-12 calendar rule (plan 12-05).
///
/// `step_months == 0 && step_micros == 0` -> auto-detect via
/// `anofox_fcst_core::detect_step`. `step_months > 0 && step_micros == 0` ->
/// explicit `Months(step_months)`. `step_months == 0 && step_micros > 0` ->
/// explicit `Fixed(step_micros)`. Any negative value, or both non-zero, is
/// `INVALID_INPUT`. `horizon == 0` returns `true` immediately (`out_dates`
/// may be null in that case). `length == 0` is `INVALID_INPUT`.
///
/// `out_dates` is CALLER-allocated with exactly `horizon` entries; this
/// function writes exactly `horizon` values into it (T-12-17 — never more,
/// never fewer).
///
/// # Safety
/// All pointer arguments must be valid and non-null (except `out_dates` when
/// `horizon == 0`). `dates` must have at least `length` entries; `out_dates`
/// must have at least `horizon` entries.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_future_dates(
    dates: *const i64,
    length: size_t,
    horizon: size_t,
    step_months: i64,
    step_micros: i64,
    out_dates: *mut i64,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if dates.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument: dates");
        }
        return false;
    }

    if horizon == 0 {
        return true;
    }

    if out_dates.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument: out_dates");
        }
        return false;
    }

    if length == 0 {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::InvalidInput, "dates must not be empty");
        }
        return false;
    }

    if step_months < 0 || step_micros < 0 || (step_months > 0 && step_micros > 0) {
        if !out_error.is_null() {
            (*out_error).set_error(
                ErrorCode::InvalidInput,
                "step_months and step_micros must be non-negative, and at most one may be non-zero",
            );
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let dates_vec: Vec<i64> = std::slice::from_raw_parts(dates, length).to_vec();
        let step = if step_months > 0 {
            anofox_fcst_core::CalendarStep::Months(step_months as u32)
        } else if step_micros > 0 {
            anofox_fcst_core::CalendarStep::Fixed(step_micros)
        } else {
            anofox_fcst_core::detect_step(&dates_vec)?
        };
        anofox_fcst_core::future_dates(&dates_vec, horizon, step)
    }));

    match result {
        Ok(Ok(future)) => {
            let out_slice = std::slice::from_raw_parts_mut(out_dates, horizon);
            out_slice.copy_from_slice(&future);
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

    match alloc_or_error(&filled, out_error, "Failed to allocate interpolated values") {
        Ok(ptr) => {
            *out_values = ptr;
            true
        }
        Err(()) => false,
    }
}

// ============================================================================
// Conformal Prediction Functions
// ============================================================================

/// Compute the conformity score (quantile) from calibration residuals.
///
/// Returns the (1 - alpha) quantile of absolute residuals for conformal prediction.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_conformal_quantile(
    residuals: *const c_double,
    validity: *const u64,
    length: size_t,
    alpha: c_double,
    out_result: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);

    if check_null_pointers(out_error, &[residuals as *const core::ffi::c_void]) {
        return false;
    }

    if out_result.is_null() {
        set_error(out_error, ErrorCode::NullPointer, "Null output pointer");
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let series = build_series(residuals, validity, length);
        let values = anofox_fcst_core::drop_missing(&series);
        anofox_fcst_core::conformal_quantile(&values, alpha)
    }));

    match result {
        Ok(Ok(quantile)) => {
            // Phase 16 D-05: an insufficient calibration set reports an
            // infinite quantile from the core; the FFI boundary writes it
            // as NaN so the C++ caller can turn it into SQL NULL.
            *out_result = nan_if_non_finite(quantile);
            true
        }
        Ok(Err(e)) => {
            set_error(out_error, ffi_error_code(&e), &e.to_string());
            false
        }
        Err(_) => {
            set_error(
                out_error,
                ErrorCode::PanicCaught,
                "Panic in conformal_quantile",
            );
            false
        }
    }
}

/// Apply a conformity score to point forecasts to create prediction intervals.
///
/// Creates symmetric intervals: [forecast - score, forecast + score].
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_conformal_intervals(
    forecasts: *const c_double,
    forecasts_validity: *const u64,
    length: size_t,
    conformity_score: c_double,
    out_lower: *mut *mut c_double,
    out_upper: *mut *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);

    if out_lower.is_null() || out_upper.is_null() {
        set_error(out_error, ErrorCode::NullPointer, "Null pointer argument");
        return false;
    }
    if length != 0 && forecasts.is_null() {
        set_error(out_error, ErrorCode::NullPointer, "Null pointer argument");
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        // Phase 16 WR-01 fix: a non-null, zero-length forecasts list must
        // produce an empty result, matching every sibling conformal FFI
        // entry point's "non-null, zero-length means empty" convention.
        let forecast_series = if length == 0 {
            Vec::new()
        } else {
            build_series(forecasts, forecasts_validity, length)
        };
        let filled = anofox_fcst_core::fill_missing(&forecast_series, 0.0);
        let (mut lower, mut upper) =
            anofox_fcst_core::conformal_intervals(&filled, conformity_score);
        anofox_fcst_core::propagate_missing(&mut lower, &forecast_series);
        anofox_fcst_core::propagate_missing(&mut upper, &forecast_series);
        (lower, upper)
    }));

    match result {
        Ok((lower, upper)) => {
            // Allocate lower bounds with null check
            let lower_ptr =
                match alloc_or_error(&lower, out_error, "Failed to allocate lower bounds") {
                    Ok(ptr) => ptr,
                    Err(()) => return false,
                };

            // Allocate upper bounds with null check
            match alloc_or_error(&upper, out_error, "Failed to allocate upper bounds") {
                Ok(upper_ptr) => {
                    *out_lower = lower_ptr;
                    *out_upper = upper_ptr;
                    true
                }
                Err(()) => {
                    // Clean up already allocated lower bounds
                    free_ptr(lower_ptr as *mut core::ffi::c_void);
                    false
                }
            }
        }
        Err(_) => {
            set_error(
                out_error,
                ErrorCode::PanicCaught,
                "Panic in conformal_intervals",
            );
            false
        }
    }
}

/// Perform split conformal prediction in one step.
///
/// Computes conformity score from residuals and applies to forecasts.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_conformal_predict(
    residuals: *const c_double,
    residuals_validity: *const u64,
    residuals_length: size_t,
    forecasts: *const c_double,
    forecasts_validity: *const u64,
    forecasts_length: size_t,
    alpha: c_double,
    out_result: *mut ConformalResultFFI,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);

    if residuals.is_null() || forecasts.is_null() || out_result.is_null() {
        set_error(out_error, ErrorCode::NullPointer, "Null pointer argument");
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let residual_series = build_series(residuals, residuals_validity, residuals_length);
        let residual_values = anofox_fcst_core::drop_missing(&residual_series);
        let forecast_series = build_series(forecasts, forecasts_validity, forecasts_length);
        let filled_forecasts = anofox_fcst_core::fill_missing(&forecast_series, 0.0);
        let mut conf_result =
            anofox_fcst_core::conformal_predict(&residual_values, &filled_forecasts, alpha)?;
        anofox_fcst_core::propagate_missing(&mut conf_result.point, &forecast_series);
        anofox_fcst_core::propagate_missing(&mut conf_result.lower, &forecast_series);
        anofox_fcst_core::propagate_missing(&mut conf_result.upper, &forecast_series);
        Ok::<_, anofox_fcst_core::ForecastError>(conf_result)
    }));

    match result {
        Ok(Ok(conf_result)) => {
            fill_conformal_result(out_result, &conf_result);
            true
        }
        Ok(Err(e)) => {
            set_error(out_error, ffi_error_code(&e), &e.to_string());
            false
        }
        Err(_) => {
            set_error(
                out_error,
                ErrorCode::PanicCaught,
                "Panic in conformal_predict",
            );
            false
        }
    }
}

/// Compute conformal prediction intervals directly from per-row calibration
/// (actual, forecast) pairs and a list of point forecasts (Phase 16 D-01/D-02).
///
/// A null method pointer means "symmetric"; otherwise the method name is
/// parsed strictly via `ConformalMethod::from_str` (an unknown name is an
/// `InvalidInput` error, not a silent fallback).
///
/// # Safety
/// All non-null pointer arguments must be valid. A data pointer may be null
/// only when its paired length is 0 (never calls `from_raw_parts` on a null
/// pointer with a nonzero length).
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_conformal_from_pairs(
    cal_actuals: *const c_double,
    cal_actuals_validity: *const u64,
    cal_actuals_len: size_t,
    cal_forecasts: *const c_double,
    cal_forecasts_validity: *const u64,
    cal_forecasts_len: size_t,
    cal_difficulty: *const c_double,
    cal_difficulty_validity: *const u64,
    cal_difficulty_len: size_t,
    point_forecasts: *const c_double,
    point_validity: *const u64,
    point_len: size_t,
    point_difficulty: *const c_double,
    point_difficulty_validity: *const u64,
    point_difficulty_len: size_t,
    alpha: c_double,
    method: *const c_char,
    out_result: *mut ConformalResultFFI,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);

    if out_result.is_null() {
        set_error(out_error, ErrorCode::NullPointer, "Null pointer argument");
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let actuals_series = if cal_actuals_len == 0 {
            Vec::new()
        } else {
            build_series(cal_actuals, cal_actuals_validity, cal_actuals_len)
        };
        let forecasts_series = if cal_forecasts_len == 0 {
            Vec::new()
        } else {
            build_series(cal_forecasts, cal_forecasts_validity, cal_forecasts_len)
        };
        // Phase 16 D-08: null pointer or zero length = "no difficulty_col at
        // all" (never calls from_raw_parts on a null pointer with nonzero
        // length).
        let cal_difficulty_series = if cal_difficulty.is_null() || cal_difficulty_len == 0 {
            None
        } else {
            Some(build_series(
                cal_difficulty,
                cal_difficulty_validity,
                cal_difficulty_len,
            ))
        };
        let point_series = if point_len == 0 {
            Vec::new()
        } else {
            build_series(point_forecasts, point_validity, point_len)
        };
        let point_difficulty_series = if point_difficulty.is_null() || point_difficulty_len == 0 {
            None
        } else {
            Some(build_series(
                point_difficulty,
                point_difficulty_validity,
                point_difficulty_len,
            ))
        };

        let method_str = if method.is_null() {
            "symmetric"
        } else {
            match CStr::from_ptr(method).to_str() {
                Ok(s) => s,
                Err(_) => {
                    return Err(anofox_fcst_core::ForecastError::InvalidInput(
                        "method is not valid UTF-8".to_string(),
                    ))
                }
            }
        };
        let parsed_method: anofox_fcst_core::ConformalMethod = method_str.parse()?;

        anofox_fcst_core::conformal_from_pairs(
            &actuals_series,
            &forecasts_series,
            cal_difficulty_series.as_deref(),
            &point_series,
            point_difficulty_series.as_deref(),
            alpha,
            parsed_method,
        )
    }));

    match result {
        Ok(Ok(conf_result)) => {
            fill_conformal_result(out_result, &conf_result);
            true
        }
        Ok(Err(e)) => {
            set_error(out_error, ffi_error_code(&e), &e.to_string());
            false
        }
        Err(_) => {
            set_error(
                out_error,
                ErrorCode::PanicCaught,
                "Panic in conformal_from_pairs",
            );
            false
        }
    }
}

/// Calibrate a conformal predictor from per-row `(actual, forecast)` pairs,
/// without applying it to any point forecasts yet (Phase 16 D-03).
///
/// A null method pointer means "symmetric"; otherwise the method name is
/// parsed strictly via `ConformalMethod::from_str`.
///
/// # Safety
/// All non-null pointer arguments must be valid. A data pointer may be null
/// only when its paired length is 0.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_conformal_calibrate(
    actuals: *const c_double,
    actuals_validity: *const u64,
    actuals_len: size_t,
    forecasts: *const c_double,
    forecasts_validity: *const u64,
    forecasts_len: size_t,
    difficulty: *const c_double,
    difficulty_validity: *const u64,
    difficulty_len: size_t,
    alpha: c_double,
    method: *const c_char,
    out_result: *mut ConformalCalibrationFFI,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);

    if out_result.is_null() {
        set_error(out_error, ErrorCode::NullPointer, "Null pointer argument");
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let actuals_series = if actuals_len == 0 {
            Vec::new()
        } else {
            build_series(actuals, actuals_validity, actuals_len)
        };
        let forecasts_series = if forecasts_len == 0 {
            Vec::new()
        } else {
            build_series(forecasts, forecasts_validity, forecasts_len)
        };
        // Phase 16 D-08: null pointer or zero length = "no difficulty_col".
        let difficulty_series = if difficulty.is_null() || difficulty_len == 0 {
            None
        } else {
            Some(build_series(
                difficulty,
                difficulty_validity,
                difficulty_len,
            ))
        };

        let method_str = if method.is_null() {
            "symmetric"
        } else {
            match CStr::from_ptr(method).to_str() {
                Ok(s) => s,
                Err(_) => {
                    return Err(anofox_fcst_core::ForecastError::InvalidInput(
                        "method is not valid UTF-8".to_string(),
                    ))
                }
            }
        };
        let parsed_method: anofox_fcst_core::ConformalMethod = method_str.parse()?;

        anofox_fcst_core::conformal_calibrate(
            &actuals_series,
            &forecasts_series,
            difficulty_series.as_deref(),
            alpha,
            parsed_method,
        )
    }));

    match result {
        Ok(Ok(cal_result)) => {
            fill_conformal_calibration(out_result, &cal_result);
            true
        }
        Ok(Err(e)) => {
            set_error(out_error, ffi_error_code(&e), &e.to_string());
            false
        }
        Err(_) => {
            set_error(
                out_error,
                ErrorCode::PanicCaught,
                "Panic in conformal_calibrate",
            );
            false
        }
    }
}

/// Perform conformal prediction with multiple coverage levels.
///
/// Computes prediction intervals at multiple alpha levels simultaneously.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_conformal_predict_multi(
    residuals: *const c_double,
    residuals_validity: *const u64,
    residuals_length: size_t,
    forecasts: *const c_double,
    forecasts_length: size_t,
    alphas: *const c_double,
    n_alphas: size_t,
    out_result: *mut ConformalMultiResultFFI,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);

    if residuals.is_null() || forecasts.is_null() || alphas.is_null() || out_result.is_null() {
        set_error(out_error, ErrorCode::NullPointer, "Null pointer argument");
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let residual_series = build_series(residuals, residuals_validity, residuals_length);
        let residual_values: Vec<f64> = residual_series.iter().filter_map(|v| *v).collect();
        let forecast_slice = std::slice::from_raw_parts(forecasts, forecasts_length);
        let alpha_slice = std::slice::from_raw_parts(alphas, n_alphas);
        anofox_fcst_core::conformal_predict_multi(&residual_values, forecast_slice, alpha_slice)
    }));

    match result {
        Ok(Ok(multi_result)) => {
            fill_conformal_multi_result(out_result, &multi_result);
            true
        }
        Ok(Err(e)) => {
            set_error(out_error, ffi_error_code(&e), &e.to_string());
            false
        }
        Err(_) => {
            set_error(
                out_error,
                ErrorCode::PanicCaught,
                "Panic in conformal_predict_multi",
            );
            false
        }
    }
}

/// Perform asymmetric conformal prediction.
///
/// Uses separate quantiles for positive and negative residuals.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_conformal_predict_asymmetric(
    residuals: *const c_double,
    residuals_validity: *const u64,
    residuals_length: size_t,
    forecasts: *const c_double,
    forecasts_validity: *const u64,
    forecasts_length: size_t,
    alpha: c_double,
    out_result: *mut ConformalResultFFI,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);

    if residuals.is_null() || forecasts.is_null() || out_result.is_null() {
        set_error(out_error, ErrorCode::NullPointer, "Null pointer argument");
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let residual_series = build_series(residuals, residuals_validity, residuals_length);
        let residual_values = anofox_fcst_core::drop_missing(&residual_series);
        let forecast_series = build_series(forecasts, forecasts_validity, forecasts_length);
        let filled_forecasts = anofox_fcst_core::fill_missing(&forecast_series, 0.0);
        let mut conf_result = anofox_fcst_core::conformal_predict_asymmetric(
            &residual_values,
            &filled_forecasts,
            alpha,
        )?;
        anofox_fcst_core::propagate_missing(&mut conf_result.point, &forecast_series);
        anofox_fcst_core::propagate_missing(&mut conf_result.lower, &forecast_series);
        anofox_fcst_core::propagate_missing(&mut conf_result.upper, &forecast_series);
        Ok::<_, anofox_fcst_core::ForecastError>(conf_result)
    }));

    match result {
        Ok(Ok(conf_result)) => {
            fill_conformal_result(out_result, &conf_result);
            true
        }
        Ok(Err(e)) => {
            set_error(out_error, ffi_error_code(&e), &e.to_string());
            false
        }
        Err(_) => {
            set_error(
                out_error,
                ErrorCode::PanicCaught,
                "Panic in conformal_predict_asymmetric",
            );
            false
        }
    }
}

/// Compute interval width (upper - lower) for each prediction.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_interval_width(
    lower: *const c_double,
    upper: *const c_double,
    length: size_t,
    out_widths: *mut *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);

    if lower.is_null() || upper.is_null() || out_widths.is_null() {
        set_error(out_error, ErrorCode::NullPointer, "Null pointer argument");
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let lower_slice = std::slice::from_raw_parts(lower, length);
        let upper_slice = std::slice::from_raw_parts(upper, length);
        anofox_fcst_core::interval_width(lower_slice, upper_slice)
    }));

    match result {
        Ok(widths) => {
            *out_widths = vec_to_c_double_array(&widths);
            true
        }
        Err(_) => {
            set_error(out_error, ErrorCode::PanicCaught, "Panic in interval_width");
            false
        }
    }
}

/// Compute mean interval width.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_mean_interval_width(
    lower: *const c_double,
    lower_validity: *const u64,
    upper: *const c_double,
    upper_validity: *const u64,
    length: size_t,
    out_result: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);

    if lower.is_null() || upper.is_null() || out_result.is_null() {
        set_error(out_error, ErrorCode::NullPointer, "Null pointer argument");
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let lower_series = build_series(lower, lower_validity, length);
        let upper_series = build_series(upper, upper_validity, length);
        let filtered = anofox_fcst_core::complete_cases(&[&lower_series, &upper_series])?;
        Ok::<_, anofox_fcst_core::ForecastError>(anofox_fcst_core::mean_interval_width(
            &filtered[0],
            &filtered[1],
        ))
    }));

    match result {
        Ok(Ok(mean_width)) => {
            *out_result = mean_width;
            true
        }
        Ok(Err(e)) => {
            set_error(out_error, ffi_error_code(&e), &e.to_string());
            false
        }
        Err(_) => {
            set_error(
                out_error,
                ErrorCode::PanicCaught,
                "Panic in mean_interval_width",
            );
            false
        }
    }
}

// ============================================================================
// Conformal Learn/Apply API (v2)
// ============================================================================

/// Learn a calibration profile from residuals.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_conformal_learn(
    residuals: *const c_double,
    residuals_validity: *const u64,
    residuals_length: size_t,
    alphas: *const c_double,
    n_alphas: size_t,
    method: types::ConformalMethodFFI,
    strategy: types::ConformalStrategyFFI,
    difficulty: *const c_double, // Optional, can be null for non-adaptive
    difficulty_validity: *const u64,
    out_profile: *mut types::CalibrationProfileFFI,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);

    if residuals.is_null() || alphas.is_null() || out_profile.is_null() {
        set_error(out_error, ErrorCode::NullPointer, "Null pointer argument");
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let residual_series = build_series(residuals, residuals_validity, residuals_length);
        let alpha_slice = std::slice::from_raw_parts(alphas, n_alphas);

        // Phase 16 D-08: (residual, difficulty) kept aligned by complete-case
        // filtering BEFORE any residual is dropped -- dropping residuals
        // first (the pre-16-02 behaviour) would silently misalign the two
        // arrays as soon as any residual is missing.
        let (residual_values, difficulty_opt): (Vec<f64>, Option<Vec<f64>>) = if difficulty
            .is_null()
        {
            (anofox_fcst_core::drop_missing(&residual_series), None)
        } else {
            let difficulty_series = build_series(difficulty, difficulty_validity, residuals_length);
            let complete =
                anofox_fcst_core::complete_cases(&[&residual_series, &difficulty_series])?;
            (complete[0].clone(), Some(complete[1].clone()))
        };

        anofox_fcst_core::conformal_learn(
            &residual_values,
            alpha_slice,
            method.into(),
            strategy.into(),
            difficulty_opt.as_deref(),
        )
    }));

    match result {
        Ok(Ok(profile)) => {
            fill_calibration_profile(out_profile, &profile);
            true
        }
        Ok(Err(e)) => {
            set_error(out_error, ffi_error_code(&e), &e.to_string());
            false
        }
        Err(_) => {
            set_error(
                out_error,
                ErrorCode::PanicCaught,
                "Panic in conformal_learn",
            );
            false
        }
    }
}

/// Apply a calibration profile to generate prediction intervals.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_conformal_apply(
    forecasts: *const c_double,
    forecasts_validity: *const u64,
    n_forecasts: size_t,
    profile: *const types::CalibrationProfileFFI,
    difficulty: *const c_double, // Optional, can be null for non-adaptive
    difficulty_validity: *const u64,
    out_intervals: *mut types::PredictionIntervalsFFI,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);

    if forecasts.is_null() || profile.is_null() || out_intervals.is_null() {
        set_error(out_error, ErrorCode::NullPointer, "Null pointer argument");
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let forecast_series = build_series(forecasts, forecasts_validity, n_forecasts);
        let filled_forecasts = anofox_fcst_core::fill_missing(&forecast_series, 0.0);
        let forecast_slice = filled_forecasts.as_slice();

        // Reconstruct CalibrationProfile from FFI
        let profile_ref = &*profile;
        let core_profile = anofox_fcst_core::CalibrationProfile {
            method: profile_ref.method.into(),
            strategy: profile_ref.strategy.into(),
            alphas: std::slice::from_raw_parts(profile_ref.alphas, profile_ref.n_levels).to_vec(),
            state_vector: if profile_ref.state_vector.is_null() {
                Vec::new()
            } else {
                std::slice::from_raw_parts(profile_ref.state_vector, profile_ref.state_vector_len)
                    .to_vec()
            },
            scores_lower: std::slice::from_raw_parts(
                profile_ref.scores_lower,
                profile_ref.n_levels,
            )
            .to_vec(),
            scores_upper: std::slice::from_raw_parts(
                profile_ref.scores_upper,
                profile_ref.n_levels,
            )
            .to_vec(),
            n_residuals: profile_ref.n_residuals,
        };

        // Phase 16 D-08: NaN-for-NULL (not drop) -- a missing prediction
        // difficulty must give NaN bounds at that position only, never
        // shift or drop other forecasts.
        let difficulty_opt: Option<Vec<f64>> = if difficulty.is_null() {
            None
        } else {
            Some(build_values(difficulty, difficulty_validity, n_forecasts))
        };

        let mut intervals = anofox_fcst_core::conformal_apply(
            forecast_slice,
            &core_profile,
            difficulty_opt.as_deref(),
        )?;
        anofox_fcst_core::propagate_missing(&mut intervals.point, &forecast_series);
        for level_lower in intervals.lower.iter_mut() {
            anofox_fcst_core::propagate_missing(level_lower, &forecast_series);
        }
        for level_upper in intervals.upper.iter_mut() {
            anofox_fcst_core::propagate_missing(level_upper, &forecast_series);
        }
        Ok::<_, anofox_fcst_core::ForecastError>(intervals)
    }));

    match result {
        Ok(Ok(intervals)) => {
            fill_prediction_intervals(out_intervals, &intervals);
            true
        }
        Ok(Err(e)) => {
            set_error(out_error, ffi_error_code(&e), &e.to_string());
            false
        }
        Err(_) => {
            set_error(
                out_error,
                ErrorCode::PanicCaught,
                "Panic in conformal_apply",
            );
            false
        }
    }
}

/// Applies a single pre-computed conformity `score` to `forecasts`,
/// optionally scaled by a per-row `difficulty` (Phase 16 D-08, CONF-04).
/// Backs the SQL `ts_conformal_intervals(forecasts, conformity_score,
/// method, difficulty)` 4-arg overload.
///
/// A null `method` pointer means "symmetric". A null `difficulty` pointer
/// or zero `difficulty_len` means "no difficulty_col at all" (never calls
/// `from_raw_parts` on a null pointer with nonzero length).
///
/// # Safety
/// All non-null pointer arguments must be valid.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_conformal_apply_score(
    forecasts: *const c_double,
    forecasts_validity: *const u64,
    n_forecasts: size_t,
    score: c_double,
    method: *const c_char,
    difficulty: *const c_double,
    difficulty_validity: *const u64,
    difficulty_len: size_t,
    out_lower: *mut *mut c_double,
    out_upper: *mut *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);

    if out_lower.is_null() || out_upper.is_null() {
        set_error(out_error, ErrorCode::NullPointer, "Null pointer argument");
        return false;
    }
    if n_forecasts != 0 && forecasts.is_null() {
        set_error(out_error, ErrorCode::NullPointer, "Null pointer argument");
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        // Phase 16 WR-01: a non-null, zero-length forecasts list must
        // produce an empty result, matching every sibling conformal FFI
        // entry point (`anofox_ts_conformal_from_pairs`,
        // `anofox_ts_conformal_calibrate`, `anofox_ts_conformal_apply`).
        let forecast_series = if n_forecasts == 0 {
            Vec::new()
        } else {
            build_series(forecasts, forecasts_validity, n_forecasts)
        };
        let difficulty_series: Vec<Option<f64>> = if difficulty.is_null() || difficulty_len == 0 {
            Vec::new()
        } else {
            build_series(difficulty, difficulty_validity, difficulty_len)
        };

        let method_str = if method.is_null() {
            "symmetric"
        } else {
            match CStr::from_ptr(method).to_str() {
                Ok(s) => s,
                Err(_) => {
                    return Err(anofox_fcst_core::ForecastError::InvalidInput(
                        "method is not valid UTF-8".to_string(),
                    ))
                }
            }
        };
        let parsed_method: anofox_fcst_core::ConformalMethod = method_str.parse()?;

        anofox_fcst_core::conformal_apply_score(
            &forecast_series,
            score,
            parsed_method,
            &difficulty_series,
        )
    }));

    match result {
        Ok(Ok((lower, upper))) => {
            // Non-finite (NaN missing marker, or +/-inf unbounded) bounds
            // are written as NaN for C++ to turn into SQL NULL.
            let lower_nan: Vec<f64> = lower.iter().copied().map(nan_if_non_finite).collect();
            let upper_nan: Vec<f64> = upper.iter().copied().map(nan_if_non_finite).collect();

            let lower_ptr =
                match alloc_or_error(&lower_nan, out_error, "Failed to allocate lower bounds") {
                    Ok(ptr) => ptr,
                    Err(()) => return false,
                };
            match alloc_or_error(&upper_nan, out_error, "Failed to allocate upper bounds") {
                Ok(upper_ptr) => {
                    *out_lower = lower_ptr;
                    *out_upper = upper_ptr;
                    true
                }
                Err(()) => {
                    free_ptr(lower_ptr as *mut core::ffi::c_void);
                    false
                }
            }
        }
        Ok(Err(e)) => {
            set_error(out_error, ffi_error_code(&e), &e.to_string());
            false
        }
        Err(_) => {
            set_error(
                out_error,
                ErrorCode::PanicCaught,
                "Panic in conformal_apply_score",
            );
            false
        }
    }
}

/// Parse a conformal strategy name, matching
/// `anofox_fcst_core::ConformalStrategy::from_str` exactly -- including the
/// D-09/G15 explanatory error for every removed `jackknife+` alias and the
/// "unknown conformal strategy" error for anything else. C++'s
/// `ParseConformalStrategy` wraps this instead of re-implementing the
/// parse/default logic, so there is exactly one place that recognizes a
/// strategy name.
///
/// # Safety
/// `name` and `out_strategy` must be valid, non-null pointers; `name` must
/// point to a NUL-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_conformal_parse_strategy(
    name: *const c_char,
    out_strategy: *mut types::ConformalStrategyFFI,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);
    if name.is_null() || out_strategy.is_null() {
        set_error(out_error, ErrorCode::NullPointer, "Null pointer argument");
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let name_str = match CStr::from_ptr(name).to_str() {
            Ok(s) => s,
            Err(_) => {
                return Err(anofox_fcst_core::ForecastError::InvalidInput(
                    "strategy name is not valid UTF-8".to_string(),
                ))
            }
        };
        name_str.parse::<anofox_fcst_core::ConformalStrategy>()
    }));

    match result {
        Ok(Ok(strategy)) => {
            *out_strategy = strategy.into();
            true
        }
        Ok(Err(e)) => {
            set_error(out_error, ffi_error_code(&e), &e.to_string());
            false
        }
        Err(_) => {
            set_error(
                out_error,
                ErrorCode::PanicCaught,
                "Panic in conformal_parse_strategy",
            );
            false
        }
    }
}

/// Parse a conformal method name, matching
/// `anofox_fcst_core::ConformalMethod::from_str` exactly. C++'s
/// `ParseConformalMethod` wraps this instead of re-implementing the
/// parse/default logic.
///
/// # Safety
/// `name` and `out_method` must be valid, non-null pointers; `name` must
/// point to a NUL-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_conformal_parse_method(
    name: *const c_char,
    out_method: *mut types::ConformalMethodFFI,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);
    if name.is_null() || out_method.is_null() {
        set_error(out_error, ErrorCode::NullPointer, "Null pointer argument");
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let name_str = match CStr::from_ptr(name).to_str() {
            Ok(s) => s,
            Err(_) => {
                return Err(anofox_fcst_core::ForecastError::InvalidInput(
                    "method name is not valid UTF-8".to_string(),
                ))
            }
        };
        name_str.parse::<anofox_fcst_core::ConformalMethod>()
    }));

    match result {
        Ok(Ok(method)) => {
            *out_method = method.into();
            true
        }
        Ok(Err(e)) => {
            set_error(out_error, ffi_error_code(&e), &e.to_string());
            false
        }
        Err(_) => {
            set_error(
                out_error,
                ErrorCode::PanicCaught,
                "Panic in conformal_parse_method",
            );
            false
        }
    }
}

/// Compute empirical coverage of prediction intervals.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_conformal_coverage(
    actuals: *const c_double,
    actuals_validity: *const u64,
    lower: *const c_double,
    lower_validity: *const u64,
    upper: *const c_double,
    upper_validity: *const u64,
    length: size_t,
    out_coverage: *mut c_double,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);

    if actuals.is_null() || lower.is_null() || upper.is_null() || out_coverage.is_null() {
        set_error(out_error, ErrorCode::NullPointer, "Null pointer argument");
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let actuals_series = build_series(actuals, actuals_validity, length);
        let lower_series = build_series(lower, lower_validity, length);
        let upper_series = build_series(upper, upper_validity, length);
        let filtered =
            anofox_fcst_core::complete_cases(&[&actuals_series, &lower_series, &upper_series])?;
        anofox_fcst_core::conformal_coverage(&filtered[0], &filtered[1], &filtered[2])
    }));

    match result {
        Ok(Ok(cov)) => {
            *out_coverage = cov;
            true
        }
        Ok(Err(e)) => {
            set_error(out_error, ffi_error_code(&e), &e.to_string());
            false
        }
        Err(_) => {
            set_error(
                out_error,
                ErrorCode::PanicCaught,
                "Panic in conformal_coverage",
            );
            false
        }
    }
}

/// Compute comprehensive conformal evaluation metrics.
///
/// # Safety
/// All pointer arguments must be valid and non-null.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_conformal_evaluate(
    actuals: *const c_double,
    actuals_validity: *const u64,
    lower: *const c_double,
    lower_validity: *const u64,
    upper: *const c_double,
    upper_validity: *const u64,
    length: size_t,
    alpha: c_double,
    out_eval: *mut types::ConformalEvaluationFFI,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);

    if actuals.is_null() || lower.is_null() || upper.is_null() || out_eval.is_null() {
        set_error(out_error, ErrorCode::NullPointer, "Null pointer argument");
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let actuals_series = build_series(actuals, actuals_validity, length);
        let lower_series = build_series(lower, lower_validity, length);
        let upper_series = build_series(upper, upper_validity, length);
        let filtered =
            anofox_fcst_core::complete_cases(&[&actuals_series, &lower_series, &upper_series])?;
        anofox_fcst_core::conformal_evaluate(&filtered[0], &filtered[1], &filtered[2], alpha)
    }));

    match result {
        Ok(Ok(eval)) => {
            (*out_eval).coverage = eval.coverage;
            (*out_eval).violation_rate = eval.violation_rate;
            (*out_eval).mean_width = eval.mean_width;
            (*out_eval).winkler_score = eval.winkler_score;
            (*out_eval).n_observations = eval.n_observations;
            true
        }
        Ok(Err(e)) => {
            set_error(out_error, ffi_error_code(&e), &e.to_string());
            false
        }
        Err(_) => {
            set_error(
                out_error,
                ErrorCode::PanicCaught,
                "Panic in conformal_evaluate",
            );
            false
        }
    }
}

// ============================================================================
// Bootstrap Prediction Functions
// ============================================================================

/// Compute bootstrap prediction intervals from residuals and point forecasts.
///
/// # Safety
/// All pointers must be valid. Output struct must be zeroed before calling.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_bootstrap_intervals(
    residuals: *const c_double,
    residuals_validity: *const u64,
    residuals_len: size_t,
    forecasts: *const c_double,
    forecasts_validity: *const u64,
    forecasts_len: size_t,
    n_paths: c_int,
    coverage: c_double,
    seed: i64,
    out_result: *mut types::BootstrapResultFFI,
    out_error: *mut types::AnofoxError,
) -> bool {
    init_error(out_error);
    if check_null_pointers(
        out_error,
        &[
            residuals as *const _,
            forecasts as *const _,
            out_result as *const _,
        ],
    ) {
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let residual_series = build_series(residuals, residuals_validity, residuals_len);
        let res_values = anofox_fcst_core::drop_missing(&residual_series);
        let forecast_series = build_series(forecasts, forecasts_validity, forecasts_len);
        let filled_forecasts = anofox_fcst_core::fill_missing(&forecast_series, 0.0);
        let seed_opt = if seed >= 0 { Some(seed as u64) } else { None };

        // Phase 16 T-16-17: n_paths < 1 is a caller error, never silently
        // clamped up to 1 -- `bootstrap_intervals` itself rejects < 10, but
        // a negative/zero c_int must not reach `as usize` wraparound first.
        if n_paths < 1 {
            return Err(anofox_fcst_core::ForecastError::InvalidInput(format!(
                "n_paths must be >= 10, got {n_paths}"
            )));
        }
        let mut intervals = anofox_fcst_core::bootstrap_intervals(
            &res_values,
            &filled_forecasts,
            n_paths as usize,
            coverage,
            seed_opt,
        )?;
        anofox_fcst_core::propagate_missing(&mut intervals.point, &forecast_series);
        anofox_fcst_core::propagate_missing(&mut intervals.lower, &forecast_series);
        anofox_fcst_core::propagate_missing(&mut intervals.upper, &forecast_series);
        Ok::<_, anofox_fcst_core::ForecastError>(intervals)
    }));

    match result {
        Ok(Ok(intervals)) => {
            (*out_result).n_forecasts = intervals.point.len();
            (*out_result).coverage = intervals.coverage;

            (*out_result).point =
                match alloc_or_error(&intervals.point, out_error, "Failed to allocate point") {
                    Ok(ptr) => ptr,
                    Err(()) => return false,
                };
            (*out_result).lower =
                match alloc_or_error(&intervals.lower, out_error, "Failed to allocate lower") {
                    Ok(ptr) => ptr,
                    Err(()) => return false,
                };
            (*out_result).upper =
                match alloc_or_error(&intervals.upper, out_error, "Failed to allocate upper") {
                    Ok(ptr) => ptr,
                    Err(()) => return false,
                };
            true
        }
        Ok(Err(e)) => {
            set_error(out_error, ffi_error_code(&e), &e.to_string());
            false
        }
        Err(_) => {
            set_error(
                out_error,
                ErrorCode::PanicCaught,
                "Panic in bootstrap_intervals",
            );
            false
        }
    }
}

/// Compute bootstrap quantile forecasts at multiple quantile levels.
///
/// # Safety
/// All pointers must be valid. Output struct must be zeroed before calling.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_bootstrap_quantiles(
    residuals: *const c_double,
    residuals_validity: *const u64,
    residuals_len: size_t,
    forecasts: *const c_double,
    forecasts_validity: *const u64,
    forecasts_len: size_t,
    n_paths: c_int,
    quantile_levels: *const c_double,
    n_quantiles: size_t,
    seed: i64,
    out_result: *mut types::BootstrapQuantileResultFFI,
    out_error: *mut types::AnofoxError,
) -> bool {
    init_error(out_error);
    if check_null_pointers(
        out_error,
        &[
            residuals as *const _,
            forecasts as *const _,
            quantile_levels as *const _,
            out_result as *const _,
        ],
    ) {
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let residual_series = build_series(residuals, residuals_validity, residuals_len);
        let res_values = anofox_fcst_core::drop_missing(&residual_series);
        let forecast_series = build_series(forecasts, forecasts_validity, forecasts_len);
        let filled_forecasts = anofox_fcst_core::fill_missing(&forecast_series, 0.0);
        let q_slice = std::slice::from_raw_parts(quantile_levels, n_quantiles);
        let seed_opt = if seed >= 0 { Some(seed as u64) } else { None };

        if n_paths < 1 {
            return Err(anofox_fcst_core::ForecastError::InvalidInput(format!(
                "n_paths must be >= 10, got {n_paths}"
            )));
        }
        let mut qr = anofox_fcst_core::bootstrap_quantiles(
            &res_values,
            &filled_forecasts,
            n_paths as usize,
            q_slice,
            seed_opt,
        )?;
        anofox_fcst_core::propagate_missing(&mut qr.point, &forecast_series);
        for row in qr.values.iter_mut() {
            anofox_fcst_core::propagate_missing(row, &forecast_series);
        }
        Ok::<_, anofox_fcst_core::ForecastError>(qr)
    }));

    match result {
        Ok(Ok(qr)) => {
            (*out_result).n_forecasts = qr.point.len();
            (*out_result).n_quantiles = qr.quantiles.len();

            (*out_result).point =
                match alloc_or_error(&qr.point, out_error, "Failed to allocate point") {
                    Ok(ptr) => ptr,
                    Err(()) => return false,
                };
            (*out_result).quantiles =
                match alloc_or_error(&qr.quantiles, out_error, "Failed to allocate quantiles") {
                    Ok(ptr) => ptr,
                    Err(()) => return false,
                };

            // Flatten values matrix: values[q * n_forecasts + t]
            let n_f = qr.point.len();
            let mut flat: Vec<f64> = Vec::with_capacity(qr.quantiles.len() * n_f);
            for q_vals in &qr.values {
                flat.extend_from_slice(q_vals);
            }
            (*out_result).values =
                match alloc_or_error(&flat, out_error, "Failed to allocate values") {
                    Ok(ptr) => ptr,
                    Err(()) => return false,
                };
            true
        }
        Ok(Err(e)) => {
            set_error(out_error, ffi_error_code(&e), &e.to_string());
            false
        }
        Err(_) => {
            set_error(
                out_error,
                ErrorCode::PanicCaught,
                "Panic in bootstrap_quantiles",
            );
            false
        }
    }
}

/// Free a BootstrapResultFFI.
///
/// # Safety
/// Must only be called with a valid pointer returned by `anofox_ts_bootstrap_intervals`.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_bootstrap_result(result: *mut types::BootstrapResultFFI) {
    if result.is_null() {
        return;
    }
    free_ptr((*result).point as *mut _);
    free_ptr((*result).lower as *mut _);
    free_ptr((*result).upper as *mut _);
    (*result).point = ptr::null_mut();
    (*result).lower = ptr::null_mut();
    (*result).upper = ptr::null_mut();
}

/// Free a BootstrapQuantileResultFFI.
///
/// # Safety
/// Must only be called with a valid pointer returned by `anofox_ts_bootstrap_quantiles`.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_bootstrap_quantile_result(
    result: *mut types::BootstrapQuantileResultFFI,
) {
    if result.is_null() {
        return;
    }
    free_ptr((*result).point as *mut _);
    free_ptr((*result).quantiles as *mut _);
    free_ptr((*result).values as *mut _);
    (*result).point = ptr::null_mut();
    (*result).quantiles = ptr::null_mut();
    (*result).values = ptr::null_mut();
}

// ============================================================================
// Per-Step Conformal Prediction Functions
// ============================================================================

/// Compute per-step conformal prediction intervals.
///
/// Each horizon step gets its own interval width. Requires fold-organized,
/// FLAT data: `fold_forecasts`/`fold_actuals` are `fold_forecasts_len`
/// elements long (fold-major), validated against `horizon` in Rust (D-10 —
/// `fold_forecasts_len` must be an exact multiple of `horizon`; the caller
/// never computes `n_folds` itself). Validity bitmasks make the per-step
/// quantile computation pairwise-complete (D-02); a missing `point_forecasts`
/// entry is position-preserving in the output (NaN at that position only).
///
/// # Safety
/// All pointers must be valid (validity pointers may be null = all valid).
/// Output struct must be zeroed before calling.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_conformal_predict_per_step(
    fold_forecasts: *const c_double,
    fold_forecasts_validity: *const u64,
    fold_forecasts_len: size_t,
    fold_actuals: *const c_double,
    fold_actuals_validity: *const u64,
    fold_actuals_len: size_t,
    horizon: size_t,
    point_forecasts: *const c_double,
    point_validity: *const u64,
    n_point_forecasts: size_t,
    alpha: c_double,
    out_result: *mut types::ConformalPerStepResultFFI,
    out_error: *mut types::AnofoxError,
) -> bool {
    init_error(out_error);
    if check_null_pointers(
        out_error,
        &[
            fold_forecasts as *const _,
            fold_actuals as *const _,
            point_forecasts as *const _,
            out_result as *const _,
        ],
    ) {
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let fold_forecasts_series =
            build_series(fold_forecasts, fold_forecasts_validity, fold_forecasts_len);
        let fold_actuals_series =
            build_series(fold_actuals, fold_actuals_validity, fold_actuals_len);
        let point_series = build_series(point_forecasts, point_validity, n_point_forecasts);

        anofox_fcst_core::conformal::conformal_predict_per_step_validated(
            &fold_forecasts_series,
            &fold_actuals_series,
            horizon,
            &point_series,
            alpha,
        )
    }));

    match result {
        Ok(Ok(ps)) => {
            (*out_result).n_forecasts = ps.point.len();
            (*out_result).coverage = ps.coverage;

            // Phase 16 D-05/G16: an insufficient step's half-width (and the
            // bounds derived from it) is infinite -- written as NaN so C++
            // can turn it into SQL NULL; a NaN half-width is the only marker
            // of an insufficient step (no new per-step field).
            let lower_nan: Vec<f64> = ps.lower.iter().copied().map(nan_if_non_finite).collect();
            let upper_nan: Vec<f64> = ps.upper.iter().copied().map(nan_if_non_finite).collect();
            let half_widths_nan: Vec<f64> = ps
                .half_widths
                .iter()
                .copied()
                .map(nan_if_non_finite)
                .collect();

            (*out_result).point =
                match alloc_or_error(&ps.point, out_error, "Failed to allocate point") {
                    Ok(ptr) => ptr,
                    Err(()) => return false,
                };
            (*out_result).lower =
                match alloc_or_error(&lower_nan, out_error, "Failed to allocate lower") {
                    Ok(ptr) => ptr,
                    Err(()) => return false,
                };
            (*out_result).upper =
                match alloc_or_error(&upper_nan, out_error, "Failed to allocate upper") {
                    Ok(ptr) => ptr,
                    Err(()) => return false,
                };
            (*out_result).half_widths = match alloc_or_error(
                &half_widths_nan,
                out_error,
                "Failed to allocate half_widths",
            ) {
                Ok(ptr) => ptr,
                Err(()) => return false,
            };
            true
        }
        Ok(Err(e)) => {
            let code = match e {
                anofox_fcst_core::ForecastError::InvalidInput(_) => ErrorCode::InvalidInput,
                anofox_fcst_core::ForecastError::InsufficientData { .. } => {
                    ErrorCode::InsufficientData
                }
                _ => ErrorCode::ComputationError,
            };
            set_error(out_error, code, &format!("{}", e));
            false
        }
        Err(_) => {
            set_error(
                out_error,
                ErrorCode::PanicCaught,
                "Panic in conformal_predict_per_step",
            );
            false
        }
    }
}

/// Free a ConformalPerStepResultFFI.
///
/// # Safety
/// Must only be called with a valid pointer returned by `anofox_ts_conformal_predict_per_step`.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_conformal_per_step_result(
    result: *mut types::ConformalPerStepResultFFI,
) {
    if result.is_null() {
        return;
    }
    free_ptr((*result).point as *mut _);
    free_ptr((*result).lower as *mut _);
    free_ptr((*result).upper as *mut _);
    free_ptr((*result).half_widths as *mut _);
    (*result).point = ptr::null_mut();
    (*result).lower = ptr::null_mut();
    (*result).upper = ptr::null_mut();
    (*result).half_widths = ptr::null_mut();
}

/// Helper function to fill a CalibrationProfileFFI from a core CalibrationProfile.
///
/// Phase 16 D-05: a per-alpha score can be `f64::INFINITY` when that level's
/// calibration is insufficient; written here as NaN so C++ can turn it into
/// SQL NULL.
unsafe fn fill_calibration_profile(
    out: *mut types::CalibrationProfileFFI,
    profile: &anofox_fcst_core::CalibrationProfile,
) {
    let state_vector_nan: Vec<f64> = profile
        .state_vector
        .iter()
        .copied()
        .map(nan_if_non_finite)
        .collect();
    let scores_lower_nan: Vec<f64> = profile
        .scores_lower
        .iter()
        .copied()
        .map(nan_if_non_finite)
        .collect();
    let scores_upper_nan: Vec<f64> = profile
        .scores_upper
        .iter()
        .copied()
        .map(nan_if_non_finite)
        .collect();

    (*out).method = profile.method.into();
    (*out).strategy = profile.strategy.into();
    (*out).alphas = vec_to_c_double_array(&profile.alphas);
    (*out).state_vector = vec_to_c_double_array(&state_vector_nan);
    (*out).state_vector_len = profile.state_vector.len();
    (*out).scores_lower = vec_to_c_double_array(&scores_lower_nan);
    (*out).scores_upper = vec_to_c_double_array(&scores_upper_nan);
    (*out).n_levels = profile.alphas.len();
    (*out).n_residuals = profile.n_residuals;
}

/// Helper function to fill a PredictionIntervalsFFI from a core PredictionIntervals.
unsafe fn fill_prediction_intervals(
    out: *mut types::PredictionIntervalsFFI,
    intervals: &anofox_fcst_core::PredictionIntervals,
) {
    let n_forecasts = intervals.point.len();
    let n_levels = intervals.coverage.len();

    (*out).point = vec_to_c_double_array(&intervals.point);
    (*out).n_forecasts = n_forecasts;
    (*out).coverage = vec_to_c_double_array(&intervals.coverage);
    (*out).n_levels = n_levels;
    (*out).method = intervals.method.into();

    // Flatten lower/upper: [level0_forecasts..., level1_forecasts..., ...]
    let total_size = n_levels * n_forecasts;
    let lower_ptr = alloc_double_array(total_size);
    let upper_ptr = alloc_double_array(total_size);

    for level_idx in 0..n_levels {
        for fcst_idx in 0..n_forecasts {
            let flat_idx = level_idx * n_forecasts + fcst_idx;
            // Phase 16 D-05: an insufficient level's score is infinite, so
            // its bounds are +/-inf -- written as NaN for C++ to NULL.
            *lower_ptr.add(flat_idx) = nan_if_non_finite(intervals.lower[level_idx][fcst_idx]);
            *upper_ptr.add(flat_idx) = nan_if_non_finite(intervals.upper[level_idx][fcst_idx]);
        }
    }

    (*out).lower = lower_ptr;
    (*out).upper = upper_ptr;
}

/// Helper function to fill a ConformalResultFFI from a core ConformalResult.
/// Maps a non-finite value to NaN, leaving a finite value untouched (Phase
/// 16: the core represents "unbounded" with +/-inf; the FFI boundary writes
/// non-finite score/coverage/bounds as NaN so C++ can turn them into SQL
/// NULL via its existing NaN-as-NULL writers).
#[inline]
fn nan_if_non_finite(v: f64) -> f64 {
    if v.is_finite() {
        v
    } else {
        f64::NAN
    }
}

unsafe fn fill_conformal_result(
    out: *mut ConformalResultFFI,
    result: &anofox_fcst_core::ConformalResult,
) {
    let lower_nan: Vec<f64> = result
        .lower
        .iter()
        .copied()
        .map(nan_if_non_finite)
        .collect();
    let upper_nan: Vec<f64> = result
        .upper
        .iter()
        .copied()
        .map(nan_if_non_finite)
        .collect();

    (*out).point = vec_to_c_double_array(&result.point);
    (*out).lower = vec_to_c_double_array(&lower_nan);
    (*out).upper = vec_to_c_double_array(&upper_nan);
    (*out).n_forecasts = result.point.len();
    (*out).coverage = result.coverage; // already NaN when insufficient
    (*out).nominal_level = result.nominal_level;
    (*out).conformity_score = nan_if_non_finite(result.conformity_score);
    (*out).n_calibration = result.n_calibration;
    (*out).calibration_insufficient = result.calibration_insufficient;

    // Copy method name
    let method_bytes = result.method.as_bytes();
    let len = method_bytes.len().min(31);
    for (i, &b) in method_bytes[..len].iter().enumerate() {
        (*out).method[i] = b as c_char;
    }
    (*out).method[len] = 0; // Null terminator
}

/// Helper function to fill a ConformalCalibrationFFI from a core ConformalCalibration.
unsafe fn fill_conformal_calibration(
    out: *mut ConformalCalibrationFFI,
    result: &anofox_fcst_core::ConformalCalibration,
) {
    (*out).coverage = result.coverage; // already NaN when insufficient
    (*out).nominal_level = result.nominal_level;
    (*out).conformity_score = nan_if_non_finite(result.conformity_score);
    (*out).n_calibration = result.n_calibration;
    (*out).calibration_insufficient = result.calibration_insufficient;

    let method_str = result.method.to_string();
    let method_bytes = method_str.as_bytes();
    let len = method_bytes.len().min(31);
    for (i, &b) in method_bytes[..len].iter().enumerate() {
        (*out).method[i] = b as c_char;
    }
    (*out).method[len] = 0; // Null terminator
}

/// Helper function to fill a ConformalMultiResultFFI from a core ConformalMultiResult.
unsafe fn fill_conformal_multi_result(
    out: *mut ConformalMultiResultFFI,
    result: &anofox_fcst_core::ConformalMultiResult,
) {
    let n_forecasts = result.point.len();
    let n_levels = result.intervals.len();

    (*out).point = vec_to_c_double_array(&result.point);
    (*out).n_forecasts = n_forecasts;
    (*out).n_levels = n_levels;

    // Allocate and fill coverage levels and conformity scores
    (*out).coverage_levels = alloc_double_array(n_levels);
    (*out).conformity_scores = alloc_double_array(n_levels);

    for (i, interval) in result.intervals.iter().enumerate() {
        *(*out).coverage_levels.add(i) = interval.coverage;
        *(*out).conformity_scores.add(i) = interval.conformity_score;
    }

    // Flatten lower and upper bounds (level-major order)
    let total_size = n_forecasts * n_levels;
    (*out).lower = alloc_double_array(total_size);
    (*out).upper = alloc_double_array(total_size);

    for (level_idx, interval) in result.intervals.iter().enumerate() {
        for (forecast_idx, (&l, &u)) in interval.lower.iter().zip(interval.upper.iter()).enumerate()
        {
            let flat_idx = level_idx * n_forecasts + forecast_idx;
            *(*out).lower.add(flat_idx) = l;
            *(*out).upper.add(flat_idx) = u;
        }
    }
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

/// Free a FlatMultiPeriodResult.
///
/// Frees all parallel arrays allocated by the flat period detection functions.
///
/// # Safety
/// The result pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_flat_multi_period_result(
    result: *mut types::FlatMultiPeriodResult,
) {
    if result.is_null() {
        return;
    }
    let r = &mut *result;

    if !r.period_values.is_null() {
        free(r.period_values as *mut core::ffi::c_void);
        r.period_values = ptr::null_mut();
    }
    if !r.confidence_values.is_null() {
        free(r.confidence_values as *mut core::ffi::c_void);
        r.confidence_values = ptr::null_mut();
    }
    if !r.strength_values.is_null() {
        free(r.strength_values as *mut core::ffi::c_void);
        r.strength_values = ptr::null_mut();
    }
    if !r.amplitude_values.is_null() {
        free(r.amplitude_values as *mut core::ffi::c_void);
        r.amplitude_values = ptr::null_mut();
    }
    if !r.phase_values.is_null() {
        free(r.phase_values as *mut core::ffi::c_void);
        r.phase_values = ptr::null_mut();
    }
    if !r.iteration_values.is_null() {
        free(r.iteration_values as *mut core::ffi::c_void);
        r.iteration_values = ptr::null_mut();
    }
    if !r.matches_expected_values.is_null() {
        free(r.matches_expected_values as *mut core::ffi::c_void);
        r.matches_expected_values = ptr::null_mut();
    }
    if !r.matched_expected_values.is_null() {
        free(r.matched_expected_values as *mut core::ffi::c_void);
        r.matched_expected_values = ptr::null_mut();
    }
    if !r.match_deviation_values.is_null() {
        free(r.match_deviation_values as *mut core::ffi::c_void);
        r.match_deviation_values = ptr::null_mut();
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

/// Free a ConformalResultFFI.
///
/// # Safety
/// The result pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_conformal_result(result: *mut types::ConformalResultFFI) {
    if result.is_null() {
        return;
    }
    let r = &mut *result;

    if !r.point.is_null() {
        free(r.point as *mut core::ffi::c_void);
        r.point = ptr::null_mut();
    }
    if !r.lower.is_null() {
        free(r.lower as *mut core::ffi::c_void);
        r.lower = ptr::null_mut();
    }
    if !r.upper.is_null() {
        free(r.upper as *mut core::ffi::c_void);
        r.upper = ptr::null_mut();
    }
}

/// Free a ConformalMultiResultFFI.
///
/// # Safety
/// The result pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_conformal_multi_result(
    result: *mut types::ConformalMultiResultFFI,
) {
    if result.is_null() {
        return;
    }
    let r = &mut *result;

    if !r.point.is_null() {
        free(r.point as *mut core::ffi::c_void);
        r.point = ptr::null_mut();
    }
    if !r.coverage_levels.is_null() {
        free(r.coverage_levels as *mut core::ffi::c_void);
        r.coverage_levels = ptr::null_mut();
    }
    if !r.conformity_scores.is_null() {
        free(r.conformity_scores as *mut core::ffi::c_void);
        r.conformity_scores = ptr::null_mut();
    }
    if !r.lower.is_null() {
        free(r.lower as *mut core::ffi::c_void);
        r.lower = ptr::null_mut();
    }
    if !r.upper.is_null() {
        free(r.upper as *mut core::ffi::c_void);
        r.upper = ptr::null_mut();
    }
}

/// Free a CalibrationProfileFFI.
///
/// # Safety
/// The result pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_calibration_profile(
    result: *mut types::CalibrationProfileFFI,
) {
    if result.is_null() {
        return;
    }
    let r = &mut *result;

    if !r.alphas.is_null() {
        free(r.alphas as *mut core::ffi::c_void);
        r.alphas = ptr::null_mut();
    }
    if !r.state_vector.is_null() {
        free(r.state_vector as *mut core::ffi::c_void);
        r.state_vector = ptr::null_mut();
    }
    if !r.scores_lower.is_null() {
        free(r.scores_lower as *mut core::ffi::c_void);
        r.scores_lower = ptr::null_mut();
    }
    if !r.scores_upper.is_null() {
        free(r.scores_upper as *mut core::ffi::c_void);
        r.scores_upper = ptr::null_mut();
    }
}

/// Free a PredictionIntervalsFFI.
///
/// # Safety
/// The result pointer must be valid or null.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_prediction_intervals(
    result: *mut types::PredictionIntervalsFFI,
) {
    if result.is_null() {
        return;
    }
    let r = &mut *result;

    if !r.point.is_null() {
        free(r.point as *mut core::ffi::c_void);
        r.point = ptr::null_mut();
    }
    if !r.coverage.is_null() {
        free(r.coverage as *mut core::ffi::c_void);
        r.coverage = ptr::null_mut();
    }
    if !r.lower.is_null() {
        free(r.lower as *mut core::ffi::c_void);
        r.lower = ptr::null_mut();
    }
    if !r.upper.is_null() {
        free(r.upper as *mut core::ffi::c_void);
        r.upper = ptr::null_mut();
    }
}

// ============================================================================
// Stationarity / Diagnostic Functions (Phase 1: STAT-01 ADF)
// ============================================================================

/// Run the Augmented Dickey-Fuller (ADF) unit-root test.
///
/// # Arguments
///
/// * `values`     — Pointer to a contiguous array of `f64` values (the time series).
/// * `validity`   — DuckDB validity bitmask (`NULL` means all valid). Bit i of
///                  `validity[i/64]` is 1 when element i is non-NULL.
///                  NULL entries become `NaN` in the series passed to the crate.
/// * `length`     — Number of elements in `values`.
/// * `max_lags`   — Maximum lag count for AIC selection. Pass `-1` for automatic
///                  selection (`floor((n-1)^(1/3))`). Clamped the way statsmodels
///                  does by the crate.
/// * `regression` — Optional NUL-terminated C string, `"c"` or `"ct"`
///                  (case-insensitive). `null` means `"c"` (D-05). Any other
///                  value is `ErrorCode::InvalidInput` with message
///                  "expected one of: c, ct".
/// * `out_result` — Pointer to caller-allocated `AnofoxStationarityResult`.
///                  Initialised to `Default` before the computation so that
///                  partial results are never exposed on error.
/// * `out_error`  — Pointer to caller-allocated `AnofoxError`. Set on failure.
///
/// Returns `true` on success, `false` on error (check `out_error`).
///
/// # Safety
///
/// `values` and `out_result` must be non-null. `validity` may be null (meaning all valid).
/// `length` must equal the number of valid `f64` elements at `values`. `regression`
/// must be either null or a valid NUL-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_adf(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    max_lags: c_int,
    regression: *const c_char,
    out_result: *mut AnofoxStationarityResult,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);

    // Threat T-01-01: null pointer check
    let ptrs = &[
        values as *const core::ffi::c_void,
        out_result as *const core::ffi::c_void,
    ];
    if check_null_pointers(out_error, ptrs) {
        return false;
    }

    // Initialise output to Default so callers never see uninitialised memory
    *out_result = AnofoxStationarityResult::default();

    // Threat T-01-03: empty series → return Default (NaN statistic), not an error
    if length == 0 {
        return true;
    }

    // D-05/T-17-03: parse `regression` OUTSIDE catch_unwind so an invalid
    // value is reported as InvalidInput, not caught as a panic.
    let regression = match parse_regression(regression) {
        Ok(r) => r,
        Err(e) => {
            set_error(out_error, ffi_error_code(&e), &e.to_string());
            return false;
        }
    };

    // Threat T-01-04: clamp max_lags; negative → auto (None)
    let max_lags_opt: Option<usize> = if max_lags < 0 {
        None
    } else {
        Some(max_lags as usize)
    };

    // Threat T-01-02: catch panics in the Rust computation
    let result = catch_unwind(AssertUnwindSafe(|| {
        // Use build_values: NULLs become NaN (consistent with crate NaN-propagation behaviour)
        let series = build_values(values, validity, length);
        anofox_fcst_core::adf(&series, max_lags_opt, regression)
    }));

    match result {
        Ok(r) => {
            *out_result = r.into();
            true
        }
        Err(_) => {
            set_error(out_error, ErrorCode::PanicCaught, "Panic in anofox_ts_adf");
            false
        }
    }
}

/// Phase 12 D-03/D-04 single-series missing-data gate: trims leading/trailing
/// missing values and rejects any remaining interior missing value, naming its
/// 1-based position in the ORIGINAL list. `validity` may be null (all valid);
/// NaN is treated as missing (Phase 10 D-12 convention) — see
/// `anofox_fcst_core::missing::trim_edges_reject_interior`.
///
/// - `length == 0` -> success, `*out_start = 0`, `*out_length = 0` (`values`
///   may be null in that case only).
/// - Nothing present after trimming -> success, `*out_start = 0`,
///   `*out_length = 0` (D-04: caller emits SQL NULL).
/// - Otherwise -> success, `*out_start`/`*out_length` describe the trimmed,
///   fully-present interior `[start, start+length)` of the ORIGINAL array.
/// - An interior missing value -> failure, `ErrorCode::InvalidInput`, message
///   contains "interior NULL at position k" (1-based).
///
/// # Safety
///
/// `out_start`, `out_length` and `out_error` must be non-null. `values` must
/// be non-null when `length > 0`. `validity` may be null. `length` must equal
/// the number of `f64` elements at `values`.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_series_gate(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    out_start: *mut size_t,
    out_length: *mut size_t,
    out_error: *mut AnofoxError,
) -> bool {
    init_error(out_error);

    if check_null_pointers(
        out_error,
        &[
            out_start as *const core::ffi::c_void,
            out_length as *const core::ffi::c_void,
        ],
    ) {
        return false;
    }

    if length == 0 {
        *out_start = 0;
        *out_length = 0;
        return true;
    }

    if check_null_pointers(out_error, &[values as *const core::ffi::c_void]) {
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let series = build_series(values, validity, length);
        anofox_fcst_core::trim_edges_reject_interior(&series)
    }));

    match result {
        Ok(Ok(None)) => {
            *out_start = 0;
            *out_length = 0;
            true
        }
        Ok(Ok(Some(range))) => {
            *out_start = range.start;
            *out_length = range.len();
            true
        }
        Ok(Err(e)) => {
            set_error(out_error, ErrorCode::InvalidInput, &e.to_string());
            false
        }
        Err(_) => {
            set_error(
                out_error,
                ErrorCode::PanicCaught,
                "Panic in anofox_ts_series_gate",
            );
            false
        }
    }
}

/// Run the KPSS stationarity test on a single series (STAT-02).
///
/// # Safety
///
/// `values` and `out_result` must be non-null. `validity` may be null (all
/// valid). `length` must equal the number of `f64` elements at `values`.
/// `regression` must be either null or a valid NUL-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_kpss(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    lags: c_int,
    regression: *const c_char,
    out_result: *mut AnofoxStationarityResult,
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

    *out_result = AnofoxStationarityResult::default();

    if length == 0 {
        return true;
    }

    // D-05a: parse `regression` OUTSIDE catch_unwind (see anofox_ts_adf).
    let regression = match parse_regression(regression) {
        Ok(r) => r,
        Err(e) => {
            set_error(out_error, ffi_error_code(&e), &e.to_string());
            return false;
        }
    };

    let lags_opt: Option<usize> = if lags < 0 { None } else { Some(lags as usize) };

    let result = catch_unwind(AssertUnwindSafe(|| {
        let series = build_values(values, validity, length);
        anofox_fcst_core::kpss(&series, lags_opt, regression)
    }));

    match result {
        Ok(r) => {
            *out_result = r.into();
            true
        }
        Err(_) => {
            set_error(out_error, ErrorCode::PanicCaught, "Panic in anofox_ts_kpss");
            false
        }
    }
}

/// Run the combined ADF + KPSS stationarity verdict on a single series (STAT-03).
///
/// # Safety
///
/// `values` and `out_result` must be non-null. `validity` may be null (all
/// valid). `length` must equal the number of `f64` elements at `values`.
/// `regression` must be either null or a valid NUL-terminated C string.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_stationarity(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    regression: *const c_char,
    out_result: *mut AnofoxCombinedStationarityResult,
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

    *out_result = AnofoxCombinedStationarityResult::default();

    if length == 0 {
        return true;
    }

    // D-04/D-05: parse `regression` OUTSIDE catch_unwind (see anofox_ts_adf).
    let regression = match parse_regression(regression) {
        Ok(r) => r,
        Err(e) => {
            set_error(out_error, ffi_error_code(&e), &e.to_string());
            return false;
        }
    };

    let result = catch_unwind(AssertUnwindSafe(|| {
        let series = build_values(values, validity, length);
        anofox_fcst_core::stationarity(&series, regression)
    }));

    match result {
        Ok(r) => {
            (*out_result).adf_statistic = r.adf_statistic;
            (*out_result).adf_p_value = r.adf_p_value;
            (*out_result).kpss_statistic = r.kpss_statistic;
            (*out_result).kpss_p_value = r.kpss_p_value;
            (*out_result).adf_is_stationary = r.adf_is_stationary;
            (*out_result).kpss_is_stationary = r.kpss_is_stationary;
            // D-04: None (NULL verdict) copies an empty string; the C++
            // caller sets the verdict child NULL when the buffer is empty.
            copy_string_to_buffer(r.verdict.unwrap_or(""), &mut (*out_result).verdict);
            true
        }
        Err(_) => {
            set_error(
                out_error,
                ErrorCode::PanicCaught,
                "Panic in anofox_ts_stationarity",
            );
            false
        }
    }
}

/// Ljung-Box white-noise test on residuals, model-aware (RESID-01, D-06/DIAG-04).
///
/// `fitted_params` — number of estimated model parameters (e.g. `p + q` for
/// an ARMA(p, q) residual series); `df = lags - fitted_params`. A negative
/// value is `ErrorCode::InvalidInput`. `df <= 0` at the effective (resolved,
/// clamped) lag count is `ErrorCode::InvalidInput` naming `fitted_params`,
/// the lags and "degrees of freedom" (see `ffi_error_code`).
///
/// # Safety
/// `values` and `out_result` must be non-null. `validity` may be null (all valid).
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_ljung_box(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    lags: c_int,
    fitted_params: c_int,
    out_result: *mut AnofoxLjungBoxResult,
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
    *out_result = AnofoxLjungBoxResult::default();
    if length == 0 {
        return true;
    }
    if fitted_params < 0 {
        set_error(
            out_error,
            ErrorCode::InvalidInput,
            &format!("fitted_params must be >= 0, got {fitted_params}"),
        );
        return false;
    }
    let lags_opt: Option<usize> = if lags < 0 { None } else { Some(lags as usize) };
    let fitted_params = fitted_params as usize;
    let result = catch_unwind(AssertUnwindSafe(|| {
        let series = build_values(values, validity, length);
        anofox_fcst_core::ljung_box(&series, lags_opt, fitted_params)
    }));
    match result {
        Ok(Ok(r)) => {
            *out_result = r.into();
            true
        }
        Ok(Err(e)) => {
            set_error(out_error, ffi_error_code(&e), &e.to_string());
            false
        }
        Err(_) => {
            set_error(
                out_error,
                ErrorCode::PanicCaught,
                "Panic in anofox_ts_ljung_box",
            );
            false
        }
    }
}

/// Durbin-Watson first-order autocorrelation statistic on residuals (RESID-02).
///
/// # Safety
/// `values` and `out_result` must be non-null. `validity` may be null (all valid).
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_durbin_watson(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    out_result: *mut AnofoxDurbinWatsonResult,
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
    *out_result = AnofoxDurbinWatsonResult::default();
    if length == 0 {
        return true;
    }
    let result = catch_unwind(AssertUnwindSafe(|| {
        let series = build_values(values, validity, length);
        anofox_fcst_core::durbin_watson(&series)
    }));
    match result {
        Ok(r) => {
            (*out_result).statistic = r.statistic;
            copy_string_to_buffer(r.interpretation, &mut (*out_result).interpretation);
            true
        }
        Err(_) => {
            set_error(
                out_error,
                ErrorCode::PanicCaught,
                "Panic in anofox_ts_durbin_watson",
            );
            false
        }
    }
}

/// Jarque-Bera normality test on residuals (RESID-03).
///
/// # Safety
/// `values` and `out_result` must be non-null. `validity` may be null (all valid).
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_jarque_bera(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    out_result: *mut AnofoxJarqueBeraResult,
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
    *out_result = AnofoxJarqueBeraResult::default();
    if length == 0 {
        return true;
    }
    let result = catch_unwind(AssertUnwindSafe(|| {
        let series = build_values(values, validity, length);
        anofox_fcst_core::jarque_bera(&series)
    }));
    match result {
        Ok(r) => {
            *out_result = r.into();
            true
        }
        Err(_) => {
            set_error(
                out_error,
                ErrorCode::PanicCaught,
                "Panic in anofox_ts_jarque_bera",
            );
            false
        }
    }
}

/// Combined residual-diagnostics report (RESID-04): Ljung-Box + Durbin-Watson +
/// Jarque-Bera with a pass/fail adequacy verdict (Ljung-Box p-value > `alpha`).
///
/// # Safety
/// `values` and `out_result` must be non-null. `validity` may be null (all valid).
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_residual_diagnostics(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    alpha: c_double,
    out_result: *mut AnofoxResidualDiagnosticsResult,
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
    *out_result = AnofoxResidualDiagnosticsResult::default();
    if length == 0 {
        return true;
    }
    let alpha_val = if alpha.is_finite() && alpha > 0.0 && alpha < 1.0 {
        alpha
    } else {
        0.05
    };
    let result = catch_unwind(AssertUnwindSafe(|| {
        let series = build_values(values, validity, length);
        anofox_fcst_core::residual_diagnostics(&series, alpha_val)
    }));
    match result {
        Ok(r) => {
            (*out_result).lb_statistic = r.lb_statistic;
            (*out_result).lb_p_value = r.lb_p_value;
            (*out_result).lb_lags = r.lb_lags;
            (*out_result).dw_statistic = r.dw_statistic;
            copy_string_to_buffer(r.dw_interpretation, &mut (*out_result).dw_interpretation);
            (*out_result).jb_statistic = r.jb_statistic;
            (*out_result).jb_p_value = r.jb_p_value;
            (*out_result).jb_skewness = r.jb_skewness;
            (*out_result).jb_excess_kurtosis = r.jb_excess_kurtosis;
            (*out_result).adequate = r.adequate;
            true
        }
        Err(_) => {
            set_error(
                out_error,
                ErrorCode::PanicCaught,
                "Panic in anofox_ts_residual_diagnostics",
            );
            false
        }
    }
}

// ============================================================================
// Panel Forecasting (Phase 2: GLOB-01..03)
// ============================================================================

// Imports for global panel models (Phase 2: GLOB-01..03)
use anofox_fcst_core::fill_nulls_interpolate;
use anofox_forecast::models::exponential::{GlobalAutoETS, ModelPool};
use anofox_forecast::models::intermittent::GlobalCroston;
use anofox_forecast::models::theta::GlobalTheta;

/// Error type used by panel FFI impl — wraps upstream ForecastError for uniformity.
#[derive(Debug)]
pub(crate) enum PanelForecastError {
    /// Model is not supported by the panel function.
    InvalidModel(String),
    /// A parameter string (croston_variant, model_pool) failed strict
    /// parsing — D-07: never a silent default/dropped entry.
    InvalidParameter(String),
    /// Upstream forecast error (fit/predict failure).
    Upstream(anofox_forecast::ForecastError),
}

impl std::fmt::Display for PanelForecastError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            PanelForecastError::InvalidModel(msg) => write!(f, "invalid model: {}", msg),
            PanelForecastError::InvalidParameter(msg) => write!(f, "{}", msg),
            PanelForecastError::Upstream(e) => write!(f, "{}", e),
        }
    }
}

impl From<anofox_forecast::ForecastError> for PanelForecastError {
    fn from(e: anofox_forecast::ForecastError) -> Self {
        PanelForecastError::Upstream(e)
    }
}

/// Inner logic for panel forecasting — testable without FFI pointer marshalling.
///
/// # Arguments
/// - `flat`: flat packed matrix `[n_series * series_len]`; NaN marks gaps.
/// - `n_series`: number of series in the panel.
/// - `series_len`: number of time steps per series (all equal after alignment).
/// - `method`: model method string: "GlobalETS", "GlobalTheta", or "GlobalCroston".
/// - `horizon`: forecast horizon.
/// - `seasonal_period`: seasonal period (0 = non-seasonal Reduced pool for GlobalETS;
///    ignored for GlobalTheta and GlobalCroston).
/// - `model_pool_str`: GlobalETS model pool override (D-07, case-insensitive):
///    `None`/`""`/`"reduced"` → Reduced (documented default); `"complete"` →
///    Complete; anything else is `InvalidParameter` — never a silent default.
/// - `variant_str`: GlobalCroston variant (D-07, case-insensitive):
///    `None`/`""`/`"classic"` → Classic; `"sba"` → SBA; anything else is
///    `InvalidParameter` — never a silent default.
///
/// # Returns
/// `Vec<Vec<f64>>` of shape `[n_series][horizon]`, all finite values.
pub(crate) fn forecast_panel_impl(
    flat: &[f64],
    n_series: usize,
    series_len: usize,
    method: &str,
    horizon: usize,
    seasonal_period: usize,
    model_pool_str: Option<&str>,
    variant_str: Option<&str>,
) -> std::result::Result<Vec<Vec<f64>>, PanelForecastError> {
    // Chunk flat array into per-series slices, convert NaN to None, then
    // impute with linear interpolation (fill_nulls_interpolate handles
    // leading/trailing/interior gaps).
    let panel: Vec<Vec<f64>> = (0..n_series)
        .map(|i| {
            let slice = &flat[i * series_len..(i + 1) * series_len];
            let with_opts: Vec<Option<f64>> = slice
                .iter()
                .map(|&v| if v.is_nan() { None } else { Some(v) })
                .collect();
            fill_nulls_interpolate(&with_opts)
        })
        .collect();

    match method {
        "GlobalETS" => {
            let pool = match model_pool_str.map(|s| s.trim().to_lowercase()) {
                None => ModelPool::Reduced,
                Some(ref s) if s.is_empty() || s == "reduced" => ModelPool::Reduced,
                Some(ref s) if s == "complete" => ModelPool::Complete,
                Some(_) => {
                    return Err(PanelForecastError::InvalidParameter(format!(
                        "unknown model_pool '{}'; expected 'reduced' or 'complete'",
                        model_pool_str.unwrap_or("")
                    )))
                }
            };
            // period=0 means "non-seasonal"; map to 1 so the ETS update loop
            // (`t % period`) never divides by zero. With period=1, has_seasonal
            // evaluates to false and only non-seasonal candidates are tried.
            let safe_period = if seasonal_period == 0 {
                1
            } else {
                seasonal_period
            };
            let mut model = GlobalAutoETS::new(safe_period, pool);
            model.fit(&panel)?;
            Ok(model.predict(horizon))
        }
        "GlobalTheta" => {
            // GlobalTheta requires no seasonal period — pooled Theta fits a shared
            // smoothing parameter across all series, with per-series level and slope.
            let mut model = GlobalTheta::new();
            model.fit(&panel)?;
            Ok(model.predict(horizon))
        }
        "GlobalCroston" => {
            // Select Croston variant from the params MAP key `croston_variant`
            // (D-07, case-insensitive, strict). "sba" → Syntetos-Boylan
            // Approximation (multiplies forecast by 1 - α/2). Default (None
            // or "classic") → Classic Croston. Use the named constructors
            // (new/sba) rather than with_variant, since
            // global_croston::CrostonVariant is not re-exported at the
            // module level.
            let mut model = match variant_str.map(|s| s.trim().to_lowercase()) {
                None => GlobalCroston::new(),
                Some(ref s) if s.is_empty() || s == "classic" => GlobalCroston::new(),
                Some(ref s) if s == "sba" => GlobalCroston::sba(),
                Some(_) => {
                    return Err(PanelForecastError::InvalidParameter(format!(
                        "unknown croston_variant '{}'; expected 'classic' or 'sba'",
                        variant_str.unwrap_or("")
                    )))
                }
            };
            model.fit(&panel)?;
            Ok(model.predict(horizon))
        }
        other => Err(PanelForecastError::InvalidModel(format!(
            "Unknown panel method: {}. Supported: GlobalETS, GlobalTheta, GlobalCroston",
            other
        ))),
    }
}

/// Forecast a panel of equal-length time series using a single cross-series global model.
///
/// `values` is a flat packed matrix in series-major order:
/// `values[s * series_len + t]` is the value of series `s` at time step `t`.
/// `NaN` values in the flat matrix are treated as missing and will be imputed
/// by `fill_nulls_interpolate` inside the Rust body before fitting.
///
/// On success writes to `*out_result` and returns `true`.
/// On failure writes to `*out_error` and returns `false`.
///
/// The `forecasts` buffer in `*out_result` must be freed by calling
/// `anofox_free_panel_forecast_result`.
///
/// # Safety
/// - `values`, `method`, and `out_result` must be non-null.
/// - `out_error` may be null (errors are still reported via `false` return).
/// - `variant` may be null (GlobalCroston "SBA" variant; None/empty = Classic).
/// - `model_pool` may be null (GlobalETS pool override; None/empty = Reduced, "Complete" = full pool).
/// - `values` must point to a buffer of at least `n_series * series_len` doubles.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_forecast_panel(
    values: *const c_double,
    n_series: size_t,
    series_len: size_t,
    method: *const c_char,
    horizon: size_t,
    seasonal_period: size_t,
    variant: *const c_char,
    model_pool: *const c_char,
    out_result: *mut PanelForecastResult,
    out_error: *mut AnofoxError,
) -> bool {
    // Initialise error slot first
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    // Null-check mandatory pointers
    if values.is_null() || method.is_null() || out_result.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        // Parse method string
        let method_str = CStr::from_ptr(method).to_str().unwrap_or("");

        // Parse optional croston_variant (D-07, strict): invalid UTF-8 is an
        // error, never silently swallowed to None (forecast_panel_impl does
        // the case-insensitive "classic"/"sba" validation).
        let variant_str: Option<&str> = if variant.is_null() {
            None
        } else {
            let s = CStr::from_ptr(variant).to_str().map_err(|_| {
                PanelForecastError::InvalidParameter("croston_variant: invalid UTF-8".to_string())
            })?;
            if s.is_empty() {
                None
            } else {
                Some(s)
            }
        };

        // Parse optional model_pool (D-07, strict): invalid UTF-8 is an
        // error, never silently swallowed to None (forecast_panel_impl does
        // the case-insensitive "reduced"/"complete" validation).
        let model_pool_str: Option<&str> = if model_pool.is_null() {
            None
        } else {
            let s = CStr::from_ptr(model_pool).to_str().map_err(|_| {
                PanelForecastError::InvalidParameter("model_pool: invalid UTF-8".to_string())
            })?;
            if s.is_empty() {
                None
            } else {
                Some(s)
            }
        };

        // Slice the flat matrix — Rust owns nothing, just a view
        let len = n_series.checked_mul(series_len).ok_or_else(|| {
            PanelForecastError::InvalidModel(
                "Panel dimensions overflow (n_series * series_len > usize::MAX)".into(),
            )
        })?;
        let flat = std::slice::from_raw_parts(values, len);

        // Inner logic (testable separately)
        forecast_panel_impl(
            flat,
            n_series,
            series_len,
            method_str,
            horizon,
            seasonal_period,
            model_pool_str,
            variant_str,
        )
        .map(|preds| (preds, method_str.to_owned()))
    }));

    match result {
        Ok(Ok((preds, method_name))) => {
            // Validate output dimensions before allocating.
            if horizon == 0 {
                if !out_error.is_null() {
                    (*out_error).set_error(ErrorCode::InvalidInput, "horizon must be > 0");
                }
                return false;
            }
            let total = match n_series.checked_mul(horizon) {
                Some(t) => t,
                None => {
                    if !out_error.is_null() {
                        (*out_error).set_error(
                            ErrorCode::InvalidInput,
                            "Panel output dimensions overflow (n_series * horizon > usize::MAX)",
                        );
                    }
                    return false;
                }
            };
            // Allocate flat output buffer and copy predictions
            let buf = if total > 0 {
                let raw = alloc_double_array(total);
                if raw.is_null() {
                    if !out_error.is_null() {
                        (*out_error).set_error(
                            ErrorCode::AllocationError,
                            "Failed to allocate panel forecast buffer",
                        );
                    }
                    return false;
                }
                for (s, series_preds) in preds.iter().enumerate() {
                    for (h, &v) in series_preds.iter().enumerate() {
                        *raw.add(s * horizon + h) = v;
                    }
                }
                raw
            } else {
                std::ptr::null_mut()
            };

            // Populate result struct
            (*out_result).forecasts = buf;
            (*out_result).n_series = n_series;
            (*out_result).n_horizon = horizon;

            // Write null-padded model name from the actual method string
            // (not hardcoded) so the emitted model_name column reflects the method used.
            let name_bytes = method_name.as_bytes();
            let dest = &mut (*out_result).model_name;
            for b in dest.iter_mut() {
                *b = 0;
            }
            let copy_len = name_bytes.len().min(63);
            for (i, &b) in name_bytes[..copy_len].iter().enumerate() {
                dest[i] = b as c_char;
            }

            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                // Map PanelForecastError variant to ErrorCode
                let code = match &e {
                    PanelForecastError::InvalidModel(_) => ErrorCode::InvalidModel,
                    PanelForecastError::InvalidParameter(_) => ErrorCode::InvalidInput,
                    PanelForecastError::Upstream(
                        anofox_forecast::ForecastError::InsufficientData { .. },
                    ) => ErrorCode::InsufficientData,
                    _ => ErrorCode::ComputationError,
                };
                (*out_error).set_error(code, &e.to_string());
            }
            false
        }
        Err(_) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::PanicCaught, "Panic in anofox_ts_forecast_panel");
            }
            false
        }
    }
}

/// Free a `PanelForecastResult` allocated by `anofox_ts_forecast_panel`.
///
/// Nulls the `forecasts` pointer after freeing to prevent double-free.
///
/// # Safety
/// `result` must be null or a valid pointer to a `PanelForecastResult` whose
/// `forecasts` field was set by `anofox_ts_forecast_panel`.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_panel_forecast_result(result: *mut PanelForecastResult) {
    if result.is_null() {
        return;
    }
    let r = &mut *result;
    if !r.forecasts.is_null() {
        anofox_free_double_array(r.forecasts);
        r.forecasts = ptr::null_mut();
    }
}

// ============================================================================
// Panel FFI tests (Task 1)
// ============================================================================

// Shared std+serde_json-only fixture loader, reached via a relative
// #[path] below from CARGO_MANIFEST_DIR (this crate's own directory) —
// the same loader file anofox-fcst-core itself #[path]s in from its own
// crate root, not a duplicate copy (09-03; see 09-RESEARCH.md Open
// Question 2). repo_root() inside it resolves identically from either
// crate, since both sit two directories below the workspace root.
#[cfg(test)]
#[path = "../../anofox-fcst-core/tests/common/mod.rs"]
mod test_fixtures;

#[cfg(test)]
mod panel_ffi_tests {
    use super::*;

    /// Helper: call `forecast_panel_impl` with a flat matrix built from `series` rows.
    fn flat_from_series(series: &[&[f64]]) -> Vec<f64> {
        let n = series[0].len();
        let mut flat = Vec::with_capacity(series.len() * n);
        for s in series {
            assert_eq!(s.len(), n, "all series must have the same length");
            flat.extend_from_slice(s);
        }
        flat
    }

    /// Test 1 — happy path: 3-series equal-length panel, GlobalETS, horizon=4.
    #[test]
    fn test_happy_path_global_ets() {
        let s1: Vec<f64> = (1..=12).map(|x| x as f64).collect();
        let s2: Vec<f64> = (2..=13).map(|x| x as f64 * 1.5).collect();
        let s3: Vec<f64> = (3..=14).map(|x| x as f64 * 0.8).collect();
        let series: &[&[f64]] = &[&s1, &s2, &s3];
        let flat = flat_from_series(series);

        let result = forecast_panel_impl(&flat, 3, 12, "GlobalETS", 4, 0, None, None);
        assert!(result.is_ok(), "expected Ok, got: {:?}", result.err());
        let preds = result.unwrap();
        assert_eq!(preds.len(), 3, "expected 3 series forecasts");
        // WR-03 (Phase 18 review): the former (0.0..=30.0)/(0.0..=40.0)/
        // (0.0..=25.0) bounds were 2-3x the true per-series range and would
        // pass a systematically-too-high/low forecast. Closed form: each
        // series is perfectly linear (s1: 1..12 step 1, s2: 3..19.5 step
        // 1.5, s3: 2.4..11.2 step 0.8). Bound = [first_value, last_value +
        // span] per series, where span = last-first -- the lower bound rules
        // out a forecast that regresses below the series' own historical
        // minimum, the upper bound allows one more "span" of growth beyond
        // the last observed point (headroom for GlobalETS pooling shared
        // parameters across the panel, confirmed live: observed
        // [[12.77..14.99], [20.65..23.98], [11.82..13.59]] for s1/s2/s3).
        let bounds = [(1.0, 23.0), (3.0, 36.0), (2.4, 20.0)];
        for (i, series_preds) in preds.iter().enumerate() {
            assert_eq!(series_preds.len(), 4, "expected horizon=4 for series {}", i);
            let (lo, hi) = bounds[i];
            for &v in series_preds {
                assert!(
                    v.is_finite(),
                    "expected finite forecast, got NaN/Inf for series {}",
                    i
                );
                assert!(
                    (lo..=hi).contains(&v),
                    "series {i} forecast {v} out of sane range [{lo}, {hi}]"
                );
            }
        }
    }

    /// Test 2 — NaN imputation: series with interior gap is densified before fit.
    #[test]
    fn test_nan_imputation() {
        let mut s1: Vec<f64> = (1..=12).map(|x| x as f64).collect();
        s1[5] = f64::NAN; // interior gap
        let s2: Vec<f64> = (2..=13).map(|x| x as f64 * 1.5).collect();
        let s3: Vec<f64> = (3..=14).map(|x| x as f64 * 0.8).collect();
        let series: &[&[f64]] = &[&s1, &s2, &s3];
        let flat = flat_from_series(series);

        let result = forecast_panel_impl(&flat, 3, 12, "GlobalETS", 4, 0, None, None);
        assert!(
            result.is_ok(),
            "expected Ok after NaN imputation, got: {:?}",
            result.err()
        );
        let preds = result.unwrap();
        assert_eq!(preds.len(), 3);
        // Value check: same tightened per-series sane magnitude bounds as
        // test_happy_path_global_ets (the s1 interior NaN gap is imputed,
        // so the forecast should still track s1's own ~1..12 range). See
        // WR-03 comment above for the closed-form derivation.
        let bounds = [(1.0, 23.0), (3.0, 36.0), (2.4, 20.0)];
        for (i, series_preds) in preds.iter().enumerate() {
            assert_eq!(series_preds.len(), 4);
            let (lo, hi) = bounds[i];
            for &v in series_preds {
                assert!(v.is_finite(), "expected no NaN after imputation");
                assert!(
                    (lo..=hi).contains(&v),
                    "series {i} forecast {v} out of sane range [{lo}, {hi}]"
                );
            }
        }
    }

    /// Test 3 — unknown method returns InvalidModel error.
    #[test]
    fn test_unknown_method_returns_error() {
        let s1: Vec<f64> = (1..=12).map(|x| x as f64).collect();
        let s2: Vec<f64> = (2..=13).map(|x| x as f64 * 1.5).collect();
        let s3: Vec<f64> = (3..=14).map(|x| x as f64 * 0.8).collect();
        let series: &[&[f64]] = &[&s1, &s2, &s3];
        let flat = flat_from_series(series);

        let result = forecast_panel_impl(&flat, 3, 12, "Nope", 4, 0, None, None);
        assert!(result.is_err(), "expected error for unknown method");
        let err = result.unwrap_err();
        match &err {
            PanelForecastError::InvalidModel(msg) => {
                assert!(msg.contains("Nope"), "error should mention the model name");
            }
            other => panic!("expected InvalidModel, got: {:?}", other),
        }
    }

    /// Test 4 — GlobalTheta: 3-series equal-length panel, horizon=4, all finite.
    #[test]
    fn test_global_theta_happy_path() {
        let s1: Vec<f64> = (1..=12).map(|x| x as f64).collect();
        let s2: Vec<f64> = (2..=13).map(|x| x as f64 * 1.5).collect();
        let s3: Vec<f64> = (3..=14).map(|x| x as f64 * 0.8).collect();
        let series: &[&[f64]] = &[&s1, &s2, &s3];
        let flat = flat_from_series(series);

        let result = forecast_panel_impl(&flat, 3, 12, "GlobalTheta", 4, 0, None, None);
        assert!(
            result.is_ok(),
            "GlobalTheta expected Ok, got: {:?}",
            result.err()
        );
        let preds = result.unwrap();
        assert_eq!(preds.len(), 3, "expected 3 series forecasts");
        // Value check: same tightened per-series sane magnitude bounds as
        // the GlobalETS happy-path test above. See WR-03 comment there for
        // the closed-form derivation.
        let bounds = [(1.0, 23.0), (3.0, 36.0), (2.4, 20.0)];
        for (i, series_preds) in preds.iter().enumerate() {
            assert_eq!(series_preds.len(), 4, "expected horizon=4 for series {}", i);
            let (lo, hi) = bounds[i];
            for &v in series_preds {
                assert!(
                    v.is_finite(),
                    "expected finite Theta forecast for series {}",
                    i
                );
                assert!(
                    (lo..=hi).contains(&v),
                    "series {i} forecast {v} out of sane range [{lo}, {hi}]"
                );
            }
        }
    }

    /// Test 5 — GlobalCroston Classic: 3-series intermittent panel, horizon=4.
    /// Values should be non-negative and flat per series (Croston is constant forecast).
    #[test]
    fn test_global_croston_classic() {
        // Intermittent panel: mostly zeros, ≥2 demands in each series
        let s1: Vec<f64> = vec![0.0, 3.0, 0.0, 0.0, 5.0, 0.0, 0.0, 0.0, 2.0, 0.0, 0.0, 4.0];
        let s2: Vec<f64> = vec![1.0, 0.0, 0.0, 2.0, 0.0, 0.0, 3.0, 0.0, 0.0, 1.0, 0.0, 0.0];
        let s3: Vec<f64> = vec![0.0, 0.0, 5.0, 0.0, 0.0, 2.0, 0.0, 0.0, 4.0, 0.0, 0.0, 3.0];
        let series: &[&[f64]] = &[&s1, &s2, &s3];
        let flat = flat_from_series(series);

        let result = forecast_panel_impl(&flat, 3, 12, "GlobalCroston", 4, 0, None, None);
        assert!(
            result.is_ok(),
            "GlobalCroston Classic expected Ok, got: {:?}",
            result.err()
        );
        let preds = result.unwrap();
        assert_eq!(preds.len(), 3, "expected 3 series forecasts");
        for (i, series_preds) in preds.iter().enumerate() {
            assert_eq!(series_preds.len(), 4, "expected horizon=4 for series {}", i);
            for &v in series_preds {
                assert!(
                    v.is_finite(),
                    "expected finite Croston Classic forecast for series {}",
                    i
                );
                assert!(
                    v >= 0.0,
                    "Croston forecasts must be non-negative, got {} for series {}",
                    v,
                    i
                );
            }
            // Croston is a flat forecast: all horizon steps should be equal
            let first = series_preds[0];
            for &v in &series_preds[1..] {
                assert!(
                    (v - first).abs() < 1e-10,
                    "Croston Classic should be flat (constant) per series {}: {} != {}",
                    i,
                    v,
                    first
                );
            }
        }
    }

    /// Test 6 — GlobalCroston SBA: same panel, SBA forecast ≤ Classic (bias correction).
    #[test]
    fn test_global_croston_sba_le_classic() {
        let s1: Vec<f64> = vec![0.0, 3.0, 0.0, 0.0, 5.0, 0.0, 0.0, 0.0, 2.0, 0.0, 0.0, 4.0];
        let s2: Vec<f64> = vec![1.0, 0.0, 0.0, 2.0, 0.0, 0.0, 3.0, 0.0, 0.0, 1.0, 0.0, 0.0];
        let s3: Vec<f64> = vec![0.0, 0.0, 5.0, 0.0, 0.0, 2.0, 0.0, 0.0, 4.0, 0.0, 0.0, 3.0];
        let series: &[&[f64]] = &[&s1, &s2, &s3];
        let flat = flat_from_series(series);

        let classic_result = forecast_panel_impl(&flat, 3, 12, "GlobalCroston", 4, 0, None, None);
        let sba_result =
            forecast_panel_impl(&flat, 3, 12, "GlobalCroston", 4, 0, None, Some("SBA"));

        assert!(
            classic_result.is_ok(),
            "Classic expected Ok, got: {:?}",
            classic_result.err()
        );
        assert!(
            sba_result.is_ok(),
            "SBA expected Ok, got: {:?}",
            sba_result.err()
        );

        let classic_preds = classic_result.unwrap();
        let sba_preds = sba_result.unwrap();
        assert_eq!(classic_preds.len(), 3);
        assert_eq!(sba_preds.len(), 3);

        for i in 0..3 {
            let c = classic_preds[i][0]; // flat forecast, just check first step
            let s = sba_preds[i][0];
            assert!(
                s.is_finite(),
                "SBA forecast must be finite for series {}",
                i
            );
            assert!(
                s >= 0.0,
                "SBA forecasts must be non-negative, got {} for series {}",
                s,
                i
            );
            // SBA multiplies by (1 - alpha/2) so SBA ≤ Classic
            assert!(
                s <= c + 1e-10,
                "SBA forecast {} should be ≤ Classic {} for series {}",
                s,
                c,
                i
            );
        }
    }

    /// D-07: croston_variant is case-insensitive and strict — "sba"/"SBA"
    /// both select SBA, "classic"/"" select Classic, and a misspelling
    /// ("sab") is a hard InvalidParameter — never a silent Classic default.
    #[test]
    fn croston_variant_case_insensitive_and_strict() {
        let s1: Vec<f64> = vec![0.0, 3.0, 0.0, 0.0, 5.0, 0.0, 0.0, 0.0, 2.0, 0.0, 0.0, 4.0];
        let s2: Vec<f64> = vec![1.0, 0.0, 0.0, 2.0, 0.0, 0.0, 3.0, 0.0, 0.0, 1.0, 0.0, 0.0];
        let s3: Vec<f64> = vec![0.0, 0.0, 5.0, 0.0, 0.0, 2.0, 0.0, 0.0, 4.0, 0.0, 0.0, 3.0];
        let series: &[&[f64]] = &[&s1, &s2, &s3];
        let flat = flat_from_series(series);

        let upper = forecast_panel_impl(&flat, 3, 12, "GlobalCroston", 4, 0, None, Some("SBA"));
        let lower = forecast_panel_impl(&flat, 3, 12, "GlobalCroston", 4, 0, None, Some("sba"));
        assert!(upper.is_ok(), "SBA should succeed: {:?}", upper.err());
        assert!(lower.is_ok(), "sba should succeed: {:?}", lower.err());
        assert_eq!(
            upper.unwrap(),
            lower.unwrap(),
            "case-insensitive SBA/sba must produce identical forecasts"
        );

        let classic_empty = forecast_panel_impl(&flat, 3, 12, "GlobalCroston", 4, 0, None, None);
        let classic_named =
            forecast_panel_impl(&flat, 3, 12, "GlobalCroston", 4, 0, None, Some("classic"));
        assert!(classic_empty.is_ok());
        assert!(classic_named.is_ok());
        assert_eq!(classic_empty.unwrap(), classic_named.unwrap());

        let misspelled =
            forecast_panel_impl(&flat, 3, 12, "GlobalCroston", 4, 0, None, Some("sab"));
        assert!(
            misspelled.is_err(),
            "misspelled croston_variant must be a hard error, not a silent Classic default"
        );
        match misspelled.unwrap_err() {
            PanelForecastError::InvalidParameter(msg) => {
                assert!(
                    msg.contains("croston_variant") && msg.contains("sab"),
                    "error should name croston_variant and the bad value, got: {msg}"
                );
            }
            other => panic!("expected InvalidParameter, got: {:?}", other),
        }
    }

    /// D-07: GlobalETS model_pool is case-insensitive and strict —
    /// "complete"/"Complete" both accepted, and a misspelling ("compete")
    /// is a hard InvalidParameter — never a silent Reduced default.
    #[test]
    fn model_pool_case_insensitive_and_strict() {
        let s1: Vec<f64> = (1..=12).map(|x| x as f64).collect();
        let s2: Vec<f64> = (2..=13).map(|x| x as f64 * 1.5).collect();
        let s3: Vec<f64> = (3..=14).map(|x| x as f64 * 0.8).collect();
        let series: &[&[f64]] = &[&s1, &s2, &s3];
        let flat = flat_from_series(series);

        let title_case =
            forecast_panel_impl(&flat, 3, 12, "GlobalETS", 4, 0, Some("Complete"), None);
        let lower_case =
            forecast_panel_impl(&flat, 3, 12, "GlobalETS", 4, 0, Some("complete"), None);
        assert!(
            title_case.is_ok(),
            "Complete should succeed: {:?}",
            title_case.err()
        );
        assert!(
            lower_case.is_ok(),
            "complete should succeed: {:?}",
            lower_case.err()
        );

        let misspelled =
            forecast_panel_impl(&flat, 3, 12, "GlobalETS", 4, 0, Some("compete"), None);
        assert!(
            misspelled.is_err(),
            "misspelled model_pool must be a hard error, not a silent Reduced default"
        );
        match misspelled.unwrap_err() {
            PanelForecastError::InvalidParameter(msg) => {
                assert!(
                    msg.contains("model_pool") && msg.contains("compete"),
                    "error should name model_pool and the bad value, got: {msg}"
                );
            }
            other => panic!("expected InvalidParameter, got: {:?}", other),
        }
    }

    /// D-07, FFI boundary: a misspelled croston_variant reaching the real
    /// `anofox_ts_forecast_panel` entry point returns `false` with
    /// `ErrorCode::InvalidInput` — proves the strict parsing is wired
    /// through the C string marshalling, not just the testable inner fn.
    #[test]
    fn ffi_croston_variant_misspelled_returns_invalid_input() {
        use std::ffi::CString;

        let s1: Vec<f64> = vec![0.0, 3.0, 0.0, 0.0, 5.0, 0.0, 0.0, 0.0, 2.0, 0.0, 0.0, 4.0];
        let s2: Vec<f64> = vec![1.0, 0.0, 0.0, 2.0, 0.0, 0.0, 3.0, 0.0, 0.0, 1.0, 0.0, 0.0];
        let s3: Vec<f64> = vec![0.0, 0.0, 5.0, 0.0, 0.0, 2.0, 0.0, 0.0, 4.0, 0.0, 0.0, 3.0];
        let series: &[&[f64]] = &[&s1, &s2, &s3];
        let flat = flat_from_series(series);

        let method = CString::new("GlobalCroston").unwrap();
        let variant = CString::new("sab").unwrap();

        let mut out_result = PanelForecastResult::default();
        let mut out_error = AnofoxError::success();

        let ok = unsafe {
            anofox_ts_forecast_panel(
                flat.as_ptr(),
                3,
                12,
                method.as_ptr(),
                4,
                0,
                variant.as_ptr(),
                std::ptr::null(),
                &mut out_result as *mut PanelForecastResult,
                &mut out_error as *mut AnofoxError,
            )
        };

        assert!(!ok, "misspelled croston_variant must return false");
        assert_eq!(out_error.code, ErrorCode::InvalidInput);
        let msg = unsafe { CStr::from_ptr(out_error.message.as_ptr()) }
            .to_str()
            .unwrap_or("");
        assert!(
            msg.contains("croston_variant"),
            "error message should name croston_variant, got: {msg}"
        );
    }

    /// DEP-02 proof point: every (case, method) in the panel_nan_guard
    /// fixture gets exactly its fixture outcome — "finite" => Ok with
    /// n_series rows of horizon all-finite values, "error" => Err. An
    /// all-missing series reaches the model as all-NaN (fill_nulls_interpolate
    /// leaves an all-None series NaN — see imputation.rs) and +inf is never
    /// imputed, so the anofox-forecast >=0.15.9 global-model guards must
    /// reject both rather than silently returning a zero/NaN forecast.
    #[test]
    fn panel_nan_guard_fixture_outcomes() {
        let fx = crate::test_fixtures::load_fixture("panel_nan_guard/cases.json");
        let horizon = crate::test_fixtures::num(&fx["parameters"]["horizon"]) as usize;
        let seasonal_period =
            crate::test_fixtures::num(&fx["parameters"]["seasonal_period"]) as usize;

        let cases = fx["cases"]
            .as_object()
            .expect("panel_nan_guard/cases.json: 'cases' is not an object");

        let mut mismatches: Vec<String> = Vec::new();

        for (case_name, case) in cases {
            let n_series = crate::test_fixtures::num(&case["n_series"]) as usize;
            let series_len = crate::test_fixtures::num(&case["series_len"]) as usize;

            let rows = case["values"]
                .as_array()
                .unwrap_or_else(|| panic!("{case_name}: 'values' is not an array"));
            let mut flat: Vec<f64> = Vec::with_capacity(n_series * series_len);
            for row in rows {
                let row_vals = crate::test_fixtures::f64_vec(row);
                assert_eq!(
                    row_vals.len(),
                    series_len,
                    "{case_name}: series row has length {}, want series_len={series_len}",
                    row_vals.len()
                );
                flat.extend(row_vals);
            }
            assert_eq!(
                flat.len(),
                n_series * series_len,
                "{case_name}: flattened length {} != n_series*series_len={}",
                flat.len(),
                n_series * series_len
            );

            let expect = case["expect"]
                .as_object()
                .unwrap_or_else(|| panic!("{case_name}: 'expect' is not an object"));

            for (method, outcome) in expect {
                let want_outcome = outcome.as_str().unwrap_or_else(|| {
                    panic!("{case_name}.{method}: 'expect' value is not a string")
                });

                let result = forecast_panel_impl(
                    &flat,
                    n_series,
                    series_len,
                    method,
                    horizon,
                    seasonal_period,
                    None,
                    None,
                );

                match (want_outcome, &result) {
                    ("finite", Ok(preds)) => {
                        if preds.len() != n_series {
                            mismatches.push(format!(
                                "{case_name}.{method}: expected {n_series} series rows, got {}",
                                preds.len()
                            ));
                            continue;
                        }
                        for (s, row) in preds.iter().enumerate() {
                            if row.len() != horizon {
                                mismatches.push(format!(
                                    "{case_name}.{method}: series {s} has {} values, want horizon={horizon}",
                                    row.len()
                                ));
                                continue;
                            }
                            if let Some((h, v)) =
                                row.iter().enumerate().find(|(_, v)| !v.is_finite())
                            {
                                mismatches.push(format!(
                                    "{case_name}.{method}: series {s} step {h} is non-finite: {v}"
                                ));
                            }
                        }
                    }
                    ("finite", Err(e)) => {
                        mismatches.push(format!(
                            "{case_name}.{method}: expected finite, got Err: {e}"
                        ));
                    }
                    ("error", Ok(preds)) => {
                        mismatches.push(format!(
                            "{case_name}.{method}: expected error, got Ok: {preds:?}"
                        ));
                    }
                    ("error", Err(_)) => {}
                    (other, _) => {
                        mismatches.push(format!(
                            "{case_name}.{method}: unexpected expect value {other:?}"
                        ));
                    }
                }
            }
        }

        assert!(
            mismatches.is_empty(),
            "panel_nan_guard_fixture_outcomes: {} mismatch(es):\n{}",
            mismatches.len(),
            mismatches.join("\n")
        );
    }
}

// ============================================================================
// Phase 3: Kalman + GARCH FFI tests
// ============================================================================

#[cfg(test)]
mod kalman_garch_ffi_tests {
    use super::*;
    use std::ffi::CStr;

    /// Helper: call anofox_ts_forecast with a model name and opts override function.
    fn call_ts_forecast<F>(
        values: &[f64],
        horizon: usize,
        model_name: &[u8],
        configure_opts: F,
    ) -> Result<Vec<f64>, String>
    where
        F: FnOnce(&mut ForecastOptions),
    {
        // Build validity bitmap: all valid (1-bit per observation)
        let n_words = (values.len() + 63) / 64;
        let mut validity: Vec<u64> = vec![!0u64; n_words];
        // Pad last word for exact count
        if values.len() % 64 != 0 {
            let remainder = values.len() % 64;
            validity[n_words - 1] = (1u64 << remainder) - 1;
        }

        let mut opts = ForecastOptions::default();
        // Set model string
        let name_bytes = model_name;
        let copy_len = name_bytes.len().min(31);
        for (i, &b) in name_bytes[..copy_len].iter().enumerate() {
            opts.model[i] = b as i8;
        }
        opts.model[copy_len] = 0;
        opts.horizon = horizon as i32;
        configure_opts(&mut opts);

        let mut result = ForecastResult::default();
        let mut error = AnofoxError::default();

        let ok = unsafe {
            anofox_ts_forecast(
                values.as_ptr(),
                validity.as_ptr(),
                values.len(),
                &opts as *const ForecastOptions,
                &mut result as *mut ForecastResult,
                &mut error as *mut AnofoxError,
            )
        };

        if ok {
            let n = result.n_forecasts;
            let point = unsafe { std::slice::from_raw_parts(result.point_forecasts, n).to_vec() };
            // Free the result
            unsafe {
                anofox_free_forecast_result(&mut result as *mut ForecastResult);
            }
            Ok(point)
        } else {
            let msg = unsafe {
                CStr::from_ptr(error.message.as_ptr())
                    .to_string_lossy()
                    .into_owned()
            };
            Err(msg)
        }
    }

    #[test]
    fn test_ffi_kalman_local_level_returns_horizon_rows() {
        let values: Vec<f64> = (0..25).map(|i| 10.0 + i as f64 * 0.5).collect();
        let result = call_ts_forecast(&values, 5, b"Kalman", |_opts| {
            // kalman_model stays empty => local_level (default)
        });
        assert!(
            result.is_ok(),
            "Kalman FFI should succeed: {:?}",
            result.err()
        );
        let point = result.unwrap();
        assert_eq!(point.len(), 5, "Kalman FFI must return 5 point forecasts");
        // WR-03 (Phase 18 review): the former (0.0..=40.0) bound was nearly
        // double the true 10..22 input range. Closed form: input is
        // perfectly linear (value[i] = 10+0.5i, i=0..24), last observed =
        // 22, span = 12. A local-level Kalman filter has no trend
        // component, so it should persist near the trailing filtered level
        // rather than continue the trend -- confirmed live: observed flat
        // forecast of 20.65 across all 5 horizon steps. Bound at
        // last_observed +/- span/2 (15.0..=25.0 comfortably contains 20.65
        // while still catching a trend-continuation regression, which would
        // push forecasts up toward/past 24, or a magnitude blowup/sign
        // flip).
        for &v in &point {
            assert!(v.is_finite(), "Kalman FFI forecast must be finite");
            assert!(
                (15.0..=25.0).contains(&v),
                "Kalman forecast {v} out of sane range"
            );
        }
    }

    #[test]
    fn test_ffi_kalman_local_linear_trend_differs_from_local_level() {
        let values: Vec<f64> = (0..30).map(|i| 5.0 + i as f64 * 1.2).collect();

        let result_ll = call_ts_forecast(&values, 5, b"Kalman", |_opts| {
            // local_level default
        });
        let result_llt = call_ts_forecast(&values, 5, b"Kalman", |opts| {
            let spec = b"local_linear_trend\0";
            for (i, &b) in spec.iter().enumerate() {
                opts.kalman_model[i] = b as i8;
            }
        });

        assert!(
            result_ll.is_ok(),
            "Kalman local_level FFI: {:?}",
            result_ll.err()
        );
        assert!(
            result_llt.is_ok(),
            "Kalman local_linear_trend FFI: {:?}",
            result_llt.err()
        );
        let ll = result_ll.unwrap();
        let llt = result_llt.unwrap();
        assert_eq!(ll.len(), 5);
        assert_eq!(llt.len(), 5);
        // On a trended series the two specs should produce different forecasts
        assert_ne!(
            ll[0], llt[0],
            "local_level and local_linear_trend must produce different forecasts"
        );
    }
}

// ============================================================================
// Strict FFI option-string parsing (D-07/T-10-07, plan 10-02 Task 2)
// ============================================================================

#[cfg(test)]
mod strict_option_parsing_ffi_tests {
    use super::*;

    fn values() -> Vec<f64> {
        (0..30).map(|i| 10.0 + i as f64 * 0.5).collect()
    }

    fn call_raw(opts: &ForecastOptions) -> (bool, ErrorCode) {
        let data = values();
        let n_words = data.len().div_ceil(64);
        let validity: Vec<u64> = vec![u64::MAX; n_words];
        let mut result = ForecastResult::default();
        let mut error = AnofoxError::default();
        let ok = unsafe {
            anofox_ts_forecast(
                data.as_ptr(),
                validity.as_ptr(),
                data.len(),
                opts as *const ForecastOptions,
                &mut result as *mut ForecastResult,
                &mut error as *mut AnofoxError,
            )
        };
        if !ok {
            unsafe { anofox_free_forecast_result(&mut result as *mut ForecastResult) };
        }
        (ok, error.code)
    }

    #[test]
    fn invalid_utf8_model_name_is_invalid_input_not_auto_fallback() {
        let mut opts = ForecastOptions::default();
        // A lone 0xFF byte is never valid UTF-8 continuation/lead; followed
        // by a NUL terminator so c_field_str's bounded read still finds one.
        opts.model[0] = 0xFFu8 as i8;
        opts.model[1] = 0;
        opts.horizon = 5;

        let (ok, code) = call_raw(&opts);
        assert!(
            !ok,
            "invalid UTF-8 model name must fail, not silently fall back to \"auto\""
        );
        assert_eq!(code, ErrorCode::InvalidInput);
    }

    #[test]
    fn malformed_seasonal_periods_entry_is_invalid_input() {
        let mut opts = ForecastOptions::default();
        let model = b"MSTL\0";
        for (i, &b) in model.iter().enumerate() {
            opts.model[i] = b as i8;
        }
        let periods = b"[7, abc]\0";
        for (i, &b) in periods.iter().enumerate() {
            opts.seasonal_periods_str[i] = b as i8;
        }
        opts.horizon = 5;

        let (ok, code) = call_raw(&opts);
        assert!(
            !ok,
            "a non-numeric seasonal_periods entry must fail, not be silently dropped"
        );
        assert_eq!(code, ErrorCode::InvalidInput);
    }
}

// ============================================================================
// VAR Multivariate Forecasting (Phase 3 CLAS-03)
// ============================================================================

/// Inner logic for VAR multivariate forecasting — testable without FFI pointer marshalling.
///
/// # Arguments
/// - `flat`: flat packed matrix `[k_vars * series_len]` in variable-major order;
///   `flat[v * series_len + t]` is the value of variable `v` at time step `t`.
///   NaN values are imputed via linear interpolation before fitting.
/// - `k_vars`: number of variables K.
/// - `series_len`: number of observations per variable N (all equal).
/// - `order`: lag order p (0 is treated as 1).
/// - `horizon`: forecast horizon H.
///
/// # Returns
/// `Vec<Vec<f64>>` of shape `[k_vars][horizon]`.
pub(crate) fn forecast_var_impl(
    flat: &[f64],
    k_vars: usize,
    series_len: usize,
    order: usize,
    horizon: usize,
) -> std::result::Result<Vec<Vec<f64>>, String> {
    if k_vars == 0 || series_len == 0 {
        return Err("VAR: empty data (k_vars=0 or series_len=0)".into());
    }

    // Reconstruct K series from flat variable-major matrix, imputing NaN via interpolation.
    let data: Vec<Vec<f64>> = (0..k_vars)
        .map(|v| {
            let slice = &flat[v * series_len..(v + 1) * series_len];
            // Convert to Option<f64> for fill_nulls_interpolate
            let with_opts: Vec<Option<f64>> = slice
                .iter()
                .map(|&x| if x.is_nan() { None } else { Some(x) })
                .collect();
            fill_nulls_interpolate(&with_opts)
        })
        .collect();

    let safe_order = order.max(1);
    let mut model = anofox_forecast::models::var::VAR::new(safe_order);
    model
        .fit(&data)
        .map_err(|e| format!("VAR fit failed: {}", e))?;
    model
        .predict(horizon)
        .map_err(|e| format!("VAR predict failed: {}", e))
}

/// Forecast a multivariate time series using a VAR(p) model.
///
/// `flat_data` is a flat packed matrix in variable-major order:
/// `flat_data[v * series_len + t]` is the value of variable `v` at time step `t`.
/// NaN values in the flat matrix are treated as missing and imputed by
/// `fill_nulls_interpolate` before fitting.
///
/// On success writes to `*out_result` and returns `true`.
/// On failure writes to `*out_error` and returns `false`.
///
/// The `forecasts` buffer in `*out_result` must be freed by calling
/// `anofox_free_var_forecast_result`.
///
/// # Safety
/// - `flat_data` and `out_result` must be non-null.
/// - `out_error` may be null (errors are still reported via `false` return).
/// - `flat_data` must point to a buffer of at least `k_vars * series_len` doubles.
/// - `k_vars * series_len` must not overflow `usize`.
/// - `k_vars * horizon` must not overflow `usize`.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_forecast_var(
    flat_data: *const c_double, // flat [k_vars * series_len] matrix, variable-major; NaN = missing
    k_vars: size_t,
    series_len: size_t,
    order: size_t, // lag order p (0 → 1)
    horizon: size_t,
    out_result: *mut VARForecastResult,
    out_error: *mut AnofoxError,
) -> bool {
    // Initialise error slot first
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    // Null-check mandatory pointers
    if flat_data.is_null() || out_result.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(
                ErrorCode::NullPointer,
                "Null pointer argument to anofox_ts_forecast_var",
            );
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        // Checked multiplication: k_vars * series_len must not overflow usize
        let len = k_vars.checked_mul(series_len).ok_or_else(|| {
            "VAR dimensions overflow (k_vars * series_len > usize::MAX)".to_string()
        })?;

        // Slice the flat matrix — Rust owns nothing, just a view
        let flat = std::slice::from_raw_parts(flat_data, len);

        // Inner logic (testable separately)
        forecast_var_impl(flat, k_vars, series_len, order, horizon)
    }));

    match result {
        Ok(Ok(preds)) => {
            // Validate output dimensions before allocating
            if horizon == 0 {
                if !out_error.is_null() {
                    (*out_error).set_error(ErrorCode::InvalidInput, "horizon must be > 0");
                }
                return false;
            }
            // Checked multiplication: k_vars * horizon must not overflow usize
            let total = match k_vars.checked_mul(horizon) {
                Some(t) => t,
                None => {
                    if !out_error.is_null() {
                        (*out_error).set_error(
                            ErrorCode::InvalidInput,
                            "VAR output dimensions overflow (k_vars * horizon > usize::MAX)",
                        );
                    }
                    return false;
                }
            };

            // Allocate flat output buffer and copy predictions (variable-major order)
            let raw = alloc_double_array(total);
            if raw.is_null() && total > 0 {
                if !out_error.is_null() {
                    (*out_error).set_error(
                        ErrorCode::AllocationError,
                        "Failed to allocate VAR forecast buffer",
                    );
                }
                return false;
            }

            for (v, var_preds) in preds.iter().enumerate() {
                for (h, &val) in var_preds.iter().enumerate() {
                    *raw.add(v * horizon + h) = val;
                }
            }

            (*out_result).forecasts = raw;
            (*out_result).k_vars = k_vars;
            (*out_result).n_horizon = horizon;

            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::ComputationError, &e);
            }
            false
        }
        Err(_) => {
            if !out_error.is_null() {
                (*out_error).set_error(ErrorCode::PanicCaught, "Panic in anofox_ts_forecast_var");
            }
            false
        }
    }
}

/// Free a `VARForecastResult` allocated by `anofox_ts_forecast_var`.
///
/// Nulls the `forecasts` pointer after freeing to prevent double-free.
///
/// # Safety
/// `result` must be null or a valid pointer to a `VARForecastResult` whose
/// `forecasts` field was set by `anofox_ts_forecast_var`.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_var_forecast_result(result: *mut VARForecastResult) {
    if result.is_null() {
        return;
    }
    let r = &mut *result;
    if !r.forecasts.is_null() {
        // Use anofox_free_double_array (the public C fn) to match the alloc_double_array pairing
        anofox_free_double_array(r.forecasts);
        r.forecasts = ptr::null_mut();
    }
}

// ============================================================================
// VAR FFI tests (Task 1 — RED phase: these tests drive the implementation)
// ============================================================================

#[cfg(test)]
mod var_ffi_tests {
    use super::*;

    /// Helper: generate synthetic VAR(1) data as a flat variable-major matrix.
    /// Returns [k_vars * series_len] vector.
    fn generate_var1_flat(k_vars: usize, series_len: usize) -> Vec<f64> {
        let mut flat = vec![0.0f64; k_vars * series_len];
        // Simple VAR(1): each variable is a weighted sum of lagged values + noise
        let a = [[0.6f64, 0.1], [0.05, 0.7]];
        let c = [0.5f64, 0.3];
        // Initialise
        for v in 0..k_vars.min(2) {
            flat[v * series_len] = c[v];
        }
        for t in 1..series_len {
            for v in 0..k_vars.min(2) {
                let mut val = c[v];
                for u in 0..k_vars.min(2) {
                    val += a[v][u] * flat[u * series_len + (t - 1)];
                }
                // Small deterministic perturbation (no rng needed)
                val += 0.01 * (t as f64).sin();
                flat[v * series_len + t] = val;
            }
        }
        flat
    }

    /// Test 1 — happy path: forecast_var_impl on 2-variable VAR(1) data, order=1, horizon=5.
    #[test]
    fn test_var_impl_happy_path() {
        let k_vars = 2usize;
        let series_len = 50usize;
        let flat = generate_var1_flat(k_vars, series_len);

        let result = forecast_var_impl(&flat, k_vars, series_len, 1, 5);
        assert!(
            result.is_ok(),
            "forecast_var_impl should succeed: {:?}",
            result.err()
        );
        let preds = result.unwrap();
        assert_eq!(
            preds.len(),
            k_vars,
            "expected {} variable forecasts",
            k_vars
        );
        for (v, var_preds) in preds.iter().enumerate() {
            assert_eq!(var_preds.len(), 5, "expected horizon=5 for variable {}", v);
            for &val in var_preds {
                assert!(
                    val.is_finite(),
                    "expected finite forecast for variable {}, got NaN/Inf",
                    v
                );
            }
        }
    }

    /// Test 2 — empty data: forecast_var_impl on k_vars=0 or series_len=0 returns Err.
    #[test]
    fn test_var_impl_empty_returns_err() {
        let result_empty_k = forecast_var_impl(&[], 0, 0, 1, 5);
        assert!(result_empty_k.is_err(), "k_vars=0 should return Err");

        let result_zero_len = forecast_var_impl(&[1.0, 2.0], 2, 0, 1, 5);
        assert!(result_zero_len.is_err(), "series_len=0 should return Err");
    }

    /// Test 3 — FFI fill+free: anofox_ts_forecast_var with valid data fills out_result
    /// and returns true; then anofox_free_var_forecast_result nulls the forecasts pointer.
    #[test]
    fn test_ffi_var_fill_and_free() {
        let k_vars = 2usize;
        let series_len = 50usize;
        let flat = generate_var1_flat(k_vars, series_len);
        let horizon = 5usize;

        let mut out_result = VARForecastResult::default();
        let mut out_error = AnofoxError::success();

        let ok = unsafe {
            anofox_ts_forecast_var(
                flat.as_ptr(),
                k_vars,
                series_len,
                1, // order
                horizon,
                &mut out_result as *mut VARForecastResult,
                &mut out_error as *mut AnofoxError,
            )
        };

        assert!(
            ok,
            "anofox_ts_forecast_var should return true on valid input"
        );
        assert!(
            !out_result.forecasts.is_null(),
            "forecasts pointer must be non-null"
        );
        assert_eq!(out_result.k_vars, k_vars, "k_vars must match");
        assert_eq!(out_result.n_horizon, horizon, "n_horizon must match");

        // Read some values to verify they are finite
        unsafe {
            for idx in 0..(k_vars * horizon) {
                let val = *out_result.forecasts.add(idx);
                assert!(
                    val.is_finite(),
                    "forecast[{}] must be finite, got {}",
                    idx,
                    val
                );
            }
        }

        // Free the result — this should null the forecasts pointer
        unsafe {
            anofox_free_var_forecast_result(&mut out_result as *mut VARForecastResult);
        }
        assert!(
            out_result.forecasts.is_null(),
            "forecasts pointer must be null after free"
        );
    }

    /// Test 4 — null guard: passing null flat_data sets out_error to NullPointer and returns false.
    #[test]
    fn test_ffi_var_null_guard() {
        let mut out_result = VARForecastResult::default();
        let mut out_error = AnofoxError::success();

        let ok = unsafe {
            anofox_ts_forecast_var(
                std::ptr::null(), // null flat_data — should trigger NullPointer error
                2,
                50,
                1,
                5,
                &mut out_result as *mut VARForecastResult,
                &mut out_error as *mut AnofoxError,
            )
        };

        assert!(!ok, "null flat_data should return false");
        // The error code should be NullPointer
        let code = unsafe { CStr::from_ptr(out_error.message.as_ptr()) }.to_string_lossy();
        assert!(
            !code.is_empty(),
            "error message must be non-empty on null input"
        );
    }
}

// ============================================================================
// Phase 5 (ENS-02): Explicit-member ensemble forecast
// ============================================================================

/// Forecast using an explicit list of named member models combined via the
/// specified `CombinationMethod`.
///
/// The member list is passed as a null-delimited concatenated C string
/// (e.g. `"AutoARIMA\0AutoETS\0Theta\0"`) plus explicit byte length
/// `members_buf_len` so Rust never has to scan past the buffer end.
///
/// Mirrors `anofox_ts_forecast` but dispatches to
/// `anofox_fcst_core::forecast_explicit_ensemble` rather than the single-model
/// `forecast()` path. `lower_bounds`/`upper_bounds` are the crate `Ensemble`'s
/// own `predict_with_intervals` bounds (pooled across members) at
/// `confidence_level` (D-01, plan 10-03) — `null` only when the crate itself
/// returns no bounds, matching every other forecast entry point's convention.
///
/// # Safety
/// All pointer arguments must be valid for the stated lengths: `values`/`validity`
/// for `length` elements, `members_buf` for `members_buf_len` bytes, and
/// `out_result`/`out_error` must be valid, writable pointers. `members_buf` and
/// `combination_method` must be non-null (the latter may be an empty C string).
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_forecast_ensemble(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    members_buf: *const c_char, // null-delimited: "AutoARIMA\0AutoETS\0Theta\0"
    members_buf_len: size_t,    // total byte length of members_buf
    members_count: size_t,      // number of member strings in members_buf
    combination_method: *const c_char, // C string, NULL or "" → "mean"
    seasonal_period: c_int,
    horizon: c_int,
    confidence_level: c_double,
    out_result: *mut ForecastResult,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if values.is_null() || members_buf.is_null() || out_result.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let series = build_series(values, validity, length);

        // Recover member list from null-delimited buffer bounded by members_buf_len
        let member_bytes = std::slice::from_raw_parts(members_buf as *const u8, members_buf_len);
        let member_names: Vec<String> = member_bytes
            .split(|&b| b == 0)
            .filter(|s| !s.is_empty())
            .map(|s| String::from_utf8_lossy(s).into_owned())
            .collect();

        // Defensive: count mismatch means a C++ marshalling bug
        if member_names.len() != members_count {
            return Err(anofox_fcst_core::ForecastError::InvalidParameter {
                param: "members".to_string(),
                value: format!(
                    "parsed {} names but members_count = {}",
                    member_names.len(),
                    members_count
                ),
                reason: "members_buf / members_count mismatch (C++ marshalling bug)".to_string(),
            });
        }

        // Parse combination_method (NULL or empty → None → "mean" in core)
        let method_opt: Option<String> = if combination_method.is_null() {
            None
        } else {
            CStr::from_ptr(combination_method)
                .to_str()
                .ok()
                .filter(|s| !s.is_empty())
                .map(str::to_owned)
        };

        anofox_fcst_core::forecast_explicit_ensemble(
            &series,
            horizon as usize,
            &member_names,
            method_opt.as_deref(),
            seasonal_period as usize,
            confidence_level,
        )
    }));

    match result {
        Ok(Ok(forecast)) => {
            let n_forecasts = forecast.point.len();
            (*out_result).n_forecasts = n_forecasts;

            // Copy point forecasts
            (*out_result).point_forecasts = match alloc_or_error(
                &forecast.point,
                out_error,
                "Failed to allocate point forecasts",
            ) {
                Ok(ptr) => ptr,
                Err(()) => return false,
            };

            // D-01 (plan 10-03): the crate Ensemble's own pooled prediction
            // interval bounds — null only when the crate itself returns
            // none (matching alloc_or_error's empty-vec convention).
            (*out_result).lower_bounds = match alloc_or_error(
                &forecast.lower,
                out_error,
                "Failed to allocate lower bounds",
            ) {
                Ok(ptr) => ptr,
                Err(()) => return false,
            };
            (*out_result).upper_bounds = match alloc_or_error(
                &forecast.upper,
                out_error,
                "Failed to allocate upper bounds",
            ) {
                Ok(ptr) => ptr,
                Err(()) => return false,
            };

            // No fitted values / residuals in ensemble path
            (*out_result).fitted_values = ptr::null_mut();
            (*out_result).n_fitted = 0;
            (*out_result).residuals = ptr::null_mut();
            (*out_result).n_residuals = 0;

            // Copy model name ("Ensemble")
            copy_string_to_buffer(&forecast.model_name, &mut (*out_result).model_name);

            (*out_result).aic = f64::NAN;
            (*out_result).bic = f64::NAN;
            (*out_result).mse = f64::NAN;

            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                let error_code = match e.to_code() {
                    1 => ErrorCode::NullPointer,
                    2 => ErrorCode::InvalidInput,
                    3 => ErrorCode::ComputationError,
                    4 => ErrorCode::AllocationError,
                    5 => ErrorCode::InvalidModel,
                    6 => ErrorCode::InsufficientData,
                    7 => ErrorCode::InvalidDateFormat,
                    8 => ErrorCode::InvalidFrequency,
                    9 => ErrorCode::InvalidInput, // InvalidParameter → InvalidInput
                    _ => ErrorCode::InternalError,
                };
                (*out_error).set_error(error_code, &e.to_string());
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
// Phase 6 (INSP-01): Ensemble introspection FFI surface
// ============================================================================

/// Result struct for ensemble member introspection.
///
/// Both `anofox_ts_ensemble_inspect` and `anofox_ts_auto_ensemble_inspect` return
/// via a pointer to this struct. The companion `anofox_free_ensemble_inspect_result`
/// must be called after unpacking to avoid memory leaks.
///
/// `member_names_buf` is a null-delimited concatenation of all member names:
///   "AutoARIMA\0AutoETS\0Theta\0"
/// Parse by splitting on b==0 and filtering empty slices.
///
/// `weights` is either:
/// - a `count`-length array (one weight per member)
/// - or `null` when no weights are available (AutoEnsemble non-Mean)
///
/// `scores` is either:
/// - a `count`-length array of in-sample MSE scores (AutoEnsemble path)
/// - or `null` (explicit-member path has no per-member score)
#[repr(C)]
pub struct EnsembleInspectResult {
    pub count: size_t,                 // number of members
    pub member_names_buf: *mut c_char, // null-delimited names; length = member_names_buf_len
    pub member_names_buf_len: size_t,  // byte length of member_names_buf (including all NULs)
    pub weights: *mut c_double, // len=count; null if no weights available (non-Mean AutoEnsemble)
    pub scores: *mut c_double,  // len=count MSE scores; null for explicit-member inspect
}

/// Inspect combination weights for an explicit-member ensemble (INSP-01).
///
/// Builds and fits the Ensemble from the named members on the series, then returns
/// the per-member combination weights. Weights sum to 1.0 for all six methods.
/// For Mean and Median, weights are equal (1/k). For WeightedMSE, InverseAIC,
/// Stacking, and HorizonAdaptive, weights are crate-computed.
///
/// `scores` in the result is always `null` (explicit-member has no per-member MSE score).
///
/// # Safety
/// All pointer arguments must be valid. `out_result` must point to a zeroed `EnsembleInspectResult`.
/// Call `anofox_free_ensemble_inspect_result(out_result)` after unpacking.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_ensemble_inspect(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    members_buf: *const c_char, // null-delimited: "AutoARIMA\0AutoETS\0Theta\0"
    members_buf_len: size_t,    // total byte length of members_buf
    members_count: size_t,      // number of member strings
    combination_method: *const c_char, // C string, NULL or "" → "mean"
    seasonal_period: c_int,
    out_result: *mut EnsembleInspectResult,
    out_error: *mut AnofoxError,
) -> bool {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }

    if values.is_null() || members_buf.is_null() || out_result.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::NullPointer, "Null pointer argument");
        }
        return false;
    }

    let result = catch_unwind(AssertUnwindSafe(|| {
        let series = build_series(values, validity, length);

        // Recover member list from null-delimited buffer bounded by members_buf_len
        let member_bytes = std::slice::from_raw_parts(members_buf as *const u8, members_buf_len);
        let member_names: Vec<String> = member_bytes
            .split(|&b| b == 0)
            .filter(|s| !s.is_empty())
            .map(|s| String::from_utf8_lossy(s).into_owned())
            .collect();

        // Defensive: count mismatch means a C++ marshalling bug
        if member_names.len() != members_count {
            return Err(anofox_fcst_core::ForecastError::InvalidParameter {
                param: "members".to_string(),
                value: format!(
                    "parsed {} names but members_count = {}",
                    member_names.len(),
                    members_count
                ),
                reason: "members_buf / members_count mismatch (C++ marshalling bug)".to_string(),
            });
        }

        // Parse combination_method (NULL or empty → None → "mean" in core)
        let method_opt: Option<String> = if combination_method.is_null() {
            None
        } else {
            CStr::from_ptr(combination_method)
                .to_str()
                .ok()
                .filter(|s| !s.is_empty())
                .map(str::to_owned)
        };

        anofox_fcst_core::inspect_explicit_ensemble(
            &series,
            &member_names,
            method_opt.as_deref(),
            seasonal_period as usize,
        )
    }));

    match result {
        Ok(Ok(pairs)) => {
            let count = pairs.len();
            (*out_result).count = count;
            (*out_result).scores = ptr::null_mut(); // explicit-member has no MSE score

            // Build null-delimited names buffer
            let mut names_buf: Vec<u8> = Vec::new();
            let weights_vec: Vec<f64> = pairs
                .iter()
                .map(|(name, weight)| {
                    names_buf.extend_from_slice(name.as_bytes());
                    names_buf.push(0u8); // null terminator
                    *weight
                })
                .collect();

            // Allocate and copy names buffer
            let buf_len = names_buf.len();
            (*out_result).member_names_buf_len = buf_len;
            if buf_len > 0 {
                let ptr = malloc(buf_len) as *mut c_char;
                if ptr.is_null() {
                    if !out_error.is_null() {
                        (*out_error).set_error(
                            ErrorCode::AllocationError,
                            "Failed to allocate member_names_buf",
                        );
                    }
                    return false;
                }
                ptr::copy_nonoverlapping(names_buf.as_ptr() as *const c_char, ptr, buf_len);
                (*out_result).member_names_buf = ptr;
            } else {
                (*out_result).member_names_buf = ptr::null_mut();
            }

            // Allocate and copy weights array
            (*out_result).weights =
                match alloc_or_error(&weights_vec, out_error, "Failed to allocate weights") {
                    Ok(ptr) => ptr,
                    Err(()) => {
                        // Free already-allocated names_buf
                        if !(*out_result).member_names_buf.is_null() {
                            free((*out_result).member_names_buf as *mut core::ffi::c_void);
                            (*out_result).member_names_buf = ptr::null_mut();
                        }
                        return false;
                    }
                };

            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                let error_code = match e.to_code() {
                    1 => ErrorCode::NullPointer,
                    2 => ErrorCode::InvalidInput,
                    3 => ErrorCode::ComputationError,
                    4 => ErrorCode::AllocationError,
                    5 => ErrorCode::InvalidModel,
                    6 => ErrorCode::InsufficientData,
                    7 => ErrorCode::InvalidDateFormat,
                    8 => ErrorCode::InvalidFrequency,
                    9 => ErrorCode::InvalidInput, // InvalidParameter → InvalidInput
                    _ => ErrorCode::InternalError,
                };
                (*out_error).set_error(error_code, &e.to_string());
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

/// Inspect selected members and in-sample MSE scores for an AutoEnsemble (INSP-01).
///
/// Builds and fits an AutoEnsemble, then returns the selected top-K members with
/// their in-sample MSE scores. For Mean combination, `weights` is a `count`-length
/// array with every element equal to 1/k. For all other combination methods
/// (WeightedMSE, InverseAIC, Stacking, HorizonAdaptive), `weights` is `null`
/// (crate 0.15.3 does not expose inner ensemble weights via public API).
///
/// # Safety
/// All pointer arguments must be valid. `out_result` must point to a zeroed `EnsembleInspectResult`.
/// Call `anofox_free_ensemble_inspect_result(out_result)` after unpacking.
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_auto_ensemble_inspect(
    values: *const c_double,
    validity: *const u64,
    length: size_t,
    top_k: c_int,
    combination_method: *const c_char, // C string, NULL or "" → "weighted_mse" (crate default)
    seasonal_period: c_int,
    out_result: *mut EnsembleInspectResult,
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

        // Convert Vec<Option<f64>> to Vec<f64> for the AutoEnsemble path
        // (AutoEnsemble takes &[f64]; interpolate nulls as the core function does for explicit)
        let values_f64: Vec<f64> = series.iter().map(|v| v.unwrap_or(f64::NAN)).collect();

        // Parse combination_method (NULL or empty → None → "weighted_mse" in core)
        let method_opt: Option<String> = if combination_method.is_null() {
            None
        } else {
            CStr::from_ptr(combination_method)
                .to_str()
                .ok()
                .filter(|s| !s.is_empty())
                .map(str::to_owned)
        };

        anofox_fcst_core::inspect_auto_ensemble(
            &values_f64,
            top_k as usize,
            method_opt.as_deref(),
            seasonal_period as usize,
        )
    }));

    match result {
        Ok(Ok(triples)) => {
            let count = triples.len();
            (*out_result).count = count;

            // Build null-delimited names buffer + collect weights and scores
            let mut names_buf: Vec<u8> = Vec::new();
            let mut scores_vec: Vec<f64> = Vec::with_capacity(count);
            let mut weights_opt: Vec<Option<f64>> = Vec::with_capacity(count);

            for (name, score, weight) in &triples {
                names_buf.extend_from_slice(name.as_bytes());
                names_buf.push(0u8);
                scores_vec.push(*score);
                weights_opt.push(*weight);
            }

            // Allocate and copy names buffer
            let buf_len = names_buf.len();
            (*out_result).member_names_buf_len = buf_len;
            if buf_len > 0 {
                let ptr = malloc(buf_len) as *mut c_char;
                if ptr.is_null() {
                    if !out_error.is_null() {
                        (*out_error).set_error(
                            ErrorCode::AllocationError,
                            "Failed to allocate member_names_buf",
                        );
                    }
                    return false;
                }
                ptr::copy_nonoverlapping(names_buf.as_ptr() as *const c_char, ptr, buf_len);
                (*out_result).member_names_buf = ptr;
            } else {
                (*out_result).member_names_buf = ptr::null_mut();
            }

            // Allocate and copy scores array
            (*out_result).scores =
                match alloc_or_error(&scores_vec, out_error, "Failed to allocate scores") {
                    Ok(ptr) => ptr,
                    Err(()) => {
                        if !(*out_result).member_names_buf.is_null() {
                            free((*out_result).member_names_buf as *mut core::ffi::c_void);
                            (*out_result).member_names_buf = ptr::null_mut();
                        }
                        return false;
                    }
                };

            // Allocate weights only if ALL are Some (Mean combination); else null
            let all_some = weights_opt.iter().all(|w| w.is_some());
            if all_some && !weights_opt.is_empty() {
                let weights_vec: Vec<f64> = weights_opt.iter().map(|w| w.unwrap()).collect();
                (*out_result).weights =
                    match alloc_or_error(&weights_vec, out_error, "Failed to allocate weights") {
                        Ok(ptr) => ptr,
                        Err(()) => {
                            if !(*out_result).member_names_buf.is_null() {
                                free((*out_result).member_names_buf as *mut core::ffi::c_void);
                                (*out_result).member_names_buf = ptr::null_mut();
                            }
                            if !(*out_result).scores.is_null() {
                                free((*out_result).scores as *mut core::ffi::c_void);
                                (*out_result).scores = ptr::null_mut();
                            }
                            return false;
                        }
                    };
            } else {
                (*out_result).weights = ptr::null_mut(); // signals NULL weight column to C++
            }

            true
        }
        Ok(Err(e)) => {
            if !out_error.is_null() {
                let error_code = match e.to_code() {
                    1 => ErrorCode::NullPointer,
                    2 => ErrorCode::InvalidInput,
                    3 => ErrorCode::ComputationError,
                    4 => ErrorCode::AllocationError,
                    5 => ErrorCode::InvalidModel,
                    6 => ErrorCode::InsufficientData,
                    7 => ErrorCode::InvalidDateFormat,
                    8 => ErrorCode::InvalidFrequency,
                    9 => ErrorCode::InvalidInput, // InvalidParameter → InvalidInput
                    _ => ErrorCode::InternalError,
                };
                (*out_error).set_error(error_code, &e.to_string());
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

/// Free an EnsembleInspectResult allocated by `anofox_ts_ensemble_inspect` or
/// `anofox_ts_auto_ensemble_inspect`.
///
/// Frees `member_names_buf`, `weights`, and `scores` if non-null, then zeroes
/// the struct. Matches the allocation ownership model exactly.
///
/// # Safety
/// `result` must be null or a valid pointer to an `EnsembleInspectResult` whose
/// members were allocated by the corresponding inspect function.
#[no_mangle]
pub unsafe extern "C" fn anofox_free_ensemble_inspect_result(result: *mut EnsembleInspectResult) {
    if result.is_null() {
        return;
    }
    let r = &mut *result;

    if !r.member_names_buf.is_null() {
        free(r.member_names_buf as *mut core::ffi::c_void);
        r.member_names_buf = ptr::null_mut();
    }
    if !r.weights.is_null() {
        free(r.weights as *mut core::ffi::c_void);
        r.weights = ptr::null_mut();
    }
    if !r.scores.is_null() {
        free(r.scores as *mut core::ffi::c_void);
        r.scores = ptr::null_mut();
    }
    r.count = 0;
    r.member_names_buf_len = 0;
}

// ============================================================================
// Version
// ============================================================================

#[no_mangle]
pub extern "C" fn anofox_fcst_version() -> *const c_char {
    static VERSION: &[u8] = b"0.1.0\0";
    VERSION.as_ptr() as *const c_char
}
