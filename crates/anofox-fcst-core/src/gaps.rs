//! Gap filling and series extension functions.

use crate::error::{ForecastError, Result};
use crate::FrequencyType;
use chrono::{Datelike, Months, NaiveDateTime, Timelike};

/// Convert microseconds since epoch to NaiveDateTime.
fn micros_to_datetime(micros: i64) -> NaiveDateTime {
    let secs = micros / 1_000_000;
    let nsecs = ((micros % 1_000_000) * 1000) as u32;
    chrono::DateTime::from_timestamp(secs, nsecs)
        .map(|dt| dt.naive_utc())
        .unwrap_or_default()
}

/// Convert NaiveDateTime to microseconds since epoch.
fn datetime_to_micros(dt: NaiveDateTime) -> i64 {
    dt.and_utc().timestamp_micros()
}

/// Get the start of month for a given datetime (first day at midnight).
fn start_of_month(dt: NaiveDateTime) -> NaiveDateTime {
    dt.with_day(1)
        .unwrap_or(dt)
        .with_hour(0)
        .unwrap_or(dt)
        .with_minute(0)
        .unwrap_or(dt)
        .with_second(0)
        .unwrap_or(dt)
        .with_nanosecond(0)
        .unwrap_or(dt)
}

/// Get the start of quarter for a given datetime.
fn start_of_quarter(dt: NaiveDateTime) -> NaiveDateTime {
    let quarter_month = ((dt.month() - 1) / 3) * 3 + 1;
    dt.with_month(quarter_month)
        .unwrap_or(dt)
        .with_day(1)
        .unwrap_or(dt)
        .with_hour(0)
        .unwrap_or(dt)
        .with_minute(0)
        .unwrap_or(dt)
        .with_second(0)
        .unwrap_or(dt)
        .with_nanosecond(0)
        .unwrap_or(dt)
}

/// Get the start of year for a given datetime.
fn start_of_year(dt: NaiveDateTime) -> NaiveDateTime {
    dt.with_month(1)
        .unwrap_or(dt)
        .with_day(1)
        .unwrap_or(dt)
        .with_hour(0)
        .unwrap_or(dt)
        .with_minute(0)
        .unwrap_or(dt)
        .with_second(0)
        .unwrap_or(dt)
        .with_nanosecond(0)
        .unwrap_or(dt)
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
    let mut result_dates = Vec::new();
    let mut result_values = Vec::new();

    let (first_date, first_val) = pairs[0];
    result_dates.push(first_date);
    result_values.push(first_val);

    for i in 1..pairs.len() {
        let prev_dt = micros_to_datetime(pairs[i - 1].0);
        let curr_dt = micros_to_datetime(pairs[i].0);
        let (curr_date, curr_val) = pairs[i];

        // Calculate months between observations
        let prev_months = prev_dt.year() * 12 + prev_dt.month() as i32;
        let curr_months = curr_dt.year() * 12 + curr_dt.month() as i32;
        let month_diff = curr_months - prev_months;

        // Insert missing months
        if month_diff > 1 {
            let prev_start = start_of_month(prev_dt);
            for step in 1..month_diff {
                // Add step months to the start of the previous month
                if let Some(missing_dt) = prev_start.checked_add_months(Months::new(step as u32)) {
                    result_dates.push(datetime_to_micros(missing_dt));
                    result_values.push(None);
                }
            }
        }

        result_dates.push(curr_date);
        result_values.push(curr_val);
    }

    Ok((result_dates, result_values))
}

/// Fill gaps for quarterly frequency using calendar-based arithmetic.
fn fill_gaps_quarterly(pairs: &[(i64, Option<f64>)]) -> Result<(Vec<i64>, Vec<Option<f64>>)> {
    let mut result_dates = Vec::new();
    let mut result_values = Vec::new();

    let (first_date, first_val) = pairs[0];
    result_dates.push(first_date);
    result_values.push(first_val);

    for i in 1..pairs.len() {
        let prev_dt = micros_to_datetime(pairs[i - 1].0);
        let curr_dt = micros_to_datetime(pairs[i].0);
        let (curr_date, curr_val) = pairs[i];

        // Calculate quarters between observations
        let prev_quarters = prev_dt.year() * 4 + ((prev_dt.month() - 1) / 3) as i32;
        let curr_quarters = curr_dt.year() * 4 + ((curr_dt.month() - 1) / 3) as i32;
        let quarter_diff = curr_quarters - prev_quarters;

        // Insert missing quarters
        if quarter_diff > 1 {
            let prev_start = start_of_quarter(prev_dt);
            for step in 1..quarter_diff {
                // Add step*3 months to the start of the previous quarter
                if let Some(missing_dt) =
                    prev_start.checked_add_months(Months::new((step * 3) as u32))
                {
                    result_dates.push(datetime_to_micros(missing_dt));
                    result_values.push(None);
                }
            }
        }

        result_dates.push(curr_date);
        result_values.push(curr_val);
    }

    Ok((result_dates, result_values))
}

/// Fill gaps for yearly frequency using calendar-based arithmetic.
fn fill_gaps_yearly(pairs: &[(i64, Option<f64>)]) -> Result<(Vec<i64>, Vec<Option<f64>>)> {
    let mut result_dates = Vec::new();
    let mut result_values = Vec::new();

    let (first_date, first_val) = pairs[0];
    result_dates.push(first_date);
    result_values.push(first_val);

    for i in 1..pairs.len() {
        let prev_dt = micros_to_datetime(pairs[i - 1].0);
        let curr_dt = micros_to_datetime(pairs[i].0);
        let (curr_date, curr_val) = pairs[i];

        // Calculate years between observations
        let year_diff = curr_dt.year() - prev_dt.year();

        // Insert missing years
        if year_diff > 1 {
            let prev_start = start_of_year(prev_dt);
            for step in 1..year_diff {
                // Add step*12 months (1 year) to the start of the previous year
                if let Some(missing_dt) =
                    prev_start.checked_add_months(Months::new((step * 12) as u32))
                {
                    result_dates.push(datetime_to_micros(missing_dt));
                    result_values.push(None);
                }
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
        FrequencyType::Monthly => {
            let last_dt = micros_to_datetime(last_date);
            let target_dt = micros_to_datetime(target_date);
            let last_months = last_dt.year() * 12 + last_dt.month() as i32;
            let target_months = target_dt.year() * 12 + target_dt.month() as i32;

            let last_start = start_of_month(last_dt);
            for step in 1..=(target_months - last_months) {
                if let Some(new_dt) = last_start.checked_add_months(Months::new(step as u32)) {
                    let new_micros = datetime_to_micros(new_dt);
                    if new_micros > last_date && new_micros <= target_date {
                        result_dates.push(new_micros);
                        result_values.push(None);
                    }
                }
            }
        }
        FrequencyType::Quarterly => {
            let last_dt = micros_to_datetime(last_date);
            let target_dt = micros_to_datetime(target_date);
            let last_quarters = last_dt.year() * 4 + ((last_dt.month() - 1) / 3) as i32;
            let target_quarters = target_dt.year() * 4 + ((target_dt.month() - 1) / 3) as i32;

            let last_start = start_of_quarter(last_dt);
            for step in 1..=(target_quarters - last_quarters) {
                if let Some(new_dt) = last_start.checked_add_months(Months::new((step * 3) as u32))
                {
                    let new_micros = datetime_to_micros(new_dt);
                    if new_micros > last_date && new_micros <= target_date {
                        result_dates.push(new_micros);
                        result_values.push(None);
                    }
                }
            }
        }
        FrequencyType::Yearly => {
            let last_dt = micros_to_datetime(last_date);
            let target_dt = micros_to_datetime(target_date);

            let last_start = start_of_year(last_dt);
            for step in 1..=(target_dt.year() - last_dt.year()) {
                if let Some(new_dt) = last_start.checked_add_months(Months::new((step * 12) as u32))
                {
                    let new_micros = datetime_to_micros(new_dt);
                    if new_micros > last_date && new_micros <= target_date {
                        result_dates.push(new_micros);
                        result_values.push(None);
                    }
                }
            }
        }
    }

    Ok((result_dates, result_values))
}

/// Legacy fill_forward function for backward compatibility.
/// Uses Fixed frequency type.
pub fn fill_forward_legacy(
    dates: &[i64],
    values: &[Option<f64>],
    target_date: i64,
    frequency_seconds: i64,
) -> Result<(Vec<i64>, Vec<Option<f64>>)> {
    fill_forward(
        dates,
        values,
        target_date,
        frequency_seconds,
        FrequencyType::Fixed,
    )
}

/// Detect the frequency of a time series in seconds.
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

    // Find the most common difference (mode)
    let mut counts = std::collections::HashMap::new();
    for d in &diffs {
        *counts.entry(*d).or_insert(0) += 1;
    }

    let mode = counts
        .into_iter()
        .max_by_key(|(_, count)| *count)
        .map(|(diff, _)| diff)
        .unwrap();

    Ok(mode)
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
}
