//! Series filtering and preprocessing functions.
//!
//! This module provides utilities for filtering and cleaning time series data:
//!
//! - **Constant detection**: Identify and remove series with no variation
//! - **Length filtering**: Remove series that are too short for analysis
//! - **Edge trimming**: Remove leading/trailing zeros
//! - **Differencing**: Compute differences for stationarity
//!
//! # Example Usage
//!
//! ```
//! use anofox_fcst_core::filter::{is_constant, drop_edge_zeros};
//!
//! let values = vec![Some(0.0), Some(1.0), Some(2.0), Some(0.0)];
//! let trimmed = drop_edge_zeros(&values);
//! assert!(!is_constant(&trimmed));
//! ```

use crate::error::Result;

/// Checks if a series is constant (all non-NULL values are the same).
///
/// A series is considered constant if all its non-NULL values are equal
/// within floating-point epsilon tolerance.
///
/// # Arguments
/// * `values` - Slice of optional values to check
///
/// # Returns
/// `true` if the series is constant or has fewer than 2 non-NULL values
///
/// # Example
/// ```
/// use anofox_fcst_core::filter::is_constant;
/// assert!(is_constant(&[Some(5.0), Some(5.0), None, Some(5.0)]));
/// assert!(!is_constant(&[Some(1.0), Some(2.0), Some(3.0)]));
/// ```
pub fn is_constant(values: &[Option<f64>]) -> bool {
    let non_null: Vec<f64> = values.iter().filter_map(|v| *v).collect();

    if non_null.len() < 2 {
        return true;
    }

    let first = non_null[0];
    non_null.iter().all(|v| (v - first).abs() < f64::EPSILON)
}

/// Checks if a series is too short for analysis.
///
/// # Arguments
/// * `values` - Slice of optional values to check
/// * `min_length` - Minimum required number of non-NULL values
///
/// # Returns
/// `true` if the series has fewer than `min_length` non-NULL values
pub fn is_short(values: &[Option<f64>], min_length: usize) -> bool {
    let non_null_count = values.iter().filter(|v| v.is_some()).count();
    non_null_count < min_length
}

/// Filters out constant series from a collection.
///
/// Returns the indices of series that are NOT constant (i.e., have variation).
///
/// # Arguments
/// * `series_list` - Collection of time series to filter
///
/// # Returns
/// Vector of indices corresponding to non-constant series
pub fn filter_constant(series_list: &[Vec<Option<f64>>]) -> Vec<usize> {
    series_list
        .iter()
        .enumerate()
        .filter(|(_, s)| !is_constant(s))
        .map(|(i, _)| i)
        .collect()
}

/// Filters out short series from a collection.
///
/// Returns the indices of series that have at least `min_length` non-NULL values.
///
/// # Arguments
/// * `series_list` - Collection of time series to filter
/// * `min_length` - Minimum required non-NULL values
///
/// # Returns
/// Vector of indices corresponding to sufficiently long series
pub fn filter_short(series_list: &[Vec<Option<f64>>], min_length: usize) -> Vec<usize> {
    series_list
        .iter()
        .enumerate()
        .filter(|(_, s)| !is_short(s, min_length))
        .map(|(i, _)| i)
        .collect()
}

/// Drop leading zeros from a series.
pub fn drop_leading_zeros(values: &[Option<f64>]) -> Vec<Option<f64>> {
    let first_nonzero = values
        .iter()
        .position(|v| match v {
            Some(x) => x.abs() > f64::EPSILON,
            None => false,
        })
        .unwrap_or(values.len());

    values[first_nonzero..].to_vec()
}

/// Drop trailing zeros from a series.
pub fn drop_trailing_zeros(values: &[Option<f64>]) -> Vec<Option<f64>> {
    let last_nonzero = values
        .iter()
        .rposition(|v| match v {
            Some(x) => x.abs() > f64::EPSILON,
            None => false,
        })
        .map(|i| i + 1)
        .unwrap_or(0);

    values[..last_nonzero].to_vec()
}

/// Drop both leading and trailing zeros from a series.
pub fn drop_edge_zeros(values: &[Option<f64>]) -> Vec<Option<f64>> {
    let trimmed = drop_leading_zeros(values);
    drop_trailing_zeros(&trimmed)
}

/// Compute difference of a series at given order.
pub fn diff(values: &[f64], order: usize) -> Result<Vec<f64>> {
    if order == 0 {
        return Ok(values.to_vec());
    }

    let mut result = values.to_vec();

    for _ in 0..order {
        if result.len() < 2 {
            return Ok(vec![]);
        }

        result = result.windows(2).map(|w| w[1] - w[0]).collect();
    }

    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_is_constant() {
        assert!(is_constant(&[Some(1.0), Some(1.0), Some(1.0)]));
        assert!(!is_constant(&[Some(1.0), Some(2.0), Some(1.0)]));
        assert!(is_constant(&[Some(1.0), None, Some(1.0)]));
    }

    #[test]
    fn test_drop_leading_zeros() {
        let values = vec![Some(0.0), Some(0.0), Some(1.0), Some(2.0), Some(0.0)];
        let result = drop_leading_zeros(&values);
        assert_eq!(result, vec![Some(1.0), Some(2.0), Some(0.0)]);
    }

    #[test]
    fn test_drop_trailing_zeros() {
        let values = vec![Some(0.0), Some(1.0), Some(2.0), Some(0.0), Some(0.0)];
        let result = drop_trailing_zeros(&values);
        assert_eq!(result, vec![Some(0.0), Some(1.0), Some(2.0)]);
    }

    #[test]
    fn test_diff() {
        let values = vec![1.0, 2.0, 4.0, 7.0];
        let result = diff(&values, 1).unwrap();
        assert_eq!(result, vec![1.0, 2.0, 3.0]);

        let result2 = diff(&values, 2).unwrap();
        assert_eq!(result2, vec![1.0, 1.0]);
    }

    #[test]
    fn test_is_short() {
        let values = vec![Some(1.0), Some(2.0), Some(3.0)];
        assert!(!is_short(&values, 3));
        assert!(is_short(&values, 4));

        // With NULLs
        let with_nulls = vec![Some(1.0), None, Some(3.0), None, Some(5.0)];
        assert!(!is_short(&with_nulls, 3)); // 3 non-null values
        assert!(is_short(&with_nulls, 4)); // only 3 non-null values
    }

    #[test]
    fn test_filter_constant() {
        let series_list = vec![
            vec![Some(1.0), Some(1.0), Some(1.0)], // constant
            vec![Some(1.0), Some(2.0), Some(3.0)], // not constant
            vec![Some(5.0), Some(5.0)],            // constant
            vec![Some(0.0), Some(1.0)],            // not constant
        ];

        let indices = filter_constant(&series_list);
        assert_eq!(indices, vec![1, 3]);
    }

    #[test]
    fn test_filter_short() {
        let series_list = vec![
            vec![Some(1.0), Some(2.0)],                        // 2 values
            vec![Some(1.0), Some(2.0), Some(3.0), Some(4.0)],  // 4 values
            vec![Some(1.0)],                                   // 1 value
            vec![Some(1.0), None, Some(3.0), None, Some(5.0)], // 3 non-null
        ];

        let indices = filter_short(&series_list, 3);
        assert_eq!(indices, vec![1, 3]);
    }

    #[test]
    fn test_drop_edge_zeros() {
        let values = vec![
            Some(0.0),
            Some(0.0),
            Some(1.0),
            Some(2.0),
            Some(0.0),
            Some(0.0),
        ];
        let result = drop_edge_zeros(&values);
        assert_eq!(result, vec![Some(1.0), Some(2.0)]);
    }

    #[test]
    fn test_drop_edge_zeros_all_zeros() {
        let values = vec![Some(0.0), Some(0.0), Some(0.0)];
        let result = drop_edge_zeros(&values);
        assert!(result.is_empty());
    }

    #[test]
    fn test_diff_order_zero() {
        let values = vec![1.0, 2.0, 3.0];
        let result = diff(&values, 0).unwrap();
        assert_eq!(result, values);
    }

    #[test]
    fn test_diff_short_series() {
        let values = vec![1.0];
        let result = diff(&values, 2).unwrap();
        assert!(result.is_empty());
    }
}
