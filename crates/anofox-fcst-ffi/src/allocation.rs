//! Memory allocation utilities for FFI functions.
//!
//! This module provides helper functions for allocating and copying
//! arrays between Rust and C.

use crate::types::{AnofoxError, ErrorCode};
use core::ffi::{c_char, c_double, c_int};
use std::ptr;

// Memory allocation - use libc on native, std::alloc on WASM
#[cfg(not(target_family = "wasm"))]
use libc::{free, malloc};

#[cfg(target_family = "wasm")]
unsafe fn malloc(size: usize) -> *mut core::ffi::c_void {
    use std::alloc::{alloc, Layout};
    let layout = Layout::from_size_align(size, 8).expect("8-byte alignment is always valid");
    alloc(layout) as *mut core::ffi::c_void
}

#[cfg(target_family = "wasm")]
unsafe fn free(ptr: *mut core::ffi::c_void) {
    use std::alloc::{dealloc, Layout};
    if !ptr.is_null() {
        let layout = Layout::from_size_align(1, 8).expect("8-byte alignment is always valid");
        dealloc(ptr as *mut u8, layout);
    }
}

/// Allocate a C array of doubles.
///
/// # Safety
/// Returns null on allocation failure or if n is 0.
#[inline]
pub unsafe fn alloc_double_array(n: usize) -> *mut c_double {
    if n == 0 {
        return ptr::null_mut();
    }
    malloc(n * std::mem::size_of::<c_double>()) as *mut c_double
}

/// Allocate a C array of integers.
///
/// # Safety
/// Returns null on allocation failure or if n is 0.
#[inline]
pub unsafe fn alloc_int_array(n: usize) -> *mut c_int {
    if n == 0 {
        return ptr::null_mut();
    }
    malloc(n * std::mem::size_of::<c_int>()) as *mut c_int
}

/// Allocate a validity bitmask for n elements.
///
/// # Safety
/// Returns null on allocation failure or if n is 0.
#[inline]
pub unsafe fn alloc_validity(n: usize) -> *mut u64 {
    if n == 0 {
        return ptr::null_mut();
    }
    let n_words = n.div_ceil(64);
    malloc(n_words * std::mem::size_of::<u64>()) as *mut u64
}

/// Copy a Rust slice to a newly allocated C array.
///
/// # Safety
/// Returns null on allocation failure or if slice is empty.
pub unsafe fn slice_to_c_array<T: Copy>(slice: &[T]) -> *mut T {
    if slice.is_empty() {
        return ptr::null_mut();
    }

    let ptr = malloc(std::mem::size_of_val(slice)) as *mut T;
    if !ptr.is_null() {
        ptr::copy_nonoverlapping(slice.as_ptr(), ptr, slice.len());
    }
    ptr
}

/// Copy a `Vec<f64>` to a newly allocated C double array.
///
/// # Safety
/// Returns null on allocation failure or if vec is empty.
#[inline]
pub unsafe fn vec_to_c_double_array(vec: &[f64]) -> *mut c_double {
    slice_to_c_array(vec)
}

/// Copy a `Vec<i32>` to a newly allocated C int array.
///
/// # Safety
/// Returns null on allocation failure or if vec is empty.
#[inline]
pub unsafe fn vec_to_c_int_array(vec: &[i32]) -> *mut c_int {
    slice_to_c_array(vec)
}

/// Allocate and copy an array, setting error on failure.
///
/// # Safety
/// out_ptr and out_error must be valid pointers.
/// Returns true on success, false on allocation failure.
pub unsafe fn alloc_and_copy_array<T: Copy>(
    items: &[T],
    out_ptr: *mut *mut T,
    out_error: *mut AnofoxError,
) -> bool {
    if items.is_empty() {
        *out_ptr = ptr::null_mut();
        return true;
    }

    let ptr = malloc(std::mem::size_of_val(items)) as *mut T;
    if ptr.is_null() {
        if !out_error.is_null() {
            (*out_error).set_error(ErrorCode::AllocationError, "Memory allocation failed");
        }
        return false;
    }

    ptr::copy_nonoverlapping(items.as_ptr(), ptr, items.len());
    *out_ptr = ptr;
    true
}

/// Allocate and copy a string array.
///
/// # Safety
/// out_array must be a valid pointer.
/// Returns true on success, false on allocation failure.
pub unsafe fn alloc_string_array(strings: &[&str], out_array: *mut *mut *mut c_char) -> bool {
    let n = strings.len();
    if n == 0 {
        *out_array = ptr::null_mut();
        return true;
    }

    let array_ptr = malloc(n * std::mem::size_of::<*mut c_char>()) as *mut *mut c_char;
    if array_ptr.is_null() {
        return false;
    }

    for (i, s) in strings.iter().enumerate() {
        let str_len = s.len() + 1;
        let str_ptr = malloc(str_len) as *mut c_char;
        if str_ptr.is_null() {
            // Clean up already allocated strings
            for j in 0..i {
                free(*array_ptr.add(j) as *mut core::ffi::c_void);
            }
            free(array_ptr as *mut core::ffi::c_void);
            return false;
        }
        ptr::copy_nonoverlapping(s.as_ptr() as *const c_char, str_ptr, s.len());
        *str_ptr.add(s.len()) = 0; // Null terminator
        *array_ptr.add(i) = str_ptr;
    }

    *out_array = array_ptr;
    true
}

/// Free a C pointer using platform-appropriate free function.
///
/// # Safety
/// ptr must be either null or a valid pointer allocated by malloc.
#[inline]
pub unsafe fn free_ptr(ptr: *mut core::ffi::c_void) {
    if !ptr.is_null() {
        free(ptr);
    }
}

/// Macro to free multiple struct fields.
///
/// Usage:
/// ```ignore
/// free_fields!(result, field1, field2, field3);
/// ```
#[macro_export]
macro_rules! free_fields {
    ($result:expr, $($field:ident),+ $(,)?) => {{
        $(
            if !$result.$field.is_null() {
                $crate::allocation::free_ptr($result.$field as *mut core::ffi::c_void);
                $result.$field = std::ptr::null_mut();
            }
        )+
    }};
}

/// Set a validity bit in a bitmask.
///
/// # Safety
/// The validity pointer must be valid and point to an array with sufficient
/// capacity for the given index (at least `index / 64 + 1` u64 words).
#[inline]
pub unsafe fn set_validity_bit(validity: *mut u64, index: usize, is_valid: bool) {
    if validity.is_null() {
        return;
    }
    let word_idx = index / 64;
    let bit_idx = index % 64;
    if is_valid {
        *validity.add(word_idx) |= 1u64 << bit_idx;
    } else {
        *validity.add(word_idx) &= !(1u64 << bit_idx);
    }
}

/// Fill values and validity arrays from `Option<f64>` data.
///
/// # Safety
/// result_values and result_validity must point to arrays of sufficient size.
pub unsafe fn fill_optional_values(
    result_values: *mut c_double,
    result_validity: *mut u64,
    data: &[Option<f64>],
) {
    // Initialize validity to all valid
    if !result_validity.is_null() {
        let n_words = data.len().div_ceil(64);
        for i in 0..n_words {
            *result_validity.add(i) = u64::MAX;
        }
    }

    for (i, v) in data.iter().enumerate() {
        match v {
            Some(val) => {
                *result_values.add(i) = *val;
                set_validity_bit(result_validity, i, true);
            }
            None => {
                *result_values.add(i) = f64::NAN;
                set_validity_bit(result_validity, i, false);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_alloc_double_array() {
        unsafe {
            let ptr = alloc_double_array(5);
            assert!(!ptr.is_null());
            free_ptr(ptr as *mut core::ffi::c_void);

            let null_ptr = alloc_double_array(0);
            assert!(null_ptr.is_null());
        }
    }

    #[test]
    fn test_vec_to_c_double_array() {
        unsafe {
            let vec = vec![1.0, 2.0, 3.0];
            let ptr = vec_to_c_double_array(&vec);
            assert!(!ptr.is_null());

            // Verify contents
            assert_eq!(*ptr, 1.0);
            assert_eq!(*ptr.add(1), 2.0);
            assert_eq!(*ptr.add(2), 3.0);

            free_ptr(ptr as *mut core::ffi::c_void);
        }
    }

    #[test]
    fn test_set_validity_bit() {
        unsafe {
            let mut validity: [u64; 2] = [0, 0];

            set_validity_bit(validity.as_mut_ptr(), 0, true);
            assert_eq!(validity[0] & 1, 1);

            set_validity_bit(validity.as_mut_ptr(), 63, true);
            assert_eq!(validity[0] >> 63, 1);

            set_validity_bit(validity.as_mut_ptr(), 64, true);
            assert_eq!(validity[1] & 1, 1);

            set_validity_bit(validity.as_mut_ptr(), 0, false);
            assert_eq!(validity[0] & 1, 0);
        }
    }
}
