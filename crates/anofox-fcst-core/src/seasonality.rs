//! Seasonality detection and analysis.

use crate::error::{ForecastError, Result};

/// Result of seasonality analysis.
#[derive(Debug, Clone)]
pub struct SeasonalityAnalysis {
    /// Detected seasonal periods
    pub periods: Vec<i32>,
    /// Strength of each detected period (0-1)
    pub strengths: Vec<f64>,
    /// Primary (dominant) period
    pub primary_period: i32,
    /// Overall trend strength (0-1)
    pub trend_strength: f64,
    /// Overall seasonal strength (0-1)
    pub seasonal_strength: f64,
    /// Whether the series is considered seasonal
    pub is_seasonal: bool,
}

/// Detect seasonal periods in a time series using autocorrelation.
pub fn detect_seasonality(values: &[f64], max_period: Option<usize>) -> Result<Vec<i32>> {
    if values.len() < 4 {
        return Err(ForecastError::InsufficientData {
            needed: 4,
            got: values.len(),
        });
    }

    let max_lag = max_period.unwrap_or(values.len() / 2).min(values.len() / 2);

    if max_lag < 2 {
        return Ok(vec![]);
    }

    // Calculate autocorrelation at each lag
    let mean: f64 = values.iter().sum::<f64>() / values.len() as f64;
    let variance: f64 = values.iter().map(|v| (v - mean).powi(2)).sum::<f64>();

    if variance.abs() < f64::EPSILON {
        return Ok(vec![]);
    }

    let mut acf = Vec::with_capacity(max_lag);
    for lag in 1..=max_lag {
        let mut sum = 0.0;
        for i in 0..(values.len() - lag) {
            sum += (values[i] - mean) * (values[i + lag] - mean);
        }
        acf.push(sum / variance);
    }

    // Find peaks in ACF
    let mut periods = Vec::new();
    let threshold = 0.1; // Minimum ACF value to consider

    for i in 1..(acf.len() - 1) {
        if acf[i] > acf[i - 1] && acf[i] > acf[i + 1] && acf[i] > threshold {
            periods.push((i + 1) as i32);
        }
    }

    // Sort by ACF strength
    periods.sort_by(|a, b| {
        let acf_a = acf[(*a as usize) - 1];
        let acf_b = acf[(*b as usize) - 1];
        acf_b
            .partial_cmp(&acf_a)
            .unwrap_or(std::cmp::Ordering::Equal)
    });

    // Keep top 5 periods
    periods.truncate(5);

    Ok(periods)
}

/// Analyze seasonality in a time series.
pub fn analyze_seasonality(
    values: &[f64],
    max_period: Option<usize>,
) -> Result<SeasonalityAnalysis> {
    let periods = detect_seasonality(values, max_period)?;

    if periods.is_empty() {
        return Ok(SeasonalityAnalysis {
            periods: vec![],
            strengths: vec![],
            primary_period: 0,
            trend_strength: compute_trend_strength(values),
            seasonal_strength: 0.0,
            is_seasonal: false,
        });
    }

    // Calculate strength for each period
    let mean: f64 = values.iter().sum::<f64>() / values.len() as f64;
    let variance: f64 = values.iter().map(|v| (v - mean).powi(2)).sum::<f64>();

    let mut strengths = Vec::with_capacity(periods.len());
    for &period in &periods {
        let lag = period as usize;
        if lag >= values.len() {
            strengths.push(0.0);
            continue;
        }

        let mut sum = 0.0;
        for i in 0..(values.len() - lag) {
            sum += (values[i] - mean) * (values[i + lag] - mean);
        }
        let acf = if variance.abs() > f64::EPSILON {
            sum / variance
        } else {
            0.0
        };
        strengths.push(acf.clamp(0.0, 1.0));
    }

    let primary_period = periods.first().cloned().unwrap_or(0);
    let seasonal_strength = strengths.first().cloned().unwrap_or(0.0);
    let trend_strength = compute_trend_strength(values);

    Ok(SeasonalityAnalysis {
        periods,
        strengths,
        primary_period,
        trend_strength,
        seasonal_strength,
        is_seasonal: seasonal_strength > 0.1,
    })
}

/// Compute trend strength using linear regression R-squared.
fn compute_trend_strength(values: &[f64]) -> f64 {
    if values.len() < 2 {
        return 0.0;
    }

    let n = values.len() as f64;
    let x_mean = (n - 1.0) / 2.0;
    let y_mean: f64 = values.iter().sum::<f64>() / n;

    let mut ss_xy = 0.0;
    let mut ss_xx = 0.0;
    let mut ss_yy = 0.0;

    for (i, &y) in values.iter().enumerate() {
        let x = i as f64;
        ss_xy += (x - x_mean) * (y - y_mean);
        ss_xx += (x - x_mean).powi(2);
        ss_yy += (y - y_mean).powi(2);
    }

    if ss_xx.abs() < f64::EPSILON || ss_yy.abs() < f64::EPSILON {
        return 0.0;
    }

    (ss_xy.powi(2) / (ss_xx * ss_yy)).sqrt().clamp(0.0, 1.0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::f64::consts::PI;

    #[test]
    fn test_detect_seasonality_sine() {
        // Create a sine wave with period 12
        let values: Vec<f64> = (0..120)
            .map(|i| (2.0 * PI * i as f64 / 12.0).sin())
            .collect();

        let periods = detect_seasonality(&values, Some(24)).unwrap();
        assert!(!periods.is_empty());
        // Should detect period around 12
        assert!(periods.contains(&12) || periods.contains(&11) || periods.contains(&13));
    }

    #[test]
    fn test_analyze_seasonality() {
        let values: Vec<f64> = (0..120)
            .map(|i| (2.0 * PI * i as f64 / 12.0).sin() + 0.1 * i as f64)
            .collect();

        let analysis = analyze_seasonality(&values, Some(24)).unwrap();
        assert!(analysis.is_seasonal);
        assert!(analysis.seasonal_strength > 0.1);
    }
}
