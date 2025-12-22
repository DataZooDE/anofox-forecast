//! Series filtering functions.

use crate::error::Result;

/// Check if a series is constant (all values are the same).
pub fn is_constant(values: &[Option<f64>]) -> bool {
    let non_null: Vec<f64> = values.iter().filter_map(|v| *v).collect();

    if non_null.len() < 2 {
        return true;
    }

    let first = non_null[0];
    non_null.iter().all(|v| (v - first).abs() < f64::EPSILON)
}

/// Check if a series is too short (below minimum length).
pub fn is_short(values: &[Option<f64>], min_length: usize) -> bool {
    let non_null_count = values.iter().filter(|v| v.is_some()).count();
    non_null_count < min_length
}

/// Drop constant series from a collection.
/// Returns indices of non-constant series.
pub fn filter_constant(series_list: &[Vec<Option<f64>>]) -> Vec<usize> {
    series_list
        .iter()
        .enumerate()
        .filter(|(_, s)| !is_constant(s))
        .map(|(i, _)| i)
        .collect()
}

/// Drop short series from a collection.
/// Returns indices of series with sufficient length.
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
}
