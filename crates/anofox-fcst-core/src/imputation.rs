//! Missing value imputation functions.

/// Fill NULL values with a constant.
pub fn fill_nulls_const(values: &[Option<f64>], fill_value: f64) -> Vec<f64> {
    values.iter().map(|v| v.unwrap_or(fill_value)).collect()
}

/// Fill NULL values with the last observed value (forward fill / LOCF).
pub fn fill_nulls_forward(values: &[Option<f64>]) -> Vec<Option<f64>> {
    let mut result = Vec::with_capacity(values.len());
    let mut last_value: Option<f64> = None;

    for v in values {
        match v {
            Some(x) => {
                last_value = Some(*x);
                result.push(Some(*x));
            }
            None => {
                result.push(last_value);
            }
        }
    }

    result
}

/// Fill NULL values with the next observed value (backward fill / NOCB).
pub fn fill_nulls_backward(values: &[Option<f64>]) -> Vec<Option<f64>> {
    let mut result = vec![None; values.len()];
    let mut next_value: Option<f64> = None;

    for (i, v) in values.iter().enumerate().rev() {
        match v {
            Some(x) => {
                next_value = Some(*x);
                result[i] = Some(*x);
            }
            None => {
                result[i] = next_value;
            }
        }
    }

    result
}

/// Fill NULL values with the series mean.
pub fn fill_nulls_mean(values: &[Option<f64>]) -> Vec<f64> {
    let non_null: Vec<f64> = values.iter().filter_map(|v| *v).collect();

    if non_null.is_empty() {
        return vec![f64::NAN; values.len()];
    }

    let mean = non_null.iter().sum::<f64>() / non_null.len() as f64;

    values.iter().map(|v| v.unwrap_or(mean)).collect()
}

/// Fill NULL values with linear interpolation.
pub fn fill_nulls_interpolate(values: &[Option<f64>]) -> Vec<f64> {
    if values.is_empty() {
        return vec![];
    }

    let mut result: Vec<f64> = vec![f64::NAN; values.len()];

    // Find first and last non-null indices
    let first_idx = values.iter().position(|v| v.is_some());
    let last_idx = values.iter().rposition(|v| v.is_some());

    if first_idx.is_none() || last_idx.is_none() {
        return result;
    }

    let first = first_idx.expect("checked is_none() above");
    let last = last_idx.expect("checked is_none() above");

    // Fill before first value with first value
    if let Some(v) = values[first] {
        for item in result.iter_mut().take(first) {
            *item = v;
        }
    }

    // Fill after last value with last value
    if let Some(v) = values[last] {
        for item in result.iter_mut().skip(last + 1) {
            *item = v;
        }
    }

    // Interpolate between known values
    let mut prev_idx = first;
    let mut prev_val = values[first].expect("position() guarantees values[first] is Some");
    result[first] = prev_val;

    for i in (first + 1)..=last {
        if let Some(v) = values[i] {
            // Fill gap with interpolation
            let gap = i - prev_idx;
            if gap > 1 {
                let slope = (v - prev_val) / gap as f64;
                for j in 1..gap {
                    result[prev_idx + j] = prev_val + slope * j as f64;
                }
            }
            result[i] = v;
            prev_idx = i;
            prev_val = v;
        }
    }

    result
}

#[cfg(test)]
mod tests {
    use super::*;
    use approx::assert_relative_eq;

    #[test]
    fn test_fill_nulls_const() {
        let values = vec![Some(1.0), None, Some(3.0), None];
        let result = fill_nulls_const(&values, 0.0);
        assert_eq!(result, vec![1.0, 0.0, 3.0, 0.0]);
    }

    #[test]
    fn test_fill_nulls_forward() {
        let values = vec![Some(1.0), None, None, Some(4.0), None];
        let result = fill_nulls_forward(&values);
        assert_eq!(
            result,
            vec![Some(1.0), Some(1.0), Some(1.0), Some(4.0), Some(4.0)]
        );
    }

    #[test]
    fn test_fill_nulls_backward() {
        let values = vec![None, Some(2.0), None, Some(4.0), None];
        let result = fill_nulls_backward(&values);
        assert_eq!(
            result,
            vec![Some(2.0), Some(2.0), Some(4.0), Some(4.0), None]
        );
    }

    #[test]
    fn test_fill_nulls_mean() {
        let values = vec![Some(1.0), None, Some(3.0), None, Some(5.0)];
        let result = fill_nulls_mean(&values);
        assert_relative_eq!(result[1], 3.0, epsilon = 0.001);
        assert_relative_eq!(result[3], 3.0, epsilon = 0.001);
    }

    #[test]
    fn test_fill_nulls_interpolate() {
        let values = vec![Some(1.0), None, None, Some(4.0)];
        let result = fill_nulls_interpolate(&values);
        assert_relative_eq!(result[0], 1.0, epsilon = 0.001);
        assert_relative_eq!(result[1], 2.0, epsilon = 0.001);
        assert_relative_eq!(result[2], 3.0, epsilon = 0.001);
        assert_relative_eq!(result[3], 4.0, epsilon = 0.001);
    }
}
