//! FFI round-trip proof for the `anofox_ts_features_list` / `anofox_free_ts_features_list`
//! pairing (#289, BUG-02).
//!
//! `anofox_ts_features_list` allocates its `char**` name array and every name
//! through this crate's own allocator shim (a size-prefixed `std::alloc` shim
//! on WASM, libc `malloc` on native). Releasing it with anything other than
//! the matching `anofox_free_ts_features_list` corrupts the heap on WASM
//! (reads/writes bytes before the real allocation) even though it happens to
//! be harmless on native, where the shim is just libc `malloc`/`free`. This
//! test exercises the paired allocate/free cycle directly, independent of the
//! C++ call site.

use std::ffi::CStr;
use std::os::raw::c_char;

use anofox_fcst_ffi::{anofox_free_ts_features_list, anofox_ts_features_list};

/// Calls `anofox_ts_features_list`, decodes every name to a `String`, then
/// releases the array via `anofox_free_ts_features_list`. Returns the decoded
/// names so callers can assert on content without re-touching freed memory.
fn list_feature_names() -> Vec<String> {
    let mut names_ptr: *mut c_char = std::ptr::null_mut();
    let mut count: usize = 0;

    let success = unsafe { anofox_ts_features_list(&mut names_ptr, &mut count) };
    assert!(success, "anofox_ts_features_list returned false");

    let names_array = names_ptr as *mut *mut c_char;
    let mut names = Vec::with_capacity(count);

    if !names_array.is_null() {
        for i in 0..count {
            let name_ptr = unsafe { *names_array.add(i) };
            assert!(!name_ptr.is_null(), "feature name {i} is null");
            let c_str = unsafe { CStr::from_ptr(name_ptr) };
            let s = c_str
                .to_str()
                .unwrap_or_else(|e| panic!("feature name {i} is not valid UTF-8: {e}"));
            assert!(!s.is_empty(), "feature name {i} is empty");
            names.push(s.to_owned());
        }
    }

    unsafe { anofox_free_ts_features_list(names_array, count) };

    names
}

#[test]
fn ts_features_list_returns_117_unique_names_via_paired_free() {
    let names = list_feature_names();

    assert_eq!(
        names.len(),
        117,
        "expected 117 feature names, got {}",
        names.len()
    );

    let unique: std::collections::HashSet<&str> = names.iter().map(|s| s.as_str()).collect();
    assert_eq!(
        unique.len(),
        117,
        "expected 117 unique feature names, got {} unique out of {}",
        unique.len(),
        names.len()
    );

    assert!(
        names.iter().any(|n| n == "abs_energy"),
        "expected feature list to contain abs_energy"
    );
    assert!(
        names.iter().any(|n| n == "zero_crossing_rate"),
        "expected feature list to contain zero_crossing_rate"
    );
}

#[test]
fn anofox_free_ts_features_list_is_a_null_safe_noop() {
    // Must not crash when called with a null array / zero count — mirrors the
    // defensive pattern of anofox_free_warnings(null, 0).
    unsafe { anofox_free_ts_features_list(std::ptr::null_mut(), 0) };
}
