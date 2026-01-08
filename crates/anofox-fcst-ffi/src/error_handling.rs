//! Standardized error handling utilities for FFI functions.
//!
//! This module provides helper functions and macros to reduce boilerplate
//! in FFI error handling patterns.

use crate::types::{AnofoxError, ErrorCode};
use anofox_fcst_core::ForecastError;
use std::panic::{catch_unwind, AssertUnwindSafe, UnwindSafe};

/// Initialize error output to success state.
///
/// # Safety
/// The error pointer must be valid if non-null.
#[inline]
pub unsafe fn init_error(out_error: *mut AnofoxError) {
    if !out_error.is_null() {
        *out_error = AnofoxError::success();
    }
}

/// Set an error on the output error pointer.
///
/// # Safety
/// The error pointer must be valid if non-null.
#[inline]
pub unsafe fn set_error(out_error: *mut AnofoxError, code: ErrorCode, message: &str) {
    if !out_error.is_null() {
        (*out_error).set_error(code, message);
    }
}

/// Check if any of the given pointers are null, and set an error if so.
///
/// # Safety
/// The error pointer must be valid if non-null.
#[inline]
pub unsafe fn check_null_pointers(
    out_error: *mut AnofoxError,
    ptrs: &[*const core::ffi::c_void],
) -> bool {
    for ptr in ptrs {
        if ptr.is_null() {
            set_error(out_error, ErrorCode::NullPointer, "Null pointer argument");
            return true;
        }
    }
    false
}

/// Execute an FFI function with standardized error handling.
///
/// This function handles:
/// - Initializing the error output to success
/// - Catching panics
/// - Converting Rust errors to FFI errors
///
/// # Safety
/// The error pointer must be valid if non-null.
///
/// # Returns
/// `Some(value)` on success, `None` on error
pub unsafe fn ffi_try<F, T>(out_error: *mut AnofoxError, f: F) -> Option<T>
where
    F: FnOnce() -> Result<T, ForecastError> + UnwindSafe,
{
    init_error(out_error);

    let result = catch_unwind(AssertUnwindSafe(f));

    match result {
        Ok(Ok(value)) => Some(value),
        Ok(Err(e)) => {
            set_error(out_error, ErrorCode::ComputationError, &e.to_string());
            None
        }
        Err(_) => {
            set_error(out_error, ErrorCode::PanicCaught, "Panic in Rust code");
            None
        }
    }
}

/// Execute an FFI function that returns a bool with standardized error handling.
///
/// Returns `true` on success, `false` on error.
///
/// # Safety
/// The error pointer must be valid if non-null.
pub unsafe fn ffi_bool<F>(out_error: *mut AnofoxError, f: F) -> bool
where
    F: FnOnce() -> Result<(), ForecastError> + UnwindSafe,
{
    ffi_try(out_error, || f().map(|_| ())).is_some()
}

/// Macro for common FFI function pattern with null checks.
///
/// Usage:
/// ```ignore
/// ffi_execute!(out_error, [values, out_result], {
///     // Your code here
/// })
/// ```
#[macro_export]
macro_rules! ffi_execute {
    ($out_error:expr, [$($ptr:expr),+ $(,)?], $body:block) => {{
        use $crate::error_handling::{init_error, check_null_pointers, set_error};
        use $crate::types::ErrorCode;
        use std::panic::{catch_unwind, AssertUnwindSafe};

        unsafe {
            init_error($out_error);

            let ptrs: &[*const core::ffi::c_void] = &[
                $($ptr as *const core::ffi::c_void),+
            ];

            if check_null_pointers($out_error, ptrs) {
                return false;
            }

            let result = catch_unwind(AssertUnwindSafe(|| $body));

            match result {
                Ok(Ok(value)) => value,
                Ok(Err(e)) => {
                    set_error($out_error, ErrorCode::ComputationError, &e.to_string());
                    return false;
                }
                Err(_) => {
                    set_error($out_error, ErrorCode::PanicCaught, "Panic in Rust code");
                    return false;
                }
            }
        }
    }};
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_init_error() {
        let mut error = AnofoxError::default();
        unsafe {
            init_error(&mut error);
        }
        assert_eq!(error.code, ErrorCode::Success);
    }

    #[test]
    fn test_set_error() {
        let mut error = AnofoxError::default();
        unsafe {
            set_error(&mut error, ErrorCode::NullPointer, "test error");
        }
        assert_eq!(error.code, ErrorCode::NullPointer);
    }

    #[test]
    fn test_ffi_try_success() {
        let mut error = AnofoxError::default();
        let result = unsafe { ffi_try(&mut error, || Ok::<_, ForecastError>(42)) };
        assert_eq!(result, Some(42));
        assert_eq!(error.code, ErrorCode::Success);
    }

    #[test]
    fn test_ffi_try_error() {
        let mut error = AnofoxError::default();
        let result = unsafe {
            ffi_try(&mut error, || {
                Err::<i32, _>(ForecastError::InvalidInput("test".to_string()))
            })
        };
        assert_eq!(result, None);
        assert_eq!(error.code, ErrorCode::ComputationError);
    }
}
