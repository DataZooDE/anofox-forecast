//! Missing-data policy owned by Rust — Phase 12 D-02/D-03/D-04; C++/SQL only marshal validity.
//!
//! Every function here treats `Some(f64::NAN)` exactly like `None` (Phase 10 D-12
//! convention): NaN is a second missing-value sentinel used by callers that pass
//! "NaN-for-NULL" arrays (e.g. the `ts_*_by` table-function surface) instead of a
//! real validity bitmask.

use crate::error::{ForecastError, Result};

/// Returns `true` if the value is missing: `None`, or `Some(NaN)`.
#[inline]
fn is_missing(value: Option<f64>) -> bool {
    match value {
        None => true,
        Some(v) => v.is_nan(),
    }
}

/// Pairwise/row-wise complete-case filtering across N columns of equal length (D-02).
///
/// All columns must have equal length, or an error names the mismatched lengths.
/// An index is kept iff every column is present (non-`None`, non-`NaN`) at that
/// index. Returns one `Vec<f64>` per input column, each possibly empty.
pub fn complete_cases(columns: &[&[Option<f64>]]) -> Result<Vec<Vec<f64>>> {
    if columns.is_empty() {
        return Ok(Vec::new());
    }

    let len = columns[0].len();
    for col in columns.iter().skip(1) {
        if col.len() != len {
            return Err(ForecastError::InvalidInput(format!(
                "arrays must have the same length: {} vs {}",
                len,
                col.len()
            )));
        }
    }

    let mut outputs: Vec<Vec<f64>> = columns.iter().map(|_| Vec::new()).collect();

    for i in 0..len {
        let all_present = columns.iter().all(|col| !is_missing(col[i]));
        if all_present {
            for (out, col) in outputs.iter_mut().zip(columns.iter()) {
                // Safe: all_present guarantees Some(v) and v is not NaN.
                out.push(col[i].unwrap());
            }
        }
    }

    Ok(outputs)
}

/// Trim leading/trailing missing values, then reject any remaining interior
/// missing value (D-03/D-04).
///
/// - `Ok(None)` when no present value exists in the input.
/// - `Err(InvalidInput)` naming the first interior missing position, 1-based,
///   in the ORIGINAL (untrimmed) list — matching SQL list indexing.
/// - `Ok(Some(start..end))` otherwise: the half-open range of the trimmed,
///   fully-present interior.
pub fn trim_edges_reject_interior(values: &[Option<f64>]) -> Result<Option<std::ops::Range<usize>>> {
    let first_idx = values.iter().position(|v| !is_missing(*v));
    let first_idx = match first_idx {
        Some(idx) => idx,
        None => return Ok(None),
    };
    let last_idx = values.iter().rposition(|v| !is_missing(*v)).unwrap();

    for (offset, v) in values[first_idx..=last_idx].iter().enumerate() {
        if is_missing(*v) {
            let original_pos = first_idx + offset;
            return Err(ForecastError::InvalidInput(format!(
                "interior NULL at position {}",
                original_pos + 1
            )));
        }
    }

    Ok(Some(first_idx..last_idx + 1))
}

/// Drops missing values, keeping present values in their original order.
///
/// Used for residual pools (conformal/bootstrap, Phase 12 plan 12-04).
pub fn drop_missing(values: &[Option<f64>]) -> Vec<f64> {
    values
        .iter()
        .filter(|v| !is_missing(**v))
        .map(|v| v.unwrap())
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn complete_cases_drops_incomplete_pairs() {
        let a: Vec<Option<f64>> = vec![Some(1.0), None, Some(3.0), Some(4.0)];
        let b: Vec<Option<f64>> = vec![Some(1.0), Some(2.0), None, Some(4.0)];
        let result = complete_cases(&[&a, &b]).unwrap();
        assert_eq!(result[0], vec![1.0, 4.0]);
        assert_eq!(result[1], vec![1.0, 4.0]);
    }

    #[test]
    fn complete_cases_treats_nan_as_missing() {
        let a: Vec<Option<f64>> = vec![Some(1.0), Some(f64::NAN), Some(3.0)];
        let b: Vec<Option<f64>> = vec![Some(1.0), Some(2.0), Some(3.0)];
        let result = complete_cases(&[&a, &b]).unwrap();
        assert_eq!(result[0], vec![1.0, 3.0]);
        assert_eq!(result[1], vec![1.0, 3.0]);
    }

    #[test]
    fn complete_cases_length_mismatch_names_both_lengths() {
        let a: Vec<Option<f64>> = vec![Some(1.0), Some(2.0), Some(3.0)];
        let b: Vec<Option<f64>> = vec![Some(1.0), Some(2.0)];
        let err = complete_cases(&[&a, &b]).unwrap_err();
        let msg = err.to_string();
        assert!(msg.contains('3'));
        assert!(msg.contains('2'));
    }

    #[test]
    fn complete_cases_three_columns_keeps_intersection() {
        let a: Vec<Option<f64>> = vec![Some(1.0), None, Some(3.0), Some(4.0)];
        let b: Vec<Option<f64>> = vec![Some(1.0), Some(2.0), Some(3.0), Some(4.0)];
        let c: Vec<Option<f64>> = vec![Some(1.0), Some(2.0), None, Some(4.0)];
        let result = complete_cases(&[&a, &b, &c]).unwrap();
        assert_eq!(result[0], vec![1.0, 4.0]);
        assert_eq!(result[1], vec![1.0, 4.0]);
        assert_eq!(result[2], vec![1.0, 4.0]);
    }

    #[test]
    fn trim_edges_trims_leading_and_trailing() {
        let v: Vec<Option<f64>> = vec![None, None, Some(1.0), Some(2.0), None];
        let result = trim_edges_reject_interior(&v).unwrap();
        assert_eq!(result, Some(2..4));
    }

    #[test]
    fn trim_edges_all_missing_is_none() {
        let v: Vec<Option<f64>> = vec![None, None];
        let result = trim_edges_reject_interior(&v).unwrap();
        assert_eq!(result, None);
    }

    #[test]
    fn trim_edges_empty_is_none() {
        let v: Vec<Option<f64>> = vec![];
        let result = trim_edges_reject_interior(&v).unwrap();
        assert_eq!(result, None);
    }

    #[test]
    fn trim_edges_interior_null_errors_with_position() {
        let v: Vec<Option<f64>> = vec![Some(1.0), Some(2.0), None, Some(4.0)];
        let err = trim_edges_reject_interior(&v).unwrap_err();
        assert!(err.to_string().contains("interior NULL at position 3"));
    }

    #[test]
    fn trim_edges_interior_nan_errors_with_original_position() {
        let v: Vec<Option<f64>> = vec![None, Some(1.0), Some(f64::NAN), Some(3.0), None];
        let err = trim_edges_reject_interior(&v).unwrap_err();
        assert!(err.to_string().contains("interior NULL at position 3"));
    }

    #[test]
    fn drop_missing_keeps_present_values_in_order() {
        let v: Vec<Option<f64>> = vec![Some(1.0), None, Some(f64::NAN), Some(4.0)];
        let result = drop_missing(&v);
        assert_eq!(result, vec![1.0, 4.0]);
    }
}
