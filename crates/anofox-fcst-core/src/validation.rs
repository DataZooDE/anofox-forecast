//! Statistical validation and diagnostic functions.
//!
//! This module wraps `anofox_forecast::validation` and exposes flat,
//! owned result types suitable for FFI use.
//!
//! # Example
//!
//! ```no_run
//! use anofox_fcst_core::validation::{adf, StationarityOut};
//!
//! let series: Vec<f64> = (0..50).map(|i| i as f64 + 0.1 * (i as f64 % 7.0)).collect();
//! let result = adf(&series, None);
//! println!("ADF statistic: {}", result.statistic);
//! println!("p-value: {}", result.p_value);
//! ```

use anofox_forecast::validation;

/// Flat, owned result from an ADF or KPSS stationarity test.
///
/// Field order is fixed — FFI consumers depend on it:
///   statistic, p_value, lags, is_stationary, cv_1pct, cv_5pct, cv_10pct
#[derive(Debug, Clone)]
pub struct StationarityOut {
    /// Test statistic (ADF: negative t-statistic; KPSS: positive statistic)
    pub statistic: f64,
    /// Approximate p-value (MacKinnon table for ADF; piecewise linear for KPSS)
    pub p_value: f64,
    /// Number of lags used
    pub lags: usize,
    /// `true` if series appears stationary at the 5% significance level
    pub is_stationary: bool,
    /// Critical value at 1%
    pub cv_1pct: f64,
    /// Critical value at 5%
    pub cv_5pct: f64,
    /// Critical value at 10%
    pub cv_10pct: f64,
}

impl From<validation::StationarityResult> for StationarityOut {
    fn from(r: validation::StationarityResult) -> Self {
        Self {
            statistic: r.statistic,
            p_value: r.p_value,
            lags: r.lags,
            is_stationary: r.is_stationary,
            cv_1pct: r.critical_values.cv_1pct,
            cv_5pct: r.critical_values.cv_5pct,
            cv_10pct: r.critical_values.cv_10pct,
        }
    }
}

/// Run the Augmented Dickey-Fuller (ADF) test for unit-root stationarity.
///
/// # Arguments
///
/// * `series` — Time series values (must be non-empty for meaningful results;
///   returns NaN statistic for n < 4)
/// * `max_lags` — Maximum number of lags for AIC selection.
///   `None` → automatic: `floor((n-1)^(1/3))`, clamped to `min(max_lags, n/2-1).max(1)`.
///
/// # Notes
///
/// The underlying crate uses a constant-only (`'c'`) regression. The `'ct'`
/// (constant + trend) and `'n'` (no constant) modes are not available in
/// `anofox-forecast` v0.15.3.
///
/// p-values are approximate (9-point MacKinnon lookup table).
pub fn adf(series: &[f64], max_lags: Option<usize>) -> StationarityOut {
    validation::adf_test(series, max_lags).into()
}

#[cfg(test)]
mod tests {
    use super::*;
    use approx::assert_relative_eq;

    /// Build a pseudo-random walk: x_{t+1} = x_t + noise where noise is
    /// periodic but bounded (deterministic, reproducible).
    fn random_walk(n: usize, seed: u64) -> Vec<f64> {
        let mut series = vec![0.0f64; n];
        let mut x = seed;
        for i in 1..n {
            // simple LCG step (Numerical Recipes constants)
            x = x.wrapping_mul(1664525).wrapping_add(1013904223);
            let step = (x as f64 / u64::MAX as f64) * 2.0 - 1.0;
            series[i] = series[i - 1] + step;
        }
        series
    }

    /// Build a mean-reverting (stationary) AR(1) series: x_t = 0.3 * x_{t-1} + noise
    fn ar1_stationary(n: usize, seed: u64) -> Vec<f64> {
        let mut series = vec![0.0f64; n];
        let mut x = seed;
        for i in 1..n {
            x = x.wrapping_mul(1664525).wrapping_add(1013904223);
            let noise = (x as f64 / u64::MAX as f64) * 0.4 - 0.2;
            series[i] = 0.3 * series[i - 1] + noise;
        }
        series
    }

    #[test]
    fn adf_returns_finite_statistic_and_nonneg_lags() {
        let series = random_walk(50, 42);
        let result = adf(&series, None);
        assert!(result.statistic.is_finite(), "statistic should be finite for n=50");
        // lags is usize, always >= 0
        assert!(
            result.p_value >= 0.0 && result.p_value <= 1.0,
            "p_value should be in [0, 1]"
        );
    }

    #[test]
    fn adf_stationary_series_more_negative_than_random_walk() {
        // Run with the same seed so the noise pattern is comparable
        let rw = random_walk(80, 99);
        let ar = ar1_stationary(80, 99);
        let rw_result = adf(&rw, None);
        let ar_result = adf(&ar, None);
        // Stationary series (AR(1) with |phi|=0.3) should have a more negative ADF statistic
        assert!(
            ar_result.statistic < rw_result.statistic,
            "AR(1) statistic ({:.4}) should be more negative than random walk ({:.4})",
            ar_result.statistic,
            rw_result.statistic
        );
    }

    #[test]
    fn adf_short_series_returns_nan_without_panicking() {
        let short = vec![1.0, 2.0, 3.0]; // n = 3 < 4
        let result = adf(&short, None);
        assert!(
            result.statistic.is_nan(),
            "series shorter than 4 should return NaN statistic, got {}",
            result.statistic
        );
    }

    #[test]
    fn adf_max_lags_override_one() {
        let series = random_walk(50, 7);
        let result_1 = adf(&series, Some(1));
        // With max_lags=1, the used lag should be at most 1
        assert!(
            result_1.lags <= 1,
            "lags ({}) should be <= max_lags (1)",
            result_1.lags
        );
    }

    #[test]
    fn adf_critical_values_match_known_constants() {
        // For constant-only regression, the ADF critical values are hardcoded MacKinnon constants
        let series = random_walk(50, 1);
        let result = adf(&series, None);
        assert_relative_eq!(result.cv_1pct, -3.43, epsilon = 0.1);
        assert_relative_eq!(result.cv_5pct, -2.86, epsilon = 0.1);
        assert_relative_eq!(result.cv_10pct, -2.57, epsilon = 0.1);
    }
}
