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
use crate::filter::is_constant;
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

/// Run the KPSS test for (level or trend) stationarity, matching
/// `tseries::kpss.test` (`lshort = TRUE`) to numerical tolerance (D-05/D-05a).
///
/// # Arguments
///
/// * `series` — Time series values.
/// * `lags` — Bandwidth (number of lags) for the Bartlett long-run variance
///   estimator. `None` → automatic `floor(4*(n/100)^0.25)`, clamped
///   `.min(n/2).max(1)`.
/// * `regression` — `Constant` ("c", `null = "Level"`) dispatches to the
///   crate's own `kpss_test`; `ConstantTrend` ("ct", `null = "Trend"`)
///   dispatches to [`kpss_trend`] (D-05a — not available in anofox-forecast
///   0.16.1, so implemented here in the extension's Rust core; an upstream
///   port is deferred).
///
/// # Notes
///
/// KPSS reverses the ADF null hypothesis: H0 = the series *is*
/// stationary (around a constant, or around a constant + linear trend
/// under `ConstantTrend`). `is_stationary` is `true` when the statistic
/// fails to reject that null (statistic below the 5% critical value).
/// p-values are approximate (piecewise-linear interpolation of the
/// appropriate KPSS table, clamped to `[0.01, 0.10]`, matching
/// `tseries::kpss.test` exactly).
pub fn kpss(
    series: &[f64],
    lags: Option<usize>,
    regression: StationarityRegression,
) -> StationarityOut {
    match regression {
        StationarityRegression::Constant => validation::kpss_test(series, lags).into(),
        StationarityRegression::ConstantTrend => kpss_trend(series, lags),
    }
}

/// `tseries::kpss.test`'s Trend table (`null = "Trend"`): linear
/// interpolation between these `(statistic, p_value)` pairs, clamped to
/// `[0.01, 0.10]` at both ends (R's `approx(..., rule = 2)`), mirroring
/// [`validation::kpss_p_value`]'s Level table but with the Trend critical
/// values.
const KPSS_TREND_TABLE: [(f64, f64); 4] =
    [(0.119, 0.10), (0.146, 0.05), (0.176, 0.025), (0.216, 0.01)];

fn trend_kpss_p_value(stat: f64) -> f64 {
    if stat.is_nan() {
        return f64::NAN;
    }
    if stat <= KPSS_TREND_TABLE[0].0 {
        return KPSS_TREND_TABLE[0].1;
    }
    if stat >= KPSS_TREND_TABLE[KPSS_TREND_TABLE.len() - 1].0 {
        return KPSS_TREND_TABLE[KPSS_TREND_TABLE.len() - 1].1;
    }
    for i in 0..KPSS_TREND_TABLE.len() - 1 {
        let (x1, y1) = KPSS_TREND_TABLE[i];
        let (x2, y2) = KPSS_TREND_TABLE[i + 1];
        if stat >= x1 && stat <= x2 {
            return y1 + (y2 - y1) * (stat - x1) / (x2 - x1);
        }
    }
    unreachable!("stat is within table bounds by the checks above")
}

/// Trend KPSS (D-05a): `tseries::kpss.test(x, null = "Trend", lshort = TRUE)`.
/// Not available in anofox-forecast 0.16.1 -- implemented here in the
/// extension's Rust core (deferred upstream port, see 17-CONTEXT.md).
///
/// Residuals come from the OLS fit of `series` on a constant and the time
/// index `t = 0..n-1` (closed-form intercept/slope -- no matrix library,
/// mirroring `detrending::detrend_linear`'s spirit but kept local since this
/// is a diagnostic, not a decomposition). The long-run variance estimator is
/// IDENTICAL to the crate's level-KPSS Bartlett kernel (weights
/// `1 - j/(l+1)`, `/n` autocovariances) with the SAME default/clamped
/// bandwidth. Critical values are the tseries Trend table (0.216/0.146/
/// 0.119 at 1%/5%/10%); the p-value is [`trend_kpss_p_value`]'s
/// interpolation. A non-positive or non-finite long-run variance (e.g. an
/// exactly-linear series, whose OLS residuals are ~0) returns the `NaN`
/// result via [`normalize_degenerate`] rather than panicking or dividing by
/// zero.
fn kpss_trend(series: &[f64], lags: Option<usize>) -> StationarityOut {
    let n = series.len();
    if n < 4 {
        return StationarityOut {
            statistic: f64::NAN,
            p_value: f64::NAN,
            lags: lags.unwrap_or(0),
            is_stationary: false,
            cv_1pct: f64::NAN,
            cv_5pct: f64::NAN,
            cv_10pct: f64::NAN,
        };
    }

    let l = lags
        .unwrap_or_else(|| (4.0 * (n as f64 / 100.0).powf(0.25)).floor() as usize)
        .min(n / 2)
        .max(1);

    // Closed-form OLS: y = a + b*t, t = 0..n-1.
    let mean_t = (n as f64 - 1.0) / 2.0;
    let mean_y = series.iter().sum::<f64>() / n as f64;
    let mut sxx = 0.0;
    let mut sxy = 0.0;
    for (i, &y) in series.iter().enumerate() {
        let dt = i as f64 - mean_t;
        sxx += dt * dt;
        sxy += dt * (y - mean_y);
    }
    let residuals: Vec<f64> = if sxx > 0.0 {
        let b = sxy / sxx;
        let a = mean_y - b * mean_t;
        series
            .iter()
            .enumerate()
            .map(|(i, &y)| y - (a + b * i as f64))
            .collect()
    } else {
        series.iter().map(|&y| y - mean_y).collect()
    };

    let mut cumsum = vec![0.0; n];
    cumsum[0] = residuals[0];
    for i in 1..n {
        cumsum[i] = cumsum[i - 1] + residuals[i];
    }
    let numerator: f64 = cumsum.iter().map(|&s| s * s).sum::<f64>() / (n * n) as f64;

    let mut variance = residuals.iter().map(|&r| r * r).sum::<f64>() / n as f64;
    for j in 1..=l {
        let weight = 1.0 - j as f64 / (l + 1) as f64;
        let autocovar: f64 = residuals
            .iter()
            .skip(j)
            .zip(residuals.iter())
            .map(|(&x, &y)| x * y)
            .sum::<f64>()
            / n as f64;
        variance += 2.0 * weight * autocovar;
    }

    if !variance.is_finite() || variance <= 0.0 {
        return StationarityOut {
            statistic: f64::NAN,
            p_value: f64::NAN,
            lags: l,
            is_stationary: false,
            cv_1pct: f64::NAN,
            cv_5pct: f64::NAN,
            cv_10pct: f64::NAN,
        };
    }

    let statistic = numerator / variance;
    let cv_1pct = 0.216;
    let cv_5pct = 0.146;
    let cv_10pct = 0.119;
    let p_value = trend_kpss_p_value(statistic);
    let is_stationary = statistic < cv_5pct;

    normalize_degenerate(StationarityOut {
        statistic,
        p_value,
        lags: l,
        is_stationary,
        cv_1pct,
        cv_5pct,
        cv_10pct,
    })
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
    /// D-04 four-way verdict: one of `stationary`, `non_stationary`,
    /// `trend_stationary`, `difference_stationary`, `constant`; `None` means
    /// SQL NULL (either test's statistic is `NaN` -- e.g. too-short series,
    /// or an exactly-collinear regression).
    pub verdict: Option<&'static str>,
}

/// Classify the D-04 four-way stationarity verdict from the two per-test
/// stationarity flags (standard ADF+KPSS combination, statsmodels
/// convention):
/// * ADF rejects, KPSS does not reject → `stationary`
/// * ADF does not reject, KPSS rejects → `non_stationary`
/// * neither rejects                   → `trend_stationary` (stationary around a deterministic trend)
/// * both reject                       → `difference_stationary` (unit root → difference it)
///
/// `adf_is_stationary`/`kpss_is_stationary` are each test's OWN
/// "is stationary" flag (ADF: rejects the unit-root null; KPSS: fails to
/// reject the stationarity null) -- so "ADF rejects" is
/// `adf_is_stationary == true` and "KPSS rejects" is
/// `kpss_is_stationary == false`.
pub fn classify_stationarity(adf_is_stationary: bool, kpss_is_stationary: bool) -> &'static str {
    match (adf_is_stationary, kpss_is_stationary) {
        (true, true) => "stationary",
        (false, true) => "trend_stationary",
        (false, false) => "non_stationary",
        (true, false) => "difference_stationary",
    }
}

/// Run both ADF and KPSS and derive the combined D-04 four-way verdict
/// (D-04, D-05). `regression` applies to BOTH tests.
///
/// The verdict is decided before the two-test mapping:
/// * a constant series (`n >= 2`, all present values equal per the crate's
///   own [`is_constant`] predicate) → `Some("constant")`
/// * either test's statistic is `NaN` → `None` (SQL NULL)
/// * otherwise → `Some(classify_stationarity(..))`
///
/// The per-test fields (`adf_statistic`, `kpss_statistic`, ...) are always
/// the plain [`adf`]/[`kpss`] outputs, so they keep equalling `ts_adf` /
/// `ts_kpss` run directly on the same series and regression.
pub fn stationarity(series: &[f64], regression: StationarityRegression) -> CombinedStationarityOut {
    let adf_r = adf(series, None, regression);
    let kpss_r = kpss(series, None, regression);

    let present: Vec<Option<f64>> = series.iter().map(|&v| Some(v)).collect();
    let verdict = if series.len() >= 2 && is_constant(&present) {
        Some("constant")
    } else if adf_r.statistic.is_nan() || kpss_r.statistic.is_nan() {
        None
    } else {
        Some(classify_stationarity(
            adf_r.is_stationary,
            kpss_r.is_stationary,
        ))
    };

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

/// Ljung-Box test on residuals, model-aware (RESID-01, D-06/DIAG-04).
///
/// `lags` — number of autocorrelation lags to test; `None` → the crate's
/// default `min(10, n/5).max(1)`, then clamped to `n - 1`.
/// `fitted_params` — number of estimated model parameters (e.g. `p + q` for
/// an ARMA(p, q) residual series); `df = effective_lags - fitted_params`.
///
/// Returns `Err(InvalidParameter)` when `fitted_params` would leave
/// `df <= 0` at the *effective* (resolved/clamped) lag count — the crate
/// itself silently floors that case to `df = 1`, which this wrapper treats
/// as a caller error instead (T-17-07). `n < 3` still returns the crate's
/// `NaN` result (lags 0, df 0) with no error, since there is no lag count to
/// validate against yet.
///
/// When the statistic is finite and positive, the p-value is recomputed via
/// the exact chi-square survival function (`statrs::distribution::ChiSquared`),
/// matching R `pchisq`/`Box.test` exactly — the crate's own `df > 30` branch
/// uses a Wilson-Hilferty approximation that deviates by up to ~2e-4.
pub fn ljung_box(
    residuals: &[f64],
    lags: Option<usize>,
    fitted_params: usize,
) -> Result<LjungBoxOut, ForecastError> {
    let n = residuals.len();

    // n < 3: the crate returns a NaN result (lags 0, df 0) with no lag
    // resolution performed; nothing to validate fitted_params against.
    if n < 3 {
        return Ok(validation::ljung_box(residuals, lags, fitted_params).into());
    }

    // Resolve the effective lags exactly as the crate does, so the df
    // validation below matches what the crate will actually compute.
    let effective_lags = lags.unwrap_or_else(|| 10.min(n / 5).max(1));
    let effective_lags = effective_lags.min(n - 1);

    if fitted_params >= effective_lags {
        return Err(ForecastError::InvalidParameter {
            param: "fitted_params".to_string(),
            value: fitted_params.to_string(),
            reason: format!(
                "degrees of freedom lags - fitted_params must be >= 1 (lags={effective_lags}, fitted_params={fitted_params})"
            ),
        });
    }

    let mut out: LjungBoxOut = validation::ljung_box(residuals, lags, fitted_params).into();
    // fitted_params < effective_lags was just checked, so the crate's own
    // `df.max(1)` flooring never engages here: df == effective_lags - fitted_params.
    out.df = effective_lags - fitted_params;

    if out.statistic.is_finite() && out.statistic > 0.0 {
        use statrs::distribution::{ChiSquared, ContinuousCDF};
        if let Ok(chi) = ChiSquared::new(out.df as f64) {
            out.p_value = chi.sf(out.statistic);
        }
    }

    Ok(out)
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
    /// Approximate two-sided p-value for H0: no first-order autocorrelation
    /// (see [`durbin_watson_p_value`]); `NaN` when undefined.
    pub p_value: f64,
}

/// Durbin-Watson first-order autocorrelation statistic on residuals (RESID-02).
pub fn durbin_watson(residuals: &[f64]) -> DurbinWatsonOut {
    let r = validation::durbin_watson(residuals);
    let degenerate = residuals.iter().all(|&e| e == 0.0);
    let p_value = if degenerate {
        f64::NAN
    } else {
        durbin_watson_p_value(r.statistic, residuals.len())
    };
    DurbinWatsonOut {
        statistic: r.statistic,
        interpretation: autocorrelation_label(r.interpretation),
        p_value,
    }
}

/// Approximate two-sided p-value of a Durbin-Watson statistic (#284).
///
/// Normal approximation with the exact null moments of `d` for residuals
/// taken as given (no regressors, so `k = 0`; the statistic is not
/// demeaned): with the DW matrix `A`, `P = tr(A) = 2(n-1)` and
/// `Q = tr(A^2) = 6n - 8`, giving `E[d] = P/n` and
/// `Var[d] = 2(Q - P E[d]) / (n(n+2))`. This is the moment formula of R
/// `lmtest::dwtest(exact = FALSE)` evaluated at a design with no columns;
/// `p = 2 min(Φ(z), 1 - Φ(z))` with `z = (d - E[d]) / sd(d)`.
///
/// Returns `NaN` for `n < 3` or a non-finite statistic.
pub fn durbin_watson_p_value(statistic: f64, n: usize) -> f64 {
    if n < 3 || !statistic.is_finite() {
        return f64::NAN;
    }
    use statrs::distribution::{ContinuousCDF, Normal};
    let nf = n as f64;
    let p = 2.0 * (nf - 1.0);
    let q = 6.0 * nf - 8.0;
    let mean = p / nf;
    let var = 2.0 * (q - p * mean) / (nf * (nf + 2.0));
    if var.is_nan() || var <= 0.0 {
        return f64::NAN;
    }
    let z = (statistic - mean) / var.sqrt();
    let cdf = Normal::new(0.0, 1.0).map(|d| d.cdf(z)).unwrap_or(f64::NAN);
    (2.0 * cdf.min(1.0 - cdf)).clamp(0.0, 1.0)
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
    /// Approximate two-sided Durbin-Watson p-value (#284).
    pub dw_p_value: f64,
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
    // fitted_params 0 with the default lags is never an Err from `ljung_box`
    // (df <= 0 would require fitted_params >= effective_lags, impossible at
    // 0 unless effective_lags is 0 — which only happens when `n < 3`, and
    // that path returns `Ok` with a NaN result, not an error). Map an
    // unreachable Err to the same NaN fields rather than unwrap/expect.
    let lb = ljung_box(residuals, None, 0).unwrap_or(LjungBoxOut {
        statistic: f64::NAN,
        p_value: f64::NAN,
        lags: 0,
        df: 0,
    });
    let dw = durbin_watson(residuals);
    let jb = jarque_bera(residuals);
    let adequate = lb.p_value.is_finite() && lb.p_value > alpha;
    ResidualDiagnosticsOut {
        lb_statistic: lb.statistic,
        lb_p_value: lb.p_value,
        lb_lags: lb.lags,
        dw_statistic: dw.statistic,
        dw_interpretation: dw.interpretation,
        dw_p_value: dw.p_value,
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
                if s.eq_ignore_ascii_case("c") {
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
        let result = kpss(&series, None, StationarityRegression::Constant);
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
        let rw_k = kpss(&rw, None, StationarityRegression::Constant);
        let ar_k = kpss(&ar, None, StationarityRegression::Constant);
        assert!(
            rw_k.statistic > ar_k.statistic,
            "random-walk KPSS ({:.4}) should exceed stationary KPSS ({:.4})",
            rw_k.statistic,
            ar_k.statistic
        );
    }

    #[test]
    fn classify_stationarity_truth_table() {
        // D-04: ADF rejects, KPSS does not reject -> stationary.
        assert_eq!(classify_stationarity(true, true), "stationary");
        // ADF does not reject, KPSS rejects -> non_stationary.
        assert_eq!(classify_stationarity(false, false), "non_stationary");
        // Neither rejects -> trend_stationary.
        assert_eq!(classify_stationarity(false, true), "trend_stationary");
        // Both reject -> difference_stationary.
        assert_eq!(classify_stationarity(true, false), "difference_stationary");
    }

    #[test]
    fn stationarity_verdict_is_one_of_four_labels() {
        let series = ar1_stationary(100, 11);
        let combined = stationarity(&series, StationarityRegression::Constant);
        assert!(
            matches!(
                combined.verdict,
                Some("stationary")
                    | Some("trend_stationary")
                    | Some("difference_stationary")
                    | Some("non_stationary")
            ),
            "verdict must be one of the four labels, got {:?}",
            combined.verdict
        );
        // The combined statistics must match the individual test outputs.
        assert_relative_eq!(
            combined.adf_statistic,
            adf(&series, None, StationarityRegression::Constant).statistic
        );
        assert_relative_eq!(
            combined.kpss_statistic,
            kpss(&series, None, StationarityRegression::Constant).statistic
        );
    }

    #[test]
    fn trend_kpss_matches_behavior_spec() {
        // trend_stationary fixture series (100 values) from
        // validation/reference/python/gen_stationarity_reference.py.
        let ts: Vec<f64> = vec![
            11.6868, 11.6724, 11.7833, 11.1672, 11.9244, 11.9678, 11.7347, 11.9132, 13.0209,
            13.9755, 12.6389, 11.3045, 11.1077, 12.8573, 12.3706, 12.1477, 13.7922, 14.5765,
            13.4204, 10.7194, 11.9456, 13.974, 14.2568, 12.7642, 12.7654, 14.6512, 15.689, 12.7434,
            11.9204, 13.6517, 15.4728, 16.7095, 15.8291, 15.5605, 16.3353, 17.4651, 17.2086,
            18.1492, 19.4742, 19.7893, 19.7197, 20.1087, 18.5176, 19.301, 17.9914, 19.8214,
            19.0297, 17.6203, 17.5362, 19.0964, 19.0789, 20.4123, 21.7995, 20.9125, 21.1322,
            22.2103, 21.8015, 24.1885, 23.4572, 22.7352, 22.3906, 22.7709, 23.1019, 24.8245,
            23.6303, 23.6606, 24.6127, 23.1483, 23.5438, 24.2914, 22.9941, 23.6819, 25.4222,
            27.0908, 27.1649, 26.4485, 25.8, 27.1473, 27.7016, 27.4023, 26.4704, 26.2581, 26.9843,
            26.2706, 28.1165, 28.9246, 28.1264, 27.9331, 27.9118, 30.5434, 29.5322, 28.9346,
            27.1721, 27.8209, 27.5195, 28.6054, 28.6876, 29.5953, 31.4823, 29.888,
        ];
        let r = kpss(&ts, None, StationarityRegression::ConstantTrend);
        assert_relative_eq!(r.statistic, 0.11714250016361194, epsilon = 1e-10);
        assert_eq!(r.lags, 4);
        assert_relative_eq!(r.p_value, 0.1);
        assert_relative_eq!(r.cv_1pct, 0.216);
        assert_relative_eq!(r.cv_5pct, 0.146);
        assert_relative_eq!(r.cv_10pct, 0.119);
        assert!(r.is_stationary);

        // p-value interpolation table.
        assert_relative_eq!(trend_kpss_p_value(0.146), 0.05);
        assert_relative_eq!(trend_kpss_p_value(0.161), 0.0375);
        assert_relative_eq!(trend_kpss_p_value(0.05), 0.10);
        assert_relative_eq!(trend_kpss_p_value(0.30), 0.01);

        // KPSS('ct') with an explicit lags override of 1000 clamps to n/2 = 50.
        let r_clamped = kpss(&ts, Some(1000), StationarityRegression::ConstantTrend);
        assert_eq!(r_clamped.lags, 50);
        assert!(r_clamped.statistic.is_finite());

        // n = 3 -> NaN, no panic.
        let short = vec![1.0, 2.0, 3.0];
        let r_short = kpss(&short, None, StationarityRegression::ConstantTrend);
        assert!(r_short.statistic.is_nan());
        assert!(r_short.p_value.is_nan());
        assert!(!r_short.is_stationary);

        // Exactly-linear series (n=30): singular trend fit -> NaN, no panic.
        let linear: Vec<f64> = (0..30).map(|i| 2.0 + 0.5 * i as f64).collect();
        let r_linear = kpss(&linear, None, StationarityRegression::ConstantTrend);
        assert!(r_linear.statistic.is_nan());
        assert!(r_linear.p_value.is_nan());
        assert!(r_linear.cv_1pct.is_nan());
        assert!(!r_linear.is_stationary);
    }

    #[test]
    fn stationarity_constant_and_degenerate_verdicts() {
        // Constant series (n >= 2, all equal) -> Some("constant").
        let c30: Vec<f64> = vec![5.0; 30];
        assert_eq!(
            stationarity(&c30, StationarityRegression::Constant).verdict,
            Some("constant")
        );
        let c2: Vec<f64> = vec![5.0, 5.0];
        assert_eq!(
            stationarity(&c2, StationarityRegression::Constant).verdict,
            Some("constant")
        );

        // n = 1: fewer than 2 present values -- not "constant" per this
        // function's own n >= 2 guard, and ADF/KPSS are both NaN -> None.
        let c1: Vec<f64> = vec![5.0];
        assert_eq!(
            stationarity(&c1, StationarityRegression::Constant).verdict,
            None
        );

        // n = 3, non-constant, too short for ADF/KPSS -> None (NaN statistic).
        let short = vec![1.0, 2.0, 3.0];
        assert_eq!(
            stationarity(&short, StationarityRegression::Constant).verdict,
            None
        );

        // Exactly-linear series under 'ct' -> None (KPSS NaN).
        let linear: Vec<f64> = (0..30).map(|i| 2.0 + 0.5 * i as f64).collect();
        assert_eq!(
            stationarity(&linear, StationarityRegression::ConstantTrend).verdict,
            None
        );
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
        let r = ljung_box(&resid, Some(8), 0).expect("fitted_params 0 never errors");
        assert!(r.statistic.is_finite() && r.statistic >= 0.0);
        assert!(r.p_value >= 0.0 && r.p_value <= 1.0);
        // fitted_params = 0 → df == lags
        assert_eq!(r.df, r.lags);
        assert_eq!(r.lags, 8);
    }

    fn assert_fitted_params_err(result: Result<LjungBoxOut, ForecastError>, ctx: &str) {
        match result {
            Err(ForecastError::InvalidParameter { param, .. }) => {
                assert_eq!(param, "fitted_params", "{ctx}: wrong param name");
            }
            other => panic!("{ctx}: expected Err(InvalidParameter), got {other:?}"),
        }
    }

    #[test]
    fn ljung_box_fitted_params_at_or_above_lags_is_err() {
        let x40 = white_noise(40, 30);
        assert_fitted_params_err(
            ljung_box(&x40, Some(5), 5),
            "lags=5 fitted_params=5 (equal)",
        );
        assert_fitted_params_err(
            ljung_box(&x40, Some(5), 7),
            "lags=5 fitted_params=7 (above)",
        );
        assert_fitted_params_err(
            ljung_box(&x40, Some(0), 0),
            "lags=0 fitted_params=0 (equal)",
        );
    }

    #[test]
    fn ljung_box_default_lags_validated_against_fitted_params() {
        // n = 12 → default lags = min(10, 12/5).max(1) = 2
        let x12 = white_noise(12, 31);
        assert_fitted_params_err(
            ljung_box(&x12, None, 2),
            "default lags=2 fitted_params=2 (equal)",
        );
        let r = ljung_box(&x12, None, 1).expect("fitted_params=1 < default lags=2");
        assert_eq!(r.df, 1);
    }

    #[test]
    fn ljung_box_huge_lags_clamped_no_panic() {
        let x40 = white_noise(40, 32);
        let r = ljung_box(&x40, Some(1000), 2).expect("clamped lags=39 > fitted_params=2");
        assert_eq!(r.lags, 39);
        assert_eq!(r.df, 37);
        assert!(r.statistic.is_finite());
    }

    #[test]
    fn ljung_box_n_below_3_is_nan_not_err() {
        let r = ljung_box(&[1.0, 2.0], Some(1), 0).expect("n < 3 never errors");
        assert!(r.statistic.is_nan());
        assert!(r.p_value.is_nan());
    }

    #[test]
    fn ljung_box_constant_series_statistic_0_p_1() {
        let constant = vec![5.0; 30];
        let r = ljung_box(&constant, Some(5), 2).expect("fitted_params=2 < lags=5");
        assert_eq!(r.statistic, 0.0);
        assert_eq!(r.p_value, 1.0);
        assert_eq!(r.df, 3);
    }

    #[test]
    fn ljung_box_df_over_30_uses_exact_chi_square_survival_function() {
        use statrs::distribution::{ChiSquared, ContinuousCDF};
        let resid = white_noise(100, 33);
        let r = ljung_box(&resid, Some(40), 2).expect("fitted_params=2 < lags=40");
        assert_eq!(r.df, 38);
        let want = ChiSquared::new(38.0).unwrap().sf(r.statistic);
        assert_eq!(r.p_value, want);
    }

    #[test]
    fn residual_diagnostics_no_panic_and_matches_ljung_box_for_degenerate_inputs() {
        for resid in [vec![1.0, 2.0], vec![5.0; 30]] {
            let d = residual_diagnostics(&resid, 0.05);
            let lb = ljung_box(&resid, None, 0).expect("fitted_params 0 never errors");
            if lb.statistic.is_nan() {
                assert!(d.lb_statistic.is_nan());
            } else {
                assert_eq!(d.lb_statistic, lb.statistic);
            }
            if lb.p_value.is_nan() {
                assert!(d.lb_p_value.is_nan());
            } else {
                assert_eq!(d.lb_p_value, lb.p_value);
            }
            assert_eq!(d.lb_lags, lb.lags);
        }
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
