//! Detrending and decomposition for time series.
//!
//! This module wraps fdars-core's detrending and decomposition functions
//! for use with time series data in DuckDB.

use crate::error::{ForecastError, Result};
use fdars_core::detrend::{
    auto_detrend as fdars_auto_detrend, decompose_additive as fdars_decompose_additive,
    decompose_multiplicative as fdars_decompose_multiplicative, detrend_diff as fdars_detrend_diff,
    detrend_linear as fdars_detrend_linear, detrend_loess as fdars_detrend_loess,
    detrend_polynomial as fdars_detrend_polynomial, DecomposeResult as FdarsDecomposeResult,
    TrendResult as FdarsTrendResult,
};
use std::str::FromStr;

/// Method for detrending.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum DetrendMethod {
    /// Linear trend removal using least squares
    Linear,
    /// Polynomial trend removal
    Polynomial,
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

impl FromStr for DetrendMethod {
    type Err = std::convert::Infallible;

    fn from_str(s: &str) -> std::result::Result<Self, Self::Err> {
        Ok(match s.to_lowercase().as_str() {
            "linear" => Self::Linear,
            "polynomial" | "poly" => Self::Polynomial,
            "diff" | "diff1" | "difference" => Self::Diff,
            "diff2" => Self::Diff2,
            "loess" | "lowess" => Self::Loess,
            "auto" => Self::Auto,
            _ => Self::Auto,
        })
    }
}

/// Decomposition method for seasonal decomposition.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum DecomposeMethod {
    /// Additive decomposition: data = trend + seasonal + remainder
    #[default]
    Additive,
    /// Multiplicative decomposition: data = trend * seasonal * remainder
    Multiplicative,
}

impl FromStr for DecomposeMethod {
    type Err = std::convert::Infallible;

    fn from_str(s: &str) -> std::result::Result<Self, Self::Err> {
        Ok(match s.to_lowercase().as_str() {
            "additive" | "add" => Self::Additive,
            "multiplicative" | "mult" | "mul" => Self::Multiplicative,
            _ => Self::Additive,
        })
    }
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
    /// Polynomial coefficients (for polynomial methods)
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
            trend: r.trend,
            detrended: r.detrended,
            method: r.method,
            coefficients: r.coefficients,
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
            trend: r.trend,
            seasonal: r.seasonal,
            remainder: r.remainder,
            period: r.period,
            method: r.method,
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
    let result = fdars_detrend_linear(values, n, 1, &argvals);

    Ok(result.into())
}

/// Remove polynomial trend from time series.
///
/// # Arguments
/// * `values` - Time series values
/// * `degree` - Polynomial degree (1 = linear, 2 = quadratic, etc.)
///
/// # Returns
/// Detrend result with trend and detrended data
pub fn detrend_polynomial(values: &[f64], degree: usize) -> Result<DetrendResult> {
    let n = values.len();
    if n < degree + 1 {
        return Err(ForecastError::InsufficientData {
            needed: degree + 1,
            got: n,
        });
    }

    let argvals = make_argvals(n);
    let result = fdars_detrend_polynomial(values, n, 1, &argvals, degree);

    Ok(result.into())
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

    let result = fdars_detrend_diff(values, n, 1, order);

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
    let result = fdars_detrend_loess(values, n, 1, &argvals, bandwidth, degree);

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
    let result = fdars_auto_detrend(values, n, 1, &argvals);

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
        DetrendMethod::Polynomial => detrend_polynomial(values, 2),
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
    if n < 2 * period as usize {
        return Err(ForecastError::InsufficientData {
            needed: 2 * period as usize,
            got: n,
        });
    }

    if period <= 0.0 {
        return Err(ForecastError::InvalidParameter {
            param: "period".to_string(),
            value: period.to_string(),
            reason: "Period must be positive".to_string(),
        });
    }

    let argvals = make_argvals(n);
    let method = trend_method.unwrap_or("loess");
    let bw = bandwidth.unwrap_or(0.3);
    let harmonics = n_harmonics.unwrap_or(3);

    let result = fdars_decompose_additive(values, n, 1, &argvals, period, method, bw, harmonics);

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
    if n < 2 * period as usize {
        return Err(ForecastError::InsufficientData {
            needed: 2 * period as usize,
            got: n,
        });
    }

    if period <= 0.0 {
        return Err(ForecastError::InvalidParameter {
            param: "period".to_string(),
            value: period.to_string(),
            reason: "Period must be positive".to_string(),
        });
    }

    let argvals = make_argvals(n);
    let method = trend_method.unwrap_or("loess");
    let bw = bandwidth.unwrap_or(0.3);
    let harmonics = n_harmonics.unwrap_or(3);

    let result =
        fdars_decompose_multiplicative(values, n, 1, &argvals, period, method, bw, harmonics);

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
}
