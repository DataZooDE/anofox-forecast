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
///
/// Policy (D-07): `actual`/`forecast` are the already pairwise-complete
/// evaluation pairs. If ANY pairwise-complete actual is exactly `0.0`, MAPE
/// is undefined (division by zero) and this returns `Ok(None)` rather than
/// silently excluding that pair (the pre-v2026.10.16 behavior) or propagating a
/// `NaN`. Use [`smape`] or [`mase`] for series with zero/near-zero actuals
/// (intermittent demand).
///
/// # Arguments
/// * `actual` - Slice of actual observed values
/// * `forecast` - Slice of forecasted/predicted values
///
/// # Returns
/// `Ok(Some(mape))` as a percentage, `Ok(None)` if any actual is exactly
/// zero, or an error if inputs are invalid.
///
/// # Formula
/// MAPE = (100/n) * Σ|actual_i - forecast_i| / |actual_i|
pub fn mape(actual: &[f64], forecast: &[f64]) -> Result<Option<f64>> {
    validate_inputs(actual, forecast)?;

    if actual.iter().any(|a| a.abs() < f64::EPSILON) {
        return Ok(None);
    }

    let sum: f64 = actual
        .iter()
        .zip(forecast.iter())
        .map(|(a, f)| ((a - f) / a).abs())
        .sum();
    Ok(Some(sum / actual.len() as f64 * 100.0))
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

/// Mean Absolute Scaled Error (Hyndman & Koehler 2006), matching R's
/// `forecast::accuracy()` MASE definition: the out-of-sample forecast MAE,
/// scaled by the IN-SAMPLE seasonal-naive MAE (lag = `seasonal_period`).
///
/// `actual`/`forecast` are the already pairwise-complete evaluation pairs
/// (both non-empty, equal length — enforced by [`validate_inputs`] via the
/// same MAE call this uses). `insample` is the in-sample (training) series
/// with its own missing-value positions preserved as `None`; the in-sample
/// scale is computed only over index pairs `(t, t - seasonal_period)` where
/// BOTH sides are present. Missing in-sample values are never spliced out
/// and re-paired — positions are preserved exactly as in R's
/// `mean(abs(diff(insample, lag = seasonal_period)), na.rm = TRUE)`.
///
/// Policy (D-08):
/// - `seasonal_period == 0` -> [`ForecastError::InvalidInput`].
/// - No usable lag pair in `insample` (after skipping missing-touching
///   pairs) -> [`ForecastError::InsufficientData`].
/// - The in-sample scale is (numerically) zero -> `Ok(None)`. R's
///   `forecast::accuracy()` returns `Inf` in this case; this product treats
///   MASE as undefined here rather than propagating an infinity.
/// - Otherwise -> `Ok(Some(MAE(actual, forecast) / scale))`.
///
/// This REPLACES the pre-v2026.10.16 `mase(actual, forecast, baseline)` relative-MAE
/// form (`MAE(actual, forecast) / MAE(actual, baseline)`), which was not MASE at
/// all — that computation survives unchanged as [`rmae`].
///
/// # References
/// Hyndman, R.J., & Koehler, A.B. (2006). "Another look at measures of
/// forecast accuracy." *International Journal of Forecasting*, 22(4), 679-688.
pub fn mase(
    actual: &[f64],
    forecast: &[f64],
    insample: &[Option<f64>],
    seasonal_period: usize,
) -> Result<Option<f64>> {
    if seasonal_period == 0 {
        return Err(ForecastError::InvalidInput(
            "seasonal_period must be >= 1".to_string(),
        ));
    }

    validate_inputs(actual, forecast)?;
    let forecast_mae = mae(actual, forecast)?;

    // Some(NaN) is a second missing-value sentinel across this codebase
    // (crate::missing convention) -- treat it like None here too, even
    // though `insample` here is built directly from a validity bitmask and
    // should not normally carry it.
    fn present(v: Option<f64>) -> Option<f64> {
        match v {
            Some(x) if !x.is_nan() => Some(x),
            _ => None,
        }
    }

    let mut sum = 0.0_f64;
    let mut count = 0usize;
    if insample.len() > seasonal_period {
        for t in seasonal_period..insample.len() {
            if let (Some(y_t), Some(y_tm)) =
                (present(insample[t]), present(insample[t - seasonal_period]))
            {
                sum += (y_t - y_tm).abs();
                count += 1;
            }
        }
    }

    if count == 0 {
        return Err(ForecastError::InsufficientData {
            needed: seasonal_period + 1,
            got: insample.len(),
        });
    }

    let scale = sum / count as f64;

    if scale.abs() < f64::EPSILON {
        return Ok(None);
    }

    Ok(Some(forecast_mae / scale))
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
        let result = mape(&actual, &forecast).unwrap().unwrap();
        assert_relative_eq!(result, 10.0, epsilon = 0.1);
    }

    #[test]
    fn test_mape_any_zero_actual_is_none() {
        // D-07: a single zero actual makes the whole result undefined (None),
        // not a silent per-pair exclusion.
        let actual = vec![0.0, 100.0, 200.0];
        let forecast = vec![10.0, 110.0, 180.0];
        let result = mape(&actual, &forecast).unwrap();
        assert_eq!(result, None);
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
        assert!((0.0..=200.0).contains(&result));

        // Perfect forecast should give 0
        let perfect = smape(&actual, &actual).unwrap();
        assert_relative_eq!(perfect, 0.0, epsilon = 0.001);
    }

    #[test]
    fn test_mase_good_forecast_scores_below_one() {
        // insample is a clean lag-1 ramp (diffs of 1.0 throughout) -> scale 1.0.
        let actual = vec![6.0, 7.0, 8.0];
        let forecast = vec![6.1, 7.1, 8.1]; // near-perfect forecast
        let insample: Vec<Option<f64>> = (1..=5).map(|v| Some(v as f64)).collect();

        let result = mase(&actual, &forecast, &insample, 1).unwrap().unwrap();
        assert!(result < 1.0);
    }

    #[test]
    fn test_mase_matches_manual_scale_computation() {
        let actual = vec![6.0, 7.0, 8.0];
        let forecast = vec![7.0, 8.0, 9.0]; // off by exactly 1.0 each step
        let insample: Vec<Option<f64>> = (1..=5).map(|v| Some(v as f64)).collect();

        // scale = mean(|diff(insample, lag=1)|) = 1.0; MAE(actual, forecast) = 1.0.
        let result = mase(&actual, &forecast, &insample, 1).unwrap().unwrap();
        assert_relative_eq!(result, 1.0, epsilon = 0.001);
    }

    #[test]
    fn test_mase_seasonal_period_zero_is_invalid_input() {
        let actual = vec![1.0, 2.0];
        let forecast = vec![1.0, 2.0];
        let insample: Vec<Option<f64>> = vec![Some(1.0), Some(2.0), Some(3.0)];

        assert!(mase(&actual, &forecast, &insample, 0).is_err());
    }

    #[test]
    fn test_mase_constant_insample_scale_zero_is_none() {
        let actual = vec![5.0, 6.0];
        let forecast = vec![4.0, 5.0];
        let insample: Vec<Option<f64>> = vec![Some(3.0); 20];

        let result = mase(&actual, &forecast, &insample, 1).unwrap();
        assert_eq!(result, None);
    }

    #[test]
    fn test_mase_insample_missing_lag_pair_is_skipped_not_spliced() {
        // insample [1, None, 3, 10] lag=1: pairs (1,None)=skip, (None,3)=skip,
        // (3,10)=keep (diff 7.0) -- never (1,3) which splicing would produce.
        let actual = vec![10.0];
        let forecast = vec![11.0];
        let insample: Vec<Option<f64>> = vec![Some(1.0), None, Some(3.0), Some(10.0)];

        let result = mase(&actual, &forecast, &insample, 1).unwrap().unwrap();
        assert_relative_eq!(result, 1.0 / 7.0, epsilon = 0.001);
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
