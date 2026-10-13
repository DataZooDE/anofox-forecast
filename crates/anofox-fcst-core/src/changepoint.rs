//! Changepoint detection: Bayesian Online Changepoint Detection (BOCPD, Adams
//! & MacKay 2007) and PELT (Pruned Exact Linear Time, Killick et al. 2012).
//!
//! BOCPD is the only SQL-reachable surface (`_ts_detect_changepoints_bocpd`,
//! `ts_detect_changepoints(_by)`, `ts_detect_changepoints_agg`). PELT has no
//! SQL caller as of Phase 12 (removed as dead code) and stays Rust/FFI-only
//! per the Phase 15 plan 15-03 scope decision (revised D-07) -- it is checked
//! by `crates/anofox-fcst-core/tests/changepoint_reference.rs` against an
//! independent numpy exact-DP reference, never via SQL.

use crate::error::{ForecastError, Result};
use statrs::function::gamma::ln_gamma;

/// 1 / Phi^-1(0.75) -- the usual constant that rescales a MAD to a
/// Normal-equivalent standard deviation.
const MAD_TO_SIGMA: f64 = 1.482602218505602;

/// Robust noise scale shared by BOCPD's standardisation and PELT's cost
/// normalisation (D-06, D-07): the MAD of first differences, rescaled to a
/// Normal-equivalent sigma, with a fallback chain so a degenerate series
/// (constant, or whose MAD underflows to zero) never produces a zero/NaN
/// scale.
///
/// `sigma_hat = 1.482602218505602 * MAD(diff(x)) / sqrt(2)`, falling back to
/// `std(diff(x), ddof=0) / sqrt(2)` if that is not a finite positive number,
/// then to `1.0` if that fallback also fails (e.g. a 2-point series, or a
/// perfectly constant series where every difference is exactly 0).
pub fn robust_noise_scale(values: &[f64]) -> f64 {
    if values.len() < 2 {
        return 1.0;
    }
    let diffs: Vec<f64> = values.windows(2).map(|w| w[1] - w[0]).collect();
    let med_d = median(&diffs);
    let abs_dev: Vec<f64> = diffs.iter().map(|d| (d - med_d).abs()).collect();
    let mad = median(&abs_dev);
    let s = MAD_TO_SIGMA * mad / std::f64::consts::SQRT_2;
    if s > 0.0 && s.is_finite() {
        return s;
    }

    let mean: f64 = diffs.iter().sum::<f64>() / diffs.len() as f64;
    let var: f64 = diffs.iter().map(|d| (d - mean).powi(2)).sum::<f64>() / diffs.len() as f64;
    let s = var.sqrt() / std::f64::consts::SQRT_2;
    if s > 0.0 && s.is_finite() {
        return s;
    }

    1.0
}

/// Median of a slice (numpy convention: mean of the two middle values for
/// even length). Does not mutate the caller's slice.
fn median(values: &[f64]) -> f64 {
    let mut sorted = values.to_vec();
    sorted.sort_by(|a, b| a.total_cmp(b));
    let n = sorted.len();
    if n == 0 {
        return 0.0;
    }
    if n % 2 == 1 {
        sorted[n / 2]
    } else {
        (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0
    }
}

/// Result of PELT changepoint detection.
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
///
/// TODO (Phase 15 plan 15-03, G8, D-07): uses the mean, not the median --
/// scheduled to be fixed to the median-based Laplace-MLE cost in Task 2 of
/// this plan.
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
///
/// NOTE (Phase 15 plan 15-03, G7/G8, D-07): this is the pre-fix PELT,
/// unchanged by Task 1 (BOCPD tracer) -- the cost normalisation, L1 median
/// fix, and exact-safe pruning land in Task 2 of this plan.
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

/// A full BOCPD trace: the run-length posterior's MAP trajectory, the
/// per-point "new run started here" probability, and the resulting
/// changepoint flags -- everything `detect_changepoints_bocpd` needs plus
/// the intermediate quantities the numpy reference fixture pins exactly.
#[derive(Debug, Clone)]
pub struct BocpdTrace {
    /// The robust noise scale used to standardise `values` before running BOCPD.
    pub sigma_hat: f64,
    /// The median used to centre `values` before running BOCPD.
    pub loc: f64,
    /// `prob[t] = P(r_t = 1)`: the probability that `x_t` opened a new run.
    pub prob: Vec<f64>,
    /// `map_run_length[t]`: `argmax_{r >= 1} P(r_t = r)` (first maximum).
    pub map_run_length: Vec<usize>,
    /// Per-point changepoint flags (MAP run-length reset rule).
    pub is_changepoint: Vec<bool>,
    /// Sorted indices where `is_changepoint` is true.
    pub changepoints: Vec<usize>,
}

/// Run-length state over run lengths `r = 0..len(p)-1`: sufficient
/// statistics for the Normal-Gamma posterior at each run length, plus the
/// run-length probability distribution itself.
struct RunLengthState {
    p: Vec<f64>,
    cnt: Vec<f64>,
    s1: Vec<f64>,
    s2: Vec<f64>,
}

/// Normal-Gamma prior (Adams & MacKay 2007): uninformative but proper
/// (`kappa0 = alpha0 = beta0 = 1`, `mu0 = 0`, matched against `z =
/// (x - median(x)) / sigma_hat`, which is already centred near 0).
const MU0: f64 = 0.0;
const KAPPA0: f64 = 1.0;
const ALPHA0: f64 = 1.0;
const BETA0: f64 = 1.0;

/// Run-length state is capped at this many entries (T-15-09): tail
/// probability mass beyond this is merged into the last kept entry rather
/// than dropped, keeping both memory/time O(`MAX_RUN_LENGTH` * n) and the
/// total probability mass conserved (renormalised after the merge).
const MAX_RUN_LENGTH: usize = 500;

/// Run the Bayesian Online Changepoint Detection (BOCPD) algorithm (Adams &
/// MacKay, 2007) and return the full trace.
///
/// The series is standardised first: `sigma_hat = robust_noise_scale(values)`,
/// `loc = median(values)`, `z = (values - loc) / sigma_hat` -- this makes
/// the result scale- and shift-invariant (D-06): `bocpd_trace(values, h)`
/// and `bocpd_trace(values * k + c, h)` flag identical changepoints.
///
/// At each step the run-length posterior is updated via a normalised
/// Student-t predictive (the full `Gamma((nu+1)/2) / (Gamma(nu/2) *
/// sqrt(nu*pi) * scale)` kernel, computed in the log domain) over a
/// Normal-Gamma conjugate update that uses each run's SAMPLE mean (`xbar`,
/// not the posterior mean) for the sum-of-squares term -- the standard
/// conjugate-update formula, not an approximation. Run lengths beyond
/// `MAX_RUN_LENGTH` have their tail probability merged into the last kept
/// entry (not dropped) and the distribution renormalised.
///
/// A changepoint is flagged when the MAP run length resets: if
/// `argmax_{r>=1} P(r_t) < argmax_{r>=1} P(r_{t-1})`, the run start
/// `t - m_t + 1` is flagged (the point that opened the now-current,
/// shorter run). `prob[t] = P(r_t = 1)` -- the probability that `x_t`
/// itself opened a brand new run.
///
/// # Errors
/// `InsufficientData` if `values.len() < 3`; `InvalidInput` if any value is
/// non-finite.
pub fn bocpd_trace(values: &[f64], hazard_lambda: f64) -> Result<BocpdTrace> {
    let n = values.len();
    if n < 3 {
        return Err(ForecastError::InsufficientData { needed: 3, got: n });
    }
    if values.iter().any(|v| !v.is_finite()) {
        return Err(ForecastError::InvalidInput(
            "bocpd_trace: all values must be finite".to_string(),
        ));
    }

    let sigma_hat = robust_noise_scale(values);
    let loc = median(values);
    let z: Vec<f64> = values.iter().map(|v| (v - loc) / sigma_hat).collect();

    let h = 1.0 / hazard_lambda.max(1.0);

    let mut state = RunLengthState {
        p: vec![1.0],
        cnt: vec![0.0],
        s1: vec![0.0],
        s2: vec![0.0],
    };

    let mut prob = vec![0.0; n];
    let mut map_run_length = vec![0usize; n];
    let mut is_changepoint = vec![false; n];

    let mut prev_m: Option<usize> = None;

    for (t, &zt) in z.iter().enumerate() {
        let max_run = state.p.len();
        let mut logw = vec![0.0; max_run];

        for (r, logw_r) in logw.iter_mut().enumerate() {
            let n_r = state.cnt[r];
            let xbar = if n_r > 0.0 { state.s1[r] / n_r } else { 0.0 };
            let ss = (state.s2[r]
                - if n_r > 0.0 {
                    state.s1[r].powi(2) / n_r
                } else {
                    0.0
                })
            .max(0.0);

            let kappa_n = KAPPA0 + n_r;
            let mu_n = state.s1[r] / kappa_n; // (kappa0*mu0 + s1) / kappa_n, mu0 = 0
            let alpha_n = ALPHA0 + n_r / 2.0;
            let beta_n = BETA0 + ss / 2.0 + KAPPA0 * n_r * (xbar - MU0).powi(2) / (2.0 * kappa_n);

            let nu = 2.0 * alpha_n;
            let scale = (beta_n * (kappa_n + 1.0) / (alpha_n * kappa_n)).sqrt();
            let u = (zt - mu_n) / scale;

            let logpred = ln_gamma((nu + 1.0) / 2.0)
                - ln_gamma(nu / 2.0)
                - 0.5 * (nu * std::f64::consts::PI).ln()
                - scale.ln()
                - (nu + 1.0) / 2.0 * (1.0 + u * u / nu).ln();

            *logw_r = state.p[r].ln() + logpred;
        }

        let m = logw.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
        let w: Vec<f64> = logw.iter().map(|lw| (lw - m).exp()).collect();
        let sum_w: f64 = w.iter().sum();

        let mut g = vec![0.0; max_run + 1];
        g[0] = h * sum_w;
        for r in 0..max_run {
            g[r + 1] = (1.0 - h) * w[r];
        }
        let sum_g: f64 = g.iter().sum();
        let mut p_next: Vec<f64> = g.iter().map(|v| v / sum_g).collect();

        let mut cnt_next = vec![0.0; max_run + 1];
        let mut s1_next = vec![0.0; max_run + 1];
        let mut s2_next = vec![0.0; max_run + 1];
        for r in 0..max_run {
            cnt_next[r + 1] = state.cnt[r] + 1.0;
            s1_next[r + 1] = state.s1[r] + zt;
            s2_next[r + 1] = state.s2[r] + zt * zt;
        }

        if p_next.len() > MAX_RUN_LENGTH {
            let tail: f64 = p_next[MAX_RUN_LENGTH - 1..].iter().sum();
            p_next.truncate(MAX_RUN_LENGTH);
            p_next[MAX_RUN_LENGTH - 1] = tail;
            cnt_next.truncate(MAX_RUN_LENGTH);
            s1_next.truncate(MAX_RUN_LENGTH);
            s2_next.truncate(MAX_RUN_LENGTH);
            let sum_p: f64 = p_next.iter().sum();
            for v in &mut p_next {
                *v /= sum_p;
            }
        }

        // MAP run length: first argmax over r >= 1.
        let m_t = if p_next.len() > 1 {
            let (idx, _) = p_next[1..].iter().enumerate().fold(
                (0usize, f64::NEG_INFINITY),
                |(bi, bv), (i, &v)| {
                    if v > bv {
                        (i, v)
                    } else {
                        (bi, bv)
                    }
                },
            );
            idx + 1
        } else {
            0
        };

        prob[t] = if p_next.len() > 1 { p_next[1] } else { 0.0 };
        map_run_length[t] = m_t;

        if let Some(prev) = prev_m {
            if m_t < prev {
                let i = t as isize - m_t as isize + 1;
                if i >= 1 && (i as usize) < n {
                    is_changepoint[i as usize] = true;
                }
            }
        }
        prev_m = Some(m_t);

        state = RunLengthState {
            p: p_next,
            cnt: cnt_next,
            s1: s1_next,
            s2: s2_next,
        };
    }

    let changepoints: Vec<usize> = is_changepoint
        .iter()
        .enumerate()
        .filter_map(|(i, &f)| if f { Some(i) } else { None })
        .collect();

    Ok(BocpdTrace {
        sigma_hat,
        loc,
        prob,
        map_run_length,
        is_changepoint,
        changepoints,
    })
}

/// Result of BOCPD changepoint detection with per-point probabilities.
/// C++ API compatible structure.
#[derive(Debug, Clone)]
pub struct BocpdResult {
    /// Whether each point is a changepoint (MAP run-length reset rule)
    pub is_changepoint: Vec<bool>,
    /// Changepoint probability for each point [0, 1]: `P(r_t = 1)`, the
    /// probability that this point opened a new run. Zeroed out entirely
    /// when `include_probabilities` is false.
    pub changepoint_probability: Vec<f64>,
    /// Indices of detected changepoints
    pub changepoints: Vec<usize>,
}

/// Bayesian Online Changepoint Detection (BOCPD) with Normal-Gamma conjugate
/// prior (Adams & MacKay, 2007). Thin wrapper over [`bocpd_trace`] -- see
/// that function's doc comment for the full algorithm.
///
/// # Arguments
/// * `values` - Time series values
/// * `hazard_lambda` - Expected run length between changepoints (default: 250)
/// * `include_probabilities` - Whether to populate `changepoint_probability`
///   (when false, the vector is all zeros; `is_changepoint`/`changepoints`
///   are unaffected)
///
/// # Returns
/// BOCPD result with per-point changepoint flags and probabilities
pub fn detect_changepoints_bocpd(
    values: &[f64],
    hazard_lambda: f64,
    include_probabilities: bool,
) -> Result<BocpdResult> {
    let trace = bocpd_trace(values, hazard_lambda)?;

    let changepoint_probability = if include_probabilities {
        trace.prob
    } else {
        vec![0.0; values.len()]
    };

    Ok(BocpdResult {
        is_changepoint: trace.is_changepoint,
        changepoint_probability,
        changepoints: trace.changepoints,
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

    #[test]
    fn test_robust_noise_scale_constant_series_falls_back_to_one() {
        let values = vec![5.0; 30];
        assert_eq!(robust_noise_scale(&values), 1.0);
    }

    #[test]
    fn test_detect_changepoints_bocpd_step3_flags_100() {
        // GitHub Issue #71-style anchor (15-RESEARCH.md prototype): a clean
        // 3-sigma step at index 100 on a 200-point N(0,1) series must be
        // flagged, and only there.
        let mut values = vec![0.1; 200];
        for (i, v) in values.iter_mut().enumerate() {
            *v = if i < 100 { 0.0 } else { 3.0 };
        }
        let result = detect_changepoints_bocpd(&values, 250.0, true).unwrap();
        assert_eq!(result.changepoints, vec![100]);
    }

    #[test]
    fn test_detect_changepoints_bocpd_step_change_issue_71() {
        // GitHub Issue #71: 100 for 12 points, then 10 for 12 points.
        let mut values: Vec<f64> = vec![100.0; 12];
        values.extend(vec![10.0; 12]);

        let result = detect_changepoints_bocpd(&values, 250.0, true).unwrap();

        assert_eq!(result.changepoints, vec![12]);
        assert!(
            result.changepoint_probability[12] > 0.5,
            "prob[12] = {}, expected > 0.5",
            result.changepoint_probability[12]
        );
    }

    #[test]
    fn test_detect_changepoints_bocpd_insufficient_data() {
        let values = vec![1.0, 2.0];
        let result = detect_changepoints_bocpd(&values, 10.0, false);
        assert!(result.is_err());
    }

    #[test]
    fn test_detect_changepoints_bocpd_non_finite_input_is_invalid_input() {
        let values = vec![1.0, f64::NAN, 3.0];
        let result = detect_changepoints_bocpd(&values, 250.0, false);
        assert!(matches!(result, Err(ForecastError::InvalidInput(_))));
    }

    #[test]
    fn test_detect_changepoints_bocpd_include_probabilities_false_zeroes_vector() {
        let mut values = vec![0.0; 50];
        values.extend(vec![10.0; 50]);
        let result = detect_changepoints_bocpd(&values, 250.0, false).unwrap();
        assert!(result.changepoint_probability.iter().all(|&p| p == 0.0));
        // changepoints/is_changepoint are unaffected by include_probabilities.
        assert!(!result.changepoints.is_empty());
    }

    #[test]
    fn test_bocpd_trace_scale_and_shift_invariant() {
        let mut values = vec![0.0; 100];
        for (i, v) in values.iter_mut().enumerate() {
            if i >= 50 {
                *v = 5.0;
            }
        }
        let base = bocpd_trace(&values, 250.0).unwrap();
        let scaled: Vec<f64> = values.iter().map(|v| v * 100.0 + 7.0).collect();
        let scaled_trace = bocpd_trace(&scaled, 250.0).unwrap();
        assert_eq!(base.changepoints, scaled_trace.changepoints);
    }
}
