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
use crate::missing;

/// Validates that `alpha` is strictly inside `(0, 1)` (Phase 16 D-06).
///
/// `NaN` fails (`NaN > 0.0` is `false`, so the range check alone rejects it
/// without a separate `is_nan()` branch). Used on every conformal surface
/// that takes a miscoverage rate, so an out-of-range alpha is always an
/// `InvalidInput` error naming the value, never a silent clamp or NULL.
pub(crate) fn validate_alpha(alpha: f64) -> Result<()> {
    if alpha > 0.0 && alpha < 1.0 {
        Ok(())
    } else {
        Err(ForecastError::InvalidInput(format!(
            "alpha must be in (0, 1), got {}",
            alpha
        )))
    }
}

/// The finite-sample split-conformal quantile LEVEL for `n` calibration
/// residuals at miscoverage rate `alpha` (Phase 16 D-05/G12).
///
/// `k = ceil((n + 1) * (1 - alpha))` computed in `f64` in exactly that
/// order (matches numpy's equivalent expression in the reference fixture).
/// Returns `None` when `n == 0` or `k > n` -- no finite-sample interval
/// exists at this `(n, alpha)`, so the caller must report an unbounded
/// (infinite) conformity score instead of clamping the level into `[0, 1]`.
/// Otherwise returns `Some(k / n)`.
fn finite_sample_level(n: usize, alpha: f64) -> Option<f64> {
    if n == 0 {
        return None;
    }
    let nf = n as f64;
    let k = ((nf + 1.0) * (1.0 - alpha)).ceil();
    if k > nf {
        None
    } else {
        Some(k / nf)
    }
}

/// Result of conformal prediction containing intervals and metadata.
#[derive(Debug, Clone)]
pub struct ConformalResult {
    /// Point forecasts (unchanged from input)
    pub point: Vec<f64>,
    /// Lower bounds of prediction intervals
    pub lower: Vec<f64>,
    /// Upper bounds of prediction intervals
    pub upper: Vec<f64>,
    /// Empirical coverage on the calibration residuals: the fraction of
    /// calibration residuals that fall within the computed conformity
    /// margin(s) (Phase 16 D-02). This is NOT the requested level — see
    /// `nominal_level` for that.
    pub coverage: f64,
    /// The requested coverage level (1 - alpha), i.e. what used to be
    /// (incorrectly) reported as `coverage` before Phase 16 D-02.
    pub nominal_level: f64,
    /// The computed conformity score (quantile threshold)
    pub conformity_score: f64,
    /// Number of calibration residuals actually used (after dropping
    /// missing/incomplete pairs AND non-finite residuals, Phase 16 D-06).
    pub n_calibration: usize,
    /// Method used for conformal prediction
    pub method: String,
    /// `true` when no finite-sample interval exists at this `(n_calibration,
    /// alpha)` -- `ceil((n+1)(1-alpha)) > n` (Phase 16 D-05/G12). When set,
    /// `conformity_score` is `f64::INFINITY`, `lower`/`upper` are
    /// `f64::NEG_INFINITY`/`f64::INFINITY`, and `coverage` is `f64::NAN`.
    pub calibration_insufficient: bool,
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
    validate_alpha(alpha)?;

    // Phase 16 D-06/G16: non-finite residuals (NaN, +/-inf) are filtered
    // before any quantile is taken -- never sorted with an unwrapped
    // `partial_cmp`, which would panic on NaN.
    let mut abs_residuals: Vec<f64> = residuals
        .iter()
        .copied()
        .filter(|r| r.is_finite())
        .map(|r| r.abs())
        .collect();
    abs_residuals.sort_by(f64::total_cmp);

    // Phase 16 D-05/G12: when ceil((n+1)(1-alpha)) > n, no finite-sample
    // interval exists at this (n, alpha) -- report an unbounded (infinite)
    // conformity score instead of clamping the quantile level into [0, 1].
    // An empty (or all-non-finite) residual pool is `n == 0`, always
    // insufficient, and no longer an error.
    match finite_sample_level(abs_residuals.len(), alpha) {
        None => Ok(f64::INFINITY),
        Some(level) => Ok(compute_quantile(&abs_residuals, level)),
    }
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
/// assert_eq!(result.nominal_level, 0.9);
/// assert_eq!(result.lower.len(), 3);
/// assert_eq!(result.upper.len(), 3);
/// ```
pub fn conformal_predict(
    residuals: &[f64],
    forecasts: &[f64],
    alpha: f64,
) -> Result<ConformalResult> {
    let conformity_score = conformal_quantile(residuals, alpha)?;

    // Phase 16 D-06: `n_calibration` is the count of FINITE residuals --
    // the same population `conformal_quantile` computed its level over.
    let n_calibration = residuals.iter().filter(|r| r.is_finite()).count();
    let calibration_insufficient = !conformity_score.is_finite();

    let (lower, upper, coverage) = if calibration_insufficient {
        // Phase 16 D-05/G12: no finite-sample interval exists -- unbounded,
        // never clamped to the max residual.
        (
            vec![f64::NEG_INFINITY; forecasts.len()],
            vec![f64::INFINITY; forecasts.len()],
            f64::NAN,
        )
    } else {
        let (lower, upper) = conformal_intervals(forecasts, conformity_score);
        // Phase 16 D-02: `coverage` is the EMPIRICAL coverage on the
        // calibration residuals themselves (fraction of |residual| <=
        // conformity_score), not the requested level. The requested level
        // is `nominal_level`.
        let coverage = if n_calibration == 0 {
            f64::NAN
        } else {
            residuals
                .iter()
                .filter(|r| r.is_finite())
                .filter(|r| r.abs() <= conformity_score)
                .count() as f64
                / n_calibration as f64
        };
        (lower, upper, coverage)
    };

    Ok(ConformalResult {
        point: forecasts.to_vec(),
        lower,
        upper,
        coverage,
        nominal_level: 1.0 - alpha,
        conformity_score,
        n_calibration,
        method: "split_conformal".to_string(),
        calibration_insufficient,
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

/// Validates that every PRESENT (non-`NaN`) difficulty value is positive and
/// finite (Phase 16 D-08). `NaN` is the missing-value sentinel used
/// elsewhere in this module (Phase 10 D-12 convention) and is intentionally
/// NOT rejected here -- the caller drops it as a missing calibration pair or
/// propagates it as a missing prediction, not an error.
///
/// # Errors
/// `InvalidInput` naming the 1-based position and the offending value for
/// the first PRESENT value that is `<= 0.0` or infinite.
fn validate_difficulty_values(difficulty: &[f64]) -> Result<()> {
    for (i, &d) in difficulty.iter().enumerate() {
        if d.is_nan() {
            continue;
        }
        if !(d > 0.0 && d.is_finite()) {
            return Err(ForecastError::InvalidInput(format!(
                "difficulty value at position {} must be positive and finite, got {}",
                i + 1,
                d
            )));
        }
    }
    Ok(())
}

/// Computes locally-adaptive conformal intervals using an EXPLICIT per-row
/// difficulty at BOTH calibration and prediction (Phase 16 D-08, CONF-04).
///
/// Scores are `s_i = |r_i| / d_i` over complete (finite residual, present
/// difficulty) pairs; `q` is their finite-sample quantile (via
/// [`conformal_quantile`] -- insufficient reports an unbounded result, same
/// convention as every other conformal method). Bounds are `f_j -/+ q *
/// d_j` using the PREDICTION difficulty exactly as given -- there is NO
/// batch-mean normalisation, so a forecast's interval does not depend on
/// which other forecasts happen to be in the same batch (this was the
/// pre-16-02 bug, G14).
///
/// # Arguments
/// * `residuals` - Calibration residuals (actual - predicted), signed
/// * `difficulty_cal` - Per-row calibration difficulty, same length as `residuals`
/// * `forecasts` - Point forecasts to wrap with intervals
/// * `difficulty_pred` - Per-row prediction difficulty, same length as `forecasts`. A `NaN` entry
///   propagates to `NaN` (missing) bounds at that position only, independent of every other row.
/// * `alpha` - Miscoverage rate (e.g., 0.1 for 90% coverage)
///
/// # Errors
/// - length mismatches (`residuals` vs `difficulty_cal`; `forecasts` vs `difficulty_pred`):
///   `InvalidInput` naming both lengths.
/// - any PRESENT (non-`NaN`) difficulty value `<= 0.0` or infinite: `InvalidInput` naming the
///   value and its 1-based position (`validate_difficulty_values`).
pub fn conformal_predict_adaptive(
    residuals: &[f64],
    difficulty_cal: &[f64],
    forecasts: &[f64],
    difficulty_pred: &[f64],
    alpha: f64,
) -> Result<ConformalResult> {
    validate_alpha(alpha)?;

    if residuals.len() != difficulty_cal.len() {
        return Err(ForecastError::InvalidInput(format!(
            "residuals and difficulty_cal must have the same length: {} vs {}",
            residuals.len(),
            difficulty_cal.len()
        )));
    }
    if forecasts.len() != difficulty_pred.len() {
        return Err(ForecastError::InvalidInput(format!(
            "forecasts and difficulty_pred must have the same length: {} vs {}",
            forecasts.len(),
            difficulty_pred.len()
        )));
    }

    validate_difficulty_values(difficulty_cal)?;
    validate_difficulty_values(difficulty_pred)?;

    // Phase 16 D-06/G16: a calibration pair whose residual is non-finite
    // (NaN missing marker, or +/-inf) or whose difficulty is missing (NaN)
    // is dropped before any quantile.
    let scores: Vec<f64> = residuals
        .iter()
        .zip(difficulty_cal.iter())
        .filter(|(r, d)| r.is_finite() && !d.is_nan())
        .map(|(r, d)| r.abs() / d)
        .collect();
    let n_calibration = scores.len();

    let q = conformal_quantile(&scores, alpha)?;
    let calibration_insufficient = !q.is_finite();

    let (lower, upper, coverage) = if calibration_insufficient {
        (
            vec![f64::NEG_INFINITY; forecasts.len()],
            vec![f64::INFINITY; forecasts.len()],
            f64::NAN,
        )
    } else {
        let lower: Vec<f64> = forecasts
            .iter()
            .zip(difficulty_pred.iter())
            .map(|(f, d)| if d.is_nan() { f64::NAN } else { f - q * d })
            .collect();
        let upper: Vec<f64> = forecasts
            .iter()
            .zip(difficulty_pred.iter())
            .map(|(f, d)| if d.is_nan() { f64::NAN } else { f + q * d })
            .collect();
        let coverage = if n_calibration == 0 {
            f64::NAN
        } else {
            scores.iter().filter(|&&s| s <= q).count() as f64 / n_calibration as f64
        };
        (lower, upper, coverage)
    };

    Ok(ConformalResult {
        point: forecasts.to_vec(),
        lower,
        upper,
        coverage,
        nominal_level: 1.0 - alpha,
        conformity_score: q,
        n_calibration,
        method: "adaptive_conformal".to_string(),
        calibration_insufficient,
    })
}

/// Applies a single pre-computed conformity `score` to `forecasts`,
/// optionally scaled by a per-row `difficulty` (Phase 16 D-08, CONF-04).
///
/// Backs the SQL `ts_conformal_intervals(forecasts, conformity_score,
/// method, difficulty)` 4-arg overload: `method` selects how `difficulty`
/// is (or must not be) used. An infinite `score` (an insufficient
/// calibration, per the shared convention) propagates to unbounded
/// (+/-infinity) bounds through ordinary arithmetic -- no special case
/// needed here.
///
/// # Errors
/// - `method == Asymmetric`: always `InvalidInput` -- a single score cannot express two
///   (lower, upper) margins; use the `(lower, upper)` STRUCT path instead.
/// - `method == Symmetric` with any PRESENT (non-`None`) difficulty value: `InvalidInput`
///   containing "only used by method 'adaptive'".
/// - `method == Adaptive` with difficulty ALL missing (`None`/empty): `InvalidInput` naming
///   "difficulty".
/// - `forecasts.len() != difficulty.len()` (when `difficulty` is non-empty): `InvalidInput`
///   naming both lengths.
pub fn conformal_apply_score(
    forecasts: &[Option<f64>],
    score: f64,
    method: ConformalMethod,
    difficulty: &[Option<f64>],
) -> Result<(Vec<f64>, Vec<f64>)> {
    if method == ConformalMethod::Asymmetric {
        return Err(ForecastError::InvalidInput(
            "conformal_apply_score: method 'asymmetric' needs two margins (lower, upper), \
             which a single conformity_score cannot express"
                .to_string(),
        ));
    }

    // Normalize an absent (empty) difficulty list into "all missing", same
    // length as forecasts, so the rest of this function never special-cases
    // length-0 vs. all-None.
    let difficulty: std::borrow::Cow<[Option<f64>]> = if difficulty.is_empty() {
        std::borrow::Cow::Owned(vec![None; forecasts.len()])
    } else {
        std::borrow::Cow::Borrowed(difficulty)
    };

    if forecasts.len() != difficulty.len() {
        return Err(ForecastError::InvalidInput(format!(
            "forecasts and difficulty must have the same length: {} vs {}",
            forecasts.len(),
            difficulty.len()
        )));
    }

    match method {
        ConformalMethod::Symmetric => {
            if difficulty.iter().any(|d| d.is_some()) {
                return Err(ForecastError::InvalidInput(
                    "difficulty_col is only used by method 'adaptive'".to_string(),
                ));
            }
        }
        ConformalMethod::Adaptive => {
            if difficulty.iter().all(|d| d.is_none()) {
                return Err(ForecastError::InvalidInput(
                    "method 'adaptive' requires difficulty values".to_string(),
                ));
            }
        }
        ConformalMethod::Asymmetric => unreachable!("rejected above"),
    }

    let (lower, upper): (Vec<f64>, Vec<f64>) = match method {
        ConformalMethod::Symmetric => {
            let lower = forecasts
                .iter()
                .map(|f| f.map_or(f64::NAN, |v| v - score))
                .collect();
            let upper = forecasts
                .iter()
                .map(|f| f.map_or(f64::NAN, |v| v + score))
                .collect();
            (lower, upper)
        }
        ConformalMethod::Adaptive => {
            let lower = forecasts
                .iter()
                .zip(difficulty.iter())
                .map(|(f, d)| match (f, d) {
                    (Some(fv), Some(dv)) => fv - score * dv,
                    _ => f64::NAN,
                })
                .collect();
            let upper = forecasts
                .iter()
                .zip(difficulty.iter())
                .map(|(f, d)| match (f, d) {
                    (Some(fv), Some(dv)) => fv + score * dv,
                    _ => f64::NAN,
                })
                .collect();
            (lower, upper)
        }
        ConformalMethod::Asymmetric => unreachable!("rejected above"),
    };

    Ok((lower, upper))
}

/// Computes asymmetric conformal intervals using the alpha/2 and 1-alpha/2
/// finite-sample quantiles of the SIGNED residuals (Phase 16 D-07, CONF-03).
///
/// Unlike standard conformal prediction which creates symmetric intervals,
/// this method computes the upper margin as the `k/n` quantile of the signed
/// residuals and the lower margin as the `k/n` quantile of the NEGATED
/// signed residuals (both over the FULL finite-residual count `n`, not a
/// positive/negative subset), allowing for asymmetric intervals that better
/// capture skewed distributions. A negative lower margin is allowed and
/// expected for a biased model: the resulting interval may exclude the
/// model's own point forecast.
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
    validate_alpha(alpha)?;
    let half_alpha = alpha / 2.0;

    // Phase 16 D-06/G16: non-finite residuals filtered before any quantile.
    let finite: Vec<f64> = residuals
        .iter()
        .copied()
        .filter(|r| r.is_finite())
        .collect();
    let n_calibration = finite.len();

    let mut sorted_r: Vec<f64> = finite.clone();
    sorted_r.sort_by(f64::total_cmp);
    let mut sorted_neg_r: Vec<f64> = finite.iter().map(|r| -r).collect();
    sorted_neg_r.sort_by(f64::total_cmp);

    let level = finite_sample_level(n_calibration, half_alpha);
    let calibration_insufficient = level.is_none();

    let (lower_margin, upper_margin, coverage) = match level {
        None => (f64::INFINITY, f64::INFINITY, f64::NAN),
        Some(lvl) => {
            let lower_margin = compute_quantile(&sorted_neg_r, lvl);
            let upper_margin = compute_quantile(&sorted_r, lvl);
            // Phase 16 D-02: empirical coverage = fraction of SIGNED
            // calibration residuals within [-lower_margin, upper_margin].
            let coverage = if n_calibration == 0 {
                f64::NAN
            } else {
                finite
                    .iter()
                    .filter(|&&r| r >= -lower_margin && r <= upper_margin)
                    .count() as f64
                    / n_calibration as f64
            };
            (lower_margin, upper_margin, coverage)
        }
    };

    let lower: Vec<f64> = forecasts.iter().map(|f| f - lower_margin).collect();
    let upper: Vec<f64> = forecasts.iter().map(|f| f + upper_margin).collect();

    // Report the mean of the two margins as conformity score (half-width).
    let conformity_score = (lower_margin + upper_margin) / 2.0;

    Ok(ConformalResult {
        point: forecasts.to_vec(),
        lower,
        upper,
        coverage,
        nominal_level: 1.0 - alpha,
        conformity_score,
        n_calibration,
        method: "asymmetric_conformal".to_string(),
        calibration_insufficient,
    })
}

/// Computes conformal prediction intervals directly from per-row calibration
/// (actual, forecast) pairs and a list of point forecasts (Phase 16 D-01/D-02).
///
/// Unlike [`conformal_predict`]/[`conformal_predict_asymmetric`], which take an
/// already-computed residual array, this function computes `residual_i =
/// actual_i - forecast_i` itself, over complete (non-missing) pairs only
/// (`missing::complete_cases`) -- so SQL callers can pass per-row calibration
/// arrays straight out of a `GROUP BY` without computing the subtraction in SQL
/// (Phase 16 D-02: all residual arithmetic happens in Rust, not SQL).
///
/// `point_forecasts` may contain missing entries (`None`); the corresponding
/// output position stays `NaN` (`missing::propagate_missing`), so a missing
/// point forecast never shifts a later horizon step into an earlier one. An
/// empty `point_forecasts` list is valid: `point`/`lower`/`upper` come back
/// empty, and the calibration score/coverage are still computed.
///
/// `result.method` is set to the canonical [`ConformalMethod`] `Display` name
/// ("symmetric" / "asymmetric" / "adaptive"), not the legacy "split_conformal" /
/// "asymmetric_conformal" strings `conformal_predict`/`conformal_predict_asymmetric`
/// use directly.
///
/// `cal_difficulty`/`point_difficulty` (Phase 16 D-08, plan 16-02): `None`
/// means "no difficulty column at all" (same as `Some` of an all-`None`
/// slice). Only `ConformalMethod::Adaptive` may use them.
///
/// # Errors
/// - `cal_actuals.len() != cal_forecasts.len()`: `InvalidInput` naming both lengths
///   (via `missing::complete_cases`).
/// - `method != Adaptive` with any PRESENT difficulty value (calibration or
///   prediction): `InvalidInput` containing "only used by method 'adaptive'".
/// - `method == Adaptive` with NO present calibration difficulty: `InvalidInput` naming
///   `difficulty_col`.
pub fn conformal_from_pairs(
    cal_actuals: &[Option<f64>],
    cal_forecasts: &[Option<f64>],
    cal_difficulty: Option<&[Option<f64>]>,
    point_forecasts: &[Option<f64>],
    point_difficulty: Option<&[Option<f64>]>,
    alpha: f64,
    method: ConformalMethod,
) -> Result<ConformalResult> {
    let cal_difficulty_present = cal_difficulty.is_some_and(|d| d.iter().any(|v| v.is_some()));
    let point_difficulty_present = point_difficulty.is_some_and(|d| d.iter().any(|v| v.is_some()));

    if method != ConformalMethod::Adaptive && (cal_difficulty_present || point_difficulty_present) {
        return Err(ForecastError::InvalidInput(
            "difficulty_col is only used by method 'adaptive'".to_string(),
        ));
    }

    if method == ConformalMethod::Adaptive {
        if !cal_difficulty_present {
            return Err(ForecastError::InvalidInput(
                "method 'adaptive' requires difficulty_col".to_string(),
            ));
        }
        let cal_difficulty = cal_difficulty.unwrap();
        if cal_actuals.len() != cal_difficulty.len() {
            return Err(ForecastError::InvalidInput(format!(
                "cal_actuals and difficulty_col must have the same length: {} vs {}",
                cal_actuals.len(),
                cal_difficulty.len()
            )));
        }
        if let Some(pd) = point_difficulty {
            if point_forecasts.len() != pd.len() {
                return Err(ForecastError::InvalidInput(format!(
                    "point_forecasts and difficulty_col must have the same length: {} vs {}",
                    point_forecasts.len(),
                    pd.len()
                )));
            }
        }

        // Complete (actual, forecast, difficulty) triples -- a row missing
        // ANY of the three is dropped from calibration (D-02 pattern).
        let complete = missing::complete_cases(&[cal_actuals, cal_forecasts, cal_difficulty])?;
        let residuals: Vec<f64> = complete[0]
            .iter()
            .zip(complete[1].iter())
            .map(|(a, f)| a - f)
            .collect();
        let difficulty_cal = complete[2].clone();

        let filled_points = missing::fill_missing(point_forecasts, 0.0);
        let point_diff_filled: Vec<f64> = match point_difficulty {
            Some(pd) => pd.iter().map(|v| v.unwrap_or(f64::NAN)).collect(),
            None => vec![f64::NAN; point_forecasts.len()],
        };

        let mut result = conformal_predict_adaptive(
            &residuals,
            &difficulty_cal,
            &filled_points,
            &point_diff_filled,
            alpha,
        )?;

        missing::propagate_missing(&mut result.point, point_forecasts);
        missing::propagate_missing(&mut result.lower, point_forecasts);
        missing::propagate_missing(&mut result.upper, point_forecasts);
        result.method = method.to_string();
        return Ok(result);
    }

    let complete = missing::complete_cases(&[cal_actuals, cal_forecasts])?;
    let residuals: Vec<f64> = complete[0]
        .iter()
        .zip(complete[1].iter())
        .map(|(a, f)| a - f)
        .collect();

    let filled_points = missing::fill_missing(point_forecasts, 0.0);

    let mut result = match method {
        ConformalMethod::Symmetric => conformal_predict(&residuals, &filled_points, alpha)?,
        ConformalMethod::Asymmetric => {
            conformal_predict_asymmetric(&residuals, &filled_points, alpha)?
        }
        ConformalMethod::Adaptive => {
            unreachable!("ConformalMethod::Adaptive handled above")
        }
    };

    missing::propagate_missing(&mut result.point, point_forecasts);
    missing::propagate_missing(&mut result.lower, point_forecasts);
    missing::propagate_missing(&mut result.upper, point_forecasts);
    result.method = method.to_string();

    Ok(result)
}

/// Calibration profile from per-row `(actual, forecast)` calibration pairs,
/// without any point forecasts to apply to yet (Phase 16 D-03).
#[derive(Debug, Clone)]
pub struct ConformalCalibration {
    /// Method used for calibration (symmetric or adaptive -- asymmetric is
    /// rejected, see `conformal_calibrate`).
    pub method: ConformalMethod,
    /// The computed conformity score (quantile threshold).
    pub conformity_score: f64,
    /// Empirical coverage on the calibration residuals.
    pub coverage: f64,
    /// The requested coverage level (1 - alpha).
    pub nominal_level: f64,
    /// Number of calibration residuals actually used (after dropping
    /// incomplete/missing pairs).
    pub n_calibration: usize,
    /// `true` when no finite-sample interval exists at this
    /// `(n_calibration, alpha)` (Phase 16 D-05/G12); see
    /// `ConformalResult::calibration_insufficient`.
    pub calibration_insufficient: bool,
}

/// Calibrates a conformal predictor from per-row `(actual, forecast)` pairs,
/// without applying it to any point forecasts yet (Phase 16 D-03).
///
/// Built on [`conformal_from_pairs`] with an empty point-forecast list.
///
/// Asymmetric conformal produces two distinct margins (lower/upper), which
/// cannot be expressed as the single `conformity_score` this function
/// returns; callers that want asymmetric margins must go through
/// `ts_conformal_by` (which calls `conformal_from_pairs` directly, carrying
/// both margins via `lower`/`upper`), not `ts_conformal_calibrate`.
///
/// # Errors
/// `method == ConformalMethod::Asymmetric`: `InvalidInput`.
pub fn conformal_calibrate(
    actuals: &[Option<f64>],
    forecasts: &[Option<f64>],
    difficulty: Option<&[Option<f64>]>,
    alpha: f64,
    method: ConformalMethod,
) -> Result<ConformalCalibration> {
    if method == ConformalMethod::Asymmetric {
        return Err(ForecastError::InvalidInput(
            "ts_conformal_calibrate supports methods 'symmetric' and 'adaptive'; asymmetric \
             margins come from ts_conformal_by"
                .to_string(),
        ));
    }

    let result = conformal_from_pairs(actuals, forecasts, difficulty, &[], None, alpha, method)?;

    Ok(ConformalCalibration {
        method,
        conformity_score: result.conformity_score,
        coverage: result.coverage,
        nominal_level: result.nominal_level,
        n_calibration: result.n_calibration,
        calibration_insufficient: result.calibration_insufficient,
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
///
/// A `jackknife+` strategy existed before Phase 16 (D-09/G15) but was removed:
/// it computed the same quantile as `Split` (no leave-one-out refits), so it
/// gave none of the tighter-interval benefit a real jackknife+ promises while
/// implying it did. A true jackknife+ needs a leave-one-out REFIT per
/// calibration point, which this crate's GROUP BY-oriented architecture
/// cannot provide; `FromStr` below still recognizes every old alias so it can
/// explain the removal instead of silently defaulting.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum ConformalStrategy {
    /// Split conformal - uses calibration residuals directly to compute quantiles.
    /// Most efficient; suitable when calibration set is large.
    #[default]
    Split,
    /// Cross-validation conformal - uses CV residuals for better calibration.
    /// Each residual comes from a model that didn't see that observation.
    CrossVal,
}

impl std::fmt::Display for ConformalStrategy {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Split => write!(f, "split"),
            Self::CrossVal => write!(f, "crossval"),
        }
    }
}

impl std::str::FromStr for ConformalStrategy {
    type Err = ForecastError;

    fn from_str(s: &str) -> Result<Self> {
        match s.to_lowercase().as_str() {
            "split" => Ok(Self::Split),
            "crossval" | "cross_val" | "cv" => Ok(Self::CrossVal),
            // D-09/G15: every removed jackknife+ alias gets an explanatory
            // error naming its replacements, never a silent default.
            "jackknife+" | "jackknifeplus" | "jackknife_plus" | "jk+" => {
                Err(ForecastError::InvalidInput(format!(
                    "conformal strategy '{}' was removed: it computed the same quantile as \
                     'split' (no leave-one-out refits). Use 'split' or 'crossval'.",
                    s
                )))
            }
            _ => Err(ForecastError::InvalidInput(format!(
                "unknown conformal strategy '{}'; valid: split, crossval (alias cross_val, cv)",
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
            // "split" is an alias of Symmetric (Phase 16): existing docs use
            // `'method': 'split'`.
            "symmetric" | "split" | "sym" => Ok(Self::Symmetric),
            "asymmetric" | "asym" => Ok(Self::Asymmetric),
            "adaptive" | "adapt" => Ok(Self::Adaptive),
            _ => Err(ForecastError::InvalidInput(format!(
                "Unknown conformal method: '{}'. Valid: symmetric (alias split, sym), \
                 asymmetric (alias asym), adaptive (alias adapt)",
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
/// The `state_vector` stores `[lower_q1, lower_q2, ..., upper_q1, upper_q2, ...]`
/// (2 * n_alphas elements) for both strategies (Split and CrossVal use the
/// identical quantile computation).
///
/// # Fields
/// - `method`: The conformal method used (symmetric, asymmetric, adaptive)
/// - `strategy`: The calibration strategy (split, crossval)
/// - `alphas`: Coverage levels (e.g., [0.1, 0.05] for 90%, 95%)
/// - `state_vector`: quantiles [lower_q1, ..., lower_qn, upper_q1, ..., upper_qn]
/// - `scores_lower`: Lower quantiles (one per alpha) - computed from state_vector
/// - `scores_upper`: Upper quantiles (one per alpha) - computed from state_vector
#[derive(Debug, Clone)]
pub struct CalibrationProfile {
    /// The conformal method used (how intervals are calculated)
    pub method: ConformalMethod,
    /// The calibration strategy (how residuals are processed)
    pub strategy: ConformalStrategy,
    /// Coverage levels (alpha values, e.g., [0.1, 0.05])
    pub alphas: Vec<f64>,
    /// Quantiles [lower_q1, ..., lower_qn, upper_q1, ..., upper_qn]
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
/// * `strategy` - Calibration strategy to use (split, crossval)
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
        validate_alpha(alpha)?;
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
            Some(d) => validate_difficulty_values(d)?,
        }
    }

    let n_residuals = residuals.len();
    let mut scores_lower = Vec::with_capacity(alphas.len());
    let mut scores_upper = Vec::with_capacity(alphas.len());

    // Phase 16 D-06/G16: non-finite residuals filtered before any sort/quantile.
    let finite_residuals: Vec<f64> = residuals
        .iter()
        .copied()
        .filter(|r| r.is_finite())
        .collect();
    let n_finite = finite_residuals.len();
    let mut abs_residuals: Vec<f64> = finite_residuals.iter().map(|r| r.abs()).collect();
    abs_residuals.sort_by(f64::total_cmp);

    // Split and CrossVal use the identical quantile computation (CrossVal
    // assumes residuals already come from CV folds); D-09 removed the
    // JackknifePlus strategy that used to branch here.
    match method {
        ConformalMethod::Symmetric => {
            for &alpha in alphas {
                let score = match finite_sample_level(n_finite, alpha) {
                    None => f64::INFINITY,
                    Some(level) => compute_quantile(&abs_residuals, level),
                };
                scores_lower.push(score);
                scores_upper.push(score);
            }
        }

        ConformalMethod::Asymmetric => {
            // Phase 16 D-07: upper margin = quantile of the signed
            // residuals; lower margin = quantile of the negated
            // signed residuals, both over the FULL finite count
            // (matches conformal_predict_asymmetric exactly).
            let mut sorted_r: Vec<f64> = finite_residuals.clone();
            sorted_r.sort_by(f64::total_cmp);
            let mut sorted_neg_r: Vec<f64> = finite_residuals.iter().map(|r| -r).collect();
            sorted_neg_r.sort_by(f64::total_cmp);

            for &alpha in alphas {
                let half_alpha = alpha / 2.0;
                let (lower_margin, upper_margin) = match finite_sample_level(n_finite, half_alpha) {
                    None => (f64::INFINITY, f64::INFINITY),
                    Some(level) => (
                        compute_quantile(&sorted_neg_r, level),
                        compute_quantile(&sorted_r, level),
                    ),
                };

                scores_lower.push(lower_margin);
                scores_upper.push(upper_margin);
            }
        }

        ConformalMethod::Adaptive => {
            // Phase 16 D-08: scores_i = |r_i| / d_i over complete
            // (finite residual, finite difficulty) pairs -- no
            // longer the unweighted symmetric score.
            let d = difficulty.expect("validated above: difficulty required for adaptive");
            let mut scores: Vec<f64> = residuals
                .iter()
                .zip(d.iter())
                .filter(|(r, dv)| r.is_finite() && dv.is_finite())
                .map(|(r, dv)| r.abs() / dv)
                .collect();
            scores.sort_by(f64::total_cmp);
            let n_scores = scores.len();

            for &alpha in alphas {
                let score = match finite_sample_level(n_scores, alpha) {
                    None => f64::INFINITY,
                    Some(level) => compute_quantile(&scores, level),
                };
                scores_lower.push(score);
                scores_upper.push(score);
            }
        }
    }

    // state_vector stores quantiles: [lower_q1, ..., upper_q1, ...]
    let state_vector: Vec<f64> = [scores_lower.clone(), scores_upper.clone()].concat();

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
            // Phase 16 D-08: a NaN (missing) prediction difficulty is NOT
            // an error here -- it propagates to NaN bounds at that position
            // only (f - score * NaN = NaN), independent of every other row.
            Some(d) => validate_difficulty_values(d)?,
        }
    }

    let n_levels = profile.n_levels();
    let mut lower = Vec::with_capacity(n_levels);
    let mut upper = Vec::with_capacity(n_levels);

    // D-09 removed the JackknifePlus strategy's dynamic-residual branch;
    // Split and CrossVal both apply the pre-computed scores directly.
    for level_idx in 0..n_levels {
        let score_lower = profile.scores_lower[level_idx];
        let score_upper = profile.scores_upper[level_idx];

        match profile.method {
            ConformalMethod::Symmetric | ConformalMethod::Asymmetric => {
                let level_lower: Vec<f64> = forecasts.iter().map(|&f| f - score_lower).collect();
                let level_upper: Vec<f64> = forecasts.iter().map(|&f| f + score_upper).collect();
                lower.push(level_lower);
                upper.push(level_upper);
            }

            ConformalMethod::Adaptive => {
                // Phase 16 D-08: difficulty used AS GIVEN, no batch-mean
                // normalisation.
                let d = difficulty.unwrap();
                let level_lower: Vec<f64> = forecasts
                    .iter()
                    .zip(d.iter())
                    .map(|(&f, &d)| f - score_lower * d)
                    .collect();
                let level_upper: Vec<f64> = forecasts
                    .iter()
                    .zip(d.iter())
                    .map(|(&f, &d)| f + score_upper * d)
                    .collect();
                lower.push(level_lower);
                upper.push(level_upper);
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

    validate_alpha(alpha)?;

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
    fn test_validate_alpha_rejects_boundary_and_nan() {
        for bad in [0.0, 1.0, -0.1, 1.5, f64::NAN] {
            let err = validate_alpha(bad).unwrap_err();
            assert!(
                err.to_string().contains("alpha must be in (0, 1), got"),
                "alpha={bad}: {err}"
            );
        }
        assert!(validate_alpha(0.1).is_ok());
        assert!(validate_alpha(0.5).is_ok());
    }

    #[test]
    fn test_conformal_quantile_rejects_alpha_zero_naming_value() {
        let residuals = vec![1.0, 2.0, 3.0, 4.0, 5.0];
        let err = conformal_quantile(&residuals, 0.0).unwrap_err();
        assert!(err.to_string().contains("alpha must be in (0, 1), got 0"));
    }

    #[test]
    fn test_conformal_quantile_insufficient_n5_alpha_0_1() {
        // ceil(6 * 0.9) = 6 > 5 -> insufficient.
        let residuals = vec![1.0, 2.0, 3.0, 4.0, 5.0];
        let q = conformal_quantile(&residuals, 0.1).unwrap();
        assert!(q.is_infinite());
    }

    #[test]
    fn test_conformal_quantile_sufficient_n5_alpha_0_2() {
        // ceil(6 * 0.8) = 5 <= 5 -> sufficient, level = 1.0 -> max residual.
        let residuals = vec![1.0, 2.0, 3.0, 4.0, 5.0];
        let q = conformal_quantile(&residuals, 0.2).unwrap();
        assert_relative_eq!(q, 5.0, epsilon = 1e-9);
    }

    #[test]
    fn test_conformal_quantile_filters_non_finite_without_panicking() {
        let residuals = vec![1.0, f64::NAN, f64::INFINITY, f64::NEG_INFINITY, 2.0, 3.0];
        // Only [1.0, 2.0, 3.0] are finite -> same as calling with just those.
        let with_non_finite = conformal_quantile(&residuals, 0.5).unwrap();
        let finite_only = conformal_quantile(&[1.0, 2.0, 3.0], 0.5).unwrap();
        assert_relative_eq!(with_non_finite, finite_only, epsilon = 1e-12);
    }

    #[test]
    fn test_conformal_predict_insufficient_reports_unbounded() {
        let residuals = vec![1.0, 2.0, 3.0, 4.0, 5.0];
        let forecasts = vec![10.0];
        let result = conformal_predict(&residuals, &forecasts, 0.1).unwrap();
        assert!(result.calibration_insufficient);
        assert!(result.conformity_score.is_infinite());
        assert!(result.lower[0].is_infinite() && result.lower[0] < 0.0);
        assert!(result.upper[0].is_infinite() && result.upper[0] > 0.0);
        assert!(result.coverage.is_nan());
        assert_eq!(result.n_calibration, 5);
    }

    #[test]
    fn test_conformal_predict_sufficient_is_not_flagged() {
        let residuals = vec![1.0, 2.0, 3.0, 4.0, 5.0];
        let forecasts = vec![10.0];
        let result = conformal_predict(&residuals, &forecasts, 0.2).unwrap();
        assert!(!result.calibration_insufficient);
        assert_relative_eq!(result.conformity_score, 5.0, epsilon = 1e-9);
        assert_relative_eq!(result.coverage, 1.0, epsilon = 1e-9);
    }

    #[test]
    fn test_conformal_quantile_empty_is_insufficient_not_error() {
        // Phase 16 D-05: an empty residual pool is n=0, always insufficient,
        // and no longer an error -- it reports an infinite (unbounded) score.
        let residuals: Vec<f64> = vec![];
        let q = conformal_quantile(&residuals, 0.1).unwrap();
        assert!(q.is_infinite() && q > 0.0);
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
        assert_relative_eq!(result.nominal_level, 0.9, epsilon = 0.001);
        // Empirical coverage on the calibration set itself: the conformity
        // score is the max abs residual (0.8) at this n/alpha, so every
        // calibration residual is within it.
        assert_relative_eq!(result.coverage, 1.0, epsilon = 0.001);
        assert_eq!(result.n_calibration, 10);
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
        // Phase 16 D-08: new signature takes calibration AND prediction
        // difficulty separately. difficulty_cal all 1.0 keeps the scores
        // numerically identical to the pre-16-02 unweighted case.
        let residuals = vec![0.5, -0.3, 0.8, -0.2, 0.4, -0.6, 0.3, -0.4, 0.7, -0.5];
        let difficulty_cal = vec![1.0; 10];
        let forecasts = vec![100.0, 105.0, 110.0];
        let difficulty_pred = vec![1.0, 2.0, 0.5]; // Second point is hardest

        let result = conformal_predict_adaptive(
            &residuals,
            &difficulty_cal,
            &forecasts,
            &difficulty_pred,
            0.1,
        )
        .unwrap();

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
        let difficulty_cal = vec![1.0, 1.0, 1.0];
        let forecasts = vec![100.0, 105.0, 110.0];
        let difficulty_pred = vec![1.0, -2.0, 0.5]; // Negative difficulty

        assert!(conformal_predict_adaptive(
            &residuals,
            &difficulty_cal,
            &forecasts,
            &difficulty_pred,
            0.1
        )
        .is_err());
    }

    #[test]
    fn test_conformal_predict_adaptive_batch_independence() {
        // Phase 16 D-08/G14: the prediction difficulty is used AS GIVEN --
        // a forecast's interval does not depend on which other forecasts
        // are in the same batch (no batch-mean normalisation).
        let residuals = vec![0.5, -0.3, 0.8, -0.2, 0.4, -0.6, 0.3, -0.4, 0.7, -0.5];
        let difficulty_cal = vec![1.0; 10];

        let alone =
            conformal_predict_adaptive(&residuals, &difficulty_cal, &[100.0], &[2.0], 0.1).unwrap();
        let with_others = conformal_predict_adaptive(
            &residuals,
            &difficulty_cal,
            &[50.0, 100.0, 9999.0],
            &[0.5, 2.0, 1000.0],
            0.1,
        )
        .unwrap();

        assert_eq!(alone.lower[0], with_others.lower[1]);
        assert_eq!(alone.upper[0], with_others.upper[1]);
    }

    #[test]
    fn test_conformal_predict_asymmetric() {
        // Skewed residuals: mostly positive (over-predictions). alpha 0.2
        // (not 0.1): with n=10, half_alpha=0.05 makes ceil(11*0.95)=11 > 10
        // INSUFFICIENT under Phase 16 D-05 at alpha 0.1; alpha 0.2 gives
        // half_alpha=0.1, ceil(11*0.9)=10 <= 10, sufficient.
        let residuals = vec![2.0, 3.0, 2.5, 3.5, 4.0, -0.5, -0.3, -0.2, -0.1, 2.8];
        let forecasts = vec![100.0];

        let result = conformal_predict_asymmetric(&residuals, &forecasts, 0.2).unwrap();

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
    fn test_conformal_method_split_alias() {
        // Phase 16: 'split' is an alias of Symmetric (docs used this name).
        assert_eq!(
            "split".parse::<ConformalMethod>().unwrap(),
            ConformalMethod::Symmetric
        );
        let err = "bogus".parse::<ConformalMethod>().unwrap_err();
        let msg = err.to_string();
        assert!(msg.contains("symmetric (alias split"));
    }

    #[test]
    fn test_conformal_from_pairs_null_actual_excluded_from_n_calibration() {
        let actuals: Vec<Option<f64>> = vec![Some(101.0), None, Some(109.0), Some(111.0)];
        let forecasts: Vec<Option<f64>> = vec![Some(100.0), Some(100.0), Some(100.0), Some(100.0)];
        let points: Vec<Option<f64>> = vec![Some(100.0)];

        let result = conformal_from_pairs(
            &actuals,
            &forecasts,
            None,
            &points,
            None,
            0.1,
            ConformalMethod::Symmetric,
        )
        .unwrap();
        // 4 calibration rows, 1 NULL actual dropped -> 3 used.
        assert_eq!(result.n_calibration, 3);
    }

    #[test]
    fn test_conformal_from_pairs_length_mismatch_names_both_lengths() {
        let actuals: Vec<Option<f64>> = vec![Some(1.0), Some(2.0), Some(3.0)];
        let forecasts: Vec<Option<f64>> = vec![Some(1.0), Some(2.0)];
        let points: Vec<Option<f64>> = vec![];

        let err = conformal_from_pairs(
            &actuals,
            &forecasts,
            None,
            &points,
            None,
            0.1,
            ConformalMethod::Symmetric,
        )
        .unwrap_err();
        let msg = err.to_string();
        assert!(msg.contains('3'));
        assert!(msg.contains('2'));
    }

    #[test]
    fn test_conformal_from_pairs_empty_point_list_is_valid() {
        let actuals: Vec<Option<f64>> = vec![Some(100.5), Some(99.5), Some(100.2), Some(99.8)];
        let forecasts: Vec<Option<f64>> = vec![Some(100.0), Some(100.0), Some(100.0), Some(100.0)];
        let points: Vec<Option<f64>> = vec![];

        let result = conformal_from_pairs(
            &actuals,
            &forecasts,
            None,
            &points,
            None,
            0.1,
            ConformalMethod::Symmetric,
        )
        .unwrap();
        assert!(result.point.is_empty());
        assert!(result.lower.is_empty());
        assert!(result.upper.is_empty());
    }

    #[test]
    fn test_conformal_from_pairs_null_point_position_stays_nan() {
        let actuals: Vec<Option<f64>> = vec![Some(100.5), Some(99.5), Some(100.2), Some(99.8)];
        let forecasts: Vec<Option<f64>> = vec![Some(100.0), Some(100.0), Some(100.0), Some(100.0)];
        let points: Vec<Option<f64>> = vec![Some(30.0), None, Some(20.0)];

        let result = conformal_from_pairs(
            &actuals,
            &forecasts,
            None,
            &points,
            None,
            0.1,
            ConformalMethod::Symmetric,
        )
        .unwrap();
        assert_eq!(result.point.len(), 3);
        assert!(result.point[0].is_finite());
        assert!(result.point[1].is_nan());
        assert!(result.lower[1].is_nan());
        assert!(result.upper[1].is_nan());
        assert!(result.point[2].is_finite());
    }

    #[test]
    fn test_conformal_from_pairs_adaptive_requires_difficulty() {
        let actuals: Vec<Option<f64>> = vec![Some(1.0), Some(2.0)];
        let forecasts: Vec<Option<f64>> = vec![Some(1.0), Some(1.0)];
        let points: Vec<Option<f64>> = vec![];

        let err = conformal_from_pairs(
            &actuals,
            &forecasts,
            None,
            &points,
            None,
            0.1,
            ConformalMethod::Adaptive,
        )
        .unwrap_err();
        assert!(err.to_string().contains("difficulty"));
    }

    #[test]
    fn test_conformal_from_pairs_symmetric_rejects_present_difficulty() {
        let actuals: Vec<Option<f64>> = vec![Some(1.0), Some(2.0), Some(3.0)];
        let forecasts: Vec<Option<f64>> = vec![Some(0.0), Some(0.0), Some(0.0)];
        let difficulty: Vec<Option<f64>> = vec![Some(1.0), Some(2.0), Some(1.0)];
        let points: Vec<Option<f64>> = vec![];

        let err = conformal_from_pairs(
            &actuals,
            &forecasts,
            Some(&difficulty),
            &points,
            None,
            0.1,
            ConformalMethod::Symmetric,
        )
        .unwrap_err();
        assert!(err.to_string().contains("only used by method 'adaptive'"));
    }

    #[test]
    fn test_conformal_from_pairs_adaptive_with_difficulty_matches_predict_adaptive() {
        let actuals: Vec<Option<f64>> = vec![
            Some(100.5),
            Some(99.0),
            Some(101.0),
            Some(98.0),
            Some(102.0),
        ];
        let forecasts: Vec<Option<f64>> = vec![Some(100.0); 5];
        let difficulty: Vec<Option<f64>> =
            vec![Some(1.0), Some(2.0), Some(1.0), Some(2.0), Some(1.0)];
        let points: Vec<Option<f64>> = vec![Some(10.0)];
        let point_difficulty: Vec<Option<f64>> = vec![Some(2.0)];

        let result = conformal_from_pairs(
            &actuals,
            &forecasts,
            Some(&difficulty),
            &points,
            Some(&point_difficulty),
            0.5,
            ConformalMethod::Adaptive,
        )
        .unwrap();
        assert_eq!(result.method, "adaptive");
        assert_eq!(result.n_calibration, 5);

        // Direct call with the same (already-complete) data must agree.
        let residuals = vec![0.5, -1.0, 1.0, -2.0, 2.0];
        let difficulty_cal = vec![1.0, 2.0, 1.0, 2.0, 1.0];
        let direct =
            conformal_predict_adaptive(&residuals, &difficulty_cal, &[10.0], &[2.0], 0.5).unwrap();
        assert_relative_eq!(result.lower[0], direct.lower[0], epsilon = 1e-9);
        assert_relative_eq!(result.upper[0], direct.upper[0], epsilon = 1e-9);
    }

    #[test]
    fn test_conformal_calibrate_adaptive_with_difficulty() {
        let actuals: Vec<Option<f64>> = vec![Some(101.0), Some(98.0), Some(103.0), Some(96.0)];
        let forecasts: Vec<Option<f64>> = vec![Some(100.0); 4];
        let difficulty: Vec<Option<f64>> = vec![Some(1.0), Some(2.0), Some(1.0), Some(2.0)];

        let cal = conformal_calibrate(
            &actuals,
            &forecasts,
            Some(&difficulty),
            0.5,
            ConformalMethod::Adaptive,
        )
        .unwrap();
        assert_eq!(cal.method, ConformalMethod::Adaptive);
        assert_eq!(cal.n_calibration, 4);
        assert!(cal.conformity_score > 0.0);
    }

    #[test]
    fn test_conformal_from_pairs_method_is_canonical_display_name() {
        let actuals: Vec<Option<f64>> = vec![Some(1.0), Some(-1.0), Some(2.0)];
        let forecasts: Vec<Option<f64>> = vec![Some(0.0), Some(0.0), Some(0.0)];
        let points: Vec<Option<f64>> = vec![Some(0.0)];

        let result = conformal_from_pairs(
            &actuals,
            &forecasts,
            None,
            &points,
            None,
            0.1,
            ConformalMethod::Symmetric,
        )
        .unwrap();
        assert_eq!(result.method, "symmetric");

        let result = conformal_from_pairs(
            &actuals,
            &forecasts,
            None,
            &points,
            None,
            0.1,
            ConformalMethod::Asymmetric,
        )
        .unwrap();
        assert_eq!(result.method, "asymmetric");
    }

    #[test]
    fn test_conformal_calibrate_rejects_asymmetric() {
        let actuals: Vec<Option<f64>> = vec![Some(1.0), Some(-1.0), Some(2.0)];
        let forecasts: Vec<Option<f64>> = vec![Some(0.0), Some(0.0), Some(0.0)];

        let err = conformal_calibrate(&actuals, &forecasts, None, 0.1, ConformalMethod::Asymmetric)
            .unwrap_err();
        assert!(err.to_string().contains("supports methods"));
    }

    #[test]
    fn test_conformal_calibrate_symmetric_matches_from_pairs() {
        let actuals: Vec<Option<f64>> = vec![
            Some(100.5),
            Some(99.5),
            Some(100.2),
            Some(99.8),
            Some(100.9),
        ];
        let forecasts: Vec<Option<f64>> = vec![
            Some(100.0),
            Some(100.0),
            Some(100.0),
            Some(100.0),
            Some(100.0),
        ];

        let cal = conformal_calibrate(&actuals, &forecasts, None, 0.1, ConformalMethod::Symmetric)
            .unwrap();
        assert_eq!(cal.n_calibration, 5);
        assert_eq!(cal.method, ConformalMethod::Symmetric);
        assert!(cal.conformity_score > 0.0);
        assert_relative_eq!(cal.nominal_level, 0.9, epsilon = 1e-9);
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

        // Invalid
        assert!("invalid".parse::<ConformalStrategy>().is_err());
    }

    #[test]
    fn test_conformal_strategy_display() {
        assert_eq!(ConformalStrategy::Split.to_string(), "split");
        assert_eq!(ConformalStrategy::CrossVal.to_string(), "crossval");
    }

    // =========================================================================
    // D-09/G15: every removed jackknife+ alias raises an explanatory error
    // naming 'split' and 'crossval'; an otherwise-unknown strategy/method
    // string lists the valid names.
    // =========================================================================

    #[test]
    fn test_jackknife_plus_aliases_removed_with_explanatory_error() {
        for alias in [
            "jackknife+",
            "JackknifePlus",
            "jackknifeplus",
            "jackknife_plus",
            "JK+",
        ] {
            let err = alias.parse::<ConformalStrategy>().unwrap_err().to_string();
            assert!(
                err.contains("was removed"),
                "{alias}: error {err:?} does not mention removal"
            );
            assert!(
                err.contains("'split'"),
                "{alias}: error {err:?} does not name 'split'"
            );
            assert!(
                err.contains("'crossval'"),
                "{alias}: error {err:?} does not name 'crossval'"
            );
            assert!(err.len() < 255, "{alias}: error {err:?} is >= 255 bytes");
        }
    }

    #[test]
    fn test_unknown_conformal_strategy_lists_valid_names() {
        let err = "typo".parse::<ConformalStrategy>().unwrap_err().to_string();
        assert!(err.contains("split"), "error {err:?} does not list 'split'");
        assert!(
            err.contains("crossval"),
            "error {err:?} does not list 'crossval'"
        );
    }

    #[test]
    fn test_unknown_conformal_method_lists_valid_names() {
        let err = "bogus".parse::<ConformalMethod>().unwrap_err().to_string();
        assert!(
            err.contains("symmetric"),
            "error {err:?} does not list 'symmetric'"
        );
        assert!(
            err.contains("asymmetric"),
            "error {err:?} does not list 'asymmetric'"
        );
        assert!(
            err.contains("adaptive"),
            "error {err:?} does not list 'adaptive'"
        );
    }

    #[test]
    fn test_split_crossval_cv_strategies_still_parse() {
        assert_eq!(
            "split".parse::<ConformalStrategy>().unwrap(),
            ConformalStrategy::Split
        );
        assert_eq!(
            "crossval".parse::<ConformalStrategy>().unwrap(),
            ConformalStrategy::CrossVal
        );
        assert_eq!(
            "cv".parse::<ConformalStrategy>().unwrap(),
            ConformalStrategy::CrossVal
        );
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
        // Skewed residuals: mostly positive. alpha 0.2 (not 0.1): see
        // test_conformal_predict_asymmetric for why n=10 at alpha 0.1 is
        // now insufficient under Phase 16 D-05.
        let residuals = vec![2.0, 3.0, 2.5, 3.5, 4.0, -0.5, -0.3, -0.2, -0.1, 2.8];
        let alphas = vec![0.2];

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
    fn test_conformal_apply_adaptive_batch_independence() {
        // Phase 16 D-08/G14: conformal_apply's Adaptive branch no longer
        // normalises by the batch mean -- a forecast's interval doesn't
        // depend on which other forecasts are in the same batch.
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

        let alone = conformal_apply(&[105.0], &profile, Some(&[2.0])).unwrap();
        let with_others =
            conformal_apply(&[1.0, 105.0, 9999.0], &profile, Some(&[0.1, 2.0, 500.0])).unwrap();

        assert_eq!(alone.lower[0][0], with_others.lower[0][1]);
        assert_eq!(alone.upper[0][0], with_others.upper[0][1]);
    }

    #[test]
    fn test_conformal_apply_score_symmetric() {
        let forecasts: Vec<Option<f64>> = vec![Some(100.0), None, Some(110.0)];
        let (lower, upper) =
            conformal_apply_score(&forecasts, 5.0, ConformalMethod::Symmetric, &[]).unwrap();
        assert_eq!(lower[0], 95.0);
        assert_eq!(upper[0], 105.0);
        assert!(lower[1].is_nan());
        assert!(upper[1].is_nan());
        assert_eq!(lower[2], 105.0);
        assert_eq!(upper[2], 115.0);
    }

    #[test]
    fn test_conformal_apply_score_symmetric_rejects_present_difficulty() {
        let forecasts: Vec<Option<f64>> = vec![Some(100.0)];
        let difficulty: Vec<Option<f64>> = vec![Some(1.0)];
        let err = conformal_apply_score(&forecasts, 5.0, ConformalMethod::Symmetric, &difficulty)
            .unwrap_err();
        assert!(err.to_string().contains("only used by method 'adaptive'"));
    }

    #[test]
    fn test_conformal_apply_score_asymmetric_is_rejected() {
        let forecasts: Vec<Option<f64>> = vec![Some(100.0)];
        let err =
            conformal_apply_score(&forecasts, 5.0, ConformalMethod::Asymmetric, &[]).unwrap_err();
        assert!(matches!(err, ForecastError::InvalidInput(_)));
    }

    #[test]
    fn test_conformal_apply_score_adaptive_requires_difficulty() {
        let forecasts: Vec<Option<f64>> = vec![Some(100.0)];
        let err =
            conformal_apply_score(&forecasts, 5.0, ConformalMethod::Adaptive, &[]).unwrap_err();
        assert!(err.to_string().contains("difficulty"));
    }

    #[test]
    fn test_conformal_apply_score_adaptive_missing_difficulty_at_present_forecast_is_nan() {
        let forecasts: Vec<Option<f64>> = vec![Some(100.0), Some(105.0)];
        let difficulty: Vec<Option<f64>> = vec![Some(2.0), None];
        let (lower, upper) =
            conformal_apply_score(&forecasts, 3.0, ConformalMethod::Adaptive, &difficulty).unwrap();
        assert_eq!(lower[0], 94.0);
        assert_eq!(upper[0], 106.0);
        assert!(lower[1].is_nan());
        assert!(upper[1].is_nan());
    }

    #[test]
    fn test_conformal_apply_score_infinite_score_is_unbounded() {
        let forecasts: Vec<Option<f64>> = vec![Some(100.0)];
        let (lower, upper) =
            conformal_apply_score(&forecasts, f64::INFINITY, ConformalMethod::Symmetric, &[])
                .unwrap();
        assert_eq!(lower[0], f64::NEG_INFINITY);
        assert_eq!(upper[0], f64::INFINITY);
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
    fn test_winkler_score_rejects_alpha_zero() {
        let err = winkler_score(&[100.0], &[95.0], &[105.0], 0.0).unwrap_err();
        assert!(err.to_string().contains("alpha must be in (0, 1), got 0"));
    }

    #[test]
    fn test_conformal_evaluate_rejects_alpha_zero() {
        let err = conformal_evaluate(&[100.0], &[95.0], &[105.0], 0.0).unwrap_err();
        assert!(err.to_string().contains("alpha must be in (0, 1), got 0"));
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

    #[test]
    fn test_conformal_per_step_basic() {
        // 3 folds, horizon 2
        let fold_forecasts = vec![vec![100.0, 105.0], vec![110.0, 115.0], vec![120.0, 125.0]];
        let fold_actuals = vec![vec![101.0, 108.0], vec![109.0, 118.0], vec![121.0, 122.0]];
        let result =
            conformal_predict_per_step(&fold_forecasts, &fold_actuals, &[100.0, 105.0], 0.1)
                .unwrap();
        assert_eq!(result.lower.len(), 2);
        assert_eq!(result.upper.len(), 2);
        assert_eq!(result.half_widths.len(), 2);
    }
}

/// Result of per-step conformal prediction.
#[derive(Debug, Clone)]
pub struct ConformalPerStepResult {
    /// Point forecasts (unchanged from input)
    pub point: Vec<f64>,
    /// Lower bounds (per-step widths)
    pub lower: Vec<f64>,
    /// Upper bounds (per-step widths)
    pub upper: Vec<f64>,
    /// Per-step half-widths
    pub half_widths: Vec<f64>,
    /// Coverage level (1 - alpha)
    pub coverage: f64,
}

/// Compute per-step conformal prediction intervals.
///
/// Each horizon step gets its own interval width based on the actual forecast error
/// at that step. Early steps (h=1) typically get tighter intervals than later steps.
///
/// WR-03 (phase 12 review fix): this is no longer the production code path —
/// the FFI boundary (`anofox_ts_conformal_predict_per_step`) calls
/// [`conformal_predict_per_step_validated`] instead, which adds D-10 input
/// validation and D-02 pairwise-complete missing-data handling. This function
/// is kept `#[cfg(test)]`-only as a cross-check oracle: `complete_data_matches
/// _crate_backed_function` (below) asserts the two implementations agree on
/// complete data, guarding against the validated variant silently drifting
/// from the crate's own per-step quantile formula.
///
/// # Arguments
/// * `fold_forecasts` - Forecasts from each CV fold, each inner Vec is one fold's forecast horizon
/// * `fold_actuals` - Actuals from each CV fold, aligned with fold_forecasts
/// * `point_forecasts` - New point forecasts to wrap with intervals
/// * `alpha` - Miscoverage rate (e.g., 0.1 for 90% coverage)
#[cfg(test)]
pub(crate) fn conformal_predict_per_step(
    fold_forecasts: &[Vec<f64>],
    fold_actuals: &[Vec<f64>],
    point_forecasts: &[f64],
    alpha: f64,
) -> Result<ConformalPerStepResult> {
    use anofox_forecast::postprocess::ConformalPredictor;

    if fold_forecasts.len() < 2 {
        return Err(ForecastError::InsufficientData {
            needed: 2,
            got: fold_forecasts.len(),
        });
    }
    if fold_forecasts.len() != fold_actuals.len() {
        return Err(ForecastError::InvalidInput(format!(
            "fold_forecasts has {} folds but fold_actuals has {}",
            fold_forecasts.len(),
            fold_actuals.len()
        )));
    }
    validate_alpha(alpha)?;

    let coverage = 1.0 - alpha;
    let cp = ConformalPredictor::split(coverage);

    let per_step = cp.fit_per_step(fold_forecasts, fold_actuals).map_err(|e| {
        ForecastError::ComputationError(format!("Per-step conformal fit failed: {}", e))
    })?;

    let (lower, upper) = per_step.predict(point_forecasts);
    let half_widths = per_step.half_widths().to_vec();

    Ok(ConformalPerStepResult {
        point: point_forecasts.to_vec(),
        lower,
        upper,
        half_widths,
        coverage,
    })
}

/// Compute the conformal quantile the same way `ConformalPredictor`'s private
/// `compute_quantile` does for the `Split`/`CrossVal` methods: sorted
/// scores, adjusted level `((n+1) * coverage / n).min(1.0)`, index
/// `ceil(n * adjusted_level) - 1` clamped into `[0, n-1]`. Used so a per-step
/// quantile computed over a FULL (no missing pairs) step is bitwise identical
/// to [`conformal_predict_per_step`]'s result for that step (D-02).
///
/// Phase 16 D-05/G16: sorts with `f64::total_cmp` (identical ordering to the
/// old unwrapped `partial_cmp` for finite input, but never panics), and
/// returns `f64::INFINITY` when the finite-sample level helper (`alpha =
/// 1 - coverage`) reports insufficiency instead of clamping via `.min(1.0)`.
/// When sufficient, the index formula is UNCHANGED, so complete-data results
/// stay bit-identical to the crate oracle.
fn split_quantile(scores: &[f64], coverage: f64) -> f64 {
    let mut sorted = scores.to_vec();
    sorted.sort_by(f64::total_cmp);
    let n = sorted.len();
    if finite_sample_level(n, 1.0 - coverage).is_none() {
        return f64::INFINITY;
    }
    let adjusted_level = (((n + 1) as f64) * coverage / n as f64).min(1.0);
    let idx = ((n as f64) * adjusted_level).ceil() as usize;
    let idx = idx.saturating_sub(1).min(n - 1);
    sorted[idx]
}

/// Validated, pairwise-complete per-step conformal prediction (Phase 12
/// plan 12-04, D-02/D-10).
///
/// `fold_forecasts`/`fold_actuals` are FLAT arrays of `n_folds * horizon`
/// elements (fold-major: fold 0's `horizon` steps, then fold 1's, ...), with
/// `None`/`NaN` entries marking missing values. Unlike
/// [`conformal_predict_per_step`], this entry point:
/// - validates `horizon > 0`, equal lengths, and `len % horizon == 0` in Rust
///   (D-10: no silent truncation of `n_folds` by the caller)
/// - drops a (forecast, actual) pair from a step's quantile computation only
///   when that specific pair is incomplete (pairwise-complete, D-02) rather
///   than requiring every fold to be fully present
/// - is position-preserving for `point_forecasts`: a missing point forecast
///   yields `NaN` at that position in `point`/`lower`/`upper` only.
pub fn conformal_predict_per_step_validated(
    fold_forecasts: &[Option<f64>],
    fold_actuals: &[Option<f64>],
    horizon: usize,
    point_forecasts: &[Option<f64>],
    alpha: f64,
) -> Result<ConformalPerStepResult> {
    use crate::missing;

    if horizon == 0 {
        return Err(ForecastError::InvalidInput(
            "per-step conformal: horizon must be positive".to_string(),
        ));
    }
    if fold_forecasts.len() != fold_actuals.len() {
        return Err(ForecastError::InvalidInput(format!(
            "per-step conformal: fold_forecasts has {} elements but fold_actuals has {}",
            fold_forecasts.len(),
            fold_actuals.len()
        )));
    }
    if !fold_forecasts.len().is_multiple_of(horizon) {
        return Err(ForecastError::InvalidInput(format!(
            "per-step conformal: residual count {} is not a multiple of horizon {}",
            fold_forecasts.len(),
            horizon
        )));
    }
    if point_forecasts.len() != horizon {
        return Err(ForecastError::InvalidInput(format!(
            "per-step conformal: point_forecasts has {} elements but horizon is {}",
            point_forecasts.len(),
            horizon
        )));
    }

    let n_folds = fold_forecasts.len() / horizon;
    if n_folds < 2 {
        return Err(ForecastError::InsufficientData {
            needed: 2,
            got: n_folds,
        });
    }
    validate_alpha(alpha)?;

    let coverage = 1.0 - alpha;

    // Pooled fallback: every complete (forecast, actual) pair across every
    // fold and step, flattened — mirrors the crate's own pooled-quantile
    // fallback in `fit_per_step` (used when a step has fewer than 2
    // residuals), but computed over complete pairs only. Phase 16 D-05: a
    // pooled fallback with fewer than 2 pairs is INSUFFICIENT (an infinite
    // half-width), never a silent 0.0 width.
    let pooled = missing::complete_cases(&[fold_forecasts, fold_actuals])?;
    // Phase 16 D-06/G16: a complete (forecast, actual) pair that is itself
    // non-finite (e.g. both +inf, whose |f - a| is NaN) is dropped here
    // rather than reaching `split_quantile`'s sort.
    let pooled_scores: Vec<f64> = pooled[0]
        .iter()
        .zip(pooled[1].iter())
        .map(|(f, a)| (f - a).abs())
        .filter(|s| s.is_finite())
        .collect();
    let pooled_quantile = split_quantile(&pooled_scores, coverage);

    let mut half_widths = Vec::with_capacity(horizon);
    for t in 0..horizon {
        let step_forecasts: Vec<Option<f64>> = (0..n_folds)
            .map(|f| fold_forecasts[f * horizon + t])
            .collect();
        let step_actuals: Vec<Option<f64>> = (0..n_folds)
            .map(|f| fold_actuals[f * horizon + t])
            .collect();

        let complete = missing::complete_cases(&[&step_forecasts, &step_actuals])?;
        // Phase 16 D-06/G16: use only complete pairs whose |f - a| is finite
        // -- a pair that is itself non-finite (e.g. both +inf) is dropped
        // here instead of ever reaching `split_quantile`'s sort.
        let finite_scores: Vec<f64> = complete[0]
            .iter()
            .zip(complete[1].iter())
            .map(|(f, a)| (f - a).abs())
            .filter(|s| s.is_finite())
            .collect();

        let hw = if finite_scores.len() >= 2 {
            // Complete step (== n_folds, all finite) or a partial step with
            // at least 2 finite complete pairs: the same `split_quantile`
            // formula replicates the crate's own per-step quantile exactly
            // when complete (D-02) and reports insufficiency (infinite
            // half-width) via the finite-sample level helper otherwise
            // (D-05/G16) -- never the upstream `cp.fit` call, which clamps.
            split_quantile(&finite_scores, coverage)
        } else {
            pooled_quantile
        };
        half_widths.push(hw);
    }

    let point_filled = missing::fill_missing(point_forecasts, 0.0);
    let n = point_filled.len().min(half_widths.len());
    let mut lower: Vec<f64> = (0..n).map(|i| point_filled[i] - half_widths[i]).collect();
    let mut upper: Vec<f64> = (0..n).map(|i| point_filled[i] + half_widths[i]).collect();
    let mut point = point_filled.clone();

    missing::propagate_missing(&mut point, point_forecasts);
    missing::propagate_missing(&mut lower, &point_forecasts[..n]);
    missing::propagate_missing(&mut upper, &point_forecasts[..n]);

    Ok(ConformalPerStepResult {
        point,
        lower,
        upper,
        half_widths,
        coverage,
    })
}

#[cfg(test)]
mod per_step_pairwise_tests {
    use super::*;

    fn to_opt(v: &[f64]) -> Vec<Option<f64>> {
        v.iter().map(|&x| Some(x)).collect()
    }

    #[test]
    fn complete_data_matches_crate_backed_function() {
        // 3 folds, horizon 2 — same data as test_conformal_per_step_basic.
        // alpha 0.3 (not 0.1): with n=3 per step, ceil(4*0.9)=4 > 3 makes
        // alpha 0.1 INSUFFICIENT under Phase 16 D-05 -- restricted to a
        // sufficient configuration (ceil(4*0.7)=3 <= 3) so this oracle
        // comparison stays meaningful; see insufficient_step_reports_infinite
        // for the insufficient-case behavior.
        let fold_forecasts = [100.0, 105.0, 110.0, 115.0, 120.0, 125.0];
        let fold_actuals = [101.0, 108.0, 109.0, 118.0, 121.0, 122.0];
        let point = [100.0, 105.0];
        let alpha = 0.3;

        let old = conformal_predict_per_step(
            &[vec![100.0, 105.0], vec![110.0, 115.0], vec![120.0, 125.0]],
            &[vec![101.0, 108.0], vec![109.0, 118.0], vec![121.0, 122.0]],
            &point,
            alpha,
        )
        .unwrap();

        let new = conformal_predict_per_step_validated(
            &to_opt(&fold_forecasts),
            &to_opt(&fold_actuals),
            2,
            &to_opt(&point),
            alpha,
        )
        .unwrap();

        for i in 0..2 {
            assert!(
                (old.half_widths[i] - new.half_widths[i]).abs() < 1e-12,
                "step {}: old={} new={}",
                i,
                old.half_widths[i],
                new.half_widths[i]
            );
        }
        assert_eq!(old.lower, new.lower);
        assert_eq!(old.upper, new.upper);
    }

    #[test]
    fn one_missing_pair_leaves_other_steps_identical_to_complete_result() {
        // 4 folds, 3 steps. fold index 1, step 0's actual is missing.
        let fold_forecasts = [
            100.0, 102.0, 104.0, // fold 0
            110.0, 112.0, 114.0, // fold 1
            120.0, 122.0, 124.0, // fold 2
            130.0, 132.0, 134.0, // fold 3
        ];
        let fold_actuals = [
            101.0, 103.0, 106.0, // fold 0
            109.0, 111.0, 116.0, // fold 1 (step0 actual present here)
            121.0, 123.0, 127.0, // fold 2
            128.0, 134.0, 132.0, // fold 3
        ];
        let point = [100.0, 105.0, 110.0];
        // alpha 0.3 (not 0.1): sufficient for both n=4 (complete steps,
        // ceil(5*0.7)=4<=4) and n=3 (step 0 after dropping the missing
        // pair, ceil(4*0.7)=3<=3) under Phase 16 D-05.
        let alpha = 0.3;

        let complete = conformal_predict_per_step_validated(
            &to_opt(&fold_forecasts),
            &to_opt(&fold_actuals),
            3,
            &to_opt(&point),
            alpha,
        )
        .unwrap();

        // Now make fold 1, step 0's actual missing.
        let mut fold_actuals_opt = to_opt(&fold_actuals);
        fold_actuals_opt[3] = None;

        let with_missing = conformal_predict_per_step_validated(
            &to_opt(&fold_forecasts),
            &fold_actuals_opt,
            3,
            &to_opt(&point),
            alpha,
        )
        .unwrap();

        // Steps 1 and 2 (no missing pair) are unaffected.
        assert_eq!(with_missing.half_widths[1], complete.half_widths[1]);
        assert_eq!(with_missing.half_widths[2], complete.half_widths[2]);

        // Step 0 should equal cp.fit() over the 3 remaining complete pairs.
        use anofox_forecast::postprocess::ConformalPredictor;
        let cp = ConformalPredictor::split(1.0 - alpha);
        let remaining_forecasts = vec![100.0, 120.0, 130.0];
        let remaining_actuals = vec![101.0, 121.0, 128.0];
        let expected = cp
            .fit(&remaining_forecasts, &remaining_actuals)
            .unwrap()
            .quantile_value();
        assert!(
            (with_missing.half_widths[0] - expected).abs() < 1e-12,
            "got={} expected={}",
            with_missing.half_widths[0],
            expected
        );
    }

    #[test]
    fn two_folds_alpha_0_1_is_insufficient_every_step() {
        // 2 folds, horizon 3: n=2 per step. ceil(3*0.9)=3 > 2 -> insufficient
        // at every step (Phase 16 D-05).
        let fold_forecasts = to_opt(&[100.0, 105.0, 110.0, 100.0, 105.0, 110.0]);
        let fold_actuals = to_opt(&[101.0, 108.0, 109.0, 103.0, 106.0, 107.0]);
        let point = to_opt(&[100.0, 105.0, 110.0]);

        let result =
            conformal_predict_per_step_validated(&fold_forecasts, &fold_actuals, 3, &point, 0.1)
                .unwrap();

        for (i, hw) in result.half_widths.iter().enumerate() {
            assert!(hw.is_infinite(), "step {i}: expected infinite, got {hw}");
            assert!(result.lower[i].is_infinite() && result.lower[i] < 0.0);
            assert!(result.upper[i].is_infinite() && result.upper[i] > 0.0);
        }
    }

    #[test]
    fn two_folds_alpha_0_4_is_sufficient_and_bit_identical_to_oracle() {
        // Same 2-fold data, alpha 0.4: ceil(3*0.6)=2<=2 -> sufficient.
        let fold_forecasts = [100.0, 105.0, 110.0, 100.0, 105.0, 110.0];
        let fold_actuals = [101.0, 108.0, 109.0, 103.0, 106.0, 107.0];
        let point = [100.0, 105.0, 110.0];
        let alpha = 0.4;

        let old = conformal_predict_per_step(
            &[vec![100.0, 105.0, 110.0], vec![100.0, 105.0, 110.0]],
            &[vec![101.0, 108.0, 109.0], vec![103.0, 106.0, 107.0]],
            &point,
            alpha,
        )
        .unwrap();

        let new = conformal_predict_per_step_validated(
            &to_opt(&fold_forecasts),
            &to_opt(&fold_actuals),
            3,
            &to_opt(&point),
            alpha,
        )
        .unwrap();

        for i in 0..3 {
            assert!(new.half_widths[i].is_finite(), "step {i} should be finite");
            assert!(
                (old.half_widths[i] - new.half_widths[i]).abs() < 1e-12,
                "step {}: old={} new={}",
                i,
                old.half_widths[i],
                new.half_widths[i]
            );
        }
    }

    #[test]
    fn infinite_forecast_and_actual_pair_is_dropped_not_panicking() {
        // 3 folds, horizon 1. Fold 1's forecast AND actual are both +inf, so
        // |f - a| = NaN -- complete_cases keeps the pair (neither side is
        // individually NaN) but the per-step filter must still drop it by
        // score finiteness (D-06/G16), leaving 2 finite pairs (sufficient at
        // alpha 0.4: ceil(3*0.6)=2<=2) instead of panicking on the sort.
        let fold_forecasts = to_opt(&[100.0, f64::INFINITY, 110.0]);
        let fold_actuals = to_opt(&[101.0, f64::INFINITY, 109.0]);
        let point = to_opt(&[100.0]);

        let result =
            conformal_predict_per_step_validated(&fold_forecasts, &fold_actuals, 1, &point, 0.4)
                .unwrap();

        assert!(result.half_widths[0].is_finite());
        // Equals the 2-pair (fold 0, fold 2) quantile directly.
        let expected = split_quantile(&[1.0, 1.0], 0.6);
        assert!((result.half_widths[0] - expected).abs() < 1e-12);
    }

    #[test]
    fn pooled_fallback_with_fewer_than_two_pairs_is_insufficient_not_zero() {
        // 2 folds, horizon 2. Every pair except (fold 0, step 0) is missing,
        // so EVERY step has < 2 complete pairs and falls back to the pooled
        // quantile -- but the pooled set itself has only 1 pair, which is
        // insufficient at alpha 0.1 (ceil(2*0.9)=2 > 1). Must report an
        // infinite half-width at every step, never a silent 0.0 width.
        let fold_forecasts = to_opt(&[100.0, 105.0, 200.0, 205.0]);
        let fold_actuals: Vec<Option<f64>> = vec![Some(101.0), None, None, None];
        let point = to_opt(&[100.0, 105.0]);

        let result =
            conformal_predict_per_step_validated(&fold_forecasts, &fold_actuals, 2, &point, 0.1)
                .unwrap();

        for (i, hw) in result.half_widths.iter().enumerate() {
            assert!(hw.is_infinite(), "step {i}: expected infinite, got {hw}");
            assert_ne!(*hw, 0.0, "step {i}: must not be a silent 0.0 width");
        }
    }

    #[test]
    fn residual_count_not_multiple_of_horizon_errors_naming_both() {
        let seven = vec![Some(1.0); 7];
        let point = vec![Some(1.0), Some(2.0), Some(3.0)];
        let err = conformal_predict_per_step_validated(&seven, &seven, 3, &point, 0.1).unwrap_err();
        let msg = err.to_string();
        assert!(msg.contains('7'), "message should contain 7: {}", msg);
        assert!(msg.contains('3'), "message should contain 3: {}", msg);
        assert!(
            msg.contains("not a multiple of horizon"),
            "message: {}",
            msg
        );
    }

    #[test]
    fn horizon_zero_errors() {
        let v = vec![Some(1.0); 4];
        let err = conformal_predict_per_step_validated(&v, &v, 0, &v, 0.1).unwrap_err();
        assert!(matches!(err, ForecastError::InvalidInput(_)));
    }

    #[test]
    fn point_forecasts_longer_than_horizon_errors_naming_both_lengths() {
        // CR-01 regression: 2 folds * 3 steps = 6 residuals, horizon=3, but
        // point_forecasts has 5 elements (> horizon). Before the fix this
        // silently produced mismatched point/lower/upper lengths and the FFI
        // layer read `lower`/`upper` out of bounds using `point.len()`.
        let fold_forecasts = to_opt(&[100.0, 105.0, 110.0, 100.0, 105.0, 110.0]);
        let fold_actuals = to_opt(&[101.0, 108.0, 109.0, 101.0, 108.0, 109.0]);
        let point = to_opt(&[100.0, 101.0, 102.0, 103.0, 104.0]);

        let err =
            conformal_predict_per_step_validated(&fold_forecasts, &fold_actuals, 3, &point, 0.1)
                .unwrap_err();
        assert!(matches!(err, ForecastError::InvalidInput(_)));
        let msg = err.to_string();
        assert!(msg.contains('5'), "message should contain 5: {}", msg);
        assert!(msg.contains('3'), "message should contain 3: {}", msg);
    }

    #[test]
    fn point_forecasts_shorter_than_horizon_errors_naming_both_lengths() {
        let fold_forecasts = to_opt(&[100.0, 105.0, 110.0, 100.0, 105.0, 110.0]);
        let fold_actuals = to_opt(&[101.0, 108.0, 109.0, 101.0, 108.0, 109.0]);
        let point = to_opt(&[100.0, 101.0]);

        let err =
            conformal_predict_per_step_validated(&fold_forecasts, &fold_actuals, 3, &point, 0.1)
                .unwrap_err();
        assert!(matches!(err, ForecastError::InvalidInput(_)));
        let msg = err.to_string();
        assert!(msg.contains('2'), "message should contain 2: {}", msg);
        assert!(msg.contains('3'), "message should contain 3: {}", msg);
    }

    #[test]
    fn null_point_forecast_is_position_preserving() {
        let fold_forecasts = [100.0, 105.0, 110.0, 115.0, 120.0, 125.0];
        let fold_actuals = [101.0, 108.0, 109.0, 118.0, 121.0, 122.0];
        let mut point = to_opt(&[100.0, 105.0]);
        point[1] = None;

        let result = conformal_predict_per_step_validated(
            &to_opt(&fold_forecasts),
            &to_opt(&fold_actuals),
            2,
            &point,
            0.1,
        )
        .unwrap();

        assert!(!result.point[0].is_nan());
        assert!(result.point[1].is_nan());
        assert!(result.lower[1].is_nan());
        assert!(result.upper[1].is_nan());
    }
}
