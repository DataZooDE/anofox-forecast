//! Detrending and decomposition for time series.
//!
//! This module wraps fdars-core's detrending and decomposition functions
//! for use with time series data in DuckDB.

use crate::decomposition::{classical_decompose, ClassicalKind};
use crate::error::{ForecastError, Result};
use fdars_core::detrend::{
    auto_detrend as fdars_auto_detrend, decompose_additive as fdars_decompose_additive,
    decompose_multiplicative as fdars_decompose_multiplicative, detrend_diff as fdars_detrend_diff,
    detrend_linear as fdars_detrend_linear, detrend_loess as fdars_detrend_loess,
    detrend_polynomial as fdars_detrend_polynomial, DecomposeResult as FdarsDecomposeResult,
    TrendResult as FdarsTrendResult,
};
use fdars_core::matrix::FdMatrix;
use std::str::FromStr;

/// Build the single-series `(1 x n)` `FdMatrix` that every fdars-core call in
/// this crate must go through -- the ONLY place `FdMatrix::from_slice` is
/// called (T-15-01). fdars-core's `FdMatrix` convention is rows =
/// observations, columns = evaluation points; a single time series of
/// length `n` is one observation (row) of `n` evaluation points (columns),
/// i.e. `FdMatrix::from_slice(values, 1, n)`.
///
/// `FdMatrix::from_slice(values, n, 1)` still compiles -- it silently builds
/// an n-observation, 1-timepoint matrix -- and was exactly the B1/B2 bug
/// this helper exists to prevent. Review this literal `(1, n)` order at
/// every change to this function.
pub(crate) fn single_series_matrix(values: &[f64]) -> Result<FdMatrix> {
    FdMatrix::from_slice(values, 1, values.len())
        .map_err(|e| ForecastError::InvalidInput(e.to_string()))
}

/// Method for detrending.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum DetrendMethod {
    /// Linear trend removal using least squares
    Linear,
    /// Quadratic (degree 2) polynomial trend removal
    Quadratic,
    /// Cubic (degree 3) polynomial trend removal
    Cubic,
    /// First-order differencing
    Diff,
    /// Second-order differencing
    Diff2,
    /// LOESS (local polynomial regression)
    Loess,
    /// Auto-select best method using AIC
    #[default]
    Auto,
}

/// All detrend method names accepted by [`DetrendMethod::from_str`], for the
/// error message naming every valid choice (D-02).
const VALID_DETREND_METHODS: &str =
    "linear, quadratic (alias polynomial, poly), cubic, diff (alias diff1, difference), diff2, loess (alias lowess), auto";

impl FromStr for DetrendMethod {
    type Err = ForecastError;

    fn from_str(s: &str) -> std::result::Result<Self, Self::Err> {
        Ok(match s.to_lowercase().as_str() {
            "linear" => Self::Linear,
            "quadratic" | "polynomial" | "poly" => Self::Quadratic,
            "cubic" => Self::Cubic,
            "diff" | "diff1" | "difference" => Self::Diff,
            "diff2" => Self::Diff2,
            "loess" | "lowess" => Self::Loess,
            "auto" => Self::Auto,
            other => {
                return Err(ForecastError::InvalidParameter {
                    param: "method".to_string(),
                    value: other.to_string(),
                    reason: format!("expected one of: {VALID_DETREND_METHODS}"),
                })
            }
        })
    }
}

/// Decomposition method for seasonal decomposition.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum DecomposeMethod {
    /// Additive decomposition (fdars LOESS+Fourier): data = trend + seasonal + remainder
    #[default]
    Additive,
    /// Multiplicative decomposition (fdars LOESS+Fourier): data = trend * seasonal * remainder
    Multiplicative,
    /// Classical additive decomposition (R `stats::decompose`, D-10):
    /// centred moving-average trend, data = trend + seasonal + remainder
    ClassicalAdditive,
    /// Classical multiplicative decomposition (R `stats::decompose`, D-10):
    /// centred moving-average trend, data = trend * seasonal * remainder
    ClassicalMultiplicative,
}

/// All decompose method names accepted by [`DecomposeMethod::from_str`], for
/// the error message naming every valid choice.
const VALID_DECOMPOSE_METHODS: &str = "additive (alias add), multiplicative (alias mult, mul), classical_additive (alias classical), classical_multiplicative";

impl FromStr for DecomposeMethod {
    type Err = ForecastError;

    fn from_str(s: &str) -> std::result::Result<Self, Self::Err> {
        Ok(match s.to_lowercase().as_str() {
            "additive" | "add" => Self::Additive,
            "multiplicative" | "mult" | "mul" => Self::Multiplicative,
            "classical_additive" | "classical" => Self::ClassicalAdditive,
            "classical_multiplicative" => Self::ClassicalMultiplicative,
            other => {
                return Err(ForecastError::InvalidParameter {
                    param: "method".to_string(),
                    value: other.to_string(),
                    reason: format!("expected one of: {VALID_DECOMPOSE_METHODS}"),
                })
            }
        })
    }
}

/// Validate that a `decompose()` period (`f64`) is an integer `>= 2`, as
/// classical decomposition requires (D-10). Returns the usize period on
/// success.
fn classical_period_as_usize(period: f64) -> Result<usize> {
    if period < 2.0 || period.fract() != 0.0 {
        return Err(ForecastError::InvalidParameter {
            param: "period".to_string(),
            value: period.to_string(),
            reason: "classical decomposition requires an integer period >= 2".to_string(),
        });
    }
    Ok(period as usize)
}

/// Result of detrending operation.
#[derive(Debug, Clone)]
pub struct DetrendResult {
    /// Estimated trend values
    pub trend: Vec<f64>,
    /// Detrended data
    pub detrended: Vec<f64>,
    /// Method used for detrending
    pub method: String,
    /// Polynomial coefficients (for polynomial methods). For
    /// `DetrendMethod::Quadratic`/`Cubic` these are ascending powers of the
    /// raw 0-based time index (`t = 0..n-1`): `coefficients[k]` is the
    /// coefficient of `t^k`. fdars-core itself fits in normalised time
    /// `t/(n-1)`; `detrend_polynomial` below converts back to the raw-index
    /// basis before returning.
    pub coefficients: Option<Vec<f64>>,
    /// Residual sum of squares
    pub rss: f64,
    /// Number of parameters
    pub n_params: usize,
}

impl From<FdarsTrendResult> for DetrendResult {
    fn from(r: FdarsTrendResult) -> Self {
        let rss = r.rss.first().copied().unwrap_or(0.0);
        Self {
            trend: r.trend.as_slice().to_vec(),
            detrended: r.detrended.as_slice().to_vec(),
            method: r.method.into_owned(),
            coefficients: r.coefficients.map(|c| c.as_slice().to_vec()),
            rss,
            n_params: r.n_params,
        }
    }
}

/// Result of seasonal decomposition.
#[derive(Debug, Clone)]
pub struct DecomposeResult {
    /// Trend component
    pub trend: Vec<f64>,
    /// Seasonal component
    pub seasonal: Vec<f64>,
    /// Remainder/residual component
    pub remainder: Vec<f64>,
    /// Period used for decomposition
    pub period: f64,
    /// Decomposition method ("additive" or "multiplicative")
    pub method: String,
}

impl From<FdarsDecomposeResult> for DecomposeResult {
    fn from(r: FdarsDecomposeResult) -> Self {
        Self {
            trend: r.trend.as_slice().to_vec(),
            seasonal: r.seasonal.as_slice().to_vec(),
            remainder: r.remainder.as_slice().to_vec(),
            period: r.period,
            method: r.method.into_owned(),
        }
    }
}

/// Create argvals (time points) for a time series of given length.
fn make_argvals(n: usize) -> Vec<f64> {
    (0..n).map(|i| i as f64).collect()
}

/// Remove linear trend from time series using least squares.
///
/// # Arguments
/// * `values` - Time series values
///
/// # Returns
/// Detrend result with trend and detrended data
pub fn detrend_linear(values: &[f64]) -> Result<DetrendResult> {
    let n = values.len();
    if n < 2 {
        return Err(ForecastError::InsufficientData { needed: 2, got: n });
    }

    let argvals = make_argvals(n);
    let mat = single_series_matrix(values)?;
    let result = fdars_detrend_linear(&mat, &argvals);

    Ok(result.into())
}

/// Remove polynomial trend from time series.
///
/// # Arguments
/// * `values` - Time series values
/// * `degree` - Polynomial degree (1 = linear, 2 = quadratic, etc.)
///
/// # Returns
/// Detrend result with trend and detrended data. For `degree >= 2` the
/// returned `coefficients` are converted from fdars-core's normalised-time
/// basis (`t/(n-1)`) to the raw 0-based index basis: coefficient `k` is
/// divided by `(n-1)^k`. `degree == 1` delegates to fdars-core's own
/// `detrend_linear`, whose coefficients are already in the raw-index basis
/// (no conversion applied).
pub fn detrend_polynomial(values: &[f64], degree: usize) -> Result<DetrendResult> {
    let n = values.len();
    if n < degree + 1 {
        return Err(ForecastError::InsufficientData {
            needed: degree + 1,
            got: n,
        });
    }

    let argvals = make_argvals(n);
    let mat = single_series_matrix(values)?;
    let result = fdars_detrend_polynomial(&mat, &argvals, degree);
    let mut detrend_result: DetrendResult = result.into();

    if degree >= 2 {
        if let Some(ref mut coefs) = detrend_result.coefficients {
            let scale = (n - 1) as f64;
            for (k, c) in coefs.iter_mut().enumerate() {
                *c /= scale.powi(k as i32);
            }
        }
    }

    Ok(detrend_result)
}

/// Remove trend by differencing.
///
/// # Arguments
/// * `values` - Time series values
/// * `order` - Differencing order (1 or 2)
///
/// # Returns
/// Detrend result with trend and detrended data
/// Note: The detrended series will be shorter by `order` elements
pub fn detrend_diff(values: &[f64], order: usize) -> Result<DetrendResult> {
    let n = values.len();
    if n < order + 1 {
        return Err(ForecastError::InsufficientData {
            needed: order + 1,
            got: n,
        });
    }

    let mat = single_series_matrix(values)?;
    let result = fdars_detrend_diff(&mat, order);

    Ok(result.into())
}

/// Remove trend using LOESS (local polynomial regression).
///
/// # Arguments
/// * `values` - Time series values
/// * `bandwidth` - Smoothing bandwidth (0 < bandwidth <= 1)
/// * `degree` - Local polynomial degree (typically 1 or 2)
///
/// # Returns
/// Detrend result with trend and detrended data
pub fn detrend_loess(values: &[f64], bandwidth: f64, degree: usize) -> Result<DetrendResult> {
    let n = values.len();
    if n < 4 {
        return Err(ForecastError::InsufficientData { needed: 4, got: n });
    }

    if bandwidth <= 0.0 || bandwidth > 1.0 {
        return Err(ForecastError::InvalidParameter {
            param: "bandwidth".to_string(),
            value: bandwidth.to_string(),
            reason: "Bandwidth must be between 0 and 1".to_string(),
        });
    }

    let argvals = make_argvals(n);
    let mat = single_series_matrix(values)?;
    let result = fdars_detrend_loess(&mat, &argvals, bandwidth, degree);

    Ok(result.into())
}

/// Automatically select the best detrending method using AIC.
///
/// Compares linear, polynomial, and LOESS methods and selects
/// the one with the lowest AIC.
///
/// # Arguments
/// * `values` - Time series values
///
/// # Returns
/// Detrend result with trend and detrended data
pub fn detrend_auto(values: &[f64]) -> Result<DetrendResult> {
    let n = values.len();
    if n < 4 {
        return Err(ForecastError::InsufficientData { needed: 4, got: n });
    }

    let argvals = make_argvals(n);
    let mat = single_series_matrix(values)?;
    let result = fdars_auto_detrend(&mat, &argvals);

    Ok(result.into())
}

/// Detrend using the specified method.
///
/// # Arguments
/// * `values` - Time series values
/// * `method` - Detrending method to use
///
/// # Returns
/// Detrend result with trend and detrended data
pub fn detrend(values: &[f64], method: DetrendMethod) -> Result<DetrendResult> {
    match method {
        DetrendMethod::Linear => detrend_linear(values),
        DetrendMethod::Quadratic => detrend_polynomial(values, 2),
        DetrendMethod::Cubic => detrend_polynomial(values, 3),
        DetrendMethod::Diff => detrend_diff(values, 1),
        DetrendMethod::Diff2 => detrend_diff(values, 2),
        DetrendMethod::Loess => detrend_loess(values, 0.3, 1),
        DetrendMethod::Auto => detrend_auto(values),
    }
}

/// Additive seasonal decomposition.
///
/// Decomposes time series as: data = trend + seasonal + remainder
///
/// # Arguments
/// * `values` - Time series values
/// * `period` - Seasonal period
/// * `trend_method` - Method for trend extraction ("loess" or "spline")
/// * `bandwidth` - LOESS bandwidth (if using loess)
/// * `n_harmonics` - Number of Fourier harmonics for seasonal component
///
/// # Returns
/// Decomposition result with trend, seasonal, and remainder
pub fn decompose_additive(
    values: &[f64],
    period: f64,
    trend_method: Option<&str>,
    bandwidth: Option<f64>,
    n_harmonics: Option<usize>,
) -> Result<DecomposeResult> {
    let n = values.len();

    // CR-03: validate period is a positive finite number BEFORE the
    // insufficient-data check below -- `period as usize` saturates to
    // usize::MAX for +infinity, and `2 * usize::MAX` overflows (panics in
    // debug builds, wraps in release), so this order is load-bearing, not
    // cosmetic.
    if !period.is_finite() || period <= 0.0 {
        return Err(ForecastError::InvalidParameter {
            param: "period".to_string(),
            value: period.to_string(),
            reason: "Period must be a positive finite number".to_string(),
        });
    }

    if n < 2 * period as usize {
        return Err(ForecastError::InsufficientData {
            needed: 2 * period as usize,
            got: n,
        });
    }

    let argvals = make_argvals(n);
    let method = trend_method.unwrap_or("loess");
    let bw = bandwidth.unwrap_or(0.3);
    let harmonics = n_harmonics.unwrap_or(3);

    let mat = single_series_matrix(values)?;
    let result = fdars_decompose_additive(&mat, &argvals, period, method, bw, harmonics);

    Ok(result.into())
}

/// Multiplicative seasonal decomposition.
///
/// Decomposes time series as: data = trend * seasonal * remainder
///
/// # Arguments
/// * `values` - Time series values
/// * `period` - Seasonal period
/// * `trend_method` - Method for trend extraction ("loess" or "spline")
/// * `bandwidth` - LOESS bandwidth (if using loess)
/// * `n_harmonics` - Number of Fourier harmonics for seasonal component
///
/// # Returns
/// Decomposition result with trend, seasonal, and remainder
pub fn decompose_multiplicative(
    values: &[f64],
    period: f64,
    trend_method: Option<&str>,
    bandwidth: Option<f64>,
    n_harmonics: Option<usize>,
) -> Result<DecomposeResult> {
    let n = values.len();

    // CR-03: validate period before the insufficient-data check below --
    // see decompose_additive for why this order is load-bearing (infinity
    // overflow in `2 * period as usize`).
    if !period.is_finite() || period <= 0.0 {
        return Err(ForecastError::InvalidParameter {
            param: "period".to_string(),
            value: period.to_string(),
            reason: "Period must be a positive finite number".to_string(),
        });
    }

    if n < 2 * period as usize {
        return Err(ForecastError::InsufficientData {
            needed: 2 * period as usize,
            got: n,
        });
    }

    let argvals = make_argvals(n);
    let method = trend_method.unwrap_or("loess");
    let bw = bandwidth.unwrap_or(0.3);
    let harmonics = n_harmonics.unwrap_or(3);

    let mat = single_series_matrix(values)?;
    let result = fdars_decompose_multiplicative(&mat, &argvals, period, method, bw, harmonics);

    Ok(result.into())
}

/// Seasonal decomposition using the specified method.
///
/// # Arguments
/// * `values` - Time series values
/// * `period` - Seasonal period
/// * `method` - Decomposition method (additive or multiplicative)
///
/// # Returns
/// Decomposition result with trend, seasonal, and remainder
pub fn decompose(values: &[f64], period: f64, method: DecomposeMethod) -> Result<DecomposeResult> {
    match method {
        DecomposeMethod::Additive => decompose_additive(values, period, None, None, None),
        DecomposeMethod::Multiplicative => {
            decompose_multiplicative(values, period, None, None, None)
        }
        DecomposeMethod::ClassicalAdditive => {
            let p = classical_period_as_usize(period)?;
            let cd = classical_decompose(values, p, ClassicalKind::Additive)?;
            Ok(DecomposeResult {
                trend: cd.trend,
                seasonal: cd.seasonal,
                remainder: cd.remainder,
                period,
                method: "classical_additive".to_string(),
            })
        }
        DecomposeMethod::ClassicalMultiplicative => {
            let p = classical_period_as_usize(period)?;
            let cd = classical_decompose(values, p, ClassicalKind::Multiplicative)?;
            Ok(DecomposeResult {
                trend: cd.trend,
                seasonal: cd.seasonal,
                remainder: cd.remainder,
                period,
                method: "classical_multiplicative".to_string(),
            })
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::f64::consts::PI;

    fn generate_trended_series(n: usize, slope: f64, intercept: f64) -> Vec<f64> {
        (0..n).map(|i| intercept + slope * i as f64).collect()
    }

    fn generate_seasonal_series(
        n: usize,
        period: f64,
        amplitude: f64,
        trend_slope: f64,
    ) -> Vec<f64> {
        (0..n)
            .map(|i| {
                let trend = trend_slope * i as f64;
                let seasonal = amplitude * (2.0 * PI * i as f64 / period).sin();
                trend + seasonal + 10.0
            })
            .collect()
    }

    #[test]
    fn test_detrend_linear() {
        let values = generate_trended_series(100, 0.5, 10.0);
        let result = detrend_linear(&values).unwrap();

        // Verify function runs and returns correct lengths
        assert_eq!(result.trend.len(), values.len());
        assert_eq!(result.detrended.len(), values.len());
        assert_eq!(result.method, "linear");
    }

    #[test]
    fn test_detrend_polynomial() {
        let values: Vec<f64> = (0..100)
            .map(|i| 0.01 * (i as f64).powi(2) + 0.5 * i as f64 + 10.0)
            .collect();
        let result = detrend_polynomial(&values, 2).unwrap();

        assert_eq!(result.trend.len(), values.len());
    }

    #[test]
    fn test_detrend_diff() {
        let values = generate_trended_series(100, 0.5, 10.0);
        let result = detrend_diff(&values, 1);

        // Verify function runs without error
        assert!(result.is_ok());
    }

    #[test]
    fn test_detrend_loess() {
        let values = generate_trended_series(100, 0.5, 10.0);
        let result = detrend_loess(&values, 0.3, 1).unwrap();

        assert_eq!(result.trend.len(), values.len());
        assert_eq!(result.method, "loess");
    }

    #[test]
    fn test_detrend_auto() {
        let values = generate_trended_series(100, 0.5, 10.0);
        let result = detrend_auto(&values).unwrap();

        assert_eq!(result.trend.len(), values.len());
    }

    #[test]
    fn test_decompose_additive() {
        let values = generate_seasonal_series(120, 12.0, 5.0, 0.1);
        let result = decompose_additive(&values, 12.0, None, None, None).unwrap();

        assert_eq!(result.trend.len(), values.len());
        assert_eq!(result.seasonal.len(), values.len());
        assert_eq!(result.remainder.len(), values.len());
        assert_eq!(result.period, 12.0);
        assert_eq!(result.method, "additive");
    }

    #[test]
    fn test_decompose_multiplicative() {
        // Multiplicative needs positive values
        let values: Vec<f64> = (0..120)
            .map(|i| {
                let trend = 10.0 + 0.1 * i as f64;
                let seasonal = 1.0 + 0.3 * (2.0 * PI * i as f64 / 12.0).sin();
                trend * seasonal
            })
            .collect();
        let result = decompose_multiplicative(&values, 12.0, None, None, None).unwrap();

        assert_eq!(result.method, "multiplicative");
    }

    // ------------------------------------------------------------------
    // Decompose behavior (Task 3): constant/non-trivial decomposition and
    // strict DecomposeMethod::from_str.
    // ------------------------------------------------------------------

    fn pearson(a: &[f64], b: &[f64]) -> f64 {
        let n = a.len() as f64;
        let ma = a.iter().sum::<f64>() / n;
        let mb = b.iter().sum::<f64>() / n;
        let cov: f64 = a.iter().zip(b).map(|(x, y)| (x - ma) * (y - mb)).sum();
        let va: f64 = a.iter().map(|x| (x - ma).powi(2)).sum();
        let vb: f64 = b.iter().map(|y| (y - mb).powi(2)).sum();
        cov / (va.sqrt() * vb.sqrt())
    }

    #[test]
    fn test_decompose_constant_series_trend_is_exact_and_seasonal_is_zero() {
        // Before fdars-core 0.47 (B1), decompose_additive passed
        // (values, n, 1, ...) -- backwards for a single series.
        let constant = vec![1.0; 48];
        let result = decompose(&constant, 12.0, DecomposeMethod::Additive).unwrap();

        for (i, &t) in result.trend.iter().enumerate() {
            assert!((t - 1.0).abs() < 1e-12, "trend[{i}] = {t}");
        }
        for (i, &s) in result.seasonal.iter().enumerate() {
            assert!(s.abs() < 1e-12, "seasonal[{i}] = {s}");
        }
    }

    #[test]
    fn test_decompose_additive_reconstructs_input_and_recovers_seasonal() {
        let n = 120;
        let seasonal_component: Vec<f64> = (0..n)
            .map(|i| 3.0 * (2.0 * PI * i as f64 / 12.0).sin())
            .collect();
        let values: Vec<f64> = (0..n)
            .map(|i| 10.0 + 0.1 * i as f64 + seasonal_component[i])
            .collect();
        let result = decompose(&values, 12.0, DecomposeMethod::Additive).unwrap();

        for (i, &want) in values.iter().enumerate() {
            let recon = result.trend[i] + result.seasonal[i] + result.remainder[i];
            assert!(
                (recon - want).abs() < 1e-10,
                "index {i}: trend+seasonal+remainder = {recon}, input = {want}"
            );
        }
        let corr = pearson(&result.seasonal, &seasonal_component);
        assert!(corr > 0.99, "correlation with generating seasonal = {corr}");
    }

    #[test]
    fn test_decompose_multiplicative_reconstructs_input() {
        let n = 120;
        let values: Vec<f64> = (0..n)
            .map(|i| {
                let trend = 10.0 + 0.1 * i as f64;
                let seasonal = 1.0 + 0.3 * (2.0 * PI * i as f64 / 12.0).sin();
                trend * seasonal
            })
            .collect();
        let result = decompose(&values, 12.0, DecomposeMethod::Multiplicative).unwrap();

        for (i, &want) in values.iter().enumerate() {
            let recon = result.trend[i] * result.seasonal[i] * result.remainder[i];
            assert!(
                (recon - want).abs() < 1e-9,
                "index {i}: trend*seasonal*remainder = {recon}, input = {want}"
            );
        }
    }

    #[test]
    fn test_decompose_method_accepted_spellings() {
        assert_eq!(
            "additive".parse::<DecomposeMethod>().unwrap(),
            DecomposeMethod::Additive
        );
        assert_eq!(
            "add".parse::<DecomposeMethod>().unwrap(),
            DecomposeMethod::Additive
        );
        assert_eq!(
            "multiplicative".parse::<DecomposeMethod>().unwrap(),
            DecomposeMethod::Multiplicative
        );
        assert_eq!(
            "mult".parse::<DecomposeMethod>().unwrap(),
            DecomposeMethod::Multiplicative
        );
        assert_eq!(
            "mul".parse::<DecomposeMethod>().unwrap(),
            DecomposeMethod::Multiplicative
        );
    }

    // ------------------------------------------------------------------
    // Classical decomposition dispatch (D-10, Phase 15 plan 15-04)
    // ------------------------------------------------------------------

    #[test]
    fn test_decompose_method_classical_accepted_spellings() {
        assert_eq!(
            "classical".parse::<DecomposeMethod>().unwrap(),
            DecomposeMethod::ClassicalAdditive
        );
        assert_eq!(
            "classical_additive".parse::<DecomposeMethod>().unwrap(),
            DecomposeMethod::ClassicalAdditive
        );
        assert_eq!(
            "classical_multiplicative"
                .parse::<DecomposeMethod>()
                .unwrap(),
            DecomposeMethod::ClassicalMultiplicative
        );
    }

    #[test]
    fn test_decompose_classical_additive_method_string_is_classical_additive() {
        let values: Vec<f64> = (0..48)
            .map(|i| 10.0 + 0.1 * i as f64 + 3.0 * (2.0 * PI * i as f64 / 12.0).sin())
            .collect();
        let result = decompose(&values, 12.0, DecomposeMethod::ClassicalAdditive).unwrap();
        assert_eq!(result.method, "classical_additive");
        assert_eq!(result.period, 12.0);
    }

    #[test]
    fn test_decompose_classical_multiplicative_method_string_is_classical_multiplicative() {
        let values: Vec<f64> = (0..48)
            .map(|i| {
                let trend = 10.0 + 0.1 * i as f64;
                let seasonal = 1.0 + 0.3 * (2.0 * PI * i as f64 / 12.0).sin();
                trend * seasonal
            })
            .collect();
        let result = decompose(&values, 12.0, DecomposeMethod::ClassicalMultiplicative).unwrap();
        assert_eq!(result.method, "classical_multiplicative");
    }

    #[test]
    fn test_decompose_classical_non_integer_period_is_invalid_parameter() {
        let values: Vec<f64> = (0..48).map(|i| i as f64).collect();
        let result = decompose(&values, 12.5, DecomposeMethod::ClassicalAdditive);
        assert!(matches!(
            result,
            Err(ForecastError::InvalidParameter { .. })
        ));
    }

    #[test]
    fn test_decompose_classical_period_one_is_invalid_parameter() {
        let values: Vec<f64> = (0..48).map(|i| i as f64).collect();
        let result = decompose(&values, 1.0, DecomposeMethod::ClassicalAdditive);
        assert!(matches!(
            result,
            Err(ForecastError::InvalidParameter { .. })
        ));
    }

    #[test]
    fn test_decompose_classical_multiplicative_nonpositive_value_is_invalid_parameter() {
        let mut values: Vec<f64> = (1..=48).map(|i| i as f64).collect();
        values[3] = 0.0;
        let result = decompose(&values, 12.0, DecomposeMethod::ClassicalMultiplicative);
        assert!(matches!(
            result,
            Err(ForecastError::InvalidParameter { .. })
        ));
    }

    #[test]
    fn test_decompose_method_unknown_name_errors_naming_valid_methods() {
        let err = "classicalish".parse::<DecomposeMethod>().unwrap_err();
        match err {
            ForecastError::InvalidParameter {
                param,
                value,
                reason,
            } => {
                assert_eq!(param, "method");
                assert_eq!(value, "classicalish");
                assert!(reason.contains("additive"));
                assert!(reason.contains("multiplicative"));
            }
            other => panic!("expected InvalidParameter, got {other:?}"),
        }
    }

    #[test]
    fn test_invalid_bandwidth() {
        let values = generate_trended_series(100, 0.5, 10.0);
        assert!(detrend_loess(&values, 0.0, 1).is_err());
        assert!(detrend_loess(&values, 1.5, 1).is_err());
    }

    #[test]
    fn test_insufficient_data() {
        let values = vec![1.0];
        assert!(detrend_linear(&values).is_err());
    }

    // ------------------------------------------------------------------
    // D-02 / B3: strict DetrendMethod::from_str (Task 2)
    // ------------------------------------------------------------------

    #[test]
    fn test_detrend_method_accepted_spellings() {
        assert_eq!(
            "linear".parse::<DetrendMethod>().unwrap(),
            DetrendMethod::Linear
        );
        assert_eq!(
            "quadratic".parse::<DetrendMethod>().unwrap(),
            DetrendMethod::Quadratic
        );
        assert_eq!(
            "polynomial".parse::<DetrendMethod>().unwrap(),
            DetrendMethod::Quadratic
        );
        assert_eq!(
            "poly".parse::<DetrendMethod>().unwrap(),
            DetrendMethod::Quadratic
        );
        assert_eq!(
            "cubic".parse::<DetrendMethod>().unwrap(),
            DetrendMethod::Cubic
        );
        assert_eq!(
            "diff".parse::<DetrendMethod>().unwrap(),
            DetrendMethod::Diff
        );
        assert_eq!(
            "diff1".parse::<DetrendMethod>().unwrap(),
            DetrendMethod::Diff
        );
        assert_eq!(
            "difference".parse::<DetrendMethod>().unwrap(),
            DetrendMethod::Diff
        );
        assert_eq!(
            "diff2".parse::<DetrendMethod>().unwrap(),
            DetrendMethod::Diff2
        );
        assert_eq!(
            "loess".parse::<DetrendMethod>().unwrap(),
            DetrendMethod::Loess
        );
        assert_eq!(
            "lowess".parse::<DetrendMethod>().unwrap(),
            DetrendMethod::Loess
        );
        assert_eq!(
            "auto".parse::<DetrendMethod>().unwrap(),
            DetrendMethod::Auto
        );
        // Case-insensitive
        assert_eq!(
            "CUBIC".parse::<DetrendMethod>().unwrap(),
            DetrendMethod::Cubic
        );
    }

    #[test]
    fn test_detrend_method_unknown_name_errors_naming_valid_methods() {
        let err = "quartic".parse::<DetrendMethod>().unwrap_err();
        match err {
            ForecastError::InvalidParameter {
                param,
                value,
                reason,
            } => {
                assert_eq!(param, "method");
                assert_eq!(value, "quartic");
                assert!(reason.contains("linear"));
                assert!(reason.contains("quadratic"));
                assert!(reason.contains("cubic"));
                assert!(reason.contains("diff"));
                assert!(reason.contains("loess"));
                assert!(reason.contains("auto"));
            }
            other => panic!("expected InvalidParameter, got {other:?}"),
        }

        let err2 = "7".parse::<DetrendMethod>().unwrap_err();
        assert!(matches!(err2, ForecastError::InvalidParameter { .. }));
    }

    #[test]
    fn test_detrend_polynomial_insufficient_data_for_degree() {
        // Cubic needs degree + 1 = 4 points
        let values = vec![1.0, 2.0, 3.0];
        let result = detrend_polynomial(&values, 3);
        assert!(matches!(
            result,
            Err(ForecastError::InsufficientData { needed: 4, got: 3 })
        ));
    }

    #[test]
    fn test_single_series_matrix_shape() {
        let values = vec![1.0, 2.0, 3.0, 4.0, 5.0];
        let mat = single_series_matrix(&values).unwrap();
        assert_eq!(mat.shape(), (1, 5));
    }

    #[test]
    fn test_detrend_cubic_matches_polyfit_shape() {
        // Cubic series: 0.002 t^3 - 0.08 t^2 + 0.5 t + 3 (noise-free, exact fit expected)
        let n = 40;
        let values: Vec<f64> = (0..n)
            .map(|i| {
                let t = i as f64;
                0.002 * t.powi(3) - 0.08 * t.powi(2) + 0.5 * t + 3.0
            })
            .collect();
        let result = detrend_polynomial(&values, 3).unwrap();
        assert_eq!(result.n_params, 4);
        assert_eq!(result.method, "polynomial(3)");
        // Exact cubic should fit essentially perfectly.
        for (t_val, trend_val) in result.trend.iter().enumerate() {
            let expected = {
                let t = t_val as f64;
                0.002 * t.powi(3) - 0.08 * t.powi(2) + 0.5 * t + 3.0
            };
            assert!(
                (trend_val - expected).abs() < 1e-6,
                "trend[{t_val}] = {trend_val}, expected {expected}"
            );
        }
        // Coefficients (ascending powers of raw index) should recover the
        // generating polynomial.
        let coefs = result.coefficients.unwrap();
        assert!((coefs[0] - 3.0).abs() < 1e-6, "c0 = {}", coefs[0]);
        assert!((coefs[1] - 0.5).abs() < 1e-6, "c1 = {}", coefs[1]);
        assert!((coefs[2] - (-0.08)).abs() < 1e-6, "c2 = {}", coefs[2]);
        assert!((coefs[3] - 0.002).abs() < 1e-6, "c3 = {}", coefs[3]);
    }
}
