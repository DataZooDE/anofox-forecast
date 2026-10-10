//! Gap filling and series extension functions.

use crate::calendar::{all_month_end, detect_step, step_months, CalendarStep};
use crate::error::{ForecastError, Result};
use crate::FrequencyType;
use chrono::NaiveDateTime;

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
/// * `period_count` - User-declared calendar multiplier for Monthly/Quarterly/Yearly
///   frequencies (e.g. `2` for `'2mo'`, `3` for `'3q'`, `5` for `'5y'`; `1` for the
///   single-unit `'1mo'`/`'1q'`/`'1y'` strings). Ignored for `Fixed`. Clamped to a
///   minimum of `1` (CR-01 fix, 13-03 deferred item: previously hard-coded to `1`,
///   so `'2mo'` behaved identically to `'1mo'`).
///
/// # Returns
/// Tuple of (filled_dates, filled_values) where missing timestamps have NULL values
pub fn fill_gaps(
    dates: &[i64],
    values: &[Option<f64>],
    frequency_micros: i64,
    frequency_type: FrequencyType,
    period_count: u32,
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

    let count = period_count.max(1) as i64;
    match frequency_type {
        FrequencyType::Monthly => fill_gaps_monthly(&pairs, count),
        FrequencyType::Quarterly => fill_gaps_quarterly(&pairs, count),
        FrequencyType::Yearly => fill_gaps_yearly(&pairs, count),
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
/// `period_count` is the declared multiplier (e.g. `2` for `'2mo'`); `>= 1`.
fn fill_gaps_monthly(
    pairs: &[(i64, Option<f64>)],
    period_count: i64,
) -> Result<(Vec<i64>, Vec<Option<f64>>)> {
    fill_gaps_calendar(pairs, 1, period_count)
}

/// Fill gaps for quarterly frequency using calendar-based arithmetic.
/// `period_count` is the declared multiplier (e.g. `2` for `'2q'`); `>= 1`.
fn fill_gaps_quarterly(
    pairs: &[(i64, Option<f64>)],
    period_count: i64,
) -> Result<(Vec<i64>, Vec<Option<f64>>)> {
    fill_gaps_calendar(pairs, 3, period_count)
}

/// Fill gaps for yearly frequency using calendar-based arithmetic.
/// `period_count` is the declared multiplier (e.g. `2` for `'2y'`); `>= 1`.
fn fill_gaps_yearly(
    pairs: &[(i64, Option<f64>)],
    period_count: i64,
) -> Result<(Vec<i64>, Vec<Option<f64>>)> {
    fill_gaps_calendar(pairs, 12, period_count)
}

/// Shared calendar grid walk (D-04, D-09, CR-03 fix): the SINGLE source of truth for
/// "how many evenly-spaced calendar-month grid points, stepping `step_months_count`
/// months at a time from `prev`, fall strictly between `prev` and `curr`". Both
/// [`fill_gaps_calendar`] (to know where to insert NULL rows) and [`step_count`]'s
/// `Months` branch (to count missing periods for `gap_counts`/`n_gaps`) call this
/// exact function, so `ts_fill_gaps_by`'s inserted-row count and
/// `ts_stats_by`/`ts_data_quality_by`'s `n_gaps` can never disagree for a calendar
/// frequency (the bug CR-03 found: `fill_gaps_calendar` walked the grid directly
/// while `step_count` independently rounded the total elapsed distance to the
/// nearest whole step, and the two methods disagreed whenever the gap was not an
/// exact multiple of the step).
///
/// Grid points are generated by [`step_months`] from `prev` (never cumulative,
/// never anchored to a calendar-quarter/year boundary), so day-of-month/month-end
/// alignment (via `month_end`, resolved once for the whole series by
/// [`all_month_end`]) is identical to [`future_dates`](crate::calendar::future_dates)'s
/// forecast-date stepping (D-12/G24): a month-end series stays month-end, a
/// 1st-of-month series is unchanged, any other day-of-month keeps its own day
/// (clamped to the target month's length).
///
/// Returns the grid-point micros strictly between `prev` and `curr` (exclusive of
/// both ends), in walk order. An exactly-aligned `curr` (the common case) yields
/// exactly the interior grid points and stops there; an off-grid `curr` (irregular
/// spacing) yields every grid point up to (but not reaching) `curr`.
fn calendar_grid_gap_points(
    prev: i64,
    curr: i64,
    step_months_count: i64,
    month_end: bool,
) -> Result<Vec<i64>> {
    let mut points = Vec::new();
    if step_months_count <= 0 {
        return Ok(points);
    }
    let mut k: i64 = 1;
    loop {
        let candidate = step_months(prev, k * step_months_count, month_end)?;
        if candidate >= curr {
            break;
        }
        points.push(candidate);
        k += 1;
    }
    Ok(points)
}

/// Shared monthly/quarterly/yearly gap fill (D-09, G24): for each missing position
/// between consecutive observed dates, walks the shared [`calendar_grid_gap_points`]
/// grid from the PREVIOUS OBSERVED DATE (not the start of its period), with
/// month-end alignment resolved once for the whole series via [`all_month_end`].
///
/// `base_months` is the UN-multiplied calendar unit width (1 = month, 3 = quarter,
/// 12 = year). `period_count` (CR-01 fix, 13-03 deferred item) is the user-declared
/// multiplier (e.g. `2` for `'2mo'`); the two combine into a single
/// `step_months_count = base_months * period_count` (CR-03 fix: previously this
/// function and [`step_count`] measured the gap two different ways -- see
/// [`calendar_grid_gap_points`]'s doc comment).
fn fill_gaps_calendar(
    pairs: &[(i64, Option<f64>)],
    base_months: i64,
    period_count: i64,
) -> Result<(Vec<i64>, Vec<Option<f64>>)> {
    let observed: Vec<i64> = pairs.iter().map(|(d, _)| *d).collect();
    let month_end = all_month_end(&observed)?;
    let period_count = period_count.max(1);
    let step_months_count = base_months * period_count;

    let mut result_dates = Vec::new();
    let mut result_values = Vec::new();

    let (first_date, first_val) = pairs[0];
    result_dates.push(first_date);
    result_values.push(first_val);

    for i in 1..pairs.len() {
        let prev_date = pairs[i - 1].0;
        let (curr_date, curr_val) = pairs[i];

        for missing_micros in
            calendar_grid_gap_points(prev_date, curr_date, step_months_count, month_end)?
        {
            result_dates.push(missing_micros);
            result_values.push(None);
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
    fill_gaps(dates, values, frequency_seconds, FrequencyType::Fixed, 1)
}

/// Extend a time series forward to a target date.
///
/// # Arguments
/// * `dates` - Array of timestamps (as i64 microseconds since epoch)
/// * `values` - Array of values
/// * `target_date` - Target date to extend to (microseconds since epoch)
/// * `frequency_micros` - Expected frequency in microseconds (for fixed intervals)
/// * `frequency_type` - Type of frequency (Fixed, Monthly, Quarterly, Yearly)
/// * `period_count` - User-declared calendar multiplier for Monthly/Quarterly/Yearly
///   frequencies (see [`fill_gaps`]'s doc comment); ignored for `Fixed`; clamped to a
///   minimum of `1` (CR-01 fix, 13-03 deferred item).
///
/// # Returns
/// Tuple of (extended_dates, extended_values) with NULLs for filled positions
pub fn fill_forward(
    dates: &[i64],
    values: &[Option<f64>],
    target_date: i64,
    frequency_micros: i64,
    frequency_type: FrequencyType,
    period_count: u32,
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
            // CR-01 fix (13-03 deferred item): honor the declared `period_count`
            // multiplier instead of hard-coding the single-unit step.
            let count = period_count.max(1) as i64;
            let m: i64 = match frequency_type {
                FrequencyType::Monthly => count,
                FrequencyType::Quarterly => 3 * count,
                FrequencyType::Yearly => 12 * count,
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
        1,
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

/// Number of calendar steps between two consecutive sorted, distinct dates, for a
/// given [`CalendarStep`]. Always `>= 1`.
///
/// `CalendarStep::Months(m)` stores the TOTAL months per declared step (e.g. `9` for
/// a `'3q'`-at-period_count-3 step, not `3`) -- CR-01 fix, 13-03 deferred item. The
/// `Months` branch (CR-03 fix) now walks the exact same [`calendar_grid_gap_points`]
/// grid that [`fill_gaps_calendar`] inserts rows at, instead of independently
/// rounding the total elapsed distance to the nearest whole step -- the two
/// previously disagreed whenever a gap was not an exact multiple of the declared
/// step (e.g. `'3q'` over 12 months: `fill_gaps` inserted one row at the 9-month
/// grid point, while the old rounding rule rounded `12/9` to `1` and reported zero
/// missing periods for the identical series). `month_end` must be the same
/// month-end alignment [`all_month_end`] resolves for the whole series (ignored by
/// the `Fixed` branch, which has no calendar-month notion).
fn step_count(prev: i64, curr: i64, step: CalendarStep, month_end: bool) -> Result<usize> {
    match step {
        CalendarStep::Fixed(s) => {
            if s <= 0 {
                return Ok(1);
            }
            let diff = (curr - prev) as i128;
            let s128 = s as i128;
            // Round half away from zero: k = floor((2*diff + s) / (2*s)) for diff, s > 0.
            let k = (2 * diff + s128) / (2 * s128);
            Ok(k.max(1) as usize)
        }
        CalendarStep::Months(m) => {
            if m == 0 {
                return Ok(1);
            }
            let points = calendar_grid_gap_points(prev, curr, m as i64, month_end)?;
            // `points` are the grid points strictly before `curr`; the step count is
            // "number of steps to reach/pass curr" = points.len() + 1 (the final
            // step lands on or just past curr), so n_missing = points.len() = k - 1.
            Ok(points.len() + 1)
        }
    }
}

/// Resolve the [`CalendarStep`] used for gap counting from a frequency spec (D-04):
///
/// - `Monthly` -> `Months(period_count)`, `Quarterly` -> `Months(3 * period_count)`,
///   `Yearly` -> `Months(12 * period_count)`. `period_count` is the user-declared
///   calendar multiplier (e.g. `2` for `'2mo'`, `3` for `'3q'`, `5` for `'5y'`; `1`
///   for the single-unit strings), clamped to a minimum of `1` (CR-01 fix: previously
///   hard-coded to `1`/`3`/`12` regardless of the declared multiplier, so `'2mo'`
///   silently counted gaps as if every pair were missing one month).
/// - `Fixed` with `frequency_micros > 0` -> `Fixed(frequency_micros)` (declared frequency).
/// - `Fixed` with `frequency_micros <= 0` -> auto-detected via [`detect_step`] when at
///   least 2 distinct dates are present, else `None` (not enough information).
pub fn resolve_gap_step(
    dates: &[i64],
    frequency_micros: i64,
    frequency_type: FrequencyType,
    period_count: u32,
) -> Result<Option<CalendarStep>> {
    let count = period_count.max(1);
    match frequency_type {
        FrequencyType::Monthly => Ok(Some(CalendarStep::Months(count))),
        FrequencyType::Quarterly => Ok(Some(CalendarStep::Months(count * 3))),
        FrequencyType::Yearly => Ok(Some(CalendarStep::Months(count * 12))),
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
///
/// CR-03 fix: now returns `Result` because the `Months` step resolves `month_end`
/// (via [`all_month_end`], once for the whole series -- same rule
/// [`fill_gaps_calendar`] uses) and walks [`calendar_grid_gap_points`] per pair, both
/// of which are fallible (month/date arithmetic overflow). This is what makes
/// `n_missing_periods` agree with `fill_gaps`'s inserted-row count for the same
/// series and calendar frequency.
pub fn gap_counts(dates: &[i64], step: CalendarStep) -> Result<GapCounts> {
    let mut distinct = dates.to_vec();
    distinct.sort_unstable();
    distinct.dedup();

    if distinct.is_empty() {
        return Ok(GapCounts::default());
    }
    if distinct.len() == 1 {
        return Ok(GapCounts {
            expected_length: 1,
            n_missing_periods: 0,
            n_gap_runs: 0,
        });
    }

    // Only the `Months` branch of `step_count` consults `month_end`; resolving it
    // once here (rather than per-pair) matches `fill_gaps_calendar`'s single
    // whole-series resolution and keeps the two in lockstep.
    let month_end = match step {
        CalendarStep::Months(_) => all_month_end(&distinct)?,
        CalendarStep::Fixed(_) => false,
    };

    let mut n_missing_periods = 0usize;
    let mut n_gap_runs = 0usize;
    for w in distinct.windows(2) {
        let k = step_count(w[0], w[1], step, month_end)?;
        n_missing_periods += k - 1;
        if k >= 2 {
            n_gap_runs += 1;
        }
    }

    Ok(GapCounts {
        expected_length: distinct.len() + n_missing_periods,
        n_missing_periods,
        n_gap_runs,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use chrono::Datelike;

    #[test]
    fn test_fill_gaps_fixed() {
        let dates = vec![0, 100, 300]; // Missing 200
        let values = vec![Some(1.0), Some(2.0), Some(3.0)];
        let (filled_dates, filled_values) =
            fill_gaps(&dates, &values, 100, FrequencyType::Fixed, 1).unwrap();

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
            fill_gaps(&dates, &values, 0, FrequencyType::Monthly, 1).unwrap();

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
            fill_gaps(&dates, &values, 0, FrequencyType::Quarterly, 1).unwrap();

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
            fill_gaps(&dates, &values, 0, FrequencyType::Yearly, 1).unwrap();

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

    // --- CR-01 regression (13-03 deferred item): calendar multiplier must be
    // honored for Monthly/Quarterly/Yearly, not hard-coded to the single-unit step.

    #[test]
    fn test_fill_gaps_bimonthly_no_gaps_when_correctly_spaced() {
        // Jan 1, Mar 1, May 1, Jul 1 2023 -- every-other-month, period_count=2.
        // No gaps: fill_gaps must not insert Feb/Apr/Jun.
        let dates = vec![
            1672531200_i64 * 1_000_000, // 2023-01-01
            1677628800_i64 * 1_000_000, // 2023-03-01
            1682899200_i64 * 1_000_000, // 2023-05-01
            1688169600_i64 * 1_000_000, // 2023-07-01
        ];
        let values = vec![Some(1.0), Some(2.0), Some(3.0), Some(4.0)];

        let (filled_dates, filled_values) =
            fill_gaps(&dates, &values, 0, FrequencyType::Monthly, 2).unwrap();

        assert_eq!(filled_dates, dates);
        assert_eq!(filled_values, values);
    }

    #[test]
    fn test_fill_gaps_triquarterly_fills_missing_period() {
        // Jan 1 2023 and Jan 1 2024 are 4 quarters apart; at period_count=3
        // ('3q') that is 2 tri-quarterly steps with one missing period in
        // between, at Oct 1 2023 (+9 months from Jan 2023).
        let q_2023 = 1672531200_i64 * 1_000_000; // 2023-01-01
        let q_2024 = 1704067200_i64 * 1_000_000; // 2024-01-01

        let dates = vec![q_2023, q_2024];
        let values = vec![Some(100.0), Some(400.0)];

        let (filled_dates, filled_values) =
            fill_gaps(&dates, &values, 0, FrequencyType::Quarterly, 3).unwrap();

        // Should have 3 entries: Jan 2023, Oct 2023 (filled, +9 months), Jan 2024.
        assert_eq!(filled_dates.len(), 3);
        let mid_dt = micros_to_datetime(filled_dates[1]);
        assert_eq!(mid_dt.year(), 2023);
        assert_eq!(mid_dt.month(), 10);
        assert_eq!(filled_values[1], None);
    }

    #[test]
    fn test_resolve_gap_step_honors_period_count() {
        let dates = vec![0i64, 1];
        assert_eq!(
            resolve_gap_step(&dates, 0, FrequencyType::Monthly, 2).unwrap(),
            Some(CalendarStep::Months(2))
        );
        assert_eq!(
            resolve_gap_step(&dates, 0, FrequencyType::Quarterly, 3).unwrap(),
            Some(CalendarStep::Months(9))
        );
        assert_eq!(
            resolve_gap_step(&dates, 0, FrequencyType::Yearly, 5).unwrap(),
            Some(CalendarStep::Months(60))
        );
        // period_count=0 clamps to the single-unit step (defensive minimum of 1).
        assert_eq!(
            resolve_gap_step(&dates, 0, FrequencyType::Monthly, 0).unwrap(),
            Some(CalendarStep::Months(1))
        );
    }

    #[test]
    fn test_gap_counts_zero_gaps_for_correctly_spaced_bimonthly_series() {
        // 2023-01-01, 03-01, 05-01, 07-01 at a '2mo' step: n_gaps must be 0, not 3
        // (the pre-fix bug treated every pair as "1 missing month").
        let dates = vec![
            1672531200_i64 * 1_000_000,
            1677628800_i64 * 1_000_000,
            1682899200_i64 * 1_000_000,
            1688169600_i64 * 1_000_000,
        ];
        let step = resolve_gap_step(&dates, 0, FrequencyType::Monthly, 2)
            .unwrap()
            .unwrap();
        let counts = gap_counts(&dates, step).unwrap();
        assert_eq!(counts.n_missing_periods, 0);
        assert_eq!(counts.n_gap_runs, 0);
        assert_eq!(counts.expected_length, 4);
    }

    // --- CR-03 regression: fill_gaps's inserted-row count and gap_counts's
    // n_missing_periods must agree for every calendar gap, exact-multiple or not,
    // since both now walk the same calendar_grid_gap_points grid.

    /// Assert `gap_counts(dates, step).n_missing_periods` equals the number of
    /// `None` entries `fill_gaps` inserts for the same `dates`/frequency/period_count
    /// (CR-03's exact cross-check, generalized to run over every consecutive pair
    /// via the full series rather than just one pair).
    fn assert_fill_gaps_agrees_with_gap_counts(
        dates: &[i64],
        values: &[Option<f64>],
        frequency_type: FrequencyType,
        period_count: u32,
    ) {
        let (filled_dates, filled_values) =
            fill_gaps(dates, values, 0, frequency_type, period_count).unwrap();
        let n_inserted = filled_values.iter().filter(|v| v.is_none()).count();
        assert_eq!(
            filled_dates.len(),
            dates.len() + n_inserted,
            "sanity: fill_gaps should only ever ADD rows, never drop/duplicate"
        );

        let step = resolve_gap_step(dates, 0, frequency_type, period_count)
            .unwrap()
            .unwrap();
        let counts = gap_counts(dates, step).unwrap();
        assert_eq!(
            counts.n_missing_periods, n_inserted,
            "gap_counts.n_missing_periods ({}) must equal fill_gaps's inserted-row \
             count ({}) for frequency_type={:?} period_count={}",
            counts.n_missing_periods, n_inserted, frequency_type, period_count
        );
    }

    #[test]
    fn test_cr03_agreement_3q_over_5_month_gap() {
        // '1q' (period_count=1, step=3 months) over a 5-month gap: NOT an exact
        // multiple of 3 -- this is the exact CR-03 reproduction (review's `q3`
        // table): fill_gaps_calendar inserted nothing while the old step_count
        // rounding said "1 missing period". Both must now agree.
        let jan = 1672531200_i64 * 1_000_000; // 2023-01-01
        let jun = 1685577600_i64 * 1_000_000; // 2023-06-01 (5 months later)
        let dates = vec![jan, jun];
        let values = vec![Some(100.0), Some(200.0)];
        assert_fill_gaps_agrees_with_gap_counts(&dates, &values, FrequencyType::Quarterly, 1);
    }

    #[test]
    fn test_cr03_agreement_3q_over_12_months() {
        // '3q' (period_count=3, step=9 months) over 12 months: the review's `tq`
        // table -- fill_gaps inserts ONE row at +9 months, gap_counts's old
        // rounding rule said 0 missing periods (12/9 rounds to 1, k-1=0). Both
        // must now agree (1 missing period, at the shared grid point).
        let jan_2023 = 1672531200_i64 * 1_000_000; // 2023-01-01
        let jan_2024 = 1704067200_i64 * 1_000_000; // 2024-01-01
        let dates = vec![jan_2023, jan_2024];
        let values = vec![Some(100.0), Some(200.0)];
        assert_fill_gaps_agrees_with_gap_counts(&dates, &values, FrequencyType::Quarterly, 3);
    }

    #[test]
    fn test_cr03_agreement_2mo_exact_and_inexact() {
        // '2mo' (period_count=2): one exactly-spaced pair (0 missing) and one
        // off-grid pair (Jan 1 -> Jun 1 is 5 months, not a multiple of 2: 1
        // missing grid point at Mar 1, then May1 grid point is also < Jun1... walk
        // from Jan1: Mar1 (<Jun1, missing), May1 (<Jun1, missing) -> 2 missing).
        let jan = 1672531200_i64 * 1_000_000; // 2023-01-01
        let mar = 1677628800_i64 * 1_000_000; // 2023-03-01 (exact +2mo)
        let jun = 1685577600_i64 * 1_000_000; // 2023-06-01 (+3mo from Mar, off-grid)
        let dates = vec![jan, mar, jun];
        let values = vec![Some(1.0), Some(2.0), Some(3.0)];
        assert_fill_gaps_agrees_with_gap_counts(&dates, &values, FrequencyType::Monthly, 2);
    }

    #[test]
    fn test_cr03_agreement_month_end_series() {
        // Month-end series (D-09): Jan 31, Apr 30 2023 -- a '1q' step (period_count=1)
        // skipping Feb 28/29 would misalign; this exercises the month_end=true path
        // of calendar_grid_gap_points shared by both fill_gaps and gap_counts.
        let jan_31 = 1675123200_i64 * 1_000_000; // 2023-01-31 (within DST-free UTC)
        let apr_30 = 1682812800_i64 * 1_000_000; // 2023-04-30
        let dates = vec![jan_31, apr_30];
        let values = vec![Some(1.0), Some(2.0)];
        assert_fill_gaps_agrees_with_gap_counts(&dates, &values, FrequencyType::Quarterly, 1);
    }

    #[test]
    fn test_fill_forward_bimonthly_honors_period_count() {
        // Last observed 2023-01-01; extend to 2023-07-01 at a '2mo' step ->
        // 2023-03-01 and 2023-05-01 are filled (NOT every intervening month).
        let jan_1_2023 = 1672531200_i64 * 1_000_000;
        let jul_1_2023 = 1688169600_i64 * 1_000_000;
        let dates = vec![jan_1_2023];
        let values = vec![Some(1.0)];

        let (filled_dates, filled_values) =
            fill_forward(&dates, &values, jul_1_2023, 0, FrequencyType::Monthly, 2).unwrap();

        assert_eq!(filled_dates.len(), 4); // Jan, Mar, May, Jul
        let mar_dt = micros_to_datetime(filled_dates[1]);
        assert_eq!(mar_dt.month(), 3);
        assert_eq!(filled_values[1], None);
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
