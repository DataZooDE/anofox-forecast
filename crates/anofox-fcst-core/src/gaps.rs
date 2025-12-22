//! Gap filling and series extension functions.

use crate::error::{ForecastError, Result};

/// Fill gaps in a time series by inserting NULL values at missing timestamps.
///
/// # Arguments
/// * `dates` - Array of timestamps (as i64)
/// * `values` - Array of values
/// * `frequency_seconds` - Expected frequency between observations in seconds
///
/// # Returns
/// Tuple of (filled_dates, filled_values) where missing timestamps have NULL values
pub fn fill_gaps(
    dates: &[i64],
    values: &[Option<f64>],
    frequency_seconds: i64,
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
        let expected_steps = gap / frequency_seconds;

        // Insert missing timestamps
        for step in 1..expected_steps {
            let missing_date = prev_date + step * frequency_seconds;
            result_dates.push(missing_date);
            result_values.push(None);
        }

        result_dates.push(curr_date);
        result_values.push(curr_val);
    }

    Ok((result_dates, result_values))
}

/// Extend a time series forward to a target date.
pub fn fill_forward(
    dates: &[i64],
    values: &[Option<f64>],
    target_date: i64,
    frequency_seconds: i64,
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

    let mut current_date = last_date + frequency_seconds;
    while current_date <= target_date {
        result_dates.push(current_date);
        result_values.push(None);
        current_date += frequency_seconds;
    }

    Ok((result_dates, result_values))
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
    fn test_fill_gaps() {
        let dates = vec![0, 100, 300]; // Missing 200
        let values = vec![Some(1.0), Some(2.0), Some(3.0)];
        let (filled_dates, filled_values) = fill_gaps(&dates, &values, 100).unwrap();

        assert_eq!(filled_dates, vec![0, 100, 200, 300]);
        assert_eq!(filled_values, vec![Some(1.0), Some(2.0), None, Some(3.0)]);
    }

    #[test]
    fn test_detect_frequency() {
        let dates = vec![0, 100, 200, 300, 400];
        let freq = detect_frequency(&dates).unwrap();
        assert_eq!(freq, 100);
    }
}
