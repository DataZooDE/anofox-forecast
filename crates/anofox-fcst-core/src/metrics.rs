//! Evaluation metrics for time series forecasting.
//!
//! This module provides 12 standard metrics for evaluating forecast accuracy:
//!
//! - **Scale-dependent metrics**: MAE, MSE, RMSE, Bias
//! - **Percentage metrics**: MAPE, sMAPE
//! - **Scaled metrics**: MASE, rMAE
//! - **Other metrics**: R², Quantile Loss, MQ-Loss, Coverage
//!
//! # Metric Selection Guide
//!
//! | Metric | Use When |
//! |--------|----------|
//! | MAE | Need interpretable error in original units |
//! | RMSE | Want to penalize large errors more heavily |
//! | MAPE | Need percentage-based comparison across scales |
//! | sMAPE | MAPE but symmetric around zero |
//! | MASE | Comparing forecasts across different series |
//! | R² | Need explained variance proportion |

use crate::error::{ForecastError, Result};

/// Calculates Mean Absolute Error between actual and predicted values.
///
/// MAE measures the average magnitude of errors without considering direction.
/// It gives equal weight to all individual differences.
///
/// # Arguments
/// * `actual` - Slice of actual observed values
/// * `forecast` - Slice of forecasted/predicted values
///
/// # Returns
/// The mean absolute error, or an error if inputs are invalid
///
/// # Formula
/// MAE = (1/n) * Σ|actual_i - forecast_i|
///
/// # Example
/// ```
/// use anofox_fcst_core::metrics::mae;
/// let actual = vec![1.0, 2.0, 3.0];
/// let forecast = vec![1.1, 2.2, 2.8];
/// let error = mae(&actual, &forecast).unwrap();
/// assert!((error - 0.166).abs() < 0.01);
/// ```
pub fn mae(actual: &[f64], forecast: &[f64]) -> Result<f64> {
    validate_inputs(actual, forecast)?;
    let sum: f64 = actual
        .iter()
        .zip(forecast.iter())
        .map(|(a, f)| (a - f).abs())
        .sum();
    Ok(sum / actual.len() as f64)
}

/// Calculates Mean Squared Error between actual and predicted values.
///
/// MSE penalizes larger errors more heavily than smaller ones due to squaring.
/// Useful when large errors are particularly undesirable.
///
/// # Arguments
/// * `actual` - Slice of actual observed values
/// * `forecast` - Slice of forecasted/predicted values
///
/// # Returns
/// The mean squared error, or an error if inputs are invalid
///
/// # Formula
/// MSE = (1/n) * Σ(actual_i - forecast_i)²
pub fn mse(actual: &[f64], forecast: &[f64]) -> Result<f64> {
    validate_inputs(actual, forecast)?;
    let sum: f64 = actual
        .iter()
        .zip(forecast.iter())
        .map(|(a, f)| (a - f).powi(2))
        .sum();
    Ok(sum / actual.len() as f64)
}

/// Calculates Root Mean Squared Error between actual and predicted values.
///
/// RMSE is the square root of MSE, returning error in the original units.
/// More interpretable than MSE while still penalizing large errors.
///
/// # Arguments
/// * `actual` - Slice of actual observed values
/// * `forecast` - Slice of forecasted/predicted values
///
/// # Returns
/// The root mean squared error, or an error if inputs are invalid
///
/// # Formula
/// RMSE = √MSE = √[(1/n) * Σ(actual_i - forecast_i)²]
pub fn rmse(actual: &[f64], forecast: &[f64]) -> Result<f64> {
    Ok(mse(actual, forecast)?.sqrt())
}

/// Calculates Mean Absolute Percentage Error.
///
/// MAPE expresses error as a percentage of the actual values.
/// Useful for comparing forecast accuracy across different scales.
/// Note: Returns NaN if all actual values are zero.
///
/// # Arguments
/// * `actual` - Slice of actual observed values (non-zero values used)
/// * `forecast` - Slice of forecasted/predicted values
///
/// # Returns
/// The MAPE as a percentage (0-100+), or an error if inputs are invalid
///
/// # Formula
/// MAPE = (100/n) * Σ|actual_i - forecast_i| / |actual_i|
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

/// Calculates Symmetric Mean Absolute Percentage Error.
///
/// sMAPE is a symmetric version of MAPE that treats over- and under-predictions
/// equally. Values range from 0% (perfect) to 200% (maximum error).
///
/// # Arguments
/// * `actual` - Slice of actual observed values
/// * `forecast` - Slice of forecasted/predicted values
///
/// # Returns
/// The sMAPE as a percentage (0-200), or an error if inputs are invalid
///
/// # Formula
/// sMAPE = (100/n) * Σ 2|actual_i - forecast_i| / (|actual_i| + |forecast_i|)
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

/// Calculates Forecast Bias (mean error).
///
/// Bias indicates systematic over- or under-prediction.
/// Positive bias means forecasts are too high on average.
/// Negative bias means forecasts are too low on average.
///
/// # Arguments
/// * `actual` - Slice of actual observed values
/// * `forecast` - Slice of forecasted/predicted values
///
/// # Returns
/// The mean bias (forecast - actual), or an error if inputs are invalid
///
/// # Formula
/// Bias = (1/n) * Σ(forecast_i - actual_i)
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

/// Calculates Quantile Loss (Pinball Loss) for probabilistic forecasts.
///
/// Quantile loss penalizes over- and under-predictions asymmetrically
/// based on the target quantile. Useful for prediction intervals.
///
/// # Arguments
/// * `actual` - Slice of actual observed values
/// * `forecast` - Slice of forecasted quantile values
/// * `quantile` - The target quantile (0.0 to 1.0, e.g., 0.5 for median)
///
/// # Returns
/// The mean quantile loss, or an error if inputs are invalid
///
/// # Formula
/// QL = (1/n) * Σ ρ_q(actual_i - forecast_i)
/// where ρ_q(e) = q*e if e >= 0, (q-1)*e otherwise
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

/// Calculates Mean Quantile Loss across multiple quantiles.
///
/// MQ-Loss averages the quantile loss across multiple prediction quantiles,
/// providing a comprehensive measure of probabilistic forecast accuracy.
///
/// # Arguments
/// * `actual` - Slice of actual observed values
/// * `forecasts` - Vector of forecasts, one per quantile
/// * `quantiles` - Slice of target quantiles (each 0.0 to 1.0)
///
/// # Returns
/// The mean quantile loss across all quantiles, or an error if inputs are invalid
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

/// Calculates Prediction Interval Coverage.
///
/// Coverage measures the proportion of actual values that fall within
/// the predicted confidence intervals. A 95% confidence interval should
/// achieve approximately 95% coverage on well-calibrated forecasts.
///
/// # Arguments
/// * `actual` - Slice of actual observed values
/// * `lower` - Slice of lower bounds of prediction intervals
/// * `upper` - Slice of upper bounds of prediction intervals
///
/// # Returns
/// The coverage proportion (0.0 to 1.0), or an error if inputs are invalid
///
/// # Formula
/// Coverage = (1/n) * Σ I(lower_i <= actual_i <= upper_i)
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

    #[test]
    fn test_rmse() {
        let actual = vec![1.0, 2.0, 3.0];
        let forecast = vec![1.0, 2.0, 4.0];
        let result = rmse(&actual, &forecast).unwrap();
        // MSE = 1/3, RMSE = sqrt(1/3) ≈ 0.577
        assert_relative_eq!(result, (1.0_f64 / 3.0).sqrt(), epsilon = 0.001);
    }

    #[test]
    fn test_mape() {
        let actual = vec![100.0, 200.0, 300.0];
        let forecast = vec![110.0, 180.0, 330.0];
        // Errors: 10%, 10%, 10% -> MAPE = 10%
        let result = mape(&actual, &forecast).unwrap();
        assert_relative_eq!(result, 10.0, epsilon = 0.1);
    }

    #[test]
    fn test_mape_with_zeros() {
        // MAPE should handle zeros in actual values
        let actual = vec![0.0, 100.0, 200.0];
        let forecast = vec![10.0, 110.0, 180.0];
        // Only non-zero actuals counted: errors 10%, 10%
        let result = mape(&actual, &forecast).unwrap();
        assert!(result.is_finite());
        assert!(result > 0.0);
    }

    #[test]
    fn test_smape() {
        let actual = vec![100.0, 200.0, 300.0];
        let forecast = vec![110.0, 180.0, 330.0];
        let result = smape(&actual, &forecast).unwrap();
        // sMAPE should be positive and less than 200
        assert!(result > 0.0 && result < 200.0);
    }

    #[test]
    fn test_smape_bounded() {
        // sMAPE should be bounded between 0 and 200
        let actual = vec![100.0, 50.0, 25.0];
        let forecast = vec![200.0, 10.0, 100.0]; // Large errors

        let result = smape(&actual, &forecast).unwrap();
        assert!(result >= 0.0 && result <= 200.0);

        // Perfect forecast should give 0
        let perfect = smape(&actual, &actual).unwrap();
        assert_relative_eq!(perfect, 0.0, epsilon = 0.001);
    }

    #[test]
    fn test_mase() {
        let actual = vec![1.0, 2.0, 3.0, 4.0, 5.0];
        let forecast = vec![1.1, 2.1, 3.1, 4.1, 5.1]; // Good forecast
        let baseline = vec![1.5, 2.5, 3.5, 4.5, 5.5]; // Worse baseline

        let result = mase(&actual, &forecast, &baseline).unwrap();
        // Forecast is better than baseline, so MASE < 1
        assert!(result < 1.0);
    }

    #[test]
    fn test_mase_equal_performance() {
        let actual = vec![1.0, 2.0, 3.0];
        let forecast = vec![1.5, 2.5, 3.5];
        let baseline = vec![1.5, 2.5, 3.5]; // Same as forecast

        let result = mase(&actual, &forecast, &baseline).unwrap();
        assert_relative_eq!(result, 1.0, epsilon = 0.001);
    }

    #[test]
    fn test_bias_positive() {
        let actual = vec![1.0, 2.0, 3.0];
        let forecast = vec![2.0, 3.0, 4.0]; // Forecasts are 1.0 too high

        let result = bias(&actual, &forecast).unwrap();
        assert_relative_eq!(result, 1.0, epsilon = 0.001);
    }

    #[test]
    fn test_bias_negative() {
        let actual = vec![1.0, 2.0, 3.0];
        let forecast = vec![0.0, 1.0, 2.0]; // Forecasts are 1.0 too low

        let result = bias(&actual, &forecast).unwrap();
        assert_relative_eq!(result, -1.0, epsilon = 0.001);
    }

    #[test]
    fn test_bias_unbiased() {
        let actual = vec![1.0, 2.0, 3.0];
        let forecast = vec![1.0, 2.0, 3.0]; // Perfect forecast

        let result = bias(&actual, &forecast).unwrap();
        assert_relative_eq!(result, 0.0, epsilon = 0.001);
    }

    #[test]
    fn test_rmae() {
        let actual = vec![1.0, 2.0, 3.0, 4.0, 5.0];
        let pred1 = vec![1.1, 2.1, 3.1, 4.1, 5.1]; // Good model
        let pred2 = vec![1.5, 2.5, 3.5, 4.5, 5.5]; // Worse model

        let result = rmae(&actual, &pred1, &pred2).unwrap();
        // pred1 is better, so rMAE < 1
        assert!(result < 1.0);
    }

    #[test]
    fn test_quantile_loss_median() {
        let actual = vec![1.0, 2.0, 3.0, 4.0, 5.0];
        let forecast = vec![1.0, 2.0, 3.0, 4.0, 5.0]; // Perfect

        let result = quantile_loss(&actual, &forecast, 0.5).unwrap();
        assert_relative_eq!(result, 0.0, epsilon = 0.001);
    }

    #[test]
    fn test_quantile_loss_asymmetric() {
        let actual = vec![10.0];
        let low_forecast = vec![8.0]; // Under-prediction
        let high_forecast = vec![12.0]; // Over-prediction

        // At q=0.9, under-prediction is penalized more
        let loss_low = quantile_loss(&actual, &low_forecast, 0.9).unwrap();
        let loss_high = quantile_loss(&actual, &high_forecast, 0.9).unwrap();
        assert!(loss_low > loss_high);

        // At q=0.1, over-prediction is penalized more
        let loss_low_01 = quantile_loss(&actual, &low_forecast, 0.1).unwrap();
        let loss_high_01 = quantile_loss(&actual, &high_forecast, 0.1).unwrap();
        assert!(loss_high_01 > loss_low_01);
    }

    #[test]
    fn test_quantile_loss_invalid_quantile() {
        let actual = vec![1.0, 2.0, 3.0];
        let forecast = vec![1.0, 2.0, 3.0];

        assert!(quantile_loss(&actual, &forecast, -0.1).is_err());
        assert!(quantile_loss(&actual, &forecast, 1.5).is_err());
    }

    #[test]
    fn test_mqloss() {
        let actual = vec![1.0, 2.0, 3.0, 4.0, 5.0];
        let forecasts = vec![
            vec![0.5, 1.5, 2.5, 3.5, 4.5], // q=0.1 forecast
            vec![1.0, 2.0, 3.0, 4.0, 5.0], // q=0.5 forecast (median)
            vec![1.5, 2.5, 3.5, 4.5, 5.5], // q=0.9 forecast
        ];
        let quantiles = vec![0.1, 0.5, 0.9];

        let result = mqloss(&actual, &forecasts, &quantiles).unwrap();
        assert!(result >= 0.0);
    }

    #[test]
    fn test_coverage_partial() {
        let actual = vec![1.0, 2.0, 3.0, 10.0, 5.0]; // 10.0 is outside
        let lower = vec![0.5, 1.5, 2.5, 3.5, 4.5];
        let upper = vec![1.5, 2.5, 3.5, 4.5, 5.5];

        let result = coverage(&actual, &lower, &upper).unwrap();
        // 4 out of 5 are covered
        assert_relative_eq!(result, 0.8, epsilon = 0.001);
    }

    #[test]
    fn test_validate_inputs_length_mismatch() {
        let actual = vec![1.0, 2.0, 3.0];
        let forecast = vec![1.0, 2.0];

        assert!(mae(&actual, &forecast).is_err());
        assert!(mse(&actual, &forecast).is_err());
    }

    #[test]
    fn test_validate_inputs_empty() {
        let actual: Vec<f64> = vec![];
        let forecast: Vec<f64> = vec![];

        assert!(mae(&actual, &forecast).is_err());
    }
}
