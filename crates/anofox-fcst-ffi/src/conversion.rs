//! Parameter conversion utilities for FFI functions.
//!
//! This module provides helper functions for converting C types to Rust types
//! with common patterns like "0 or negative means None".

use core::ffi::{c_char, c_double, c_int};
use std::ffi::CStr;

/// Convert a `c_int` to `Option<usize>`, where values <= 0 become None.
#[inline]
pub fn to_option_usize(value: c_int) -> Option<usize> {
    if value > 0 {
        Some(value as usize)
    } else {
        None
    }
}

/// Convert a `c_double` to `Option<f64>`, where values <= 0.0 become None.
#[inline]
pub fn to_option_f64_positive(value: c_double) -> Option<f64> {
    if value > 0.0 {
        Some(value)
    } else {
        None
    }
}

/// Convert a `c_double` to `Option<f64>`, where 0.0 becomes None.
#[inline]
pub fn to_option_f64_nonzero(value: c_double) -> Option<f64> {
    if value.abs() > f64::EPSILON {
        Some(value)
    } else {
        None
    }
}

/// Convert a `c_int` to `Option<i32>`, where values < 0 become None.
#[inline]
pub fn to_option_i32_nonnegative(value: c_int) -> Option<i32> {
    if value >= 0 {
        Some(value)
    } else {
        None
    }
}

/// Convert a C string pointer to a Rust `&str` with a default value.
///
/// # Safety
/// The pointer must be null or point to a valid null-terminated string.
#[inline]
pub unsafe fn c_str_to_str(ptr: *const c_char, default: &str) -> &str {
    if ptr.is_null() {
        default
    } else {
        CStr::from_ptr(ptr).to_str().unwrap_or(default)
    }
}

/// Parse a C string to a type that implements FromStr, with a default value.
///
/// # Safety
/// The pointer must be null or point to a valid null-terminated string.
pub unsafe fn c_str_parse<T>(ptr: *const c_char, default: T) -> T
where
    T: std::str::FromStr + Clone,
{
    if ptr.is_null() {
        return default;
    }

    match CStr::from_ptr(ptr).to_str() {
        Ok(s) => s.parse().unwrap_or(default),
        Err(_) => default,
    }
}

/// Parse a C string to an enum with a default, using case-insensitive matching.
///
/// # Safety
/// The pointer must be null or point to a valid null-terminated string.
pub unsafe fn c_str_to_enum<T>(ptr: *const c_char, default_str: &str, default_val: T) -> T
where
    T: std::str::FromStr,
{
    let s = c_str_to_str(ptr, default_str);
    s.parse().unwrap_or(default_val)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_to_option_usize() {
        assert_eq!(to_option_usize(5), Some(5));
        assert_eq!(to_option_usize(0), None);
        assert_eq!(to_option_usize(-1), None);
    }

    #[test]
    fn test_to_option_f64_positive() {
        assert_eq!(to_option_f64_positive(5.5), Some(5.5));
        assert_eq!(to_option_f64_positive(0.0), None);
        assert_eq!(to_option_f64_positive(-1.0), None);
    }

    #[test]
    fn test_to_option_f64_nonzero() {
        assert_eq!(to_option_f64_nonzero(5.5), Some(5.5));
        assert_eq!(to_option_f64_nonzero(-3.0), Some(-3.0));
        assert_eq!(to_option_f64_nonzero(0.0), None);
    }

    #[test]
    fn test_c_str_to_str() {
        use std::ffi::CString;

        let c_string = CString::new("hello").unwrap();
        unsafe {
            assert_eq!(c_str_to_str(c_string.as_ptr(), "default"), "hello");
            assert_eq!(c_str_to_str(std::ptr::null(), "default"), "default");
        }
    }

    #[test]
    fn test_c_str_parse() {
        use std::ffi::CString;

        let c_string = CString::new("42").unwrap();
        unsafe {
            assert_eq!(c_str_parse::<i32>(c_string.as_ptr(), 0), 42);
            assert_eq!(c_str_parse::<i32>(std::ptr::null(), 99), 99);
        }
    }
}
