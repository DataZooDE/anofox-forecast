//! Gap filling and series extension functions.

use crate::calendar::{all_month_end, detect_step, step_months, CalendarStep};
use crate::error::{ForecastError, Result};
use crate::FrequencyType;
use chrono::{Datelike, NaiveDateTime};

/// Convert microseconds since epoch to NaiveDateTime.
///
/// `pub(crate)` so `calendar.rs` (D-12) can reuse it instead of duplicating
/// the microsecond<->NaiveDateTime conversion.
pub(crate) fn micros_to_datetime(micros: i64) -> NaiveDateTime {
    let secs = micros / 1_000_000;
    let nsecs = ((micros % 1_000_000) * 1000) as u32;
    chrono::DateTime::from_timestamp(secs, nsecs)
        .map(|dt| dt.naive_utc())
        .unwrap_or_default()
}

/// Convert NaiveDateTime to microseconds since epoch.
///
/// `pub(crate)` — see [`micros_to_datetime`].
pub(crate) fn datetime_to_micros(dt: NaiveDateTime) -> i64 {
    dt.and_utc().timestamp_micros()
}

/// Fill gaps in a time series by inserting NULL values at missing timestamps.
///
/// # Arguments
/// * `dates` - Array of timestamps (as i64 microseconds since epoch)
/// * `values` - Array of values
/// * `frequency_micros` - Expected frequency between observations in microseconds
/// * `frequency_type` - Type of frequency (Fixed, Monthly, Quarterly, Yearly)
///
/// # Returns
/// Tuple of (filled_dates, filled_values) where missing timestamps have NULL values
pub fn fill_gaps(
    dates: &[i64],
    values: &[Option<f64>],
    frequency_micros: i64,
    frequency_type: FrequencyType,
) -> Result<(Vec<i64>, Vec<Option<f64>>)> {
    if dates.len() != values.len() {
        return Err(ForecastError::InvalidInput(
            "Dates and values must have the same length".to_string(),
        ));
    }

    if dates.is_empty() {
        return Ok((vec![], vec![]));
    }

    if dates.len() == 1 {
        return Ok((dates.to_vec(), values.to_vec()));
    }

    // Sort by date
    let mut pairs: Vec<(i64, Option<f64>)> =
        dates.iter().cloned().zip(values.iter().cloned()).collect();
    pairs.sort_by_key(|(d, _)| *d);

    match frequency_type {
        FrequencyType::Monthly => fill_gaps_monthly(&pairs),
        FrequencyType::Quarterly => fill_gaps_quarterly(&pairs),
        FrequencyType::Yearly => fill_gaps_yearly(&pairs),
        FrequencyType::Fixed => fill_gaps_fixed(&pairs, frequency_micros),
    }
}

/// Fill gaps using fixed interval arithmetic.
fn fill_gaps_fixed(
    pairs: &[(i64, Option<f64>)],
    frequency_micros: i64,
) -> Result<(Vec<i64>, Vec<Option<f64>>)> {
    let mut result_dates = Vec::new();
    let mut result_values = Vec::new();

    let (first_date, first_val) = pairs[0];
    result_dates.push(first_date);
    result_values.push(first_val);

    for i in 1..pairs.len() {
        let (prev_date, _) = pairs[i - 1];
        let (curr_date, curr_val) = pairs[i];

        // Calculate expected number of steps
        let gap = curr_date - prev_date;
        let expected_steps = gap / frequency_micros;

        // Insert missing timestamps
        for step in 1..expected_steps {
            let missing_date = prev_date + step * frequency_micros;
            result_dates.push(missing_date);
            result_values.push(None);
        }

        result_dates.push(curr_date);
        result_values.push(curr_val);
    }

    Ok((result_dates, result_values))
}

/// Fill gaps for monthly frequency using calendar-based arithmetic.
fn fill_gaps_monthly(pairs: &[(i64, Option<f64>)]) -> Result<(Vec<i64>, Vec<Option<f64>>)> {
    fill_gaps_calendar(pairs, 1)
}

/// Fill gaps for quarterly frequency using calendar-based arithmetic.
fn fill_gaps_quarterly(pairs: &[(i64, Option<f64>)]) -> Result<(Vec<i64>, Vec<Option<f64>>)> {
    fill_gaps_calendar(pairs, 3)
}

/// Fill gaps for yearly frequency using calendar-based arithmetic.
fn fill_gaps_yearly(pairs: &[(i64, Option<f64>)]) -> Result<(Vec<i64>, Vec<Option<f64>>)> {
    fill_gaps_calendar(pairs, 12)
}

/// Shared monthly/quarterly/yearly gap fill (D-09, G24): for each missing position
/// between consecutive observed dates, steps from the PREVIOUS OBSERVED DATE (not
/// the start of its period) by `k * m` months via [`step_months`], with month-end
/// alignment resolved once for the whole series via [`all_month_end`]. Effect: a
/// month-end series stays month-end (2024-01-31 -> 02-29 -> 03-31), a 1st-of-month
/// series is unchanged (clamped day-of-month = 1), and any other day-of-month keeps
/// its own day (clamped to the target month's length) instead of snapping to the 1st.
fn fill_gaps_calendar(
    pairs: &[(i64, Option<f64>)],
    m: i64,
) -> Result<(Vec<i64>, Vec<Option<f64>>)> {
    let observed: Vec<i64> = pairs.iter().map(|(d, _)| *d).collect();
    let month_end = all_month_end(&observed)?;

    let mut result_dates = Vec::new();
    let mut result_values = Vec::new();

    let (first_date, first_val) = pairs[0];
    result_dates.push(first_date);
    result_values.push(first_val);

    for i in 1..pairs.len() {
        let prev_date = pairs[i - 1].0;
        let prev_dt = micros_to_datetime(prev_date);
        let curr_dt = micros_to_datetime(pairs[i].0);
        let (curr_date, curr_val) = pairs[i];

        // Calendar-granularity index difference (months / quarters / years).
        let idx_diff = match m {
            1 => month_index(curr_dt) - month_index(prev_dt),
            3 => quarter_index(curr_dt) - quarter_index(prev_dt),
            _ => curr_dt.year() as i64 - prev_dt.year() as i64,
        };

        if idx_diff > 1 {
            for step in 1..idx_diff {
                let missing_micros = step_months(prev_date, step * m, month_end)?;
                result_dates.push(missing_micros);
                result_values.push(None);
            }
        }

        result_dates.push(curr_date);
        result_values.push(curr_val);
    }

    Ok((result_dates, result_values))
}

/// Legacy fill_gaps function for backward compatibility.
/// Uses Fixed frequency type.
pub fn fill_gaps_legacy(
    dates: &[i64],
    values: &[Option<f64>],
    frequency_seconds: i64,
) -> Result<(Vec<i64>, Vec<Option<f64>>)> {
    fill_gaps(dates, values, frequency_seconds, FrequencyType::Fixed)
}

/// Extend a time series forward to a target date.
///
/// # Arguments
/// * `dates` - Array of timestamps (as i64 microseconds since epoch)
/// * `values` - Array of values
/// * `target_date` - Target date to extend to (microseconds since epoch)
/// * `frequency_micros` - Expected frequency in microseconds (for fixed intervals)
/// * `frequency_type` - Type of frequency (Fixed, Monthly, Quarterly, Yearly)
///
/// # Returns
/// Tuple of (extended_dates, extended_values) with NULLs for filled positions
pub fn fill_forward(
    dates: &[i64],
    values: &[Option<f64>],
    target_date: i64,
    frequency_micros: i64,
    frequency_type: FrequencyType,
) -> Result<(Vec<i64>, Vec<Option<f64>>)> {
    if dates.is_empty() {
        return Ok((vec![], vec![]));
    }

    let mut result_dates = dates.to_vec();
    let mut result_values = values.to_vec();

    let last_date = *dates.iter().max().unwrap();

    if target_date <= last_date {
        return Ok((result_dates, result_values));
    }

    match frequency_type {
        FrequencyType::Fixed => {
            let mut current_date = last_date + frequency_micros;
            while current_date <= target_date {
                result_dates.push(current_date);
                result_values.push(None);
                current_date += frequency_micros;
            }
        }
        FrequencyType::Monthly | FrequencyType::Quarterly | FrequencyType::Yearly => {
            // D-09, G24: step from the LAST OBSERVED DATE (not the start of its
            // period), the same month-end-preserving rule fill_gaps_calendar and
            // future_dates use. k = 1.. while the stepped date is <= target_date.
            let m: i64 = match frequency_type {
                FrequencyType::Monthly => 1,
                FrequencyType::Quarterly => 3,
                FrequencyType::Yearly => 12,
                _ => unreachable!("matched above"),
            };
            let month_end = all_month_end(dates)?;
            let mut k: i64 = 1;
            loop {
                let new_micros = step_months(last_date, k * m, month_end)?;
                if new_micros > target_date {
                    break;
                }
                result_dates.push(new_micros);
                result_values.push(None);
                k += 1;
            }
        }
    }

    Ok((result_dates, result_values))
}

/// Legacy fill_forward function for backward compatibility.
/// Uses Fixed frequency type.
///
/// `frequency_micros` -- despite the historical `_seconds`-shaped name this replaced
/// (G23, WRAP-03), every SQL/FFI caller passes MICROSECONDS, matching
/// [`detect_frequency`]'s return unit and [`fill_forward`]'s own `frequency_micros`.
pub fn fill_forward_legacy(
    dates: &[i64],
    values: &[Option<f64>],
    target_date: i64,
    frequency_micros: i64,
) -> Result<(Vec<i64>, Vec<Option<f64>>)> {
    fill_forward(
        dates,
        values,
        target_date,
        frequency_micros,
        FrequencyType::Fixed,
    )
}

/// Detect the frequency of a time series.
///
/// Returns the modal consecutive-difference step, in the SAME INTEGER UNIT as the
/// input `dates` -- MICROSECONDS SINCE EPOCH for every SQL/FFI caller in this
/// extension (every `dates` array that reaches this function is built from
/// `TimestampToMicroseconds`/`DateToMicroseconds` in the C++ layer). This was
/// previously undocumented at the public API boundary (Phase 12 deferred item (e),
/// G23/WRAP-03).
///
/// Deterministic tie rule (D-12, orchestrator decision): when two or more
/// consecutive-difference values are tied for the most common ("mode"), the
/// SMALLER interval wins, independent of map iteration order. Uses an
/// ordered map plus a `(count, Reverse(diff))` max key so the result cannot
/// depend on unordered-hash iteration order even incidentally.
pub fn detect_frequency(dates: &[i64]) -> Result<i64> {
    if dates.len() < 2 {
        return Err(ForecastError::InsufficientData {
            needed: 2,
            got: dates.len(),
        });
    }

    let mut sorted_dates = dates.to_vec();
    sorted_dates.sort();

    // Calculate differences
    let diffs: Vec<i64> = sorted_dates
        .windows(2)
        .map(|w| w[1] - w[0])
        .filter(|&d| d > 0)
        .collect();

    if diffs.is_empty() {
        return Err(ForecastError::InvalidInput(
            "Could not detect frequency".to_string(),
        ));
    }

    // Find the most common difference (mode); ties broken by the smaller
    // interval via Reverse(diff) in the max key.
    let mut counts: std::collections::BTreeMap<i64, usize> = std::collections::BTreeMap::new();
    for d in &diffs {
        *counts.entry(*d).or_insert(0) += 1;
    }

    let mode = counts
        .into_iter()
        .max_by_key(|&(diff, count)| (count, std::cmp::Reverse(diff)))
        .map(|(diff, _)| diff)
        .unwrap();

    Ok(mode)
}

/// Missing-period gap-count result (D-04, STAT-03).
///
/// `expected_length` = `n_distinct + n_missing_periods`: the number of periods that
/// should exist at the resolved step between the first and last observed date,
/// inclusive. `n_missing_periods` is the sum, over every consecutive pair of distinct
/// sorted dates, of `(step_count - 1)` — i.e. the count of missing periods ("n_gaps" as
/// of D-04, replacing the pre-v0.10.0 meaning). `n_gap_runs` counts the number of
/// consecutive-pair transitions whose step count is `>= 2` — the pre-v0.10.0 `n_gaps`
/// meaning (number of gap intervals/occurrences, not missing periods).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct GapCounts {
    pub expected_length: usize,
    pub n_missing_periods: usize,
    pub n_gap_runs: usize,
}

/// Month-granularity index: `year*12 + (month - 1)`, zero-based within the year.
fn month_index(dt: NaiveDateTime) -> i64 {
    dt.year() as i64 * 12 + (dt.month() as i64 - 1)
}

/// Quarter-granularity index: `year*4 + quarter`, zero-based within the year.
fn quarter_index(dt: NaiveDateTime) -> i64 {
    dt.year() as i64 * 4 + (dt.month() as i64 - 1) / 3
}

/// Number of calendar steps between two consecutive sorted, distinct dates, for a
/// given [`CalendarStep`]. Always `>= 1` (checked integer division; a zero or negative
/// raw difference — e.g. two distinct timestamps within the same calendar bucket for a
/// `Months` step — is clamped to the minimum of 1, per T-13-03).
fn step_count(prev: i64, curr: i64, step: CalendarStep) -> usize {
    match step {
        CalendarStep::Fixed(s) => {
            if s <= 0 {
                return 1;
            }
            let diff = (curr - prev) as i128;
            let s128 = s as i128;
            // Round half away from zero: k = floor((2*diff + s) / (2*s)) for diff, s > 0.
            let k = (2 * diff + s128) / (2 * s128);
            k.max(1) as usize
        }
        CalendarStep::Months(m) => {
            if m == 0 {
                return 1;
            }
            let prev_dt = micros_to_datetime(prev);
            let curr_dt = micros_to_datetime(curr);
            let idx_diff = match m {
                1 => month_index(curr_dt) - month_index(prev_dt),
                3 => quarter_index(curr_dt) - quarter_index(prev_dt),
                _ => curr_dt.year() as i64 - prev_dt.year() as i64,
            };
            idx_diff.max(1) as usize
        }
    }
}

/// Resolve the [`CalendarStep`] used for gap counting from a frequency spec (D-04):
///
/// - `Monthly` -> `Months(1)`, `Quarterly` -> `Months(3)`, `Yearly` -> `Months(12)`.
/// - `Fixed` with `frequency_micros > 0` -> `Fixed(frequency_micros)` (declared frequency).
/// - `Fixed` with `frequency_micros <= 0` -> auto-detected via [`detect_step`] when at
///   least 2 distinct dates are present, else `None` (not enough information).
pub fn resolve_gap_step(
    dates: &[i64],
    frequency_micros: i64,
    frequency_type: FrequencyType,
) -> Result<Option<CalendarStep>> {
    match frequency_type {
        FrequencyType::Monthly => Ok(Some(CalendarStep::Months(1))),
        FrequencyType::Quarterly => Ok(Some(CalendarStep::Months(3))),
        FrequencyType::Yearly => Ok(Some(CalendarStep::Months(12))),
        FrequencyType::Fixed => {
            if frequency_micros > 0 {
                Ok(Some(CalendarStep::Fixed(frequency_micros)))
            } else {
                let mut distinct = dates.to_vec();
                distinct.sort_unstable();
                distinct.dedup();
                if distinct.len() < 2 {
                    Ok(None)
                } else {
                    Ok(Some(detect_step(dates)?))
                }
            }
        }
    }
}

/// Count missing periods and gap runs in a date series at a given [`CalendarStep`]
/// (D-04, STAT-03). Sorts and de-duplicates `dates` first (duplicate timestamps do not
/// count as gaps or inflate `expected_length`).
///
/// A single distinct date gives `GapCounts { expected_length: 1, n_missing_periods: 0,
/// n_gap_runs: 0 }`; an empty slice gives all-zero.
pub fn gap_counts(dates: &[i64], step: CalendarStep) -> GapCounts {
    let mut distinct = dates.to_vec();
    distinct.sort_unstable();
    distinct.dedup();

    if distinct.is_empty() {
        return GapCounts::default();
    }
    if distinct.len() == 1 {
        return GapCounts {
            expected_length: 1,
            n_missing_periods: 0,
            n_gap_runs: 0,
        };
    }

    let mut n_missing_periods = 0usize;
    let mut n_gap_runs = 0usize;
    for w in distinct.windows(2) {
        let k = step_count(w[0], w[1], step);
        n_missing_periods += k - 1;
        if k >= 2 {
            n_gap_runs += 1;
        }
    }

    GapCounts {
        expected_length: distinct.len() + n_missing_periods,
        n_missing_periods,
        n_gap_runs,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_fill_gaps_fixed() {
        let dates = vec![0, 100, 300]; // Missing 200
        let values = vec![Some(1.0), Some(2.0), Some(3.0)];
        let (filled_dates, filled_values) =
            fill_gaps(&dates, &values, 100, FrequencyType::Fixed).unwrap();

        assert_eq!(filled_dates, vec![0, 100, 200, 300]);
        assert_eq!(filled_values, vec![Some(1.0), Some(2.0), None, Some(3.0)]);
    }

    #[test]
    fn test_fill_gaps_monthly() {
        // Test monthly gap filling with microsecond timestamps
        // 2023-01-01 00:00:00 UTC = 1672531200000000 microseconds
        // 2023-03-01 00:00:00 UTC = 1677628800000000 microseconds (missing February)
        let jan_1_2023 = 1672531200_i64 * 1_000_000;
        let mar_1_2023 = 1677628800_i64 * 1_000_000;

        let dates = vec![jan_1_2023, mar_1_2023];
        let values = vec![Some(100.0), Some(300.0)];

        let (filled_dates, filled_values) =
            fill_gaps(&dates, &values, 0, FrequencyType::Monthly).unwrap();

        // Should have 3 entries: Jan, Feb (filled), Mar
        assert_eq!(filled_dates.len(), 3);
        assert_eq!(filled_values.len(), 3);

        // First is original
        assert_eq!(filled_dates[0], jan_1_2023);
        assert_eq!(filled_values[0], Some(100.0));

        // Middle should be Feb 1st with NULL value
        let feb_dt = micros_to_datetime(filled_dates[1]);
        assert_eq!(feb_dt.month(), 2);
        assert_eq!(feb_dt.year(), 2023);
        assert_eq!(filled_values[1], None);

        // Last is original
        assert_eq!(filled_dates[2], mar_1_2023);
        assert_eq!(filled_values[2], Some(300.0));
    }

    #[test]
    fn test_fill_gaps_quarterly() {
        // Test quarterly gap filling
        // Q1 2023 (Jan 1) and Q3 2023 (Jul 1) - missing Q2
        let q1_2023 = 1672531200_i64 * 1_000_000; // 2023-01-01
        let q3_2023 = 1688169600_i64 * 1_000_000; // 2023-07-01

        let dates = vec![q1_2023, q3_2023];
        let values = vec![Some(100.0), Some(300.0)];

        let (filled_dates, filled_values) =
            fill_gaps(&dates, &values, 0, FrequencyType::Quarterly).unwrap();

        // Should have 3 entries: Q1, Q2 (filled), Q3
        assert_eq!(filled_dates.len(), 3);

        // Check Q2 was filled (April 1)
        let q2_dt = micros_to_datetime(filled_dates[1]);
        assert_eq!(q2_dt.month(), 4);
        assert_eq!(q2_dt.year(), 2023);
        assert_eq!(filled_values[1], None);
    }

    #[test]
    fn test_fill_gaps_yearly() {
        // Test yearly gap filling
        // 2021 and 2024 - missing 2022, 2023
        let y2021 = 1609459200_i64 * 1_000_000; // 2021-01-01
        let y2024 = 1704067200_i64 * 1_000_000; // 2024-01-01

        let dates = vec![y2021, y2024];
        let values = vec![Some(100.0), Some(400.0)];

        let (filled_dates, filled_values) =
            fill_gaps(&dates, &values, 0, FrequencyType::Yearly).unwrap();

        // Should have 4 entries: 2021, 2022, 2023, 2024
        assert_eq!(filled_dates.len(), 4);

        // Check 2022 and 2023 were filled
        let y2022_dt = micros_to_datetime(filled_dates[1]);
        let y2023_dt = micros_to_datetime(filled_dates[2]);
        assert_eq!(y2022_dt.year(), 2022);
        assert_eq!(y2023_dt.year(), 2023);
        assert_eq!(filled_values[1], None);
        assert_eq!(filled_values[2], None);
    }

    #[test]
    fn test_detect_frequency() {
        let dates = vec![0, 100, 200, 300, 400];
        let freq = detect_frequency(&dates).unwrap();
        assert_eq!(freq, 100);
    }

    #[test]
    fn test_detect_frequency_tie_breaks_to_smaller_interval_deterministically() {
        // dates diffs: {1, 2} (tie, 1 each) -> smaller (1) wins, every time.
        let dates = vec![0, 1, 3];
        for _ in 0..50 {
            assert_eq!(detect_frequency(&dates).unwrap(), 1);
        }
    }

    #[test]
    fn test_detect_frequency_tie_pattern_2_1_2_1_breaks_to_1() {
        // diffs: 2, 1, 2, 1 -> counts {2: 2, 1: 2}, tie -> smaller (1) wins.
        let dates = vec![0, 2, 3, 5, 6];
        for _ in 0..50 {
            assert_eq!(detect_frequency(&dates).unwrap(), 1);
        }
    }

    #[test]
    fn test_detect_frequency_no_tie_pattern_3_3_1_picks_majority_3() {
        // diffs: 3, 3, 1 -> counts {3: 2, 1: 1}, no tie -> majority (3) wins.
        let dates = vec![0, 3, 6, 7];
        assert_eq!(detect_frequency(&dates).unwrap(), 3);
    }
}
