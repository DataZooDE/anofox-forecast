//! Statistical validation and diagnostic functions.
//!
//! This module wraps `anofox_forecast::validation` and exposes flat,
//! owned result types suitable for FFI use.
//!
//! # Example
//!
//! ```no_run
//! use anofox_fcst_core::validation::{adf, StationarityOut, StationarityRegression};
//!
//! let series: Vec<f64> = (0..50).map(|i| i as f64 + 0.1 * (i as f64 % 7.0)).collect();
//! let result = adf(&series, None, StationarityRegression::Constant);
//! println!("ADF statistic: {}", result.statistic);
//! println!("p-value: {}", result.p_value);
//! ```

use crate::error::ForecastError;
use anofox_forecast::validation;
use anofox_forecast::validation::AdfRegression;
use std::str::FromStr;

/// Deterministic regression used by [`adf`], [`kpss`] and [`stationarity`]
/// (D-05). `Constant` ("c", the default) tests around a constant mean;
/// `ConstantTrend` ("ct") tests around a constant + linear time trend —
/// the only way to observe the `trend_stationary` / `stationary` verdicts
/// of the D-04 four-way mapping under a deterministic trend.
///
/// Parses case-insensitively from the strings `"c"` / `"ct"` via [`FromStr`];
/// any other input is `ForecastError::InvalidParameter` with
/// `reason: "expected one of: c, ct"`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum StationarityRegression {
    /// Constant-only regression (statsmodels/urca "c"). Default.
    #[default]
    Constant,
    /// Constant + linear trend regression (statsmodels/urca "ct").
    ConstantTrend,
}

impl FromStr for StationarityRegression {
    type Err = ForecastError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.to_ascii_lowercase().as_str() {
            "c" => Ok(StationarityRegression::Constant),
            "ct" => Ok(StationarityRegression::ConstantTrend),
            _ => Err(ForecastError::InvalidParameter {
                param: "regression".to_string(),
                value: s.to_string(),
                reason: "expected one of: c, ct".to_string(),
            }),
        }
    }
}

impl From<StationarityRegression> for AdfRegression {
    fn from(r: StationarityRegression) -> Self {
        match r {
            StationarityRegression::Constant => AdfRegression::Constant,
            StationarityRegression::ConstantTrend => AdfRegression::ConstantTrend,
        }
    }
}

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

/// Normalise degenerate stationarity results in one place, used by every
/// `StationarityOut` conversion: a `NaN` statistic (the crate's signal for
/// "undefined on this input" — too-short series, a singular/exactly-collinear
/// regression, or a zero long-run variance) yields `NaN` for `p_value` and
/// every critical value and forces `is_stationary` to `false`. The crate
/// itself returns `0.0` critical values (`CriticalValues::default()`) and,
/// for KPSS zero-variance, `is_stationary: true` — both misleading alongside
/// an undefined statistic, so this wrapper corrects them before they ever
/// reach SQL (D-04/T-17-04).
fn normalize_degenerate(out: StationarityOut) -> StationarityOut {
    if out.statistic.is_nan() {
        StationarityOut {
            p_value: f64::NAN,
            cv_1pct: f64::NAN,
            cv_5pct: f64::NAN,
            cv_10pct: f64::NAN,
            is_stationary: false,
            ..out
        }
    } else {
        out
    }
}

impl From<validation::StationarityResult> for StationarityOut {
    fn from(r: validation::StationarityResult) -> Self {
        let out = Self {
            statistic: r.statistic,
            p_value: r.p_value,
            lags: r.lags,
            is_stationary: r.is_stationary,
            cv_1pct: r.critical_values.cv_1pct,
            cv_5pct: r.critical_values.cv_5pct,
            cv_10pct: r.critical_values.cv_10pct,
        };
        normalize_degenerate(out)
    }
}

/// Run the Augmented Dickey-Fuller (ADF) test for unit-root stationarity,
/// matching `statsmodels.tsa.stattools.adfuller` (AIC lag selection,
/// MacKinnon (1994/2010) p-values and finite-sample critical values) to
/// numerical tolerance (D-05).
///
/// # Arguments
///
/// * `series` — Time series values (must be non-empty for meaningful results;
///   returns NaN statistic for n < 4 or a singular/exactly-collinear
///   regression under `regression`)
/// * `max_lags` — Maximum number of lags for AIC selection.
///   `None` → automatic: `floor((n-1)^(1/3))`, clamped the way statsmodels
///   does (`min(requested, n/2 - ntrend - 1)`, floored at 0).
/// * `regression` — Deterministic regression: `Constant` ("c", the default)
///   or `ConstantTrend` ("ct").
///
/// # Notes
///
/// A `NaN` statistic (too-short series, or an exactly-collinear regression —
/// e.g. `ConstantTrend` on an exactly-linear series) normalises `p_value` and
/// every critical value to `NaN` and `is_stationary` to `false`
/// ([`normalize_degenerate`]).
pub fn adf(
    series: &[f64],
    max_lags: Option<usize>,
    regression: StationarityRegression,
) -> StationarityOut {
    let mut options = validation::AdfOptions {
        regression: regression.into(),
        ..Default::default()
    };
    if let Some(m) = max_lags {
        options = options.with_max_lags(m);
    }
    validation::adf_test_with_options(series, &options).into()
}

/// Run the KPSS test for (level) stationarity, matching `tseries::kpss.test`
/// (`null = "Level"`, `lshort = TRUE`) to numerical tolerance.
///
/// # Arguments
///
/// * `series` — Time series values.
/// * `lags` — Bandwidth (number of lags) for the Bartlett long-run variance
///   estimator. `None` → automatic `floor(4*(n/100)^0.25)`, clamped
///   `.min(n/2).max(1)`.
///
/// # Notes
///
/// KPSS reverses the ADF null hypothesis: H0 = the series *is*
/// level-stationary. `is_stationary` is `true` when the statistic fails to
/// reject that null (statistic below the 5% critical value). p-values are
/// approximate (piecewise-linear interpolation of the KPSS table, clamped to
/// `[0.01, 0.10]`, matching `tseries::kpss.test` exactly). The `'ct'` (trend)
/// specification is added in [`kpss_trend`] (D-05a) via the 3-argument
/// `kpss(series, lags, regression)` wrapper.
pub fn kpss(series: &[f64], lags: Option<usize>) -> StationarityOut {
    validation::kpss_test(series, lags).into()
}

/// Combined ADF + KPSS stationarity verdict.
///
/// Field order is fixed for FFI consumers.
#[derive(Debug, Clone)]
pub struct CombinedStationarityOut {
    /// ADF test statistic
    pub adf_statistic: f64,
    /// ADF approximate p-value
    pub adf_p_value: f64,
    /// KPSS test statistic
    pub kpss_statistic: f64,
    /// KPSS approximate p-value
    pub kpss_p_value: f64,
    /// Whether ADF alone judges the series stationary (rejects unit root)
    pub adf_is_stationary: bool,
    /// Whether KPSS alone judges the series stationary (fails to reject stationarity)
    pub kpss_is_stationary: bool,
    /// Four-way verdict: one of
    /// `stationary`, `trend_stationary`, `difference_stationary`, `non_stationary`
    pub verdict: &'static str,
}

/// Classify the four-way stationarity verdict from the two per-test stationarity flags.
///
/// Standard ADF+KPSS combination (both flags mean "this test says stationary"):
/// * both stationary               → `stationary`
/// * ADF stationary, KPSS not      → `trend_stationary` (stationary around a deterministic trend)
/// * both non-stationary           → `difference_stationary` (unit root → difference it)
/// * ADF not, KPSS stationary      → `non_stationary` (conflicting / inconclusive)
pub fn classify_stationarity(adf_is_stationary: bool, kpss_is_stationary: bool) -> &'static str {
    match (adf_is_stationary, kpss_is_stationary) {
        (true, true) => "stationary",
        (true, false) => "trend_stationary",
        (false, false) => "difference_stationary",
        (false, true) => "non_stationary",
    }
}

/// Run both ADF and KPSS and derive the combined four-way verdict.
pub fn stationarity(series: &[f64]) -> CombinedStationarityOut {
    let adf_r = adf(series, None, StationarityRegression::Constant);
    let kpss_r = kpss(series, None);
    let verdict = classify_stationarity(adf_r.is_stationary, kpss_r.is_stationary);
    CombinedStationarityOut {
        adf_statistic: adf_r.statistic,
        adf_p_value: adf_r.p_value,
        kpss_statistic: kpss_r.statistic,
        kpss_p_value: kpss_r.p_value,
        adf_is_stationary: adf_r.is_stationary,
        kpss_is_stationary: kpss_r.is_stationary,
        verdict,
    }
}

// ============================================================================
// Residual diagnostics (RESID-01..04)
// ============================================================================

/// Ljung-Box white-noise test result.
#[derive(Debug, Clone)]
pub struct LjungBoxOut {
    pub statistic: f64,
    pub p_value: f64,
    pub lags: usize,
    pub df: usize,
}

impl From<validation::LjungBoxResult> for LjungBoxOut {
    fn from(r: validation::LjungBoxResult) -> Self {
        Self {
            statistic: r.statistic,
            p_value: r.p_value,
            lags: r.lags,
            df: r.df,
        }
    }
}

/// Ljung-Box test on residuals (RESID-01).
///
/// `lags` — number of autocorrelation lags to test; `None` → `min(10, n/5)`.
/// `fitted_params` is fixed at 0 (the caller supplies raw residuals, not a
/// model), so `df == lags`.
pub fn ljung_box(residuals: &[f64], lags: Option<usize>) -> LjungBoxOut {
    validation::ljung_box(residuals, lags, 0).into()
}

/// Map the crate's autocorrelation classification to a stable string label.
fn autocorrelation_label(t: validation::AutocorrelationType) -> &'static str {
    use validation::AutocorrelationType::*;
    match t {
        PositiveStrong => "positive_strong",
        PositiveWeak => "positive_weak",
        None => "none",
        NegativeWeak => "negative_weak",
        NegativeStrong => "negative_strong",
    }
}

/// Durbin-Watson result.
#[derive(Debug, Clone)]
pub struct DurbinWatsonOut {
    /// Statistic in [0, 4]; ≈2 indicates no first-order autocorrelation.
    pub statistic: f64,
    /// Interpretation label (positive_strong / positive_weak / none / negative_weak / negative_strong).
    pub interpretation: &'static str,
}

/// Durbin-Watson first-order autocorrelation statistic on residuals (RESID-02).
pub fn durbin_watson(residuals: &[f64]) -> DurbinWatsonOut {
    let r = validation::durbin_watson(residuals);
    DurbinWatsonOut {
        statistic: r.statistic,
        interpretation: autocorrelation_label(r.interpretation),
    }
}

/// Jarque-Bera normality test result.
#[derive(Debug, Clone)]
pub struct JarqueBeraOut {
    pub statistic: f64,
    pub p_value: f64,
    pub skewness: f64,
    pub excess_kurtosis: f64,
}

impl From<validation::JarqueBeraResult> for JarqueBeraOut {
    fn from(r: validation::JarqueBeraResult) -> Self {
        Self {
            statistic: r.statistic,
            p_value: r.p_value,
            skewness: r.skewness,
            excess_kurtosis: r.excess_kurtosis,
        }
    }
}

/// Jarque-Bera normality test on residuals (RESID-03).
pub fn jarque_bera(residuals: &[f64]) -> JarqueBeraOut {
    validation::jarque_bera(residuals).into()
}

/// Combined residual-diagnostics report (RESID-04).
#[derive(Debug, Clone)]
pub struct ResidualDiagnosticsOut {
    pub lb_statistic: f64,
    pub lb_p_value: f64,
    pub lb_lags: usize,
    pub dw_statistic: f64,
    pub dw_interpretation: &'static str,
    pub jb_statistic: f64,
    pub jb_p_value: f64,
    pub jb_skewness: f64,
    pub jb_excess_kurtosis: f64,
    /// `true` if residuals pass the adequacy gate: Ljung-Box p-value > `alpha`
    /// (no significant autocorrelation). JB and DW are advisory.
    pub adequate: bool,
}

/// Run all three residual tests and derive a pass/fail adequacy verdict (RESID-04).
///
/// Adequacy gate: Ljung-Box p-value > `alpha` (default caller passes 0.05).
/// Jarque-Bera (normality) and Durbin-Watson (first-order autocorrelation) are
/// reported as advisory fields but do not gate adequacy.
pub fn residual_diagnostics(residuals: &[f64], alpha: f64) -> ResidualDiagnosticsOut {
    let lb = ljung_box(residuals, None);
    let dw = durbin_watson(residuals);
    let jb = jarque_bera(residuals);
    let adequate = lb.p_value.is_finite() && lb.p_value > alpha;
    ResidualDiagnosticsOut {
        lb_statistic: lb.statistic,
        lb_p_value: lb.p_value,
        lb_lags: lb.lags,
        dw_statistic: dw.statistic,
        dw_interpretation: dw.interpretation,
        jb_statistic: jb.statistic,
        jb_p_value: jb.p_value,
        jb_skewness: jb.skewness,
        jb_excess_kurtosis: jb.excess_kurtosis,
        adequate,
    }
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
        let result = adf(&series, None, StationarityRegression::Constant);
        assert!(
            result.statistic.is_finite(),
            "statistic should be finite for n=50"
        );
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
        let rw_result = adf(&rw, None, StationarityRegression::Constant);
        let ar_result = adf(&ar, None, StationarityRegression::Constant);
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
        let result = adf(&short, None, StationarityRegression::Constant);
        assert!(
            result.statistic.is_nan(),
            "series shorter than 4 should return NaN statistic, got {}",
            result.statistic
        );
    }

    #[test]
    fn adf_max_lags_override_one() {
        let series = random_walk(50, 7);
        let result_1 = adf(&series, Some(1), StationarityRegression::Constant);
        // With max_lags=1, the used lag should be at most 1
        assert!(
            result_1.lags <= 1,
            "lags ({}) should be <= max_lags (1)",
            result_1.lags
        );
    }

    #[test]
    fn adf_critical_values_match_mackinnon_wrapper() {
        // The wrapper's critical values are exactly the crate's own MacKinnon
        // finite-sample surface at this result's effective sample size
        // (n - lags - 1), not hardcoded asymptotic constants (D-01/D-14).
        let series = random_walk(50, 1);
        let result = adf(&series, None, StationarityRegression::Constant);
        let nobs = series.len() - result.lags - 1;
        let expected = anofox_forecast::validation::mackinnon_critical_values(
            anofox_forecast::validation::AdfRegression::Constant,
            nobs,
        );
        assert_relative_eq!(result.cv_1pct, expected.cv_1pct);
        assert_relative_eq!(result.cv_5pct, expected.cv_5pct);
        assert_relative_eq!(result.cv_10pct, expected.cv_10pct);
        assert!(result.cv_1pct < result.cv_5pct);
        assert!(result.cv_5pct < result.cv_10pct);
        assert!(result.cv_10pct < 0.0);
    }

    #[test]
    fn stationarity_regression_parses_every_accepted_spelling() {
        for s in ["c", "C", "ct", "CT", "Ct", "cT"] {
            let r: StationarityRegression = s.parse().unwrap_or_else(|e| {
                panic!("expected {s:?} to parse as a StationarityRegression, got {e:?}")
            });
            assert_eq!(
                r,
                if s.to_ascii_lowercase() == "c" {
                    StationarityRegression::Constant
                } else {
                    StationarityRegression::ConstantTrend
                }
            );
        }
    }

    #[test]
    fn stationarity_regression_rejects_unknown_spellings() {
        for s in ["n", "trend", ""] {
            let err = s
                .parse::<StationarityRegression>()
                .expect_err("expected an InvalidParameter error");
            assert!(
                err.to_string().contains("expected one of: c, ct"),
                "error message {err} should contain 'expected one of: c, ct'"
            );
        }
    }

    #[test]
    fn adf_n3_gives_nan_critical_values_and_not_stationary() {
        let short = vec![1.0, 2.0, 3.0]; // n = 3 < 4
        let result = adf(&short, None, StationarityRegression::Constant);
        assert!(result.statistic.is_nan());
        assert!(result.p_value.is_nan());
        assert!(result.cv_1pct.is_nan());
        assert!(result.cv_5pct.is_nan());
        assert!(result.cv_10pct.is_nan());
        assert!(!result.is_stationary);
    }

    #[test]
    fn adf_ct_on_short_and_exactly_linear_series_returns_nan_without_panicking() {
        // n = 4: too short for a 2-regressor (constant + trend) design
        let n4 = vec![1.0, 2.0, 3.0, 4.0];
        let result = adf(&n4, None, StationarityRegression::ConstantTrend);
        assert!(result.statistic.is_nan());

        // Exactly linear series: the constant+trend regression is singular
        let linear: Vec<f64> = (0..30).map(|i| 2.0 + 0.5 * i as f64).collect();
        let result = adf(&linear, None, StationarityRegression::ConstantTrend);
        assert!(result.statistic.is_nan());
        assert!(!result.is_stationary);
    }

    #[test]
    fn adf_max_lags_1000_on_n100_ct_is_clamped() {
        let series = random_walk(100, 3);
        let result = adf(&series, Some(1000), StationarityRegression::ConstantTrend);
        assert!(
            result.lags <= 47,
            "lags ({}) should be clamped to <= 47 for n=100 'ct'",
            result.lags
        );
    }

    #[test]
    fn kpss_returns_finite_statistic_and_valid_pvalue() {
        let series = ar1_stationary(80, 5);
        let result = kpss(&series, None);
        assert!(
            result.statistic.is_finite(),
            "KPSS statistic should be finite"
        );
        assert!(
            result.p_value >= 0.0 && result.p_value <= 1.0,
            "p_value should be in [0, 1], got {}",
            result.p_value
        );
    }

    #[test]
    fn kpss_random_walk_statistic_exceeds_stationary() {
        // KPSS statistic is LARGER for non-stationary series (rejects the stationarity null)
        let rw = random_walk(120, 3);
        let ar = ar1_stationary(120, 3);
        let rw_k = kpss(&rw, None);
        let ar_k = kpss(&ar, None);
        assert!(
            rw_k.statistic > ar_k.statistic,
            "random-walk KPSS ({:.4}) should exceed stationary KPSS ({:.4})",
            rw_k.statistic,
            ar_k.statistic
        );
    }

    #[test]
    fn classify_stationarity_truth_table() {
        assert_eq!(classify_stationarity(true, true), "stationary");
        assert_eq!(classify_stationarity(true, false), "trend_stationary");
        assert_eq!(classify_stationarity(false, false), "difference_stationary");
        assert_eq!(classify_stationarity(false, true), "non_stationary");
    }

    #[test]
    fn stationarity_verdict_is_one_of_four_labels() {
        let series = ar1_stationary(100, 11);
        let combined = stationarity(&series);
        assert!(
            matches!(
                combined.verdict,
                "stationary" | "trend_stationary" | "difference_stationary" | "non_stationary"
            ),
            "verdict must be one of the four labels, got {}",
            combined.verdict
        );
        // The combined statistics must match the individual test outputs.
        assert_relative_eq!(
            combined.adf_statistic,
            adf(&series, None, StationarityRegression::Constant).statistic
        );
        assert_relative_eq!(combined.kpss_statistic, kpss(&series, None).statistic);
    }

    /// White-noise-ish residuals: LCG uniform noise, roughly independent.
    fn white_noise(n: usize, seed: u64) -> Vec<f64> {
        let mut x = seed;
        (0..n)
            .map(|_| {
                x = x.wrapping_mul(1664525).wrapping_add(1013904223);
                (x as f64 / u64::MAX as f64) - 0.5
            })
            .collect()
    }

    #[test]
    fn ljung_box_valid_output_and_df_equals_lags() {
        let resid = white_noise(100, 21);
        let r = ljung_box(&resid, Some(8));
        assert!(r.statistic.is_finite() && r.statistic >= 0.0);
        assert!(r.p_value >= 0.0 && r.p_value <= 1.0);
        // fitted_params = 0 → df == lags
        assert_eq!(r.df, r.lags);
        assert_eq!(r.lags, 8);
    }

    #[test]
    fn durbin_watson_in_range_and_labeled() {
        let resid = white_noise(100, 22);
        let r = durbin_watson(&resid);
        assert!(
            r.statistic >= 0.0 && r.statistic <= 4.0,
            "DW in [0,4], got {}",
            r.statistic
        );
        assert!(matches!(
            r.interpretation,
            "positive_strong" | "positive_weak" | "none" | "negative_weak" | "negative_strong"
        ));
    }

    #[test]
    fn durbin_watson_detects_positive_autocorrelation() {
        // Strongly positively autocorrelated residuals → DW well below 2
        let n = 100;
        let noise = white_noise(n, 23);
        let mut resid = vec![0.0; n];
        for i in 1..n {
            resid[i] = 0.9 * resid[i - 1] + noise[i];
        }
        let r = durbin_watson(&resid);
        assert!(
            r.statistic < 2.0,
            "positively autocorrelated DW should be < 2, got {}",
            r.statistic
        );
    }

    #[test]
    fn jarque_bera_valid_output() {
        let resid = white_noise(200, 24);
        let r = jarque_bera(&resid);
        assert!(r.statistic.is_finite() && r.statistic >= 0.0);
        assert!(r.p_value >= 0.0 && r.p_value <= 1.0);
        assert!(r.skewness.is_finite() && r.excess_kurtosis.is_finite());
    }

    #[test]
    fn residual_diagnostics_adequacy_gate_uses_ljung_box() {
        let resid = white_noise(120, 25);
        let d = residual_diagnostics(&resid, 0.05);
        // adequate must equal the Ljung-Box gate
        assert_eq!(d.adequate, d.lb_p_value > 0.05);
        // sub-statistics match the individual functions
        assert_relative_eq!(d.dw_statistic, durbin_watson(&resid).statistic);
        assert_relative_eq!(d.jb_statistic, jarque_bera(&resid).statistic);
    }
}
