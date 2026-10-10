//! FFI-ABI proof of the D-12 calendar rule (plan 12-05): `anofox_ts_future_dates`
//! auto-detects the calendar step from an observed date series, or accepts an
//! explicit step, and writes exactly `horizon` dates into a caller-allocated
//! buffer.

use anofox_fcst_ffi::anofox_ts_future_dates;
use anofox_fcst_ffi::types::AnofoxError;

const ONE_DAY_MICROS: i64 = 86_400_000_000;

fn ymd_micros(year: i32, month: u32, day: u32) -> i64 {
    let date = chrono::NaiveDate::from_ymd_opt(year, month, day).unwrap();
    date.and_hms_opt(0, 0, 0)
        .unwrap()
        .and_utc()
        .timestamp_micros()
}

fn month_end_series(start_year: i32, months: u32) -> Vec<i64> {
    (0..months)
        .map(|i| {
            let first =
                chrono::NaiveDate::from_ymd_opt(start_year, 1, 1).unwrap() + chrono::Months::new(i);
            let next = first + chrono::Months::new(1);
            let last = next.pred_opt().unwrap();
            last.and_hms_opt(0, 0, 0)
                .unwrap()
                .and_utc()
                .timestamp_micros()
        })
        .collect()
}

#[test]
fn auto_detect_on_month_end_series_returns_three_month_ends() {
    let dates = month_end_series(2023, 11); // Jan..Nov 2023 month-ends
    let mut out = [0i64; 3];
    let mut error = AnofoxError::default();
    let success = unsafe {
        anofox_ts_future_dates(
            dates.as_ptr(),
            dates.len(),
            3,
            0,
            0,
            out.as_mut_ptr(),
            &mut error,
        )
    };
    assert!(success, "expected success, error code: {:?}", error.code);
    assert_eq!(
        out,
        [
            ymd_micros(2023, 12, 31),
            ymd_micros(2024, 1, 31),
            ymd_micros(2024, 2, 29),
        ]
    );
}

#[test]
fn explicit_step_months_one_matches_auto_detect() {
    let dates = month_end_series(2023, 11);
    let mut out = [0i64; 3];
    let mut error = AnofoxError::default();
    let success = unsafe {
        anofox_ts_future_dates(
            dates.as_ptr(),
            dates.len(),
            3,
            1,
            0,
            out.as_mut_ptr(),
            &mut error,
        )
    };
    assert!(success, "expected success, error code: {:?}", error.code);
    assert_eq!(
        out,
        [
            ymd_micros(2023, 12, 31),
            ymd_micros(2024, 1, 31),
            ymd_micros(2024, 2, 29),
        ]
    );
}

#[test]
fn both_step_months_and_step_micros_nonzero_is_invalid_input() {
    let dates = [ymd_micros(2023, 1, 1), ymd_micros(2023, 1, 2)];
    let mut out = [0i64; 1];
    let mut error = AnofoxError::default();
    let success = unsafe {
        anofox_ts_future_dates(
            dates.as_ptr(),
            dates.len(),
            1,
            1,
            ONE_DAY_MICROS,
            out.as_mut_ptr(),
            &mut error,
        )
    };
    assert!(!success, "expected failure when both steps are non-zero");
}

#[test]
fn horizon_zero_returns_true_without_writing() {
    let dates = [ymd_micros(2023, 1, 1), ymd_micros(2023, 1, 2)];
    let mut error = AnofoxError::default();
    let success = unsafe {
        anofox_ts_future_dates(
            dates.as_ptr(),
            dates.len(),
            0,
            0,
            0,
            std::ptr::null_mut(),
            &mut error,
        )
    };
    assert!(
        success,
        "expected success for horizon=0, error code: {:?}",
        error.code
    );
}

#[test]
fn explicit_fixed_step_micros_steps_arithmetically() {
    let dates = [0i64, ONE_DAY_MICROS];
    let mut out = [0i64; 2];
    let mut error = AnofoxError::default();
    let success = unsafe {
        anofox_ts_future_dates(
            dates.as_ptr(),
            dates.len(),
            2,
            0,
            ONE_DAY_MICROS,
            out.as_mut_ptr(),
            &mut error,
        )
    };
    assert!(success, "expected success, error code: {:?}", error.code);
    assert_eq!(out, [ONE_DAY_MICROS * 2, ONE_DAY_MICROS * 3]);
}
