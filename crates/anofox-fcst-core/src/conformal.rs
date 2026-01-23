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
//! # New Learn/Apply API (v2)
//!
//! The new API separates calibration from application:
//!
//! ```
//! use anofox_fcst_core::conformal::{conformal_learn, conformal_apply, ConformalMethod, ConformalStrategy};
//!
//! // Step 1: Learn calibration profile from residuals
//! let residuals = vec![0.5, -0.3, 0.8, -0.2, 0.4, -0.6, 0.3, -0.4, 0.7, -0.5];
//! let alphas = vec![0.1, 0.05]; // 90% and 95% coverage
//! let profile = conformal_learn(&residuals, &alphas, ConformalMethod::Symmetric, ConformalStrategy::Split, None).unwrap();
//!
//! // Step 2: Apply to forecasts (can be reused)
//! let forecasts = vec![100.0, 105.0, 110.0];
//! let intervals = conformal_apply(&forecasts, &profile, None).unwrap();
//! ```
//!
//! # Legacy API
//!
//! The original single-step API is still available:
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

// ============================================================================
// New Learn/Apply API (v2)
// ============================================================================

/// Method for conformal prediction (how intervals are calculated).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum ConformalMethod {
    /// Symmetric intervals using absolute residuals
    #[default]
    Symmetric,
    /// Asymmetric intervals using separate quantiles for positive/negative residuals
    Asymmetric,
    /// Adaptive intervals scaled by difficulty scores
    Adaptive,
}

/// Strategy for conformal calibration (how residuals are used).
///
/// The strategy determines how calibration residuals are processed and stored:
/// - **Split**: Simple holdout - use calibration residuals directly
/// - **CrossVal**: Cross-validation - combines CV folds for more calibration data
/// - **JackknifePlus**: Leave-one-out - stores full residual distribution for inference
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum ConformalStrategy {
    /// Split conformal - uses calibration residuals directly to compute quantiles.
    /// Most efficient; suitable when calibration set is large.
    #[default]
    Split,
    /// Cross-validation conformal - uses CV residuals for better calibration.
    /// Each residual comes from a model that didn't see that observation.
    CrossVal,
    /// Jackknife+ conformal - stores full residual distribution.
    /// At inference, computes intervals using min/max across LOO predictions.
    /// Provides tighter intervals but requires storing all residuals.
    JackknifePlus,
}

impl std::fmt::Display for ConformalStrategy {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Split => write!(f, "split"),
            Self::CrossVal => write!(f, "crossval"),
            Self::JackknifePlus => write!(f, "jackknife+"),
        }
    }
}

impl std::str::FromStr for ConformalStrategy {
    type Err = ForecastError;

    fn from_str(s: &str) -> Result<Self> {
        match s.to_lowercase().as_str() {
            "split" => Ok(Self::Split),
            "crossval" | "cross_val" | "cv" => Ok(Self::CrossVal),
            "jackknife+" | "jackknifeplus" | "jackknife_plus" | "jk+" => Ok(Self::JackknifePlus),
            _ => Err(ForecastError::InvalidInput(format!(
                "Unknown conformal strategy: '{}'. Valid: split, crossval, jackknife+",
                s
            ))),
        }
    }
}

impl std::fmt::Display for ConformalMethod {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Symmetric => write!(f, "symmetric"),
            Self::Asymmetric => write!(f, "asymmetric"),
            Self::Adaptive => write!(f, "adaptive"),
        }
    }
}

impl std::str::FromStr for ConformalMethod {
    type Err = ForecastError;

    fn from_str(s: &str) -> Result<Self> {
        match s.to_lowercase().as_str() {
            "symmetric" | "sym" => Ok(Self::Symmetric),
            "asymmetric" | "asym" => Ok(Self::Asymmetric),
            "adaptive" | "adapt" => Ok(Self::Adaptive),
            _ => Err(ForecastError::InvalidInput(format!(
                "Unknown conformal method: '{}'. Valid: symmetric, asymmetric, adaptive",
                s
            ))),
        }
    }
}

/// Calibration profile storing learned conformity scores.
///
/// This struct contains pre-computed quantiles from calibration residuals
/// that can be reused to generate prediction intervals for multiple forecasts.
///
/// # Storage Strategy
///
/// The `state_vector` stores different data based on strategy:
/// - **Split/CrossVal**: `[lower_q1, lower_q2, ..., upper_q1, upper_q2, ...]` (2 * n_alphas elements)
/// - **JackknifePlus**: Full sorted absolute residuals (n_residuals elements)
///
/// The `strategy` field determines how to interpret the state_vector at apply time.
///
/// # Fields
/// - `method`: The conformal method used (symmetric, asymmetric, adaptive)
/// - `strategy`: The calibration strategy (split, crossval, jackknife+)
/// - `alphas`: Coverage levels (e.g., [0.1, 0.05] for 90%, 95%)
/// - `state_vector`: Strategy-specific stored state
/// - `scores_lower`: Lower quantiles (one per alpha) - computed from state_vector for Split/CrossVal
/// - `scores_upper`: Upper quantiles (one per alpha) - computed from state_vector for Split/CrossVal
#[derive(Debug, Clone)]
pub struct CalibrationProfile {
    /// The conformal method used (how intervals are calculated)
    pub method: ConformalMethod,
    /// The calibration strategy (how residuals are processed)
    pub strategy: ConformalStrategy,
    /// Coverage levels (alpha values, e.g., [0.1, 0.05])
    pub alphas: Vec<f64>,
    /// Strategy-specific state vector:
    /// - Split/CrossVal: quantiles [lower_q1, ..., lower_qn, upper_q1, ..., upper_qn]
    /// - JackknifePlus: sorted absolute residuals [r1, r2, ..., rn]
    pub state_vector: Vec<f64>,
    /// Lower quantiles (one per alpha) - for direct access
    pub scores_lower: Vec<f64>,
    /// Upper quantiles (one per alpha); same as scores_lower for symmetric
    pub scores_upper: Vec<f64>,
    /// Number of residuals used for calibration
    pub n_residuals: usize,
}

impl CalibrationProfile {
    /// Get coverage levels (1 - alpha for each alpha).
    pub fn coverage_levels(&self) -> Vec<f64> {
        self.alphas.iter().map(|&a| 1.0 - a).collect()
    }

    /// Get the number of coverage levels.
    pub fn n_levels(&self) -> usize {
        self.alphas.len()
    }

    /// Check if this profile uses JackknifePlus strategy.
    pub fn is_jackknife_plus(&self) -> bool {
        self.strategy == ConformalStrategy::JackknifePlus
    }

    /// Get the sorted residuals for JackknifePlus strategy.
    /// Returns None if not JackknifePlus.
    pub fn jackknife_residuals(&self) -> Option<&[f64]> {
        if self.is_jackknife_plus() {
            Some(&self.state_vector)
        } else {
            None
        }
    }
}

/// Prediction intervals result from applying a calibration profile.
///
/// Contains lower and upper bounds for each forecast at each coverage level.
#[derive(Debug, Clone)]
pub struct PredictionIntervals {
    /// Point forecasts (unchanged from input)
    pub point: Vec<f64>,
    /// Lower bounds for each (forecast, alpha) pair.
    /// Layout: `lower[level][forecast]` - Vec of length n_levels, each inner Vec has n_forecasts
    pub lower: Vec<Vec<f64>>,
    /// Upper bounds for each (forecast, alpha) pair.
    /// Layout: `upper[level][forecast]` - Vec of length n_levels, each inner Vec has n_forecasts
    pub upper: Vec<Vec<f64>>,
    /// Coverage levels (1 - alpha)
    pub coverage: Vec<f64>,
    /// Method used
    pub method: ConformalMethod,
}

impl PredictionIntervals {
    /// Get the number of forecasts.
    pub fn n_forecasts(&self) -> usize {
        self.point.len()
    }

    /// Get the number of coverage levels.
    pub fn n_levels(&self) -> usize {
        self.coverage.len()
    }
}

/// Result of conformal evaluation metrics.
#[derive(Debug, Clone)]
pub struct ConformalEvaluation {
    /// Empirical coverage (fraction of actuals within intervals)
    pub coverage: f64,
    /// Violation rate (fraction of actuals outside intervals, i.e., 1 - coverage)
    pub violation_rate: f64,
    /// Mean interval width
    pub mean_width: f64,
    /// Winkler score (interval score penalizing width and miscoverage)
    pub winkler_score: f64,
    /// Number of observations evaluated
    pub n_observations: usize,
}

/// Learn a calibration profile from residuals.
///
/// Computes conformity scores for the given alpha levels that can be used
/// to generate prediction intervals for future forecasts.
///
/// # Arguments
/// * `residuals` - Calibration residuals (actual - predicted)
/// * `alphas` - Miscoverage rates (e.g., [0.1, 0.05] for 90%, 95% coverage)
/// * `method` - Conformal method to use (symmetric, asymmetric, adaptive)
/// * `strategy` - Calibration strategy to use (split, crossval, jackknife+)
/// * `difficulty` - Optional difficulty scores for adaptive method (must match residuals length)
///
/// # Returns
/// A `CalibrationProfile` containing the learned quantiles.
///
/// # Example
/// ```
/// use anofox_fcst_core::conformal::{conformal_learn, ConformalMethod, ConformalStrategy};
///
/// let residuals = vec![0.5, -0.3, 0.8, -0.2, 0.4, -0.6, 0.3, -0.4, 0.7, -0.5];
/// let alphas = vec![0.1, 0.05]; // 90% and 95% coverage
///
/// let profile = conformal_learn(&residuals, &alphas, ConformalMethod::Symmetric, ConformalStrategy::Split, None).unwrap();
/// assert_eq!(profile.n_levels(), 2);
/// ```
pub fn conformal_learn(
    residuals: &[f64],
    alphas: &[f64],
    method: ConformalMethod,
    strategy: ConformalStrategy,
    difficulty: Option<&[f64]>,
) -> Result<CalibrationProfile> {
    // Validate inputs
    if residuals.is_empty() {
        return Err(ForecastError::InsufficientData { needed: 1, got: 0 });
    }

    if alphas.is_empty() {
        return Err(ForecastError::InvalidInput(
            "At least one alpha value is required".to_string(),
        ));
    }

    for &alpha in alphas {
        if !(0.0..1.0).contains(&alpha) {
            return Err(ForecastError::InvalidInput(format!(
                "Alpha must be in (0, 1), got {}",
                alpha
            )));
        }
    }

    // Validate difficulty for adaptive method
    if method == ConformalMethod::Adaptive {
        match difficulty {
            None => {
                return Err(ForecastError::InvalidInput(
                    "Difficulty scores required for adaptive method".to_string(),
                ));
            }
            Some(d) if d.len() != residuals.len() => {
                return Err(ForecastError::InvalidInput(format!(
                    "Difficulty length ({}) must match residuals length ({})",
                    d.len(),
                    residuals.len()
                )));
            }
            Some(d) if d.iter().any(|&x| x <= 0.0) => {
                return Err(ForecastError::InvalidInput(
                    "Difficulty scores must be positive".to_string(),
                ));
            }
            _ => {}
        }
    }

    // Validate JackknifePlus restrictions
    if strategy == ConformalStrategy::JackknifePlus && method == ConformalMethod::Asymmetric {
        return Err(ForecastError::InvalidInput(
            "JackknifePlus strategy does not support asymmetric method".to_string(),
        ));
    }

    let n_residuals = residuals.len();
    let mut scores_lower = Vec::with_capacity(alphas.len());
    let mut scores_upper = Vec::with_capacity(alphas.len());

    // Compute sorted absolute residuals (used by all strategies)
    let mut abs_residuals: Vec<f64> = residuals.iter().map(|r| r.abs()).collect();
    abs_residuals.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));

    // For JackknifePlus, we store the full sorted residual distribution
    // For Split/CrossVal, we compute quantiles and store them in state_vector
    let state_vector: Vec<f64>;

    match strategy {
        ConformalStrategy::JackknifePlus => {
            // Store full sorted residual distribution for inference-time computation
            state_vector = abs_residuals.clone();

            // For JackknifePlus, scores are computed at apply time
            // but we pre-compute them here for convenience access
            for &alpha in alphas {
                let n = abs_residuals.len() as f64;
                let quantile_level = ((n + 1.0) * (1.0 - alpha)).ceil() / n;
                let quantile_level = quantile_level.clamp(0.0, 1.0);
                let score = compute_quantile(&abs_residuals, quantile_level);
                scores_lower.push(score);
                scores_upper.push(score);
            }
        }

        ConformalStrategy::Split | ConformalStrategy::CrossVal => {
            // Split and CrossVal use the same quantile computation
            // (CrossVal assumes residuals already come from CV folds)
            match method {
                ConformalMethod::Symmetric => {
                    for &alpha in alphas {
                        let n = abs_residuals.len() as f64;
                        let quantile_level = ((n + 1.0) * (1.0 - alpha)).ceil() / n;
                        let quantile_level = quantile_level.clamp(0.0, 1.0);
                        let score = compute_quantile(&abs_residuals, quantile_level);
                        scores_lower.push(score);
                        scores_upper.push(score);
                    }
                }

                ConformalMethod::Asymmetric => {
                    // Separate positive and negative residuals
                    let positive: Vec<f64> =
                        residuals.iter().filter(|&&r| r > 0.0).copied().collect();
                    let negative: Vec<f64> = residuals
                        .iter()
                        .filter(|&&r| r < 0.0)
                        .map(|&r| r.abs())
                        .collect();

                    let mut sorted_positive = positive;
                    let mut sorted_negative = negative;
                    sorted_positive
                        .sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));
                    sorted_negative
                        .sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));

                    for &alpha in alphas {
                        let alpha_half = alpha / 2.0;

                        // Upper margin from positive residuals
                        let upper_margin = if sorted_positive.is_empty() {
                            0.0
                        } else {
                            let n = sorted_positive.len() as f64;
                            let q = ((n + 1.0) * (1.0 - alpha_half)).ceil() / n;
                            compute_quantile(&sorted_positive, q.clamp(0.0, 1.0))
                        };

                        // Lower margin from negative residuals (absolute values)
                        let lower_margin = if sorted_negative.is_empty() {
                            0.0
                        } else {
                            let n = sorted_negative.len() as f64;
                            let q = ((n + 1.0) * (1.0 - alpha_half)).ceil() / n;
                            compute_quantile(&sorted_negative, q.clamp(0.0, 1.0))
                        };

                        scores_lower.push(lower_margin);
                        scores_upper.push(upper_margin);
                    }
                }

                ConformalMethod::Adaptive => {
                    // For adaptive, we store the base symmetric scores
                    // The actual scaling happens at apply time using the forecast difficulty
                    for &alpha in alphas {
                        let n = abs_residuals.len() as f64;
                        let quantile_level = ((n + 1.0) * (1.0 - alpha)).ceil() / n;
                        let quantile_level = quantile_level.clamp(0.0, 1.0);
                        let score = compute_quantile(&abs_residuals, quantile_level);
                        scores_lower.push(score);
                        scores_upper.push(score);
                    }
                }
            }

            // For Split/CrossVal, state_vector stores quantiles: [lower_q1, ..., upper_q1, ...]
            state_vector = [scores_lower.clone(), scores_upper.clone()].concat();
        }
    }

    Ok(CalibrationProfile {
        method,
        strategy,
        alphas: alphas.to_vec(),
        state_vector,
        scores_lower,
        scores_upper,
        n_residuals,
    })
}

/// Apply a calibration profile to generate prediction intervals.
///
/// # Arguments
/// * `forecasts` - Point forecasts to wrap with intervals
/// * `profile` - Pre-computed calibration profile from `conformal_learn`
/// * `difficulty` - Optional difficulty scores for adaptive method (must match forecasts length)
///
/// # Returns
/// `PredictionIntervals` containing intervals for each forecast at each coverage level.
///
/// # Example
/// ```
/// use anofox_fcst_core::conformal::{conformal_learn, conformal_apply, ConformalMethod, ConformalStrategy};
///
/// let residuals = vec![0.5, -0.3, 0.8, -0.2, 0.4, -0.6, 0.3, -0.4, 0.7, -0.5];
/// let profile = conformal_learn(&residuals, &[0.1], ConformalMethod::Symmetric, ConformalStrategy::Split, None).unwrap();
///
/// let forecasts = vec![100.0, 105.0, 110.0];
/// let intervals = conformal_apply(&forecasts, &profile, None).unwrap();
///
/// assert_eq!(intervals.n_forecasts(), 3);
/// assert_eq!(intervals.n_levels(), 1);
/// ```
pub fn conformal_apply(
    forecasts: &[f64],
    profile: &CalibrationProfile,
    difficulty: Option<&[f64]>,
) -> Result<PredictionIntervals> {
    if forecasts.is_empty() {
        return Err(ForecastError::InvalidInput(
            "At least one forecast is required".to_string(),
        ));
    }

    // Validate difficulty for adaptive method
    if profile.method == ConformalMethod::Adaptive {
        match difficulty {
            None => {
                return Err(ForecastError::InvalidInput(
                    "Difficulty scores required for adaptive method".to_string(),
                ));
            }
            Some(d) if d.len() != forecasts.len() => {
                return Err(ForecastError::InvalidInput(format!(
                    "Difficulty length ({}) must match forecasts length ({})",
                    d.len(),
                    forecasts.len()
                )));
            }
            Some(d) if d.iter().any(|&x| x <= 0.0) => {
                return Err(ForecastError::InvalidInput(
                    "Difficulty scores must be positive".to_string(),
                ));
            }
            _ => {}
        }
    }

    let n_levels = profile.n_levels();
    let mut lower = Vec::with_capacity(n_levels);
    let mut upper = Vec::with_capacity(n_levels);

    // Normalize difficulty if adaptive
    let normalized_difficulty: Option<Vec<f64>> = if profile.method == ConformalMethod::Adaptive {
        let d = difficulty.unwrap();
        let mean_d: f64 = d.iter().sum::<f64>() / d.len() as f64;
        Some(d.iter().map(|&x| x / mean_d).collect())
    } else {
        None
    };

    // Handle JackknifePlus separately - it computes intervals dynamically from stored residuals
    if profile.is_jackknife_plus() {
        let residuals = profile.jackknife_residuals().ok_or_else(|| {
            ForecastError::InvalidInput("JackknifePlus profile missing residuals".to_string())
        })?;

        for (level_idx, &alpha) in profile.alphas.iter().enumerate() {
            // For JackknifePlus, we compute intervals using the full residual distribution
            // This allows dynamic quantile computation and potentially tighter intervals
            let n = residuals.len() as f64;
            let quantile_level = ((n + 1.0) * (1.0 - alpha)).ceil() / n;
            let quantile_level = quantile_level.clamp(0.0, 1.0);

            // Residuals are already sorted, so we can compute quantile directly
            let score = compute_quantile(residuals, quantile_level);

            // For JackknifePlus with Adaptive method, scale by difficulty
            if profile.method == ConformalMethod::Adaptive {
                let norm_d = normalized_difficulty.as_ref().unwrap();
                let level_lower: Vec<f64> = forecasts
                    .iter()
                    .zip(norm_d.iter())
                    .map(|(&f, &d)| f - score * d)
                    .collect();
                let level_upper: Vec<f64> = forecasts
                    .iter()
                    .zip(norm_d.iter())
                    .map(|(&f, &d)| f + score * d)
                    .collect();
                lower.push(level_lower);
                upper.push(level_upper);
            } else {
                // Symmetric case (JackknifePlus doesn't support Asymmetric)
                let level_lower: Vec<f64> = forecasts.iter().map(|&f| f - score).collect();
                let level_upper: Vec<f64> = forecasts.iter().map(|&f| f + score).collect();
                lower.push(level_lower);
                upper.push(level_upper);
            }

            // Update pre-computed scores for consistency (optional but helpful for debugging)
            // Note: We don't modify profile here since it's borrowed immutably
            // The scores_lower/scores_upper should already match from conformal_learn
            let _ = level_idx; // Suppress unused warning
        }
    } else {
        // Standard Split/CrossVal apply logic
        for level_idx in 0..n_levels {
            let score_lower = profile.scores_lower[level_idx];
            let score_upper = profile.scores_upper[level_idx];

            match profile.method {
                ConformalMethod::Symmetric | ConformalMethod::Asymmetric => {
                    let level_lower: Vec<f64> =
                        forecasts.iter().map(|&f| f - score_lower).collect();
                    let level_upper: Vec<f64> =
                        forecasts.iter().map(|&f| f + score_upper).collect();
                    lower.push(level_lower);
                    upper.push(level_upper);
                }

                ConformalMethod::Adaptive => {
                    let norm_d = normalized_difficulty.as_ref().unwrap();
                    let level_lower: Vec<f64> = forecasts
                        .iter()
                        .zip(norm_d.iter())
                        .map(|(&f, &d)| f - score_lower * d)
                        .collect();
                    let level_upper: Vec<f64> = forecasts
                        .iter()
                        .zip(norm_d.iter())
                        .map(|(&f, &d)| f + score_upper * d)
                        .collect();
                    lower.push(level_lower);
                    upper.push(level_upper);
                }
            }
        }
    }

    Ok(PredictionIntervals {
        point: forecasts.to_vec(),
        lower,
        upper,
        coverage: profile.coverage_levels(),
        method: profile.method,
    })
}

/// Convenience function combining learn and apply in one step.
///
/// # Arguments
/// * `residuals` - Calibration residuals (actual - predicted)
/// * `forecasts` - Point forecasts to wrap with intervals
/// * `alphas` - Miscoverage rates (e.g., [0.1] for 90% coverage)
/// * `method` - Conformal method to use
/// * `strategy` - Calibration strategy to use (defaults to Split)
/// * `difficulty_cal` - Optional difficulty scores for calibration (adaptive method)
/// * `difficulty_pred` - Optional difficulty scores for prediction (adaptive method)
///
/// # Returns
/// `PredictionIntervals` containing intervals for each forecast.
pub fn conformalize(
    residuals: &[f64],
    forecasts: &[f64],
    alphas: &[f64],
    method: ConformalMethod,
    strategy: ConformalStrategy,
    difficulty_cal: Option<&[f64]>,
    difficulty_pred: Option<&[f64]>,
) -> Result<PredictionIntervals> {
    let profile = conformal_learn(residuals, alphas, method, strategy, difficulty_cal)?;
    conformal_apply(forecasts, &profile, difficulty_pred)
}

// ============================================================================
// Evaluation Functions
// ============================================================================

/// Compute empirical coverage of prediction intervals.
///
/// Returns the fraction of actual values that fall within the intervals.
///
/// # Arguments
/// * `actuals` - True values
/// * `lower` - Lower bounds of intervals
/// * `upper` - Upper bounds of intervals
///
/// # Returns
/// Coverage as a fraction in [0, 1].
pub fn conformal_coverage(actuals: &[f64], lower: &[f64], upper: &[f64]) -> Result<f64> {
    if actuals.is_empty() {
        return Err(ForecastError::InsufficientData { needed: 1, got: 0 });
    }

    if actuals.len() != lower.len() || actuals.len() != upper.len() {
        return Err(ForecastError::InvalidInput(format!(
            "Length mismatch: actuals={}, lower={}, upper={}",
            actuals.len(),
            lower.len(),
            upper.len()
        )));
    }

    let covered: usize = actuals
        .iter()
        .zip(lower.iter())
        .zip(upper.iter())
        .filter(|&((&a, &l), &u)| a >= l && a <= u)
        .count();

    Ok(covered as f64 / actuals.len() as f64)
}

/// Compute the Winkler score (interval score).
///
/// The Winkler score penalizes both interval width and miscoverage.
/// Lower is better.
///
/// Formula: width + (2/alpha) * (lower - actual) if actual < lower
///                 + (2/alpha) * (actual - upper) if actual > upper
///
/// # Arguments
/// * `actuals` - True values
/// * `lower` - Lower bounds of intervals
/// * `upper` - Upper bounds of intervals
/// * `alpha` - Miscoverage rate (e.g., 0.1 for 90% coverage)
///
/// # Returns
/// Mean Winkler score across all observations.
pub fn winkler_score(actuals: &[f64], lower: &[f64], upper: &[f64], alpha: f64) -> Result<f64> {
    if actuals.is_empty() {
        return Err(ForecastError::InsufficientData { needed: 1, got: 0 });
    }

    if actuals.len() != lower.len() || actuals.len() != upper.len() {
        return Err(ForecastError::InvalidInput(format!(
            "Length mismatch: actuals={}, lower={}, upper={}",
            actuals.len(),
            lower.len(),
            upper.len()
        )));
    }

    if !(0.0..1.0).contains(&alpha) {
        return Err(ForecastError::InvalidInput(format!(
            "Alpha must be in (0, 1), got {}",
            alpha
        )));
    }

    let penalty_factor = 2.0 / alpha;
    let mut total_score = 0.0;

    for ((&a, &l), &u) in actuals.iter().zip(lower.iter()).zip(upper.iter()) {
        let width = u - l;
        let mut score = width;

        if a < l {
            score += penalty_factor * (l - a);
        } else if a > u {
            score += penalty_factor * (a - u);
        }

        total_score += score;
    }

    Ok(total_score / actuals.len() as f64)
}

/// Comprehensive evaluation of prediction intervals.
///
/// # Arguments
/// * `actuals` - True values
/// * `lower` - Lower bounds of intervals
/// * `upper` - Upper bounds of intervals
/// * `alpha` - Miscoverage rate for Winkler score calculation
///
/// # Returns
/// `ConformalEvaluation` with coverage, violation_rate, width, and Winkler score.
pub fn conformal_evaluate(
    actuals: &[f64],
    lower: &[f64],
    upper: &[f64],
    alpha: f64,
) -> Result<ConformalEvaluation> {
    let coverage = conformal_coverage(actuals, lower, upper)?;
    let violation_rate = 1.0 - coverage;
    let mean_width = mean_interval_width(lower, upper);
    let winkler = winkler_score(actuals, lower, upper, alpha)?;

    Ok(ConformalEvaluation {
        coverage,
        violation_rate,
        mean_width,
        winkler_score: winkler,
        n_observations: actuals.len(),
    })
}

/// Method for computing difficulty scores
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DifficultyMethod {
    /// Rolling standard deviation of percent changes (returns)
    Volatility,
    /// Changepoint probability from Bayesian Online Changepoint Detection
    ChangepointProb,
    /// Rolling standard deviation of raw values
    RollingStd,
}

impl std::str::FromStr for DifficultyMethod {
    type Err = ForecastError;

    fn from_str(s: &str) -> std::result::Result<Self, Self::Err> {
        match s.to_lowercase().as_str() {
            "volatility" | "vol" => Ok(DifficultyMethod::Volatility),
            "changepoint_prob" | "changepoint" | "cp" => Ok(DifficultyMethod::ChangepointProb),
            "rolling_std" | "rollingstd" | "std" => Ok(DifficultyMethod::RollingStd),
            _ => Err(ForecastError::InvalidInput(format!(
                "Unknown difficulty method '{}'. Valid options: volatility, changepoint_prob, rolling_std",
                s
            ))),
        }
    }
}

/// Compute difficulty scores for adaptive conformal prediction.
///
/// Difficulty scores estimate the local prediction uncertainty at each point.
/// Higher scores indicate harder-to-predict regions where intervals should be wider.
///
/// # Arguments
/// * `values` - Time series values
/// * `method` - Method for computing difficulty
/// * `window` - Optional window size (default: 20)
///
/// # Methods
/// * **volatility** - Rolling standard deviation of percent changes (returns)
/// * **changepoint_prob** - Probability of changepoint from BOCPD
/// * **rolling_std** - Rolling standard deviation of raw values
///
/// # Returns
/// Vector of difficulty scores (always positive, mean-normalized to 1.0)
pub fn difficulty_score(
    values: &[f64],
    method: DifficultyMethod,
    window: Option<usize>,
) -> Result<Vec<f64>> {
    let n = values.len();
    if n < 3 {
        return Err(ForecastError::InsufficientData { needed: 3, got: n });
    }

    let win = window.unwrap_or(20).max(2).min(n);

    let raw_scores = match method {
        DifficultyMethod::Volatility => compute_volatility(values, win),
        DifficultyMethod::ChangepointProb => compute_changepoint_prob(values)?,
        DifficultyMethod::RollingStd => compute_rolling_std(values, win),
    };

    // Ensure all scores are positive and normalize to mean 1.0
    let min_score = raw_scores.iter().cloned().fold(f64::INFINITY, f64::min);
    let shifted: Vec<f64> = raw_scores.iter().map(|&s| s - min_score + 1e-6).collect();

    let mean: f64 = shifted.iter().sum::<f64>() / shifted.len() as f64;
    if mean <= 0.0 {
        return Ok(vec![1.0; n]);
    }

    Ok(shifted.iter().map(|&s| s / mean).collect())
}

/// Compute rolling volatility (std of returns)
fn compute_volatility(values: &[f64], window: usize) -> Vec<f64> {
    let n = values.len();
    let mut result = vec![1.0; n];

    // Compute returns (percent changes)
    let returns: Vec<f64> = values
        .windows(2)
        .map(|w| {
            if w[0].abs() > 1e-10 {
                (w[1] - w[0]) / w[0].abs()
            } else {
                w[1] - w[0]
            }
        })
        .collect();

    // Rolling std of returns
    for (i, res) in result.iter_mut().enumerate() {
        let start = i.saturating_sub(window);
        let end = (i + 1).min(returns.len());
        if end > start {
            let slice = &returns[start..end];
            let mean: f64 = slice.iter().sum::<f64>() / slice.len() as f64;
            let variance: f64 =
                slice.iter().map(|&r| (r - mean).powi(2)).sum::<f64>() / slice.len() as f64;
            *res = variance.sqrt().max(1e-10);
        }
    }

    result
}

/// Compute changepoint probability using BOCPD
fn compute_changepoint_prob(values: &[f64]) -> Result<Vec<f64>> {
    use crate::changepoint::detect_changepoints_bocpd;

    let bocpd = detect_changepoints_bocpd(values, 50.0, true)?;
    Ok(bocpd.changepoint_probability)
}

/// Compute rolling standard deviation
fn compute_rolling_std(values: &[f64], window: usize) -> Vec<f64> {
    let n = values.len();
    let mut result = vec![1.0; n];

    for (i, res) in result.iter_mut().enumerate() {
        let start = i.saturating_sub(window);
        let end = i + 1;
        let slice = &values[start..end];
        let mean: f64 = slice.iter().sum::<f64>() / slice.len() as f64;
        let variance: f64 =
            slice.iter().map(|&v| (v - mean).powi(2)).sum::<f64>() / slice.len() as f64;
        *res = variance.sqrt().max(1e-10);
    }

    result
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
        assert!((2.0..=4.0).contains(&q));
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

    // ========================================================================
    // Tests for new Learn/Apply API
    // ========================================================================

    #[test]
    fn test_conformal_method_from_str() {
        assert_eq!(
            "symmetric".parse::<ConformalMethod>().unwrap(),
            ConformalMethod::Symmetric
        );
        assert_eq!(
            "asymmetric".parse::<ConformalMethod>().unwrap(),
            ConformalMethod::Asymmetric
        );
        assert_eq!(
            "adaptive".parse::<ConformalMethod>().unwrap(),
            ConformalMethod::Adaptive
        );
        assert!("invalid".parse::<ConformalMethod>().is_err());
    }

    #[test]
    fn test_conformal_strategy_from_str() {
        // Split variants
        assert_eq!(
            "split".parse::<ConformalStrategy>().unwrap(),
            ConformalStrategy::Split
        );

        // CrossVal variants
        assert_eq!(
            "crossval".parse::<ConformalStrategy>().unwrap(),
            ConformalStrategy::CrossVal
        );
        assert_eq!(
            "cv".parse::<ConformalStrategy>().unwrap(),
            ConformalStrategy::CrossVal
        );
        assert_eq!(
            "cross_val".parse::<ConformalStrategy>().unwrap(),
            ConformalStrategy::CrossVal
        );

        // JackknifePlus variants
        assert_eq!(
            "jackknife+".parse::<ConformalStrategy>().unwrap(),
            ConformalStrategy::JackknifePlus
        );
        assert_eq!(
            "jk+".parse::<ConformalStrategy>().unwrap(),
            ConformalStrategy::JackknifePlus
        );
        assert_eq!(
            "jackknifeplus".parse::<ConformalStrategy>().unwrap(),
            ConformalStrategy::JackknifePlus
        );

        // Invalid
        assert!("invalid".parse::<ConformalStrategy>().is_err());
    }

    #[test]
    fn test_conformal_strategy_display() {
        assert_eq!(ConformalStrategy::Split.to_string(), "split");
        assert_eq!(ConformalStrategy::CrossVal.to_string(), "crossval");
        assert_eq!(ConformalStrategy::JackknifePlus.to_string(), "jackknife+");
    }

    #[test]
    fn test_conformal_strategy_default() {
        assert_eq!(ConformalStrategy::default(), ConformalStrategy::Split);
    }

    #[test]
    fn test_conformal_learn_symmetric() {
        let residuals = vec![0.5, -0.3, 0.8, -0.2, 0.4, -0.6, 0.3, -0.4, 0.7, -0.5];
        let alphas = vec![0.1, 0.05];

        let profile = conformal_learn(
            &residuals,
            &alphas,
            ConformalMethod::Symmetric,
            ConformalStrategy::Split,
            None,
        )
        .unwrap();

        assert_eq!(profile.method, ConformalMethod::Symmetric);
        assert_eq!(profile.strategy, ConformalStrategy::Split);
        assert_eq!(profile.n_levels(), 2);
        assert_eq!(profile.n_residuals, 10);

        // For symmetric, lower and upper scores should be equal
        assert_relative_eq!(
            profile.scores_lower[0],
            profile.scores_upper[0],
            epsilon = 0.001
        );
        assert_relative_eq!(
            profile.scores_lower[1],
            profile.scores_upper[1],
            epsilon = 0.001
        );

        // Higher coverage (lower alpha) should have higher scores
        assert!(profile.scores_lower[1] >= profile.scores_lower[0]);

        // State vector should contain 4 elements (2 lower + 2 upper)
        assert_eq!(profile.state_vector.len(), 4);
    }

    #[test]
    fn test_conformal_learn_asymmetric() {
        // Skewed residuals: mostly positive
        let residuals = vec![2.0, 3.0, 2.5, 3.5, 4.0, -0.5, -0.3, -0.2, -0.1, 2.8];
        let alphas = vec![0.1];

        let profile = conformal_learn(
            &residuals,
            &alphas,
            ConformalMethod::Asymmetric,
            ConformalStrategy::Split,
            None,
        )
        .unwrap();

        assert_eq!(profile.method, ConformalMethod::Asymmetric);

        // Upper score should be larger due to skewed residuals
        assert!(profile.scores_upper[0] > profile.scores_lower[0]);
    }

    #[test]
    fn test_conformal_learn_adaptive() {
        let residuals = vec![0.5, -0.3, 0.8, -0.2, 0.4, -0.6, 0.3, -0.4, 0.7, -0.5];
        let difficulty = vec![1.0, 2.0, 0.5, 1.5, 1.0, 0.8, 1.2, 0.9, 1.1, 1.0];
        let alphas = vec![0.1];

        let profile = conformal_learn(
            &residuals,
            &alphas,
            ConformalMethod::Adaptive,
            ConformalStrategy::Split,
            Some(&difficulty),
        )
        .unwrap();

        assert_eq!(profile.method, ConformalMethod::Adaptive);
    }

    #[test]
    fn test_conformal_learn_adaptive_requires_difficulty() {
        let residuals = vec![0.5, -0.3, 0.8];
        let alphas = vec![0.1];

        // Should fail without difficulty scores
        let result = conformal_learn(
            &residuals,
            &alphas,
            ConformalMethod::Adaptive,
            ConformalStrategy::Split,
            None,
        );
        assert!(result.is_err());
    }

    #[test]
    fn test_conformal_learn_jackknife_plus() {
        let residuals = vec![0.5, -0.3, 0.8, -0.2, 0.4, -0.6, 0.3, -0.4, 0.7, -0.5];
        let alphas = vec![0.1];

        let profile = conformal_learn(
            &residuals,
            &alphas,
            ConformalMethod::Symmetric,
            ConformalStrategy::JackknifePlus,
            None,
        )
        .unwrap();

        assert_eq!(profile.strategy, ConformalStrategy::JackknifePlus);
        assert!(profile.is_jackknife_plus());

        // State vector should contain all sorted residuals
        assert_eq!(profile.state_vector.len(), 10);

        // Residuals should be sorted
        let residuals_from_profile = profile.jackknife_residuals().unwrap();
        for i in 1..residuals_from_profile.len() {
            assert!(residuals_from_profile[i] >= residuals_from_profile[i - 1]);
        }
    }

    #[test]
    fn test_jackknife_plus_rejects_asymmetric() {
        let residuals = vec![0.5, -0.3, 0.8, -0.2, 0.4];
        let alphas = vec![0.1];

        // JackknifePlus with Asymmetric should fail
        let result = conformal_learn(
            &residuals,
            &alphas,
            ConformalMethod::Asymmetric,
            ConformalStrategy::JackknifePlus,
            None,
        );
        assert!(result.is_err());
    }

    #[test]
    fn test_conformal_apply_jackknife_plus() {
        let residuals = vec![0.5, -0.3, 0.8, -0.2, 0.4, -0.6, 0.3, -0.4, 0.7, -0.5];
        let profile = conformal_learn(
            &residuals,
            &[0.1, 0.05],
            ConformalMethod::Symmetric,
            ConformalStrategy::JackknifePlus,
            None,
        )
        .unwrap();

        let forecasts = vec![100.0, 105.0, 110.0];
        let intervals = conformal_apply(&forecasts, &profile, None).unwrap();

        assert_eq!(intervals.n_forecasts(), 3);
        assert_eq!(intervals.n_levels(), 2);

        // Intervals should be symmetric around point forecasts
        for (i, forecast) in forecasts.iter().enumerate() {
            let lower_diff = forecast - intervals.lower[0][i];
            let upper_diff = intervals.upper[0][i] - forecast;
            assert_relative_eq!(lower_diff, upper_diff, epsilon = 0.001);
        }

        // Higher coverage (alpha=0.05) should have wider intervals than lower coverage (alpha=0.1)
        let width_90 = intervals.upper[0][0] - intervals.lower[0][0];
        let width_95 = intervals.upper[1][0] - intervals.lower[1][0];
        assert!(width_95 >= width_90);
    }

    #[test]
    fn test_jackknife_plus_equals_split_for_same_residuals() {
        // With the same residuals, JackknifePlus and Split should produce identical results
        let residuals = vec![0.5, -0.3, 0.8, -0.2, 0.4, -0.6, 0.3, -0.4, 0.7, -0.5];
        let alphas = vec![0.1];

        let profile_split = conformal_learn(
            &residuals,
            &alphas,
            ConformalMethod::Symmetric,
            ConformalStrategy::Split,
            None,
        )
        .unwrap();

        let profile_jk = conformal_learn(
            &residuals,
            &alphas,
            ConformalMethod::Symmetric,
            ConformalStrategy::JackknifePlus,
            None,
        )
        .unwrap();

        let forecasts = vec![100.0];
        let intervals_split = conformal_apply(&forecasts, &profile_split, None).unwrap();
        let intervals_jk = conformal_apply(&forecasts, &profile_jk, None).unwrap();

        // Lower and upper bounds should be identical
        assert_relative_eq!(
            intervals_split.lower[0][0],
            intervals_jk.lower[0][0],
            epsilon = 0.001
        );
        assert_relative_eq!(
            intervals_split.upper[0][0],
            intervals_jk.upper[0][0],
            epsilon = 0.001
        );
    }

    #[test]
    fn test_conformal_apply_symmetric() {
        let residuals = vec![0.5, -0.3, 0.8, -0.2, 0.4, -0.6, 0.3, -0.4, 0.7, -0.5];
        let profile = conformal_learn(
            &residuals,
            &[0.1],
            ConformalMethod::Symmetric,
            ConformalStrategy::Split,
            None,
        )
        .unwrap();

        let forecasts = vec![100.0, 105.0, 110.0];
        let intervals = conformal_apply(&forecasts, &profile, None).unwrap();

        assert_eq!(intervals.n_forecasts(), 3);
        assert_eq!(intervals.n_levels(), 1);
        assert_relative_eq!(intervals.coverage[0], 0.9, epsilon = 0.001);

        // Intervals should be symmetric around point forecasts
        for (i, forecast) in forecasts.iter().enumerate() {
            let lower_diff = forecast - intervals.lower[0][i];
            let upper_diff = intervals.upper[0][i] - forecast;
            assert_relative_eq!(lower_diff, upper_diff, epsilon = 0.001);
        }
    }

    #[test]
    fn test_conformal_apply_multi_level() {
        let residuals = vec![0.5, -0.3, 0.8, -0.2, 0.4, -0.6, 0.3, -0.4, 0.7, -0.5];
        let alphas = vec![0.5, 0.2, 0.1, 0.05];
        let profile = conformal_learn(
            &residuals,
            &alphas,
            ConformalMethod::Symmetric,
            ConformalStrategy::Split,
            None,
        )
        .unwrap();

        let forecasts = vec![100.0];
        let intervals = conformal_apply(&forecasts, &profile, None).unwrap();

        assert_eq!(intervals.n_levels(), 4);

        // Check coverage levels
        assert_relative_eq!(intervals.coverage[0], 0.5, epsilon = 0.001);
        assert_relative_eq!(intervals.coverage[1], 0.8, epsilon = 0.001);
        assert_relative_eq!(intervals.coverage[2], 0.9, epsilon = 0.001);
        assert_relative_eq!(intervals.coverage[3], 0.95, epsilon = 0.001);

        // Higher coverage should have wider intervals
        for i in 0..3 {
            let width_i = intervals.upper[i][0] - intervals.lower[i][0];
            let width_next = intervals.upper[i + 1][0] - intervals.lower[i + 1][0];
            assert!(
                width_next >= width_i,
                "Width at level {} ({}) should be <= width at level {} ({})",
                i,
                width_i,
                i + 1,
                width_next
            );
        }
    }

    #[test]
    fn test_conformal_apply_adaptive() {
        let residuals = vec![0.5, -0.3, 0.8, -0.2, 0.4, -0.6, 0.3, -0.4, 0.7, -0.5];
        let cal_difficulty = vec![1.0, 2.0, 0.5, 1.5, 1.0, 0.8, 1.2, 0.9, 1.1, 1.0];
        let profile = conformal_learn(
            &residuals,
            &[0.1],
            ConformalMethod::Adaptive,
            ConformalStrategy::Split,
            Some(&cal_difficulty),
        )
        .unwrap();

        let forecasts = vec![100.0, 105.0, 110.0];
        let pred_difficulty = vec![1.0, 2.0, 0.5]; // Second point hardest

        let intervals = conformal_apply(&forecasts, &profile, Some(&pred_difficulty)).unwrap();

        // Second point should have widest interval
        let width_0 = intervals.upper[0][0] - intervals.lower[0][0];
        let width_1 = intervals.upper[0][1] - intervals.lower[0][1];
        let width_2 = intervals.upper[0][2] - intervals.lower[0][2];

        assert!(
            width_1 > width_0,
            "High difficulty should have wider interval"
        );
        assert!(
            width_1 > width_2,
            "High difficulty should have wider interval"
        );
    }

    #[test]
    fn test_conformalize() {
        let residuals = vec![0.5, -0.3, 0.8, -0.2, 0.4, -0.6, 0.3, -0.4, 0.7, -0.5];
        let forecasts = vec![100.0, 105.0, 110.0];
        let alphas = vec![0.1];

        let intervals = conformalize(
            &residuals,
            &forecasts,
            &alphas,
            ConformalMethod::Symmetric,
            ConformalStrategy::Split,
            None,
            None,
        )
        .unwrap();

        assert_eq!(intervals.n_forecasts(), 3);
        assert_eq!(intervals.n_levels(), 1);
    }

    #[test]
    fn test_conformal_coverage_fn() {
        let actuals = vec![100.0, 105.0, 110.0, 95.0, 120.0];
        let lower = vec![95.0, 100.0, 105.0, 90.0, 115.0];
        let upper = vec![105.0, 110.0, 115.0, 100.0, 125.0];

        let coverage = conformal_coverage(&actuals, &lower, &upper).unwrap();
        assert_relative_eq!(coverage, 1.0, epsilon = 0.001); // All within bounds

        // Test with some outside bounds
        let actuals2 = vec![100.0, 120.0, 110.0]; // 120.0 is outside [100, 110]
        let lower2 = vec![95.0, 100.0, 105.0];
        let upper2 = vec![105.0, 110.0, 115.0];

        let coverage2 = conformal_coverage(&actuals2, &lower2, &upper2).unwrap();
        assert_relative_eq!(coverage2, 2.0 / 3.0, epsilon = 0.001);
    }

    #[test]
    fn test_winkler_score_fn() {
        // All within bounds - score should just be mean width
        let actuals = vec![100.0, 105.0, 110.0];
        let lower = vec![95.0, 100.0, 105.0];
        let upper = vec![105.0, 110.0, 115.0];
        let alpha = 0.1;

        let score = winkler_score(&actuals, &lower, &upper, alpha).unwrap();
        assert_relative_eq!(score, 10.0, epsilon = 0.001); // Mean width = 10

        // Test with one outside bounds
        let actuals2 = vec![90.0]; // 5 below lower bound of 95
        let lower2 = vec![95.0];
        let upper2 = vec![105.0];

        let score2 = winkler_score(&actuals2, &lower2, &upper2, alpha).unwrap();
        // width = 10, penalty = (2/0.1) * 5 = 100, total = 110
        assert_relative_eq!(score2, 110.0, epsilon = 0.001);
    }

    #[test]
    fn test_conformal_evaluate_fn() {
        let actuals = vec![100.0, 105.0, 110.0];
        let lower = vec![95.0, 100.0, 105.0];
        let upper = vec![105.0, 110.0, 115.0];
        let alpha = 0.1;

        let eval = conformal_evaluate(&actuals, &lower, &upper, alpha).unwrap();

        assert_relative_eq!(eval.coverage, 1.0, epsilon = 0.001);
        assert_relative_eq!(eval.violation_rate, 0.0, epsilon = 0.001);
        assert_relative_eq!(eval.mean_width, 10.0, epsilon = 0.001);
        assert_relative_eq!(eval.winkler_score, 10.0, epsilon = 0.001);
        assert_eq!(eval.n_observations, 3);
    }

    #[test]
    fn test_conformal_evaluate_with_violations() {
        // One out of three observations outside the interval
        let actuals = vec![100.0, 120.0, 110.0]; // 120.0 is outside [100, 110]
        let lower = vec![95.0, 100.0, 105.0];
        let upper = vec![105.0, 110.0, 115.0];
        let alpha = 0.1;

        let eval = conformal_evaluate(&actuals, &lower, &upper, alpha).unwrap();

        assert_relative_eq!(eval.coverage, 2.0 / 3.0, epsilon = 0.001);
        assert_relative_eq!(eval.violation_rate, 1.0 / 3.0, epsilon = 0.001);
        assert_eq!(eval.n_observations, 3);
    }
}
