//! Conformal prediction for distribution-free prediction intervals.
//!
//! This module implements conformal prediction methods that provide valid
//! prediction intervals without distributional assumptions. The intervals
//! have guaranteed finite-sample coverage under exchangeability.
//!
//! # Methods
//!
//! - **Split Conformal**: Uses a calibration set of residuals to compute intervals
//! - **Cross-Validation Conformal**: Uses CV residuals for better calibration
//! - **Adaptive Conformal**: Locally-weighted intervals based on estimated difficulty
//!
//! # Example
//!
//! ```
//! use anofox_fcst_core::conformal::{conformal_quantile, conformal_intervals};
//!
//! // Calibration residuals from validation set
//! let residuals = vec![0.5, -0.3, 0.8, -0.2, 0.4, -0.6, 0.3, -0.4, 0.7, -0.5];
//!
//! // Compute conformity score for 90% coverage (alpha = 0.1)
//! let q = conformal_quantile(&residuals, 0.1).unwrap();
//!
//! // Apply to point forecasts
//! let forecasts = vec![100.0, 105.0, 110.0];
//! let intervals = conformal_intervals(&forecasts, q);
//!
//! // intervals.lower = [100.0 - q, 105.0 - q, 110.0 - q]
//! // intervals.upper = [100.0 + q, 105.0 + q, 110.0 + q]
//! ```

use crate::error::{ForecastError, Result};

/// Result of conformal prediction containing intervals and metadata.
#[derive(Debug, Clone)]
pub struct ConformalResult {
    /// Point forecasts (unchanged from input)
    pub point: Vec<f64>,
    /// Lower bounds of prediction intervals
    pub lower: Vec<f64>,
    /// Upper bounds of prediction intervals
    pub upper: Vec<f64>,
    /// Nominal coverage level (1 - alpha)
    pub coverage: f64,
    /// The computed conformity score (quantile threshold)
    pub conformity_score: f64,
    /// Method used for conformal prediction
    pub method: String,
}

/// Result of conformal prediction with multiple coverage levels.
#[derive(Debug, Clone)]
pub struct ConformalMultiResult {
    /// Point forecasts (unchanged from input)
    pub point: Vec<f64>,
    /// Intervals for each coverage level
    pub intervals: Vec<ConformalInterval>,
}

/// A single prediction interval at a specific coverage level.
#[derive(Debug, Clone)]
pub struct ConformalInterval {
    /// Nominal coverage level (1 - alpha)
    pub coverage: f64,
    /// Lower bounds
    pub lower: Vec<f64>,
    /// Upper bounds
    pub upper: Vec<f64>,
    /// Conformity score used
    pub conformity_score: f64,
}

/// Computes the conformity score (quantile) from calibration residuals.
///
/// The conformity score is the (1 - alpha) quantile of the absolute residuals,
/// which ensures that the resulting prediction intervals have at least
/// (1 - alpha) coverage probability.
///
/// # Arguments
/// * `residuals` - Calibration residuals (actual - predicted)
/// * `alpha` - Miscoverage rate (e.g., 0.1 for 90% coverage)
///
/// # Returns
/// The conformity score threshold, or an error if inputs are invalid.
///
/// # Theory
/// For split conformal prediction, if we have n calibration points and use
/// the ceil((n+1)(1-alpha))/n quantile of |residuals|, the resulting intervals
/// have coverage >= 1-alpha for any exchangeable test point.
///
/// # Example
/// ```
/// use anofox_fcst_core::conformal::conformal_quantile;
///
/// let residuals = vec![0.5, -0.3, 0.8, -0.2, 0.4];
/// let q = conformal_quantile(&residuals, 0.1).unwrap();
/// assert!(q > 0.0);
/// ```
pub fn conformal_quantile(residuals: &[f64], alpha: f64) -> Result<f64> {
    if residuals.is_empty() {
        return Err(ForecastError::InsufficientData { needed: 1, got: 0 });
    }

    if !(0.0..1.0).contains(&alpha) {
        return Err(ForecastError::InvalidInput(
            "Alpha must be between 0 and 1 (exclusive)".to_string(),
        ));
    }

    // Compute absolute residuals (conformity scores)
    let mut abs_residuals: Vec<f64> = residuals.iter().map(|r| r.abs()).collect();

    // Sort for quantile computation
    abs_residuals.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));

    // Compute the quantile index using the finite-sample correction
    // q = ceil((n+1)(1-alpha)) / n
    let n = abs_residuals.len() as f64;
    let quantile_level = ((n + 1.0) * (1.0 - alpha)).ceil() / n;

    // Clamp to valid range [0, 1]
    let quantile_level = quantile_level.clamp(0.0, 1.0);

    // Compute quantile using linear interpolation
    let quantile = compute_quantile(&abs_residuals, quantile_level);

    Ok(quantile)
}

/// Applies a conformity score to point forecasts to create prediction intervals.
///
/// Creates symmetric intervals around the point forecasts using the
/// conformity score as the half-width.
///
/// # Arguments
/// * `forecasts` - Point forecasts
/// * `conformity_score` - The threshold from `conformal_quantile`
///
/// # Returns
/// A tuple of (lower_bounds, upper_bounds)
///
/// # Example
/// ```
/// use anofox_fcst_core::conformal::conformal_intervals;
///
/// let forecasts = vec![100.0, 105.0, 110.0];
/// let (lower, upper) = conformal_intervals(&forecasts, 5.0);
///
/// assert_eq!(lower, vec![95.0, 100.0, 105.0]);
/// assert_eq!(upper, vec![105.0, 110.0, 115.0]);
/// ```
pub fn conformal_intervals(forecasts: &[f64], conformity_score: f64) -> (Vec<f64>, Vec<f64>) {
    let lower: Vec<f64> = forecasts.iter().map(|f| f - conformity_score).collect();
    let upper: Vec<f64> = forecasts.iter().map(|f| f + conformity_score).collect();
    (lower, upper)
}

/// Performs split conformal prediction in one step.
///
/// Computes the conformity score from calibration residuals and applies it
/// to point forecasts to create prediction intervals.
///
/// # Arguments
/// * `residuals` - Calibration residuals (actual - predicted)
/// * `forecasts` - Point forecasts to wrap with intervals
/// * `alpha` - Miscoverage rate (e.g., 0.1 for 90% coverage)
///
/// # Returns
/// A `ConformalResult` containing intervals and metadata
///
/// # Example
/// ```
/// use anofox_fcst_core::conformal::conformal_predict;
///
/// let residuals = vec![0.5, -0.3, 0.8, -0.2, 0.4, -0.6, 0.3, -0.4, 0.7, -0.5];
/// let forecasts = vec![100.0, 105.0, 110.0];
///
/// let result = conformal_predict(&residuals, &forecasts, 0.1).unwrap();
///
/// assert_eq!(result.point, forecasts);
/// assert_eq!(result.coverage, 0.9);
/// assert_eq!(result.lower.len(), 3);
/// assert_eq!(result.upper.len(), 3);
/// ```
pub fn conformal_predict(
    residuals: &[f64],
    forecasts: &[f64],
    alpha: f64,
) -> Result<ConformalResult> {
    let conformity_score = conformal_quantile(residuals, alpha)?;
    let (lower, upper) = conformal_intervals(forecasts, conformity_score);

    Ok(ConformalResult {
        point: forecasts.to_vec(),
        lower,
        upper,
        coverage: 1.0 - alpha,
        conformity_score,
        method: "split_conformal".to_string(),
    })
}

/// Performs conformal prediction with multiple coverage levels.
///
/// Computes prediction intervals at multiple significance levels simultaneously,
/// which is more efficient than calling `conformal_predict` multiple times.
///
/// # Arguments
/// * `residuals` - Calibration residuals (actual - predicted)
/// * `forecasts` - Point forecasts to wrap with intervals
/// * `alphas` - Vector of miscoverage rates (e.g., [0.5, 0.2, 0.1, 0.05] for 50%, 80%, 90%, 95% coverage)
///
/// # Returns
/// A `ConformalMultiResult` containing intervals for each coverage level
///
/// # Example
/// ```
/// use anofox_fcst_core::conformal::conformal_predict_multi;
///
/// let residuals = vec![0.5, -0.3, 0.8, -0.2, 0.4, -0.6, 0.3, -0.4, 0.7, -0.5];
/// let forecasts = vec![100.0, 105.0, 110.0];
/// let alphas = vec![0.5, 0.2, 0.1, 0.05];
///
/// let result = conformal_predict_multi(&residuals, &forecasts, &alphas).unwrap();
///
/// assert_eq!(result.intervals.len(), 4);
/// // Wider coverage should have wider intervals
/// assert!(result.intervals[0].conformity_score < result.intervals[3].conformity_score);
/// ```
pub fn conformal_predict_multi(
    residuals: &[f64],
    forecasts: &[f64],
    alphas: &[f64],
) -> Result<ConformalMultiResult> {
    if alphas.is_empty() {
        return Err(ForecastError::InvalidInput(
            "At least one alpha value is required".to_string(),
        ));
    }

    let mut intervals = Vec::with_capacity(alphas.len());

    for &alpha in alphas {
        let conformity_score = conformal_quantile(residuals, alpha)?;
        let (lower, upper) = conformal_intervals(forecasts, conformity_score);

        intervals.push(ConformalInterval {
            coverage: 1.0 - alpha,
            lower,
            upper,
            conformity_score,
        });
    }

    Ok(ConformalMultiResult {
        point: forecasts.to_vec(),
        intervals,
    })
}

/// Computes locally-adaptive conformal intervals using estimated difficulty.
///
/// Instead of using a single conformity score for all predictions, this method
/// scales the intervals based on local difficulty estimates. Points predicted
/// to be more difficult (higher uncertainty) get wider intervals.
///
/// # Arguments
/// * `residuals` - Calibration residuals (actual - predicted)
/// * `forecasts` - Point forecasts to wrap with intervals
/// * `difficulty` - Estimated difficulty/uncertainty for each forecast (must be positive)
/// * `alpha` - Miscoverage rate (e.g., 0.1 for 90% coverage)
///
/// # Returns
/// A `ConformalResult` with adaptive intervals
///
/// # Note
/// The difficulty scores should reflect the expected prediction uncertainty.
/// Common choices include:
/// - Residual variance estimates from similar training points
/// - Distance to training data in feature space
/// - Model uncertainty estimates (e.g., ensemble variance)
pub fn conformal_predict_adaptive(
    residuals: &[f64],
    forecasts: &[f64],
    difficulty: &[f64],
    alpha: f64,
) -> Result<ConformalResult> {
    if forecasts.len() != difficulty.len() {
        return Err(ForecastError::InvalidInput(format!(
            "Forecasts and difficulty must have the same length: {} vs {}",
            forecasts.len(),
            difficulty.len()
        )));
    }

    // Validate difficulty scores are positive
    if difficulty.iter().any(|&d| d <= 0.0) {
        return Err(ForecastError::InvalidInput(
            "Difficulty scores must be positive".to_string(),
        ));
    }

    // Compute base conformity score
    let base_score = conformal_quantile(residuals, alpha)?;

    // Normalize difficulty scores (mean = 1)
    let mean_difficulty: f64 = difficulty.iter().sum::<f64>() / difficulty.len() as f64;
    let normalized: Vec<f64> = difficulty.iter().map(|d| d / mean_difficulty).collect();

    // Scale intervals by normalized difficulty
    let lower: Vec<f64> = forecasts
        .iter()
        .zip(normalized.iter())
        .map(|(f, d)| f - base_score * d)
        .collect();

    let upper: Vec<f64> = forecasts
        .iter()
        .zip(normalized.iter())
        .map(|(f, d)| f + base_score * d)
        .collect();

    Ok(ConformalResult {
        point: forecasts.to_vec(),
        lower,
        upper,
        coverage: 1.0 - alpha,
        conformity_score: base_score,
        method: "adaptive_conformal".to_string(),
    })
}

/// Computes asymmetric conformal intervals using signed residuals.
///
/// Unlike standard conformal prediction which creates symmetric intervals,
/// this method uses separate quantiles for positive and negative residuals,
/// allowing for asymmetric intervals that better capture skewed distributions.
///
/// # Arguments
/// * `residuals` - Calibration residuals (actual - predicted), signed
/// * `forecasts` - Point forecasts to wrap with intervals
/// * `alpha` - Miscoverage rate (e.g., 0.1 for 90% coverage)
///
/// # Returns
/// A `ConformalResult` with potentially asymmetric intervals
pub fn conformal_predict_asymmetric(
    residuals: &[f64],
    forecasts: &[f64],
    alpha: f64,
) -> Result<ConformalResult> {
    if residuals.is_empty() {
        return Err(ForecastError::InsufficientData { needed: 1, got: 0 });
    }

    if !(0.0..1.0).contains(&alpha) {
        return Err(ForecastError::InvalidInput(
            "Alpha must be between 0 and 1 (exclusive)".to_string(),
        ));
    }

    // Separate positive and negative residuals
    let positive: Vec<f64> = residuals.iter().filter(|&&r| r > 0.0).copied().collect();
    let negative: Vec<f64> = residuals
        .iter()
        .filter(|&&r| r < 0.0)
        .map(|&r| r.abs())
        .collect();

    // Compute quantiles for each direction
    // Use alpha/2 for each tail to maintain overall coverage
    let alpha_half = alpha / 2.0;

    let upper_margin = if positive.is_empty() {
        0.0
    } else {
        let mut sorted = positive.clone();
        sorted.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));
        let n = sorted.len() as f64;
        let q = ((n + 1.0) * (1.0 - alpha_half)).ceil() / n;
        compute_quantile(&sorted, q.clamp(0.0, 1.0))
    };

    let lower_margin = if negative.is_empty() {
        0.0
    } else {
        let mut sorted = negative.clone();
        sorted.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));
        let n = sorted.len() as f64;
        let q = ((n + 1.0) * (1.0 - alpha_half)).ceil() / n;
        compute_quantile(&sorted, q.clamp(0.0, 1.0))
    };

    let lower: Vec<f64> = forecasts.iter().map(|f| f - lower_margin).collect();
    let upper: Vec<f64> = forecasts.iter().map(|f| f + upper_margin).collect();

    // Report average margin as conformity score
    let avg_margin = (upper_margin + lower_margin) / 2.0;

    Ok(ConformalResult {
        point: forecasts.to_vec(),
        lower,
        upper,
        coverage: 1.0 - alpha,
        conformity_score: avg_margin,
        method: "asymmetric_conformal".to_string(),
    })
}

/// Computes quantile from sorted data using linear interpolation.
fn compute_quantile(sorted_data: &[f64], quantile: f64) -> f64 {
    if sorted_data.is_empty() {
        return f64::NAN;
    }

    if quantile <= 0.0 {
        return sorted_data[0];
    }

    if quantile >= 1.0 {
        return sorted_data[sorted_data.len() - 1];
    }

    let n = sorted_data.len();
    let index = quantile * (n - 1) as f64;
    let lower_idx = index.floor() as usize;
    let upper_idx = (lower_idx + 1).min(n - 1);
    let fraction = index - lower_idx as f64;

    sorted_data[lower_idx] * (1.0 - fraction) + sorted_data[upper_idx] * fraction
}

/// Computes the interval width (upper - lower) for each prediction.
///
/// Useful for analyzing interval characteristics.
pub fn interval_width(lower: &[f64], upper: &[f64]) -> Vec<f64> {
    lower.iter().zip(upper.iter()).map(|(l, u)| u - l).collect()
}

/// Computes the mean interval width.
pub fn mean_interval_width(lower: &[f64], upper: &[f64]) -> f64 {
    let widths = interval_width(lower, upper);
    if widths.is_empty() {
        return f64::NAN;
    }
    widths.iter().sum::<f64>() / widths.len() as f64
}

#[cfg(test)]
mod tests {
    use super::*;
    use approx::assert_relative_eq;

    #[test]
    fn test_conformal_quantile_basic() {
        // 10 residuals, sorted absolute values: 0.2, 0.3, 0.3, 0.4, 0.4, 0.5, 0.5, 0.6, 0.7, 0.8
        let residuals = vec![0.5, -0.3, 0.8, -0.2, 0.4, -0.6, 0.3, -0.4, 0.7, -0.5];
        let q = conformal_quantile(&residuals, 0.1).unwrap();

        // For n=10, alpha=0.1: ceil(11*0.9)/10 = ceil(9.9)/10 = 10/10 = 1.0
        // So we should get the maximum absolute residual
        assert!(q > 0.0);
        assert!(q <= 0.8 + 0.001); // Should be close to max
    }

    #[test]
    fn test_conformal_quantile_50_percent() {
        let residuals = vec![1.0, -1.0, 2.0, -2.0, 3.0, -3.0, 4.0, -4.0, 5.0, -5.0];
        let q = conformal_quantile(&residuals, 0.5).unwrap();

        // For 50% coverage, should be around median of absolute values
        assert!(q >= 2.0 && q <= 4.0);
    }

    #[test]
    fn test_conformal_quantile_invalid_alpha() {
        let residuals = vec![1.0, 2.0, 3.0];

        assert!(conformal_quantile(&residuals, -0.1).is_err());
        assert!(conformal_quantile(&residuals, 1.0).is_err());
        assert!(conformal_quantile(&residuals, 1.5).is_err());
    }

    #[test]
    fn test_conformal_quantile_empty() {
        let residuals: Vec<f64> = vec![];
        assert!(conformal_quantile(&residuals, 0.1).is_err());
    }

    #[test]
    fn test_conformal_intervals() {
        let forecasts = vec![100.0, 105.0, 110.0];
        let (lower, upper) = conformal_intervals(&forecasts, 5.0);

        assert_eq!(lower, vec![95.0, 100.0, 105.0]);
        assert_eq!(upper, vec![105.0, 110.0, 115.0]);
    }

    #[test]
    fn test_conformal_predict() {
        let residuals = vec![0.5, -0.3, 0.8, -0.2, 0.4, -0.6, 0.3, -0.4, 0.7, -0.5];
        let forecasts = vec![100.0, 105.0, 110.0];

        let result = conformal_predict(&residuals, &forecasts, 0.1).unwrap();

        assert_eq!(result.point, forecasts);
        assert_relative_eq!(result.coverage, 0.9, epsilon = 0.001);
        assert_eq!(result.lower.len(), 3);
        assert_eq!(result.upper.len(), 3);
        assert!(result.conformity_score > 0.0);
        assert_eq!(result.method, "split_conformal");

        // Verify symmetry
        for i in 0..3 {
            let width = result.upper[i] - result.lower[i];
            assert_relative_eq!(width, 2.0 * result.conformity_score, epsilon = 0.001);
        }
    }

    #[test]
    fn test_conformal_predict_multi() {
        let residuals = vec![0.5, -0.3, 0.8, -0.2, 0.4, -0.6, 0.3, -0.4, 0.7, -0.5];
        let forecasts = vec![100.0, 105.0, 110.0];
        let alphas = vec![0.5, 0.2, 0.1, 0.05];

        let result = conformal_predict_multi(&residuals, &forecasts, &alphas).unwrap();

        assert_eq!(result.point, forecasts);
        assert_eq!(result.intervals.len(), 4);

        // Verify coverage levels
        assert_relative_eq!(result.intervals[0].coverage, 0.5, epsilon = 0.001);
        assert_relative_eq!(result.intervals[1].coverage, 0.8, epsilon = 0.001);
        assert_relative_eq!(result.intervals[2].coverage, 0.9, epsilon = 0.001);
        assert_relative_eq!(result.intervals[3].coverage, 0.95, epsilon = 0.001);

        // Wider coverage should have wider intervals (larger conformity score)
        for i in 0..3 {
            assert!(
                result.intervals[i].conformity_score <= result.intervals[i + 1].conformity_score
            );
        }
    }

    #[test]
    fn test_conformal_predict_adaptive() {
        let residuals = vec![0.5, -0.3, 0.8, -0.2, 0.4, -0.6, 0.3, -0.4, 0.7, -0.5];
        let forecasts = vec![100.0, 105.0, 110.0];
        let difficulty = vec![1.0, 2.0, 0.5]; // Second point is hardest

        let result = conformal_predict_adaptive(&residuals, &forecasts, &difficulty, 0.1).unwrap();

        assert_eq!(result.point, forecasts);
        assert_eq!(result.method, "adaptive_conformal");

        // Second point should have widest interval (highest difficulty)
        let width_0 = result.upper[0] - result.lower[0];
        let width_1 = result.upper[1] - result.lower[1];
        let width_2 = result.upper[2] - result.lower[2];

        assert!(width_1 > width_0);
        assert!(width_1 > width_2);
        assert!(width_0 > width_2); // difficulty[0]=1.0 > difficulty[2]=0.5
    }

    #[test]
    fn test_conformal_predict_adaptive_invalid_difficulty() {
        let residuals = vec![0.5, -0.3, 0.8];
        let forecasts = vec![100.0, 105.0, 110.0];
        let difficulty = vec![1.0, -2.0, 0.5]; // Negative difficulty

        assert!(conformal_predict_adaptive(&residuals, &forecasts, &difficulty, 0.1).is_err());
    }

    #[test]
    fn test_conformal_predict_asymmetric() {
        // Skewed residuals: mostly positive (over-predictions)
        let residuals = vec![2.0, 3.0, 2.5, 3.5, 4.0, -0.5, -0.3, -0.2, -0.1, 2.8];
        let forecasts = vec![100.0];

        let result = conformal_predict_asymmetric(&residuals, &forecasts, 0.1).unwrap();

        assert_eq!(result.method, "asymmetric_conformal");

        // Upper margin should be larger than lower margin due to skewed residuals
        let upper_width = result.upper[0] - result.point[0];
        let lower_width = result.point[0] - result.lower[0];

        assert!(upper_width > lower_width);
    }

    #[test]
    fn test_interval_width() {
        let lower = vec![95.0, 100.0, 105.0];
        let upper = vec![105.0, 110.0, 115.0];

        let widths = interval_width(&lower, &upper);
        assert_eq!(widths, vec![10.0, 10.0, 10.0]);

        let mean = mean_interval_width(&lower, &upper);
        assert_relative_eq!(mean, 10.0, epsilon = 0.001);
    }

    #[test]
    fn test_compute_quantile() {
        let data = vec![1.0, 2.0, 3.0, 4.0, 5.0];

        assert_relative_eq!(compute_quantile(&data, 0.0), 1.0, epsilon = 0.001);
        assert_relative_eq!(compute_quantile(&data, 0.5), 3.0, epsilon = 0.001);
        assert_relative_eq!(compute_quantile(&data, 1.0), 5.0, epsilon = 0.001);
        assert_relative_eq!(compute_quantile(&data, 0.25), 2.0, epsilon = 0.001);
        assert_relative_eq!(compute_quantile(&data, 0.75), 4.0, epsilon = 0.001);
    }

    #[test]
    fn test_coverage_guarantee() {
        // Test that empirical coverage matches nominal coverage
        // Using a larger sample for statistical power
        let n_cal = 100;
        let n_test = 1000;

        // Generate calibration residuals (standard normal approximation)
        let mut residuals: Vec<f64> = (0..n_cal)
            .map(|i| i as f64 / n_cal as f64 * 6.0 - 3.0) // Range [-3, 3]
            .collect();

        // Shuffle a bit to simulate randomness (reverse order)
        let len = residuals.len();
        for i in 0..len / 2 {
            residuals.swap(i, len - 1 - i);
        }

        let alpha = 0.1;
        let q = conformal_quantile(&residuals, alpha).unwrap();

        // Generate test points and check coverage
        let test_residuals: Vec<f64> = (0..n_test)
            .map(|i| i as f64 / n_test as f64 * 6.0 - 3.0)
            .collect();

        let covered: usize = test_residuals.iter().filter(|&&r| r.abs() <= q).count();

        let empirical_coverage = covered as f64 / n_test as f64;

        // Coverage should be at least 1 - alpha (with some tolerance for finite samples)
        assert!(
            empirical_coverage >= 1.0 - alpha - 0.05,
            "Empirical coverage {} is too low (expected >= {})",
            empirical_coverage,
            1.0 - alpha - 0.05
        );
    }
}
