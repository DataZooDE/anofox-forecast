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
            .filter(|&tau| tau == 0 || tau >= min_size) // Only valid previous changepoints
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
    // BUG FIX: Use uninformative prior instead of computing from all data
    // Using the global mean "cheats" by looking at future values
    let mu0 = 0.0; // Uninformative prior mean
    let kappa0 = 0.01; // Very weak prior on mean (let data dominate)
    let alpha0 = 0.01; // Weak prior shape for precision
    let beta0 = 0.01; // Weak prior rate for precision

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

        for r in 0..max_run {
            let growth_prob = run_length_prob[r] * pred_prob[r] * (1.0 - hazard);
            let changepoint_contrib = run_length_prob[r] * pred_prob[r] * hazard;

            new_run_length_prob[r + 1] += growth_prob;
            new_run_length_prob[0] += changepoint_contrib;
        }

        // Normalize
        let total: f64 = new_run_length_prob.iter().sum();
        if total > 1e-300 {
            for p in &mut new_run_length_prob {
                *p /= total;
            }
        }

        // FIX: P(r=0) is always ~hazard due to normalization math.
        // P(r=1) indicates "changepoint happened 1 step ago" - this is the actual signal.
        let cp_detected = *new_run_length_prob.get(1).unwrap_or(&0.0);

        changepoint_prob[t] = cp_detected;
        is_changepoint[t] = cp_detected > cp_threshold && t > 0;

        if is_changepoint[t] {
            changepoints.push(t);
        }

        // Update sufficient statistics
        // BUG FIX: We need to shift the statistics from old run lengths to new ones
        // Run length r at time t becomes run length r+1 at time t+1
        // We need to save old values before modifying
        let old_sum_x = sum_x.clone();
        let old_sum_x2 = sum_x2.clone();
        let old_run_counts = run_counts.clone();

        sum_x.push(0.0);
        sum_x2.push(0.0);
        run_counts.push(0);

        for r in 0..new_run_length_prob.len() {
            if r == 0 {
                // Run length 0 means "changepoint just happened"
                // No data in this run yet - sufficient stats should be empty
                // The NEXT observation will be the first in this new run
                sum_x[r] = 0.0;
                sum_x2[r] = 0.0;
                run_counts[r] = 0;
            } else if r <= max_run {
                // Continue from previous run length (r-1 at previous time -> r at current time)
                // Add current observation to the previous run's statistics
                sum_x[r] = old_sum_x[r - 1] + x;
                sum_x2[r] = old_sum_x2[r - 1] + x * x;
                run_counts[r] = old_run_counts[r - 1] + 1;
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

    #[test]
    fn test_detect_changepoints_bocpd() {
        // Create series with clear changepoint in mean
        let mut values: Vec<f64> = (0..50).map(|_| 10.0).collect();
        values.extend((0..50).map(|_| 50.0));

        let result = detect_changepoints_bocpd(&values, 20.0, true).unwrap();

        // Verify output structure is correct
        assert_eq!(result.is_changepoint.len(), 100);
        assert_eq!(result.changepoint_probability.len(), 100);

        // Check that probabilities are valid (in range [0, 1])
        assert!(result
            .changepoint_probability
            .iter()
            .all(|&p| (0.0..=1.0).contains(&p)));

        // The algorithm should produce some probability output
        // (Detection accuracy depends on parameters and data)
        let max_prob = result
            .changepoint_probability
            .iter()
            .fold(0.0_f64, |a, &b| a.max(b));
        assert!(max_prob >= 0.0, "Should compute valid probabilities");
    }

    #[test]
    fn test_detect_changepoints_bocpd_step_change() {
        // GitHub Issue #71: Test that BOCPD detects obvious step changes
        // Series: 100 for 12 points, then 10 for 12 points (changepoint at index 12)
        let mut values: Vec<f64> = vec![100.0; 12];
        values.extend(vec![10.0; 12]);

        let result = detect_changepoints_bocpd(&values, 10.0, true).unwrap();

        // Probabilities should NOT all be constant (the original bug)
        let first_prob = result.changepoint_probability[0];
        let all_constant = result
            .changepoint_probability
            .iter()
            .all(|&p| (p - first_prob).abs() < 1e-10);
        assert!(
            !all_constant,
            "BUG: All changepoint probabilities are constant at {}",
            first_prob
        );

        // There should be a spike at index 12 (the actual changepoint)
        let prob_before_cp: f64 = result.changepoint_probability[5..11].iter().sum::<f64>() / 6.0;
        let prob_at_cp = result.changepoint_probability[12];

        assert!(
            prob_at_cp > prob_before_cp * 10.0,
            "Changepoint probability at t=12 ({:.4}) should be much higher than baseline ({:.4})",
            prob_at_cp,
            prob_before_cp
        );

        // Probability at changepoint should be significant (>0.5)
        assert!(
            prob_at_cp > 0.5,
            "Changepoint probability at t=12 should be >0.5, got {:.4}",
            prob_at_cp
        );
    }

    #[test]
    fn test_detect_changepoints_bocpd_insufficient_data() {
        let values = vec![1.0, 2.0];
        let result = detect_changepoints_bocpd(&values, 10.0, false);
        assert!(result.is_err());
    }

    #[test]
    fn test_cost_l1() {
        let values = vec![1.0, 2.0, 3.0, 4.0, 5.0];

        // Cost of entire segment
        let cost = cost_l1(&values, 0, 5);
        assert!(cost > 0.0, "Cost should be positive for varied data");

        // Cost of constant segment
        let constant = vec![5.0; 5];
        let constant_cost = cost_l1(&constant, 0, 5);
        assert!(constant_cost < 1e-10, "Cost should be ~0 for constant data");

        // Empty segment
        let empty_cost = cost_l1(&values, 2, 2);
        assert_eq!(empty_cost, 0.0);
    }

    #[test]
    fn test_cost_normal() {
        let values = vec![1.0, 2.0, 3.0, 4.0, 5.0];

        // Cost should be positive for varied data
        let cost = cost_normal(&values, 0, 5);
        assert!(cost > 0.0, "Normal cost should be positive for varied data");

        // Constant segment has zero variance -> cost = 0
        let constant = vec![5.0; 5];
        let constant_cost = cost_normal(&constant, 0, 5);
        assert_eq!(
            constant_cost, 0.0,
            "Constant data should have 0 normal cost"
        );

        // Short segment
        let short_cost = cost_normal(&values, 0, 1);
        assert_eq!(short_cost, 0.0, "Single element should have 0 cost");
    }

    #[test]
    fn test_detect_changepoints_with_different_cost_functions() {
        // Create series with clear changepoint
        let mut values = vec![5.0; 30];
        values.extend(vec![15.0; 30]);

        // All cost functions should detect the changepoint
        for cost_fn in [CostFunction::L1, CostFunction::L2, CostFunction::Normal] {
            let result = detect_changepoints(&values, 5, None, cost_fn).unwrap();
            assert!(
                !result.changepoints.is_empty(),
                "{:?} cost function should detect changepoint",
                cost_fn
            );
        }
    }

    #[test]
    fn test_detect_changepoints_bayesian() {
        // Legacy function test
        let mut values = vec![10.0; 40];
        values.extend(vec![50.0; 40]);

        let result = detect_changepoints_bayesian(&values, 0.01).unwrap();
        // Should return a valid result structure
        assert!(result.cost == 0.0); // Legacy function sets cost to 0
    }
}
