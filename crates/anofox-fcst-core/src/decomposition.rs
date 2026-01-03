//! Time series decomposition (MSTL).

use crate::error::{ForecastError, Result};
use std::str::FromStr;

/// Mode for handling insufficient data in MSTL decomposition.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum InsufficientDataMode {
    /// Error on insufficient data (default behavior)
    #[default]
    Fail,
    /// Apply trend-only decomposition with NULL seasonal columns
    Trend,
    /// No decomposition, all decomposition columns = NULL
    None,
}

impl FromStr for InsufficientDataMode {
    type Err = std::convert::Infallible;

    fn from_str(s: &str) -> std::result::Result<Self, Self::Err> {
        Ok(match s.to_lowercase().as_str() {
            "trend" => Self::Trend,
            "none" => Self::None,
            _ => Self::Fail,
        })
    }
}

impl InsufficientDataMode {
    /// Create from integer (for FFI).
    pub fn from_int(i: i32) -> Self {
        match i {
            1 => Self::Trend,
            2 => Self::None,
            _ => Self::Fail,
        }
    }
}

/// Result of MSTL decomposition.
#[derive(Debug, Clone)]
pub struct MstlDecomposition {
    /// Trend component (None if decomposition was skipped)
    pub trend: Option<Vec<f64>>,
    /// Seasonal components (one per period)
    pub seasonal: Vec<Vec<f64>>,
    /// Seasonal periods used
    pub periods: Vec<i32>,
    /// Remainder (residual) component (None if decomposition was skipped)
    pub remainder: Option<Vec<f64>>,
    /// Whether decomposition was actually applied
    pub decomposition_applied: bool,
}

/// Perform STL decomposition for a single seasonal period.
fn stl_decompose(values: &[f64], period: usize) -> Result<(Vec<f64>, Vec<f64>, Vec<f64>)> {
    if values.len() < 2 * period {
        return Err(ForecastError::InsufficientData {
            needed: 2 * period,
            got: values.len(),
        });
    }

    let n = values.len();

    // Simple STL approximation using moving averages
    // 1. Trend: centered moving average
    let window = if period.is_multiple_of(2) {
        period + 1
    } else {
        period
    };
    let half_window = window / 2;

    let mut trend = vec![f64::NAN; n];
    for i in half_window..(n - half_window) {
        let sum: f64 = values[i - half_window..=i + half_window].iter().sum();
        trend[i] = sum / window as f64;
    }

    // Extend trend to edges
    let first_valid = trend.iter().position(|v| !v.is_nan()).unwrap_or(0);
    let last_valid = trend.iter().rposition(|v| !v.is_nan()).unwrap_or(n - 1);

    for i in 0..first_valid {
        trend[i] = trend[first_valid];
    }
    for i in (last_valid + 1)..n {
        trend[i] = trend[last_valid];
    }

    // 2. Detrended series
    let detrended: Vec<f64> = values
        .iter()
        .zip(trend.iter())
        .map(|(v, t)| v - t)
        .collect();

    // 3. Seasonal component: average by season
    let mut seasonal = vec![0.0; n];
    let num_cycles = n / period;

    for s in 0..period {
        let mut sum = 0.0;
        let mut count = 0;
        for c in 0..=num_cycles {
            let idx = c * period + s;
            if idx < n {
                sum += detrended[idx];
                count += 1;
            }
        }
        let avg = if count > 0 { sum / count as f64 } else { 0.0 };

        // Center the seasonal component
        for c in 0..=num_cycles {
            let idx = c * period + s;
            if idx < n {
                seasonal[idx] = avg;
            }
        }
    }

    // Center seasonal component (mean = 0)
    let seasonal_mean = seasonal.iter().sum::<f64>() / n as f64;
    for s in &mut seasonal {
        *s -= seasonal_mean;
    }

    // 4. Remainder
    let remainder: Vec<f64> = values
        .iter()
        .zip(trend.iter())
        .zip(seasonal.iter())
        .map(|((v, t), s)| v - t - s)
        .collect();

    Ok((trend, seasonal, remainder))
}

/// Perform MSTL (Multiple Seasonal-Trend decomposition using Loess) decomposition.
///
/// This is a simplified version that handles multiple seasonal periods.
///
/// # Arguments
/// * `values` - Time series values
/// * `periods` - Seasonal periods to extract
/// * `insufficient_data_mode` - How to handle insufficient data
///   - `Fail`: Error on insufficient data (default)
///   - `Trend`: Apply trend-only decomposition, seasonal components are empty
///   - `None`: Skip decomposition entirely, return empty result
pub fn mstl_decompose(
    values: &[f64],
    periods: &[i32],
    insufficient_data_mode: InsufficientDataMode,
) -> Result<MstlDecomposition> {
    if values.is_empty() {
        match insufficient_data_mode {
            InsufficientDataMode::Fail => {
                return Err(ForecastError::InsufficientData { needed: 1, got: 0 });
            }
            InsufficientDataMode::Trend | InsufficientDataMode::None => {
                return Ok(MstlDecomposition {
                    trend: None,
                    seasonal: vec![],
                    periods: vec![],
                    remainder: None,
                    decomposition_applied: false,
                });
            }
        }
    }

    // Check if we have enough data for the requested periods
    let n = values.len();
    let min_period = periods
        .iter()
        .filter(|&&p| p > 0)
        .min()
        .copied()
        .unwrap_or(0) as usize;
    let insufficient = !periods.is_empty() && min_period > 0 && n < 2 * min_period;

    if insufficient {
        match insufficient_data_mode {
            InsufficientDataMode::Fail => {
                return Err(ForecastError::InsufficientData {
                    needed: 2 * min_period,
                    got: n,
                });
            }
            InsufficientDataMode::None => {
                return Ok(MstlDecomposition {
                    trend: None,
                    seasonal: vec![],
                    periods: vec![],
                    remainder: None,
                    decomposition_applied: false,
                });
            }
            InsufficientDataMode::Trend => {
                // Fall through to trend-only computation below
            }
        }
    }

    if periods.is_empty() || insufficient {
        // No seasonal periods or insufficient data with Trend mode - just compute trend and remainder
        let window = (n / 5).max(3).min(n);
        let half_window = window / 2;

        let mut trend = vec![f64::NAN; n];
        for i in half_window..(n - half_window) {
            let sum: f64 = values[i - half_window..=i + half_window].iter().sum();
            trend[i] = sum / window as f64;
        }

        // Extend trend
        let first_valid = trend.iter().position(|v| !v.is_nan()).unwrap_or(0);
        let last_valid = trend.iter().rposition(|v| !v.is_nan()).unwrap_or(n - 1);

        for i in 0..first_valid {
            trend[i] = trend[first_valid];
        }
        for i in (last_valid + 1)..n {
            trend[i] = trend[last_valid];
        }

        let remainder: Vec<f64> = values
            .iter()
            .zip(trend.iter())
            .map(|(v, t)| v - t)
            .collect();

        return Ok(MstlDecomposition {
            trend: Some(trend),
            seasonal: vec![],
            periods: vec![],
            remainder: Some(remainder),
            decomposition_applied: true,
        });
    }

    // Sort periods in descending order (handle longest first)
    let mut sorted_periods: Vec<i32> = periods.to_vec();
    sorted_periods.sort_by(|a, b| b.cmp(a));

    let n = values.len();
    let mut current = values.to_vec();
    let mut seasonal_components: Vec<Vec<f64>> = Vec::new();
    let mut final_periods: Vec<i32> = Vec::new();

    // Iteratively extract seasonal components
    for &period in &sorted_periods {
        let p = period as usize;
        if p < 2 || n < 2 * p {
            continue;
        }

        match stl_decompose(&current, p) {
            Ok((_, seasonal, _)) => {
                seasonal_components.push(seasonal.clone());
                final_periods.push(period);

                // Remove this seasonal component for next iteration
                for (i, s) in seasonal.iter().enumerate() {
                    current[i] -= s;
                }
            }
            Err(_) => continue,
        }
    }

    // Final trend extraction from remaining series
    let window = (n / 5).max(3).min(n);
    let half_window = window / 2;

    let mut trend = vec![f64::NAN; n];
    for (i, trend_val) in trend
        .iter_mut()
        .enumerate()
        .take(n.saturating_sub(half_window))
        .skip(half_window)
    {
        let end = (i + half_window + 1).min(n);
        let start = i.saturating_sub(half_window);
        let sum: f64 = current[start..end].iter().sum();
        *trend_val = sum / (end - start) as f64;
    }

    // Extend trend
    let first_valid = trend.iter().position(|v| !v.is_nan()).unwrap_or(0);
    let last_valid = trend.iter().rposition(|v| !v.is_nan()).unwrap_or(n - 1);

    for i in 0..first_valid {
        trend[i] = trend[first_valid];
    }
    for i in (last_valid + 1)..n {
        trend[i] = trend[last_valid];
    }

    // Calculate remainder
    let mut remainder = current;
    for (i, t) in trend.iter().enumerate() {
        remainder[i] -= t;
    }

    Ok(MstlDecomposition {
        trend: Some(trend),
        seasonal: seasonal_components,
        periods: final_periods,
        remainder: Some(remainder),
        decomposition_applied: true,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::f64::consts::PI;

    #[test]
    fn test_mstl_decompose() {
        // Create series with trend and seasonality
        let values: Vec<f64> = (0..120)
            .map(|i| {
                let trend = 0.1 * i as f64;
                let seasonal = 5.0 * (2.0 * PI * i as f64 / 12.0).sin();
                trend + seasonal
            })
            .collect();

        let result = mstl_decompose(&values, &[12], InsufficientDataMode::Fail).unwrap();

        assert!(result.decomposition_applied);
        assert_eq!(result.trend.unwrap().len(), values.len());
        assert_eq!(result.seasonal.len(), 1);
        assert_eq!(result.seasonal[0].len(), values.len());
        assert_eq!(result.remainder.unwrap().len(), values.len());
    }

    #[test]
    fn test_mstl_insufficient_data_fail() {
        let values = vec![1.0, 2.0, 3.0];
        let result = mstl_decompose(&values, &[12], InsufficientDataMode::Fail);
        assert!(result.is_err());
    }

    #[test]
    fn test_mstl_insufficient_data_trend() {
        let values = vec![1.0, 2.0, 3.0, 4.0, 5.0];
        let result = mstl_decompose(&values, &[12], InsufficientDataMode::Trend).unwrap();

        assert!(result.decomposition_applied);
        assert!(result.trend.is_some());
        assert!(result.seasonal.is_empty());
        assert!(result.remainder.is_some());
    }

    #[test]
    fn test_mstl_insufficient_data_none() {
        let values = vec![1.0, 2.0];
        let result = mstl_decompose(&values, &[12], InsufficientDataMode::None).unwrap();

        assert!(!result.decomposition_applied);
        assert!(result.trend.is_none());
        assert!(result.seasonal.is_empty());
        assert!(result.remainder.is_none());
    }
}
