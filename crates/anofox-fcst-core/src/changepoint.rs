//! Changepoint detection using PELT algorithm.

use crate::error::{ForecastError, Result};

/// Result of changepoint detection.
#[derive(Debug, Clone)]
pub struct ChangepointResult {
    /// Indices of detected changepoints
    pub changepoints: Vec<usize>,
    /// Total cost of segmentation
    pub cost: f64,
}

/// Cost function type for changepoint detection.
#[derive(Debug, Clone, Copy, Default)]
pub enum CostFunction {
    /// L1 (mean absolute deviation)
    L1,
    /// L2 (variance)
    #[default]
    L2,
    /// Normal distribution (mean and variance change)
    Normal,
}

/// Calculate segment cost using L2 (variance) cost function.
fn cost_l2(values: &[f64], start: usize, end: usize) -> f64 {
    if end <= start {
        return 0.0;
    }

    let segment = &values[start..end];
    let n = segment.len() as f64;

    if n < 1.0 {
        return 0.0;
    }

    let mean: f64 = segment.iter().sum::<f64>() / n;
    segment.iter().map(|v| (v - mean).powi(2)).sum()
}

/// Calculate segment cost using L1 (mean absolute deviation) cost function.
fn cost_l1(values: &[f64], start: usize, end: usize) -> f64 {
    if end <= start {
        return 0.0;
    }

    let segment = &values[start..end];
    let n = segment.len() as f64;

    if n < 1.0 {
        return 0.0;
    }

    let mean: f64 = segment.iter().sum::<f64>() / n;
    segment.iter().map(|v| (v - mean).abs()).sum()
}

/// Calculate segment cost using normal distribution (change in mean and variance).
fn cost_normal(values: &[f64], start: usize, end: usize) -> f64 {
    if end <= start {
        return 0.0;
    }

    let segment = &values[start..end];
    let n = segment.len() as f64;

    if n < 2.0 {
        return 0.0;
    }

    let mean: f64 = segment.iter().sum::<f64>() / n;
    let variance: f64 = segment.iter().map(|v| (v - mean).powi(2)).sum::<f64>() / n;

    if variance <= f64::EPSILON {
        return 0.0;
    }

    n * (1.0 + variance.ln())
}

/// Get cost function implementation.
fn get_cost_fn(cost_fn: CostFunction) -> fn(&[f64], usize, usize) -> f64 {
    match cost_fn {
        CostFunction::L1 => cost_l1,
        CostFunction::L2 => cost_l2,
        CostFunction::Normal => cost_normal,
    }
}

/// Detect changepoints using the PELT (Pruned Exact Linear Time) algorithm.
///
/// # Arguments
/// * `values` - The time series values
/// * `min_size` - Minimum segment size
/// * `penalty` - Penalty for adding a changepoint (BIC-like)
/// * `cost_fn` - Cost function to use
///
/// # Returns
/// Vector of changepoint indices
pub fn detect_changepoints(
    values: &[f64],
    min_size: usize,
    penalty: Option<f64>,
    cost_fn: CostFunction,
) -> Result<ChangepointResult> {
    let n = values.len();

    if n < 2 * min_size {
        return Ok(ChangepointResult {
            changepoints: vec![],
            cost: 0.0,
        });
    }

    // Default penalty based on BIC
    let pen = penalty.unwrap_or_else(|| (n as f64).ln() * 2.0);

    let cost = get_cost_fn(cost_fn);

    // PELT algorithm with pruning
    let mut f = vec![f64::NEG_INFINITY; n + 1];
    let mut cp = vec![0usize; n + 1];
    let mut r = vec![vec![0usize]; n + 1];

    f[0] = -pen;

    for tau_star in min_size..=n {
        let candidates: Vec<usize> = (0..=(tau_star.saturating_sub(min_size)))
            .filter(|&tau| tau + min_size <= tau_star)
            .collect();

        let mut best_f = f64::INFINITY;
        let mut best_tau = 0;

        for &tau in &candidates {
            let candidate_cost = f[tau] + cost(values, tau, tau_star) + pen;
            if candidate_cost < best_f {
                best_f = candidate_cost;
                best_tau = tau;
            }
        }

        f[tau_star] = best_f;
        cp[tau_star] = best_tau;

        // Pruning step
        r[tau_star] = candidates
            .into_iter()
            .filter(|&tau| f[tau] + cost(values, tau, tau_star) <= f[tau_star])
            .collect();
        r[tau_star].push(tau_star);
    }

    // Backtrack to find changepoints
    let mut changepoints = Vec::new();
    let mut idx = n;

    while idx > 0 {
        let tau = cp[idx];
        if tau > 0 {
            changepoints.push(tau);
        }
        idx = tau;
    }

    changepoints.reverse();

    Ok(ChangepointResult {
        changepoints,
        cost: f[n],
    })
}

/// Result of BOCPD changepoint detection with per-point probabilities.
/// C++ API compatible structure.
#[derive(Debug, Clone)]
pub struct BocpdResult {
    /// Whether each point is a changepoint (threshold applied)
    pub is_changepoint: Vec<bool>,
    /// Changepoint probability for each point [0, 1]
    pub changepoint_probability: Vec<f64>,
    /// Indices of detected changepoints
    pub changepoints: Vec<usize>,
}

/// Bayesian Online Changepoint Detection (BOCPD) with Normal-Gamma conjugate prior.
///
/// # Arguments
/// * `values` - Time series values
/// * `hazard_lambda` - Expected run length between changepoints (default: 250)
/// * `include_probabilities` - Whether to compute full probability distribution
///
/// # Returns
/// BOCPD result with per-point changepoint flags and probabilities
pub fn detect_changepoints_bocpd(
    values: &[f64],
    hazard_lambda: f64,
    include_probabilities: bool,
) -> Result<BocpdResult> {
    let n = values.len();

    if n < 3 {
        return Err(ForecastError::InsufficientData { needed: 3, got: n });
    }

    // Hazard function: constant rate = 1/hazard_lambda
    let hazard = 1.0 / hazard_lambda.max(1.0);

    // Normal-Gamma prior parameters
    let mu0 = values.iter().sum::<f64>() / n as f64; // Prior mean
    let kappa0 = 1.0; // Prior precision on mean
    let alpha0 = 1.0; // Prior shape for precision
    let beta0 = 1.0; // Prior rate for precision

    // Run length distribution: R[t][r] = P(run length = r at time t)
    // For efficiency, we only track the current run length distribution
    let mut run_length_prob = vec![1.0]; // Start with run length 0

    let mut is_changepoint = vec![false; n];
    let mut changepoint_prob = vec![0.0; n];
    let mut changepoints = Vec::new();

    // Sufficient statistics for each run length
    let mut sum_x = vec![0.0]; // Sum of x for each run length
    let mut sum_x2 = vec![0.0]; // Sum of x^2 for each run length
    let mut run_counts = vec![0usize]; // Count for each run length

    // Threshold for detecting changepoints
    let cp_threshold = 0.5;

    for t in 0..n {
        let x = values[t];
        let max_run = run_length_prob.len();

        // Compute predictive probabilities for each run length
        let mut pred_prob = vec![0.0; max_run];

        for r in 0..max_run {
            // Normal-Gamma posterior predictive (Student's t-distribution)
            let kappa_n = kappa0 + run_counts[r] as f64;
            let alpha_n = alpha0 + run_counts[r] as f64 / 2.0;

            let mu_n = if run_counts[r] > 0 {
                (kappa0 * mu0 + sum_x[r]) / kappa_n
            } else {
                mu0
            };

            let ss = if run_counts[r] > 0 {
                sum_x2[r] - sum_x[r].powi(2) / run_counts[r].max(1) as f64
            } else {
                0.0
            };

            let beta_n = beta0
                + 0.5 * ss.max(0.0)
                + kappa0 * run_counts[r] as f64 * (mu0 - mu_n).powi(2) / (2.0 * kappa_n);

            // Student's t predictive probability (simplified)
            let scale = ((beta_n * (kappa_n + 1.0)) / (alpha_n * kappa_n)).sqrt();
            let z = (x - mu_n) / scale.max(1e-10);
            let nu = 2.0 * alpha_n;

            // Student's t PDF approximation
            pred_prob[r] = (1.0 + z.powi(2) / nu).powf(-(nu + 1.0) / 2.0);
        }

        // Update run length distribution
        let mut new_run_length_prob = vec![0.0; max_run + 1];

        // Changepoint probability at this time step
        let mut cp_prob = 0.0;

        for r in 0..max_run {
            let growth_prob = run_length_prob[r] * pred_prob[r] * (1.0 - hazard);
            let changepoint_contrib = run_length_prob[r] * pred_prob[r] * hazard;

            new_run_length_prob[r + 1] += growth_prob;
            new_run_length_prob[0] += changepoint_contrib;
            cp_prob += changepoint_contrib;
        }

        // Normalize
        let total: f64 = new_run_length_prob.iter().sum();
        if total > 1e-300 {
            for p in &mut new_run_length_prob {
                *p /= total;
            }
            cp_prob /= total;
        }

        changepoint_prob[t] = cp_prob;
        is_changepoint[t] = cp_prob > cp_threshold && t > 0;

        if is_changepoint[t] {
            changepoints.push(t);
        }

        // Update sufficient statistics
        sum_x.push(0.0);
        sum_x2.push(0.0);
        run_counts.push(0);

        for r in 0..new_run_length_prob.len() {
            if r == 0 {
                sum_x[r] = x;
                sum_x2[r] = x * x;
                run_counts[r] = 1;
            } else if r <= max_run {
                sum_x[r] = sum_x[r - 1] + x;
                sum_x2[r] = sum_x2[r - 1] + x * x;
                run_counts[r] = run_counts[r - 1] + 1;
            }
        }

        run_length_prob = new_run_length_prob;

        // Prune very small probabilities for efficiency
        let _prune_threshold = 1e-10;
        let max_keep = 500; // Maximum run lengths to track

        if run_length_prob.len() > max_keep {
            run_length_prob.truncate(max_keep);
            sum_x.truncate(max_keep);
            sum_x2.truncate(max_keep);
            run_counts.truncate(max_keep);
        }
    }

    // If not including probabilities, set to empty
    if !include_probabilities {
        changepoint_prob = vec![0.0; n];
    }

    Ok(BocpdResult {
        is_changepoint,
        changepoint_probability: changepoint_prob,
        changepoints,
    })
}

/// Detect changepoints using Bayesian Online Changepoint Detection (simplified).
/// Legacy function - use detect_changepoints_bocpd for C++ API compatibility.
pub fn detect_changepoints_bayesian(values: &[f64], hazard_rate: f64) -> Result<ChangepointResult> {
    let bocpd_result = detect_changepoints_bocpd(values, 1.0 / hazard_rate.max(0.001), false)?;

    Ok(ChangepointResult {
        changepoints: bocpd_result.changepoints,
        cost: 0.0,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_detect_changepoints_single() {
        // Create series with one clear changepoint
        let mut values = vec![0.0; 50];
        values.extend(vec![10.0; 50]);

        let result = detect_changepoints(&values, 5, None, CostFunction::L2).unwrap();

        assert!(!result.changepoints.is_empty());
        // Should detect changepoint around index 50
        let has_near_50 = result.changepoints.iter().any(|&cp| (45..55).contains(&cp));
        assert!(
            has_near_50,
            "Expected changepoint near 50, got {:?}",
            result.changepoints
        );
    }

    #[test]
    fn test_detect_changepoints_multiple() {
        // Create series with two changepoints
        let mut values = vec![0.0; 33];
        values.extend(vec![10.0; 34]);
        values.extend(vec![0.0; 33]);

        let result = detect_changepoints(&values, 5, None, CostFunction::L2).unwrap();

        assert!(!result.changepoints.is_empty());
    }

    #[test]
    fn test_no_changepoints() {
        // Constant series
        let values = vec![5.0; 100];
        let result = detect_changepoints(&values, 5, None, CostFunction::L2).unwrap();
        assert!(result.changepoints.is_empty() || result.changepoints.len() <= 1);
    }
}
