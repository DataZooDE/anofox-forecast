//! Evaluation metrics for time series forecasting.
//!
//! Provides 12 standard metrics for evaluating forecast accuracy.

use crate::error::{ForecastError, Result};

/// Mean Absolute Error
pub fn mae(actual: &[f64], forecast: &[f64]) -> Result<f64> {
    validate_inputs(actual, forecast)?;
    let sum: f64 = actual
        .iter()
        .zip(forecast.iter())
        .map(|(a, f)| (a - f).abs())
        .sum();
    Ok(sum / actual.len() as f64)
}

/// Mean Squared Error
pub fn mse(actual: &[f64], forecast: &[f64]) -> Result<f64> {
    validate_inputs(actual, forecast)?;
    let sum: f64 = actual
        .iter()
        .zip(forecast.iter())
        .map(|(a, f)| (a - f).powi(2))
        .sum();
    Ok(sum / actual.len() as f64)
}

/// Root Mean Squared Error
pub fn rmse(actual: &[f64], forecast: &[f64]) -> Result<f64> {
    Ok(mse(actual, forecast)?.sqrt())
}

/// Mean Absolute Percentage Error
pub fn mape(actual: &[f64], forecast: &[f64]) -> Result<f64> {
    validate_inputs(actual, forecast)?;
    let sum: f64 = actual
        .iter()
        .zip(forecast.iter())
        .filter(|(a, _)| a.abs() > f64::EPSILON)
        .map(|(a, f)| ((a - f) / a).abs())
        .sum();
    let count = actual.iter().filter(|a| a.abs() > f64::EPSILON).count();
    if count == 0 {
        return Ok(f64::NAN);
    }
    Ok(sum / count as f64 * 100.0)
}

/// Symmetric Mean Absolute Percentage Error
pub fn smape(actual: &[f64], forecast: &[f64]) -> Result<f64> {
    validate_inputs(actual, forecast)?;
    let sum: f64 = actual
        .iter()
        .zip(forecast.iter())
        .filter(|(a, f)| a.abs() + f.abs() > f64::EPSILON)
        .map(|(a, f)| 2.0 * (a - f).abs() / (a.abs() + f.abs()))
        .sum();
    let count = actual
        .iter()
        .zip(forecast.iter())
        .filter(|(a, f)| a.abs() + f.abs() > f64::EPSILON)
        .count();
    if count == 0 {
        return Ok(f64::NAN);
    }
    Ok(sum / count as f64 * 100.0)
}

/// Mean Absolute Scaled Error
///
/// C++ API compatible: takes actual, predicted, and baseline arrays.
/// MASE = MAE(actual, predicted) / MAE(actual, baseline)
pub fn mase(actual: &[f64], forecast: &[f64], baseline: &[f64]) -> Result<f64> {
    validate_inputs(actual, forecast)?;

    if actual.len() != baseline.len() {
        return Err(ForecastError::InvalidInput(format!(
            "Actual and baseline arrays must have the same length: {} vs {}",
            actual.len(),
            baseline.len()
        )));
    }

    // Calculate MAE of the forecast
    let forecast_mae = mae(actual, forecast)?;

    // Calculate MAE of the baseline (naive forecast)
    let baseline_mae = mae(actual, baseline)?;

    if baseline_mae.abs() < f64::EPSILON {
        return Ok(f64::NAN);
    }

    Ok(forecast_mae / baseline_mae)
}

/// R-squared (Coefficient of Determination)
pub fn r2(actual: &[f64], forecast: &[f64]) -> Result<f64> {
    validate_inputs(actual, forecast)?;

    let mean: f64 = actual.iter().sum::<f64>() / actual.len() as f64;

    let ss_res: f64 = actual
        .iter()
        .zip(forecast.iter())
        .map(|(a, f)| (a - f).powi(2))
        .sum();

    let ss_tot: f64 = actual.iter().map(|a| (a - mean).powi(2)).sum();

    if ss_tot.abs() < f64::EPSILON {
        return Ok(f64::NAN);
    }

    Ok(1.0 - ss_res / ss_tot)
}

/// Forecast Bias (mean error)
pub fn bias(actual: &[f64], forecast: &[f64]) -> Result<f64> {
    validate_inputs(actual, forecast)?;
    let sum: f64 = actual.iter().zip(forecast.iter()).map(|(a, f)| f - a).sum();
    Ok(sum / actual.len() as f64)
}

/// Relative Mean Absolute Error
///
/// C++ API compatible: compares two model predictions.
/// rMAE = MAE(actual, pred1) / MAE(actual, pred2)
pub fn rmae(actual: &[f64], pred1: &[f64], pred2: &[f64]) -> Result<f64> {
    validate_inputs(actual, pred1)?;

    if actual.len() != pred2.len() {
        return Err(ForecastError::InvalidInput(format!(
            "Actual and pred2 arrays must have the same length: {} vs {}",
            actual.len(),
            pred2.len()
        )));
    }

    // MAE of first model prediction
    let pred1_mae = mae(actual, pred1)?;

    // MAE of second model prediction (baseline/benchmark)
    let pred2_mae = mae(actual, pred2)?;

    if pred2_mae.abs() < f64::EPSILON {
        return Ok(f64::NAN);
    }

    Ok(pred1_mae / pred2_mae)
}

/// Quantile Loss (Pinball Loss)
pub fn quantile_loss(actual: &[f64], forecast: &[f64], quantile: f64) -> Result<f64> {
    validate_inputs(actual, forecast)?;

    if !(0.0..=1.0).contains(&quantile) {
        return Err(ForecastError::InvalidInput(
            "Quantile must be between 0 and 1".to_string(),
        ));
    }

    let sum: f64 = actual
        .iter()
        .zip(forecast.iter())
        .map(|(a, f)| {
            let error = a - f;
            if error >= 0.0 {
                quantile * error
            } else {
                (quantile - 1.0) * error
            }
        })
        .sum();

    Ok(sum / actual.len() as f64)
}

/// Mean Quantile Loss (average across multiple quantiles)
pub fn mqloss(actual: &[f64], forecasts: &[Vec<f64>], quantiles: &[f64]) -> Result<f64> {
    if forecasts.len() != quantiles.len() {
        return Err(ForecastError::InvalidInput(
            "Number of forecasts must match number of quantiles".to_string(),
        ));
    }

    let mut total_loss = 0.0;
    for (forecast, &q) in forecasts.iter().zip(quantiles.iter()) {
        total_loss += quantile_loss(actual, forecast, q)?;
    }

    Ok(total_loss / quantiles.len() as f64)
}

/// Prediction Interval Coverage
pub fn coverage(actual: &[f64], lower: &[f64], upper: &[f64]) -> Result<f64> {
    if actual.len() != lower.len() || actual.len() != upper.len() {
        return Err(ForecastError::InvalidInput(
            "All arrays must have the same length".to_string(),
        ));
    }

    if actual.is_empty() {
        return Ok(f64::NAN);
    }

    let covered: usize = actual
        .iter()
        .zip(lower.iter())
        .zip(upper.iter())
        .filter(|((a, l), u)| *a >= *l && *a <= *u)
        .count();

    Ok(covered as f64 / actual.len() as f64)
}

fn validate_inputs(actual: &[f64], forecast: &[f64]) -> Result<()> {
    if actual.len() != forecast.len() {
        return Err(ForecastError::InvalidInput(format!(
            "Actual and forecast arrays must have the same length: {} vs {}",
            actual.len(),
            forecast.len()
        )));
    }
    if actual.is_empty() {
        return Err(ForecastError::InsufficientData { needed: 1, got: 0 });
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use approx::assert_relative_eq;

    #[test]
    fn test_mae() {
        let actual = vec![1.0, 2.0, 3.0, 4.0, 5.0];
        let forecast = vec![1.1, 2.2, 2.9, 4.1, 4.8];
        let result = mae(&actual, &forecast).unwrap();
        assert_relative_eq!(result, 0.14, epsilon = 0.01);
    }

    #[test]
    fn test_mse() {
        let actual = vec![1.0, 2.0, 3.0];
        let forecast = vec![1.0, 2.0, 4.0];
        let result = mse(&actual, &forecast).unwrap();
        assert_relative_eq!(result, 1.0 / 3.0, epsilon = 0.01);
    }

    #[test]
    fn test_r2_perfect() {
        let actual = vec![1.0, 2.0, 3.0, 4.0, 5.0];
        let forecast = vec![1.0, 2.0, 3.0, 4.0, 5.0];
        let result = r2(&actual, &forecast).unwrap();
        assert_relative_eq!(result, 1.0, epsilon = 0.001);
    }

    #[test]
    fn test_coverage() {
        let actual = vec![1.0, 2.0, 3.0, 4.0, 5.0];
        let lower = vec![0.5, 1.5, 2.5, 3.5, 4.5];
        let upper = vec![1.5, 2.5, 3.5, 4.5, 5.5];
        let result = coverage(&actual, &lower, &upper).unwrap();
        assert_relative_eq!(result, 1.0, epsilon = 0.001);
    }
}
