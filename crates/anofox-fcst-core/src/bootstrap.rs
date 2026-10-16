//! Bootstrap prediction intervals for distribution-free uncertainty quantification.
//!
//! Wraps `anofox_forecast::postprocess::BootstrapPredictor` for use in the DuckDB extension.

use crate::error::{ForecastError, Result};

/// Result of bootstrap prediction intervals.
#[derive(Debug, Clone)]
pub struct BootstrapIntervalsResult {
    /// Point forecasts (unchanged from input)
    pub point: Vec<f64>,
    /// Lower bounds of prediction intervals
    pub lower: Vec<f64>,
    /// Upper bounds of prediction intervals
    pub upper: Vec<f64>,
    /// Coverage level (e.g., 0.95 for 95%)
    pub coverage: f64,
}

/// Result of bootstrap quantile prediction.
#[derive(Debug, Clone)]
pub struct BootstrapQuantilesResult {
    /// Point forecasts (unchanged from input)
    pub point: Vec<f64>,
    /// Quantile levels (e.g., [0.1, 0.25, 0.5, 0.75, 0.9])
    pub quantiles: Vec<f64>,
    /// Values for each quantile: values[q][t] = quantile q at time t
    pub values: Vec<Vec<f64>>,
}

/// Compute bootstrap prediction intervals from residuals and point forecasts.
///
/// Uses cumulative residual resampling so intervals grow naturally with horizon.
///
/// # Arguments
/// * `residuals` - Historical residuals (actual - predicted)
/// * `forecasts` - Point forecasts to wrap with intervals
/// * `n_paths` - Number of bootstrap resampling paths (e.g., 1000)
/// * `coverage` - Coverage level (e.g., 0.95 for 95%)
/// * `seed` - Optional random seed for reproducibility
pub fn bootstrap_intervals(
    residuals: &[f64],
    forecasts: &[f64],
    n_paths: usize,
    coverage: f64,
    seed: Option<u64>,
) -> Result<BootstrapIntervalsResult> {
    use anofox_forecast::postprocess::BootstrapPredictor;

    if residuals.is_empty() {
        return Err(ForecastError::InsufficientData { needed: 1, got: 0 });
    }
    if forecasts.is_empty() {
        return Err(ForecastError::InvalidInput(
            "forecasts must not be empty".to_string(),
        ));
    }
    validate_bootstrap_inputs(coverage, n_paths, forecasts.len())?;

    // Build the predictor
    let mut predictor = BootstrapPredictor::new(coverage).n_replicates(n_paths);
    if let Some(s) = seed {
        predictor = predictor.seed(s);
    }

    // Upstream `BootstrapPredictor::fit(forecasts, actuals)` stores
    // `forecasts[i] - actuals[i]` as the residual to resample, and
    // `simulate_paths` adds the cumulative resampled residual to the point
    // forecast at predict time. We already have residuals in the
    // `actual - forecast` convention (this crate's convention throughout),
    // so to make the upstream-stored residual equal `actual - forecast` we
    // must pass OUR residuals as `forecasts` and zeros as `actuals`:
    // stored = residuals - 0 = residuals. Passing zeros first (the old,
    // buggy order) stores `0 - residuals = -residuals`, mirroring every
    // bound across the point forecast (D-10, G2).
    let zeros: Vec<f64> = vec![0.0; residuals.len()];
    let result = predictor
        .fit(residuals, &zeros)
        .map_err(|e| ForecastError::ComputationError(format!("Bootstrap fit failed: {}", e)))?;

    // Predict intervals
    let intervals = predictor.predict(&result, forecasts);

    Ok(BootstrapIntervalsResult {
        point: forecasts.to_vec(),
        lower: intervals.lower().to_vec(),
        upper: intervals.upper().to_vec(),
        coverage,
    })
}

/// Validate bootstrap inputs before any call into the upstream crate
/// (Phase 16 T-16-17): the upstream `BootstrapPredictor::new` asserts
/// `0 < coverage < 1` (a panic, not a `Result`), `n_replicates` silently
/// raises any `n < 10` up to 10, and `n_paths * horizon` sized sample
/// buffers are allocated per simulated step with no upper bound. All three
/// become an explicit `InvalidInput` error naming the offending value(s)
/// instead of a panic, a silently-adjusted sample size, or an unbounded
/// allocation.
fn validate_bootstrap_inputs(coverage: f64, n_paths: usize, horizon: usize) -> Result<()> {
    if !(coverage > 0.0 && coverage < 1.0) {
        return Err(ForecastError::InvalidInput(format!(
            "coverage must be in (0, 1), got {coverage}"
        )));
    }
    if n_paths < 10 {
        return Err(ForecastError::InvalidInput(format!(
            "n_paths must be >= 10, got {n_paths}"
        )));
    }
    let budget = n_paths.saturating_mul(horizon.max(1));
    if budget > 10_000_000 {
        return Err(ForecastError::InvalidInput(format!(
            "n_paths * horizon must be <= 10,000,000 (sample budget), got n_paths={n_paths} * horizon={horizon} = {budget}"
        )));
    }
    Ok(())
}

/// Compute bootstrap quantile forecasts at multiple quantile levels.
///
/// # Arguments
/// * `residuals` - Historical residuals (actual - predicted)
/// * `forecasts` - Point forecasts
/// * `n_paths` - Number of bootstrap resampling paths
/// * `quantile_levels` - Quantile levels to compute (e.g., [0.1, 0.25, 0.5, 0.75, 0.9])
/// * `seed` - Optional random seed for reproducibility
pub fn bootstrap_quantiles(
    residuals: &[f64],
    forecasts: &[f64],
    n_paths: usize,
    quantile_levels: &[f64],
    seed: Option<u64>,
) -> Result<BootstrapQuantilesResult> {
    use anofox_forecast::postprocess::BootstrapPredictor;

    if residuals.is_empty() {
        return Err(ForecastError::InsufficientData { needed: 1, got: 0 });
    }
    if forecasts.is_empty() {
        return Err(ForecastError::InvalidInput(
            "forecasts must not be empty".to_string(),
        ));
    }
    if quantile_levels.is_empty() {
        return Err(ForecastError::InvalidInput(
            "at least one quantile level is required".to_string(),
        ));
    }
    for &q in quantile_levels {
        if !(0.0..=1.0).contains(&q) {
            return Err(ForecastError::InvalidInput(format!(
                "quantile level {} must be between 0 and 1",
                q
            )));
        }
    }
    // Median coverage is only used to satisfy the upstream constructor's
    // `0 < coverage < 1` assert; quantile extraction below never reads it.
    // n_paths / sample-budget validation (D-10/T-16-17) still applies.
    validate_bootstrap_inputs(0.5, n_paths, forecasts.len())?;

    let mut predictor = BootstrapPredictor::new(0.5).n_replicates(n_paths);
    if let Some(s) = seed {
        predictor = predictor.seed(s);
    }

    // See bootstrap_intervals() above for why `fit(residuals, &zeros)` (not
    // `fit(&zeros, residuals)`) is the correct-sign call.
    let zeros: Vec<f64> = vec![0.0; residuals.len()];
    let result = predictor
        .fit(residuals, &zeros)
        .map_err(|e| ForecastError::ComputationError(format!("Bootstrap fit failed: {}", e)))?;

    let qf = predictor.predict_quantiles(&result, forecasts, quantile_levels);

    // Extract values: for each quantile level, get all time-step values
    let mut values = Vec::with_capacity(quantile_levels.len());
    for qi in 0..qf.n_quantiles() {
        values.push(
            qf.at_quantile(qi)
                .unwrap_or_else(|| vec![f64::NAN; forecasts.len()]),
        );
    }

    Ok(BootstrapQuantilesResult {
        point: forecasts.to_vec(),
        quantiles: quantile_levels.to_vec(),
        values,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_bootstrap_intervals_basic() {
        let residuals: Vec<f64> = (0..50).map(|i| (i as f64 * 0.7).sin() * 2.0).collect();
        let forecasts = vec![100.0, 105.0, 110.0];
        let result = bootstrap_intervals(&residuals, &forecasts, 500, 0.95, Some(42)).unwrap();
        assert_eq!(result.point, forecasts);
        assert_eq!(result.lower.len(), 3);
        assert_eq!(result.upper.len(), 3);
        for i in 0..3 {
            assert!(result.lower[i] < result.point[i]);
            assert!(result.upper[i] > result.point[i]);
        }
    }

    #[test]
    fn test_bootstrap_quantiles_basic() {
        let residuals: Vec<f64> = (0..50).map(|i| (i as f64 * 0.7).sin() * 2.0).collect();
        let forecasts = vec![100.0, 105.0];
        let quantiles = vec![0.1, 0.5, 0.9];
        let result =
            bootstrap_quantiles(&residuals, &forecasts, 500, &quantiles, Some(42)).unwrap();
        assert_eq!(result.quantiles, quantiles);
        assert_eq!(result.values.len(), 3);
        for vals in &result.values {
            assert_eq!(vals.len(), 2);
        }
    }

    #[test]
    fn test_bootstrap_empty_residuals() {
        let result = bootstrap_intervals(&[], &[100.0], 500, 0.95, None);
        assert!(result.is_err());
    }

    // =========================================================================
    // D-10/G2: mirror anchor -- a right-skewed residual set puts the long
    // tail on the UPPER side, not the lower side.
    // =========================================================================

    #[test]
    fn test_bootstrap_mirror_anchor_right_skew() {
        // 9 zeros + one 10.0 (P(drawn 10) = 0.1): at coverage 0.9 (alpha
        // 0.05/0.95), floor(0.05 * n) hits a 0 and floor(0.95 * n) hits a 10.
        let residuals = vec![0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 10.0];
        let forecasts = vec![100.0];
        let result = bootstrap_intervals(&residuals, &forecasts, 2000, 0.9, Some(42)).unwrap();
        assert_eq!(
            result.lower[0], 100.0,
            "lower should stay at the point forecast"
        );
        assert_eq!(
            result.upper[0], 110.0,
            "upper should carry the long tail (10.0), not the lower bound"
        );
    }

    #[test]
    fn test_bootstrap_mirror_anchor_left_skew_is_the_mirror_image() {
        // Negating the residuals of the right-skew case must mirror the
        // bounds around the point forecast exactly.
        let residuals = vec![0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, -10.0];
        let forecasts = vec![100.0];
        let result = bootstrap_intervals(&residuals, &forecasts, 2000, 0.9, Some(42)).unwrap();
        assert_eq!(
            result.lower[0], 90.0,
            "lower should carry the long (negative) tail"
        );
        assert_eq!(
            result.upper[0], 100.0,
            "upper should stay at the point forecast"
        );
    }

    // =========================================================================
    // T-16-17: coverage / n_paths / sample-budget validation
    // =========================================================================

    #[test]
    fn test_bootstrap_rejects_coverage_out_of_range() {
        let residuals = vec![1.0, -1.0, 2.0, -2.0, 0.5, -0.5, 1.5, -1.5, 0.3, -0.3];
        let forecasts = vec![100.0];
        let err = bootstrap_intervals(&residuals, &forecasts, 2000, 1.5, Some(42)).unwrap_err();
        assert!(
            err.to_string().contains("coverage must be in (0, 1)"),
            "unexpected error: {err}"
        );
        let err = bootstrap_intervals(&residuals, &forecasts, 2000, 0.0, Some(42)).unwrap_err();
        assert!(
            err.to_string().contains("coverage must be in (0, 1)"),
            "unexpected error: {err}"
        );
    }

    #[test]
    fn test_bootstrap_rejects_too_few_paths() {
        let residuals = vec![1.0, -1.0, 2.0, -2.0, 0.5, -0.5, 1.5, -1.5, 0.3, -0.3];
        let forecasts = vec![100.0];
        let err = bootstrap_intervals(&residuals, &forecasts, 5, 0.9, Some(42)).unwrap_err();
        assert!(
            err.to_string().contains("n_paths must be >= 10"),
            "unexpected error: {err}"
        );
    }

    #[test]
    fn test_bootstrap_rejects_oversized_sample_budget() {
        let residuals = vec![1.0, -1.0, 2.0, -2.0, 0.5, -0.5, 1.5, -1.5, 0.3, -0.3];
        let forecasts: Vec<f64> = vec![100.0; 10];
        let err =
            bootstrap_intervals(&residuals, &forecasts, 2_000_000, 0.9, Some(42)).unwrap_err();
        assert!(
            err.to_string().contains("n_paths"),
            "unexpected error: {err}"
        );
    }
}
