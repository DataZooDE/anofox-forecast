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
    if !(0.0..1.0).contains(&coverage) || coverage <= 0.0 {
        return Err(ForecastError::InvalidInput(
            "coverage must be between 0 and 1 (exclusive)".to_string(),
        ));
    }

    // Build the predictor
    let mut predictor = BootstrapPredictor::new(coverage).n_replicates(n_paths);
    if let Some(s) = seed {
        predictor = predictor.seed(s);
    }

    // Fit: compute actuals from residuals + a dummy forecast of zeros
    // BootstrapPredictor.fit(forecasts, actuals) computes residuals = actuals - forecasts
    // We have residuals directly, so actuals = residuals (with forecasts = zeros)
    let zeros: Vec<f64> = vec![0.0; residuals.len()];
    let result = predictor.fit(&zeros, residuals).map_err(|e| {
        ForecastError::ComputationError(format!("Bootstrap fit failed: {}", e))
    })?;

    // Predict intervals
    let intervals = predictor.predict(&result, forecasts);

    Ok(BootstrapIntervalsResult {
        point: forecasts.to_vec(),
        lower: intervals.lower().to_vec(),
        upper: intervals.upper().to_vec(),
        coverage,
    })
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

    // Use median coverage for the predictor (doesn't matter for quantile extraction)
    let mut predictor = BootstrapPredictor::new(0.5).n_replicates(n_paths);
    if let Some(s) = seed {
        predictor = predictor.seed(s);
    }

    let zeros: Vec<f64> = vec![0.0; residuals.len()];
    let result = predictor.fit(&zeros, residuals).map_err(|e| {
        ForecastError::ComputationError(format!("Bootstrap fit failed: {}", e))
    })?;

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
}
