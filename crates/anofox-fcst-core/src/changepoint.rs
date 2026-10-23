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
    /// Number of segment-cost evaluations performed -- a direct measure of
    /// how effective pruning was (T-15-11, D-07): the unpruned exact DP
    /// evaluates O(n^2) segment costs; working pruning keeps this far below
    /// that.
    pub n_cost_evaluations: usize,
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

/// Calculate segment cost using L2 (variance) cost function on an
/// already-normalised series `z = x / sigma_hat`.
fn cost_l2(z: &[f64], start: usize, end: usize) -> f64 {
    if end <= start {
        return 0.0;
    }
    let segment = &z[start..end];
    let n = segment.len() as f64;
    let mean: f64 = segment.iter().sum::<f64>() / n;
    segment.iter().map(|v| (v - mean).powi(2)).sum()
}

/// Calculate segment cost using L1 (median absolute deviation) cost
/// function on `z = x / sigma_hat`. Uses the segment MEDIAN, not the mean
/// (G8) -- `2*sqrt(2) * sum(|z - median(z)|)` matches the Laplace-MLE
/// negative log-likelihood up to an additive constant, consistent with
/// `L2`'s Normal-MLE-based scale.
fn cost_l1(z: &[f64], start: usize, end: usize) -> f64 {
    if end <= start {
        return 0.0;
    }
    let mut segment: Vec<f64> = z[start..end].to_vec();
    segment.sort_by(|a, b| a.total_cmp(b));
    let n = segment.len();
    let med = if n % 2 == 1 {
        segment[n / 2]
    } else {
        (segment[n / 2 - 1] + segment[n / 2]) / 2.0
    };
    let sum_abs_dev: f64 = z[start..end].iter().map(|v| (v - med).abs()).sum();
    2.0 * std::f64::consts::SQRT_2 * sum_abs_dev
}

/// Calculate segment cost using normal distribution (change in mean and
/// variance) on `z = x / sigma_hat`. The `+ 1e-8` inside the log keeps the
/// cost concave (superadditive), which is what makes PELT's pruning exact
/// even for a zero/near-zero-variance segment.
fn cost_normal(z: &[f64], start: usize, end: usize) -> f64 {
    if end <= start {
        return 0.0;
    }
    let segment = &z[start..end];
    let n = segment.len() as f64;
    let mean: f64 = segment.iter().sum::<f64>() / n;
    let var_mle: f64 = segment.iter().map(|v| (v - mean).powi(2)).sum::<f64>() / n;
    n * (1.0 + (var_mle + 1e-8).ln())
}

/// Get cost function implementation.
fn get_cost_fn(cost_fn: CostFunction) -> fn(&[f64], usize, usize) -> f64 {
    match cost_fn {
        CostFunction::L1 => cost_l1,
        CostFunction::L2 => cost_l2,
        CostFunction::Normal => cost_normal,
    }
}

/// Effective minimum segment length: `Normal` needs at least 2 points to
/// compute a variance; `L1`/`L2` accept `min_size` as given (clamped to >= 1).
fn effective_min_size(cost_fn: CostFunction, min_size: usize) -> usize {
    let base = min_size.max(1);
    match cost_fn {
        CostFunction::Normal => base.max(2),
        _ => base,
    }
}

/// Default BIC-style penalty: `3 * ln(n)` for `Normal` (one more free
/// parameter -- the variance -- than `L1`/`L2`), `2 * ln(n)` otherwise,
/// matching R `changepoint`'s `(diffparam + 1) * log(n)` convention.
fn default_penalty(cost_fn: CostFunction, n: usize) -> f64 {
    let ln_n = (n as f64).ln();
    match cost_fn {
        CostFunction::Normal => 3.0 * ln_n,
        _ => 2.0 * ln_n,
    }
}

/// Detect changepoints using the PELT (Pruned Exact Linear Time) algorithm
/// (Killick et al. 2012).
///
/// Costs are computed on `z = values / robust_noise_scale(values)` (D-07),
/// so `detect_changepoints(values, ..)` and
/// `detect_changepoints(values * k, ..)` (or `values + c`) return identical
/// changepoints for any `k != 0`/`c` -- the Phase 15 audit found 32 spurious
/// changepoints on a `values * 100` series before this fix. `L1`'s cost uses
/// the segment MEDIAN (G8), not the mean. Pruning actually discards
/// candidates once they can no longer be optimal (`F(tau) + C(tau, t) >
/// F(t)` at `t`, expiring at `t + min_size`) -- `n_cost_evaluations` on the
/// returned result lets a caller (and this crate's own tests) measure how
/// effective that pruning is.
///
/// PELT has no SQL surface (see module doc) -- this function is Rust/FFI-only.
///
/// WR-02: the exact-safe pruning of `live` (Killick et al. 2012, Theorem
/// 3.1) is amortized-efficient on typical data (see
/// `test_pelt_pruning_effective_and_exact_on_long_series`) but has no hard
/// cap on `live.len()`; an adversarially-constructed series could keep most
/// candidates alive, degrading to the unpruned O(n^2) DP bound. This is
/// acceptable while PELT stays Rust/FFI-only with no untrusted SQL caller.
/// If a SQL surface is ever added, revisit this and consider a documented
/// cap on `live.len()` (merge/drop the lowest-priority candidate once
/// exceeded), analogous to BOCPD's `MAX_RUN_LENGTH`.
///
/// # Arguments
/// * `values` - The time series values
/// * `min_size` - Minimum segment size (`Normal` enforces at least 2)
/// * `penalty` - Penalty for adding a changepoint; `None` uses a BIC-style
///   default (`2 ln n` for `L1`/`L2`, `3 ln n` for `Normal`). Must be finite
///   and non-negative if provided.
/// * `cost_fn` - Cost function to use
///
/// # Errors
/// `InvalidInput` if any value is non-finite; `InvalidParameter` if
/// `penalty` is provided but non-finite or negative.
pub fn detect_changepoints(
    values: &[f64],
    min_size: usize,
    penalty: Option<f64>,
    cost_fn: CostFunction,
) -> Result<ChangepointResult> {
    if values.iter().any(|v| !v.is_finite()) {
        return Err(ForecastError::InvalidInput(
            "detect_changepoints: all values must be finite".to_string(),
        ));
    }
    if let Some(p) = penalty {
        if !p.is_finite() || p < 0.0 {
            return Err(ForecastError::InvalidParameter {
                param: "penalty".to_string(),
                value: p.to_string(),
                reason: "must be finite and non-negative".to_string(),
            });
        }
    }

    let n = values.len();
    let m = effective_min_size(cost_fn, min_size);
    let sigma_hat = robust_noise_scale(values);
    let z: Vec<f64> = values.iter().map(|v| v / sigma_hat).collect();
    let cost = get_cost_fn(cost_fn);

    if n < 2 * m {
        return Ok(ChangepointResult {
            changepoints: vec![],
            cost: if n > 0 { cost(&z, 0, n) } else { 0.0 },
            n_cost_evaluations: if n > 0 { 1 } else { 0 },
        });
    }

    let pen = penalty.unwrap_or_else(|| default_penalty(cost_fn, n));

    // f[t] = minimal total cost (segment costs + penalty per changepoint)
    // of an optimal partition of values[0..t]. back[t] = the optimal last
    // changepoint before t. `live` is the exact-safe pruned candidate set
    // of previous changepoints still reachable from future t.
    let mut f = vec![f64::INFINITY; n + 1];
    let mut back = vec![0usize; n + 1];
    f[0] = -pen;

    let mut live: Vec<usize> = vec![0];
    let mut n_cost_evaluations: usize = 0;

    for t in m..=n {
        let mut best_f = f64::INFINITY;
        let mut best_tau = 0usize;
        for &tau in &live {
            // A candidate only becomes admissible once t - tau >= m -- a
            // candidate pushed onto `live` at some earlier t' = tau sits
            // there (unevaluated, unpruned) until enough future steps have
            // passed. Querying it before that would let a too-short
            // segment compete at all, which is exactly how an
            // under-filtered `live` set silently corrupts every later f[t].
            if t - tau < m {
                continue;
            }
            n_cost_evaluations += 1;
            let candidate = f[tau] + cost(&z, tau, t) + pen;
            if candidate < best_f {
                best_f = candidate;
                best_tau = tau;
            }
        }
        f[t] = best_f;
        back[t] = best_tau;

        // Exact-safe pruning (Killick et al. 2012, Theorem 3.1): an
        // admissible candidate tau can no longer be optimal for any future
        // split point once f[tau] + cost(tau, t) > f[t] -- it is dropped
        // here. A not-yet-admissible tau (t - tau < m) is left in `live`
        // untouched -- it has not been evaluated yet, so there is nothing
        // to prune it on. `t` itself joins the live set once it is an
        // admissible split point for some future s >= t + m.
        live.retain(|&tau| {
            if t - tau < m {
                true
            } else {
                f[tau] + cost(&z, tau, t) <= f[t]
            }
        });
        if t + m <= n {
            live.push(t);
        }
    }

    // Backtrack to find changepoints.
    let mut changepoints = Vec::new();
    let mut idx = n;
    while idx > 0 {
        let tau = back[idx];
        if tau > 0 {
            changepoints.push(tau);
        }
        idx = tau;
    }
    changepoints.reverse();

    Ok(ChangepointResult {
        changepoints,
        cost: f[n],
        n_cost_evaluations,
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

/// Options for [`detect_changepoints_bocpd_with_options`] (#287, BUG-06,
/// user decision D2-ii, 20-CONTEXT.md decisions_addendum_2).
#[derive(Debug, Clone, Copy)]
pub struct ChangepointOptions {
    /// Expected run length between changepoints (same meaning as
    /// [`detect_changepoints_bocpd`]'s `hazard_lambda`).
    pub hazard_lambda: f64,
    /// When `None` (default), `is_changepoint` is the existing MAP
    /// run-length-reset heuristic ([`bocpd_trace`]'s `is_changepoint`).
    /// When `Some(t)`, `is_changepoint[i] = i >= 1 && prob[i] >= t` for
    /// every `i` -- D2-ii. Must be finite and in `[0, 1]` or
    /// [`detect_changepoints_bocpd_with_options`] returns
    /// `ForecastError::InvalidParameter`.
    pub threshold: Option<f64>,
}

impl Default for ChangepointOptions {
    fn default() -> Self {
        Self {
            hazard_lambda: 250.0,
            threshold: None,
        }
    }
}

// RED STUB (20-06 Task 2): threshold is parsed into the options but not yet
// honoured -- this intentionally fails the threshold-rule and validation
// tests below until the GREEN commit implements the D2-ii rule.
pub fn detect_changepoints_bocpd_with_options(
    values: &[f64],
    opts: &ChangepointOptions,
) -> Result<BocpdResult> {
    detect_changepoints_bocpd(values, opts.hazard_lambda, true)
}

/// Detect changepoints using Bayesian Online Changepoint Detection (simplified).
/// Legacy function - use detect_changepoints_bocpd for C++ API compatibility.
pub fn detect_changepoints_bayesian(values: &[f64], hazard_rate: f64) -> Result<ChangepointResult> {
    let bocpd_result = detect_changepoints_bocpd(values, 1.0 / hazard_rate.max(0.001), false)?;

    Ok(ChangepointResult {
        changepoints: bocpd_result.changepoints,
        cost: 0.0,
        n_cost_evaluations: 0,
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

        // Constant segment: var_mle = 0, so cost = n*(1 + ln(1e-8)) -- the
        // `+ 1e-8` keeps this finite and concave (G8, D-07) rather than the
        // old code's special-cased exact 0.0.
        let constant = vec![5.0; 5];
        let constant_cost = cost_normal(&constant, 0, 5);
        let expected = 5.0 * (1.0 + 1e-8_f64.ln());
        assert!(
            (constant_cost - expected).abs() < 1e-9,
            "constant_cost = {constant_cost}, want {expected}"
        );
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

    // -----------------------------------------------------------------
    // Phase 15 plan 15-03 Task 2 (DET-03, D-07): PELT sigma normalisation,
    // L1 median cost, exact-safe pruning, penalty validation.
    // -----------------------------------------------------------------

    #[test]
    fn test_cost_l1_uses_median_not_mean() {
        // On an already-normalised z = [1, 2, 3, 100] (median 2.5), the L1
        // cost should be 2*sqrt(2) * (1.5 + 0.5 + 0.5 + 97.5) =
        // 2*sqrt(2)*100 = 282.842712474619, not the mean-based value.
        let z = vec![1.0, 2.0, 3.0, 100.0];
        let got = cost_l1(&z, 0, 4);
        assert!(
            (got - 282.842712474619).abs() < 1e-9,
            "cost_l1([1,2,3,100]) = {got}, want 282.842712474619 (median-based)"
        );
    }

    #[test]
    fn test_detect_changepoints_scale_invariant_for_every_cost() {
        let mut values = vec![0.0; 50];
        values.extend(vec![2.5; 50]);
        values.extend(vec![-1.0; 50]);
        let scaled: Vec<f64> = values.iter().map(|v| v * 100.0).collect();
        let shifted: Vec<f64> = values.iter().map(|v| v + 1e6).collect();

        for cost_fn in [CostFunction::L1, CostFunction::L2, CostFunction::Normal] {
            let base = detect_changepoints(&values, 5, None, cost_fn).unwrap();
            let got_scaled = detect_changepoints(&scaled, 5, None, cost_fn).unwrap();
            let got_shifted = detect_changepoints(&shifted, 5, None, cost_fn).unwrap();
            assert_eq!(
                got_scaled.changepoints, base.changepoints,
                "{cost_fn:?}: x*100 changepoints {:?} != base {:?}",
                got_scaled.changepoints, base.changepoints
            );
            assert_eq!(
                got_shifted.changepoints, base.changepoints,
                "{cost_fn:?}: x+1e6 changepoints {:?} != base {:?}",
                got_shifted.changepoints, base.changepoints
            );
        }
    }

    #[test]
    fn test_detect_changepoints_rejects_non_finite_or_negative_penalty() {
        let values = vec![0.0; 20];
        for bad_penalty in [f64::NAN, -1.0] {
            let result = detect_changepoints(&values, 2, Some(bad_penalty), CostFunction::L2);
            assert!(
                matches!(result, Err(ForecastError::InvalidParameter { .. })),
                "penalty {bad_penalty} should be rejected as InvalidParameter, got {result:?}"
            );
        }
    }

    #[test]
    fn test_detect_changepoints_short_series_cost_is_whole_series_cost() {
        // n < 2*min_size -> no changepoints, but cost must be the whole
        // series's own segment cost, not an unconditional 0.0.
        let values = vec![1.0, 5.0, 2.0, 8.0];
        let result = detect_changepoints(&values, 5, None, CostFunction::L2).unwrap();
        assert!(result.changepoints.is_empty());
        assert!(
            result.cost > 0.0,
            "short-series cost should be the whole-series L2 cost (> 0 for varied data), got {}",
            result.cost
        );
    }

    #[test]
    fn test_noiseless_step_n60_flags_30() {
        let mut values = vec![0.0; 30];
        values.extend(vec![10.0; 30]);
        let result = detect_changepoints(&values, 1, None, CostFunction::L2).unwrap();
        assert_eq!(result.changepoints, vec![30]);
    }

    #[test]
    fn test_constant_series_zero_changepoints_for_every_cost() {
        let values = vec![3.0; 40];
        for cost_fn in [CostFunction::L1, CostFunction::L2, CostFunction::Normal] {
            let result = detect_changepoints(&values, 2, None, cost_fn).unwrap();
            assert!(
                result.changepoints.is_empty(),
                "{cost_fn:?}: constant series should have no changepoints, got {:?}",
                result.changepoints
            );
        }
    }

    #[test]
    fn test_detect_changepoints_rejects_non_finite_input() {
        let values = vec![1.0, f64::NAN, 3.0, 4.0, 5.0, 6.0];
        let result = detect_changepoints(&values, 1, None, CostFunction::L2);
        assert!(matches!(result, Err(ForecastError::InvalidInput(_))));
    }

    /// Deterministic pseudo-noise (NOT `thread_rng`) for the pruning
    /// performance/equality test below -- an xorshift-style generator
    /// seeded on the index, so the test is fully reproducible.
    fn deterministic_noise(i: usize) -> f64 {
        let mut x = (i as u64).wrapping_add(0x9E3779B97F4A7C15);
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        let frac = (x.wrapping_mul(0x2545F4914F6CDD1D) >> 11) as f64 / (1u64 << 53) as f64;
        (frac - 0.5) * 0.2
    }

    /// Test-only unpruned exact DP (the ground truth PELT with pruning
    /// must reproduce exactly): scans EVERY admissible tau, never prunes.
    /// Only `CostFunction::L2` is supported -- it uses O(1) prefix-sum
    /// costs (instead of calling the crate's own `cost_l2`, which would
    /// make this O(n) per candidate and O(n^3) overall) purely so the
    /// 3000-point performance test runs in a reasonable time; this does
    /// NOT change which tau values are scanned (still every admissible
    /// one, no pruning), only how fast each one's cost is computed.
    fn unpruned_exact_dp(
        values: &[f64],
        min_size: usize,
        penalty: Option<f64>,
        cost_fn: CostFunction,
    ) -> (Vec<usize>, usize) {
        assert!(
            matches!(cost_fn, CostFunction::L2),
            "unpruned_exact_dp test helper only supports L2"
        );
        let n = values.len();
        let m = effective_min_size(cost_fn, min_size);
        let sigma_hat = robust_noise_scale(values);
        let z: Vec<f64> = values.iter().map(|v| v / sigma_hat).collect();
        let pen = penalty.unwrap_or_else(|| default_penalty(cost_fn, n));

        // Prefix sums for O(1) L2 segment cost: sum((z-mean)^2) over [a,b)
        // = sum_sq - sum^2/len.
        let mut prefix_sum = vec![0.0; n + 1];
        let mut prefix_sq = vec![0.0; n + 1];
        for i in 0..n {
            prefix_sum[i + 1] = prefix_sum[i] + z[i];
            prefix_sq[i + 1] = prefix_sq[i] + z[i] * z[i];
        }
        let l2_cost = |a: usize, b: usize| -> f64 {
            if b <= a {
                return 0.0;
            }
            let len = (b - a) as f64;
            let s = prefix_sum[b] - prefix_sum[a];
            let sq = prefix_sq[b] - prefix_sq[a];
            sq - s * s / len
        };

        let mut f = vec![f64::INFINITY; n + 1];
        let mut back = vec![0usize; n + 1];
        f[0] = -pen;
        let mut n_evals = 0usize;

        for t in m..=n {
            let candidates: Vec<usize> = (0..=t.saturating_sub(m))
                .filter(|&tau| tau == 0 || tau >= m)
                .collect();
            let mut best_f = f64::INFINITY;
            let mut best_tau = 0usize;
            for &tau in &candidates {
                n_evals += 1;
                let c = f[tau] + l2_cost(tau, t) + pen;
                if c < best_f {
                    best_f = c;
                    best_tau = tau;
                }
            }
            f[t] = best_f;
            back[t] = best_tau;
        }

        let mut changepoints = Vec::new();
        let mut idx = n;
        while idx > 0 {
            let tau = back[idx];
            if tau > 0 {
                changepoints.push(tau);
            }
            idx = tau;
        }
        changepoints.reverse();
        (changepoints, n_evals)
    }

    #[test]
    fn test_pelt_pruning_effective_and_exact_on_long_series() {
        let n = 3000;
        let n_segments = 10;
        let seg_len = n / n_segments;
        let mut values = Vec::with_capacity(n);
        for seg in 0..n_segments {
            let level = (seg as f64) * 5.0;
            for i in 0..seg_len {
                values.push(level + deterministic_noise(seg * seg_len + i));
            }
        }
        while values.len() < n {
            values.push(deterministic_noise(values.len()));
        }

        let result = detect_changepoints(&values, 5, None, CostFunction::L2).unwrap();
        let (unpruned_changepoints, unpruned_evals) =
            unpruned_exact_dp(&values, 5, None, CostFunction::L2);

        assert_eq!(
            result.changepoints, unpruned_changepoints,
            "pruned PELT result must equal the unpruned exact DP"
        );

        let full_n = n as f64;
        let threshold = 0.2 * full_n * (full_n + 1.0) / 2.0;
        assert!(
            (result.n_cost_evaluations as f64) < threshold,
            "n_cost_evaluations {} should be < 20% of unpruned n(n+1)/2 = {}  (unpruned ran {})",
            result.n_cost_evaluations,
            threshold,
            unpruned_evals
        );
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

    // -----------------------------------------------------------------
    // Phase 20 plan 20-06 Task 2 (#287, BUG-06, D2-ii): threshold option,
    // decided in Rust and shared by both SQL consumers via the FFI options
    // struct (`detect_changepoints_bocpd_with_options`).
    // -----------------------------------------------------------------

    /// #287 panel's series B (84 days, weekly seasonality, +15 level shift
    /// from day 50 / index 50): `base + 0.3*i + 5*sin(2*pi*i/7) + (i>=50 ?
    /// 15 : 0) + 1.6*cos(i*0.9) + 0.7*sin(i*1.7+base)`, base = 40.0 -- the
    /// exact SQL formula pinned in detection_decomposition.test.
    fn issue287_series_b() -> Vec<f64> {
        let base = 40.0;
        (0..84_i64)
            .map(|i| {
                let i_f = i as f64;
                base + 0.3 * i_f
                    + 5.0 * (2.0 * std::f64::consts::PI * i_f / 7.0).sin()
                    + if i >= 50 { 15.0 } else { 0.0 }
                    + 1.6 * (i_f * 0.9).cos()
                    + 0.7 * (i_f * 1.7 + base).sin()
            })
            .collect()
    }

    #[test]
    fn test_with_options_threshold_none_matches_detect_changepoints_bocpd() {
        let mut step = vec![0.0; 50];
        step.extend(vec![10.0; 50]);

        let opts = ChangepointOptions {
            hazard_lambda: 250.0,
            threshold: None,
        };
        let with_opts = detect_changepoints_bocpd_with_options(&step, &opts).unwrap();
        let base = detect_changepoints_bocpd(&step, 250.0, true).unwrap();
        assert_eq!(with_opts.is_changepoint, base.is_changepoint, "step series: is_changepoint");
        assert_eq!(
            with_opts.changepoint_probability, base.changepoint_probability,
            "step series: changepoint_probability"
        );
        assert_eq!(with_opts.changepoints, base.changepoints, "step series: changepoints");

        let b = issue287_series_b();
        let with_opts_b = detect_changepoints_bocpd_with_options(&b, &opts).unwrap();
        let base_b = detect_changepoints_bocpd(&b, 250.0, true).unwrap();
        assert_eq!(with_opts_b.is_changepoint, base_b.is_changepoint, "#287 series B: is_changepoint");
        assert_eq!(
            with_opts_b.changepoint_probability, base_b.changepoint_probability,
            "#287 series B: changepoint_probability"
        );
        assert_eq!(with_opts_b.changepoints, base_b.changepoints, "#287 series B: changepoints");
    }

    #[test]
    fn test_with_options_threshold_flags_every_row_after_first_at_or_above_threshold() {
        let mut step = vec![0.0; 50];
        step.extend(vec![10.0; 50]);

        let opts = ChangepointOptions {
            hazard_lambda: 250.0,
            threshold: Some(0.5),
        };
        let result = detect_changepoints_bocpd_with_options(&step, &opts).unwrap();
        let want: Vec<bool> = result
            .changepoint_probability
            .iter()
            .enumerate()
            .map(|(i, &p)| i >= 1 && p >= 0.5)
            .collect();
        assert_eq!(result.is_changepoint, want);
        assert!(!result.is_changepoint[0], "index 0 must never be flagged");
    }

    #[test]
    fn test_with_options_threshold_zero_flags_every_row_after_first() {
        // threshold 0.0 flags every index except 0 (D2-ii): prob[i] >= 0.0
        // is always true, but index 0 is excluded unconditionally.
        let mut step = vec![0.0; 50];
        step.extend(vec![10.0; 50]);

        let opts = ChangepointOptions {
            hazard_lambda: 250.0,
            threshold: Some(0.0),
        };
        let result = detect_changepoints_bocpd_with_options(&step, &opts).unwrap();
        assert!(
            !result.is_changepoint[0],
            "index 0 must never be flagged, even at threshold 0.0"
        );
        assert!(
            result.is_changepoint[1..].iter().all(|&f| f),
            "every row after the first must be flagged at threshold 0.0"
        );
    }

    #[test]
    fn test_with_options_threshold_out_of_range_is_invalid_parameter() {
        let mut step = vec![0.0; 10];
        step.extend(vec![10.0; 10]);

        for bad in [1.5, -0.1, f64::NAN] {
            let opts = ChangepointOptions {
                hazard_lambda: 250.0,
                threshold: Some(bad),
            };
            let result = detect_changepoints_bocpd_with_options(&step, &opts);
            match result {
                Err(ForecastError::InvalidParameter { param, .. }) => {
                    assert_eq!(param, "threshold", "threshold={bad}");
                }
                other => panic!("threshold={bad}: expected InvalidParameter, got {other:?}"),
            }
        }
    }
}
