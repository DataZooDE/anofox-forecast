//! Time series decomposition: MSTL (`forecast::mstl`) and classical
//! (`stats::decompose`).
//!
//! Phase 15 plan 15-04 (DET-04, D-09, D-10) replaced the pre-existing
//! hand-rolled classical-moving-average approximation with two R-faithful
//! implementations sharing a single STL fit ([`mstl_fit`]): `mstl_decompose`
//! (consumed by `_ts_mstl_decomposition`) and `stats::stl_strengths`
//! (`ts_stats`' `trend_strength`/`seasonality_strength` columns) both build
//! on it, so there is exactly one place the `forecast::mstl` iteration is
//! implemented. [`classical_decompose`] is an independent port of R
//! `stats::decompose`, reached from SQL via `ts_decompose_seasonal`'s
//! `classical_additive`/`classical_multiplicative` methods.

use crate::error::{ForecastError, Result};
use crate::stl::{stl, StlParams};
use crate::supsmu::supsmu;
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

/// Shared `forecast::mstl` fit (D-09) -- the single implementation
/// consumed by both [`mstl_decompose`] and `crate::stats::stl_strengths`.
#[derive(Debug, Clone)]
pub struct MstlFit {
    /// Trend from the last STL fit (or `supsmu` when no period is usable).
    pub trend: Vec<f64>,
    /// One seasonal component per usable period, same order as `periods`.
    pub seasonals: Vec<Vec<f64>>,
    /// The usable periods actually fit, ascending, de-duplicated.
    pub periods: Vec<usize>,
    /// `deseasonalised - trend` (or `values - trend` when no period is usable).
    pub remainder: Vec<f64>,
}

/// Fit the R `forecast::mstl` iteration shared by `mstl_decompose` and
/// `stats::stl_strengths` (D-09).
///
/// `periods` is filtered to the "usable" subset (`p >= 2` with `n > 2*p` --
/// R's `mstl`/`msts` drops any period needing `n/2` or more points),
/// ascending, de-duplicated. With no usable period the trend is
/// `stats::supsmu(seq_len(n), x)` (`forecast::mstl`'s own non-seasonal
/// fallback) and there is no seasonal component. With `k >= 1` usable
/// periods, two passes are made over them (ascending order): each pass adds
/// back the period's own previous seasonal estimate, re-fits
/// `stl(.., s.window = 7 + 4*(position+1))`, and subtracts the new
/// estimate before moving to the next period. The trend is the LAST STL
/// fit's trend; the remainder is the fully-deseasonalised series minus that
/// trend. For `k == 1` the two passes are mathematically a no-op beyond the
/// first (the period is added back and immediately re-subtracted against
/// the identical input), so this reduces to exactly one `stl()` call --
/// matching `tests/stl_reference.rs`'s single-period fixture (generated via
/// R's own single `stl(x, s.window = 11)`, since `7 + 4*(0+1) == 11`).
pub fn mstl_fit(values: &[f64], periods: &[usize]) -> Result<MstlFit> {
    let n = values.len();

    let mut usable: Vec<usize> = periods
        .iter()
        .copied()
        .filter(|&p| p >= 2 && n > 2 * p)
        .collect();
    usable.sort_unstable();
    usable.dedup();

    if usable.is_empty() {
        let x_idx: Vec<f64> = (1..=n).map(|i| i as f64).collect();
        let trend = supsmu(&x_idx, values)?;
        let remainder: Vec<f64> = (0..n).map(|i| values[i] - trend[i]).collect();
        return Ok(MstlFit {
            trend,
            seasonals: vec![],
            periods: vec![],
            remainder,
        });
    }

    let k = usable.len();
    let mut seas: Vec<Vec<f64>> = vec![vec![0.0; n]; k];
    let mut deseas: Vec<f64> = values.to_vec();
    let mut last_trend: Vec<f64> = vec![0.0; n];

    for _pass in 0..2 {
        for (pos, &period) in usable.iter().enumerate() {
            for (d, s) in deseas.iter_mut().zip(seas[pos].iter()) {
                *d += *s;
            }
            let s_window = 7 + 4 * (pos + 1);
            let params = StlParams::r_default(period, s_window);
            let fit = stl(&deseas, &params)?;
            seas[pos] = fit.seasonal;
            last_trend = fit.trend;
            for (d, s) in deseas.iter_mut().zip(seas[pos].iter()) {
                *d -= *s;
            }
        }
    }

    let remainder: Vec<f64> = (0..n).map(|i| deseas[i] - last_trend[i]).collect();

    Ok(MstlFit {
        trend: last_trend,
        seasonals: seas,
        periods: usable,
        remainder,
    })
}

/// Perform MSTL (Multiple Seasonal-Trend decomposition using Loess)
/// decomposition, matching R `forecast::mstl` via the shared [`mstl_fit`]
/// (D-09).
///
/// # Arguments
/// * `values` - Time series values
/// * `periods` - Requested seasonal periods. An entry `< 1` is a caller
///   error (`InvalidParameter`); an entry `== 1` is ignored (non-seasonal);
///   entries `>= 2` are "requested" periods, of which [`mstl_fit`] uses only
///   the usable subset (`p >= 2`, `n > 2*p`).
/// * `insufficient_data_mode` - How to handle the case where periods were
///   requested (`>= 2`) but NONE is usable:
///   - `Fail`: Error on insufficient data (default)
///   - `Trend`: Apply trend-only (non-seasonal) decomposition
///   - `None`: Skip decomposition entirely, return empty result
///
/// When no period is requested (or every requested entry is `1`), or when
/// at least one requested period IS usable, `insufficient_data_mode` has no
/// effect: the result comes directly from [`mstl_fit`] (which silently
/// drops any individual unusable period, matching R's own `msts` warning
/// suppression).
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

    let n = values.len();

    let mut requested: Vec<usize> = Vec::new();
    for &p in periods {
        if p < 1 {
            return Err(ForecastError::InvalidParameter {
                param: "periods".to_string(),
                value: p.to_string(),
                reason: "period must be >= 1".to_string(),
            });
        }
        if p >= 2 {
            requested.push(p as usize);
        }
    }

    let any_usable = requested.iter().any(|&p| n > 2 * p);

    if !requested.is_empty() && !any_usable {
        let min_period = *requested.iter().min().expect("requested is non-empty");
        match insufficient_data_mode {
            InsufficientDataMode::Fail => {
                return Err(ForecastError::InsufficientData {
                    needed: 2 * min_period + 1,
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
                let fit = mstl_fit(values, &[])?;
                return Ok(MstlDecomposition {
                    trend: Some(fit.trend),
                    seasonal: vec![],
                    periods: vec![],
                    remainder: Some(fit.remainder),
                    decomposition_applied: true,
                });
            }
        }
    }

    let fit = mstl_fit(values, &requested)?;
    let out_periods: Vec<i32> = fit.periods.iter().map(|&p| p as i32).collect();

    Ok(MstlDecomposition {
        trend: Some(fit.trend),
        seasonal: fit.seasonals,
        periods: out_periods,
        remainder: Some(fit.remainder),
        decomposition_applied: true,
    })
}

/// Which family of classical decomposition (R `stats::decompose(type=)`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ClassicalKind {
    /// `data = trend + seasonal + remainder`
    Additive,
    /// `data = trend * seasonal * remainder`
    Multiplicative,
}

/// Result of classical (R `stats::decompose`) seasonal decomposition.
#[derive(Debug, Clone)]
pub struct ClassicalDecomposition {
    /// Centred moving-average trend. `NaN` at the `floor(period/2)` points
    /// at each end, exactly where R's filter returns `NA`.
    pub trend: Vec<f64>,
    /// Seasonal component, period `period`, repeated to length `n`.
    pub seasonal: Vec<f64>,
    /// Remainder (additive: `x - seasonal - trend`; multiplicative:
    /// `x / seasonal / trend`). `NaN` wherever `trend` is `NaN`.
    pub remainder: Vec<f64>,
    /// The centred per-phase seasonal figure (length `period`), before
    /// being repeated out to length `n`.
    pub figure: Vec<f64>,
}

/// Port of R `stats::decompose` (D-10): a centred moving-average trend (2xm
/// filter for an even period, an m-term mean for an odd period), a
/// per-phase seasonal figure averaged over the detrended series and
/// centred, and the remainder.
///
/// The even-period trend is computed as
/// `(0.5*x[i-h] + sum(x[i-h+1..=i+h-1]) + 0.5*x[i+h]) / m` (`h = m/2`) so
/// that a constant series gives a trend of EXACTLY the constant (R's own
/// weighted-sum evaluation order can differ in the last bit, G10/G11).
///
/// `period < 2` -> `InvalidParameter`; `n < 2*period` -> `InsufficientData`
/// (matching R's "fewer than 2*f non-NA values" error); any non-finite
/// value -> `InvalidInput`; `kind == Multiplicative` with any value `<= 0`
/// -> `InvalidParameter` (R itself would silently produce `Inf`/`NaN` --
/// this is a deliberate, documented deviation, see
/// `validation/reference/README.md`).
pub fn classical_decompose(
    values: &[f64],
    period: usize,
    kind: ClassicalKind,
) -> Result<ClassicalDecomposition> {
    if period < 2 {
        return Err(ForecastError::InvalidParameter {
            param: "period".to_string(),
            value: period.to_string(),
            reason: "period must be >= 2".to_string(),
        });
    }

    let n = values.len();
    if n < 2 * period {
        return Err(ForecastError::InsufficientData {
            needed: 2 * period,
            got: n,
        });
    }

    if values.iter().any(|v| !v.is_finite()) {
        return Err(ForecastError::InvalidInput(
            "classical_decompose: values must be finite".to_string(),
        ));
    }

    if kind == ClassicalKind::Multiplicative && values.iter().any(|&v| v <= 0.0) {
        return Err(ForecastError::InvalidParameter {
            param: "values".to_string(),
            value: "<= 0".to_string(),
            reason: "multiplicative decomposition requires strictly positive values".to_string(),
        });
    }

    let m = period;
    // Note: is_multiple_of() is unstable and breaks WASM builds.
    #[allow(clippy::manual_is_multiple_of)]
    let m_even = m % 2 == 0;
    let half = m / 2;

    let mut trend = vec![f64::NAN; n];
    if m_even {
        for i in half..(n - half) {
            let mut sum = 0.5 * values[i - half] + 0.5 * values[i + half];
            for &v in &values[(i - half + 1)..(i + half)] {
                sum += v;
            }
            trend[i] = sum / m as f64;
        }
    } else {
        for i in half..(n - half) {
            let sum: f64 = values[(i - half)..=(i + half)].iter().sum();
            trend[i] = sum / m as f64;
        }
    }

    let detrended: Vec<f64> = (0..n)
        .map(|i| {
            if trend[i].is_nan() {
                f64::NAN
            } else {
                match kind {
                    ClassicalKind::Additive => values[i] - trend[i],
                    ClassicalKind::Multiplicative => values[i] / trend[i],
                }
            }
        })
        .collect();

    let mut figure = vec![0.0f64; period];
    for (s, slot) in figure.iter_mut().enumerate() {
        let mut sum = 0.0;
        let mut count = 0usize;
        let mut idx = s;
        while idx < n {
            if !detrended[idx].is_nan() {
                sum += detrended[idx];
                count += 1;
            }
            idx += period;
        }
        *slot = if count > 0 {
            sum / count as f64
        } else {
            f64::NAN
        };
    }

    let figure_mean = figure.iter().sum::<f64>() / period as f64;
    let centered_figure: Vec<f64> = match kind {
        ClassicalKind::Additive => figure.iter().map(|f| f - figure_mean).collect(),
        ClassicalKind::Multiplicative => figure.iter().map(|f| f / figure_mean).collect(),
    };

    let seasonal: Vec<f64> = (0..n).map(|i| centered_figure[i % period]).collect();

    let remainder: Vec<f64> = (0..n)
        .map(|i| {
            if trend[i].is_nan() {
                f64::NAN
            } else {
                match kind {
                    ClassicalKind::Additive => values[i] - seasonal[i] - trend[i],
                    ClassicalKind::Multiplicative => values[i] / seasonal[i] / trend[i],
                }
            }
        })
        .collect();

    Ok(ClassicalDecomposition {
        trend,
        seasonal,
        remainder,
        figure: centered_figure,
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
        assert_eq!(result.periods, vec![12]);
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

    #[test]
    fn test_mstl_decompose_period_zero_is_invalid_parameter() {
        let values: Vec<f64> = (0..48).map(|i| i as f64).collect();
        let result = mstl_decompose(&values, &[0], InsufficientDataMode::Fail);
        assert!(matches!(
            result,
            Err(ForecastError::InvalidParameter { .. })
        ));
    }

    #[test]
    fn test_mstl_decompose_negative_period_is_invalid_parameter() {
        let values: Vec<f64> = (0..48).map(|i| i as f64).collect();
        let result = mstl_decompose(&values, &[-3], InsufficientDataMode::Fail);
        assert!(matches!(
            result,
            Err(ForecastError::InvalidParameter { .. })
        ));
    }

    #[test]
    fn test_mstl_decompose_period_one_is_non_seasonal() {
        let values: Vec<f64> = (0..48).map(|i| i as f64 * 0.3).collect();
        let result = mstl_decompose(&values, &[1], InsufficientDataMode::Fail).unwrap();
        assert!(result.decomposition_applied);
        assert!(result.seasonal.is_empty());
        assert!(result.periods.is_empty());
    }

    #[test]
    fn test_mstl_decompose_constant_series_trend_is_exact() {
        let values = vec![1.0; 48];
        let result = mstl_decompose(&values, &[12], InsufficientDataMode::Fail).unwrap();
        for (i, &t) in result.trend.unwrap().iter().enumerate() {
            assert!((t - 1.0).abs() < 1e-12, "trend[{i}] = {t}");
        }
    }

    #[test]
    fn test_mstl_fit_no_usable_period_tiny_n_does_not_panic() {
        for n in [1usize, 2, 3] {
            let values: Vec<f64> = (0..n).map(|i| i as f64).collect();
            // supsmu requires n >= 4; smaller n must return an Err, never panic.
            let result = mstl_fit(&values, &[12]);
            assert!(result.is_err(), "n={n}: expected Err, got Ok");
        }
    }

    #[test]
    fn test_mstl_decompose_tiny_n_does_not_panic() {
        for n in [1usize, 2, 3] {
            let values: Vec<f64> = (0..n).map(|i| i as f64).collect();
            // No periods requested -> straight to mstl_fit(values, &[]), which
            // hits supsmu's own n < 4 InsufficientData Err, not a panic.
            let result = mstl_decompose(&values, &[], InsufficientDataMode::Fail);
            assert!(result.is_err(), "n={n}: expected Err, got Ok");
        }
    }

    // ------------------------------------------------------------------
    // classical_decompose (D-10)
    // ------------------------------------------------------------------

    #[test]
    fn test_classical_decompose_constant_additive_exact() {
        let values = vec![1.0; 48];
        let result = classical_decompose(&values, 12, ClassicalKind::Additive).unwrap();
        for i in 0..6 {
            assert!(result.trend[i].is_nan(), "trend[{i}] should be NaN");
        }
        for i in 42..48 {
            assert!(result.trend[i].is_nan(), "trend[{i}] should be NaN");
        }
        for i in 6..42 {
            assert!(
                (result.trend[i] - 1.0).abs() < 1e-12,
                "trend[{i}] = {}",
                result.trend[i]
            );
        }
        for (i, &s) in result.seasonal.iter().enumerate() {
            assert!(s.abs() < 1e-12, "seasonal[{i}] = {s}");
        }
    }

    #[test]
    fn test_classical_decompose_even_period_matches_hand_filter() {
        let values: Vec<f64> = (0..48)
            .map(|i| 10.0 + (i as f64 * 0.37).sin() * 3.0 + 0.05 * i as f64)
            .collect();
        let result = classical_decompose(&values, 12, ClassicalKind::Additive).unwrap();
        let i = 20usize;
        let h = 6usize;
        let expected = (0.5 * values[i - h]
            + values[(i - h + 1)..(i + h)].iter().sum::<f64>()
            + 0.5 * values[i + h])
            / 12.0;
        assert!((result.trend[i] - expected).abs() < 1e-12);
    }

    #[test]
    fn test_classical_decompose_odd_period_is_m_term_mean() {
        let values: Vec<f64> = (0..84)
            .map(|i| 10.0 + (i as f64 * 0.21).cos() * 2.0)
            .collect();
        let result = classical_decompose(&values, 7, ClassicalKind::Additive).unwrap();
        let i = 40usize;
        let h = 3usize;
        let expected = values[(i - h)..=(i + h)].iter().sum::<f64>() / 7.0;
        assert!((result.trend[i] - expected).abs() < 1e-12);
    }

    #[test]
    fn test_classical_decompose_period_one_is_invalid_parameter() {
        let values: Vec<f64> = (0..48).map(|i| i as f64).collect();
        let result = classical_decompose(&values, 1, ClassicalKind::Additive);
        assert!(matches!(
            result,
            Err(ForecastError::InvalidParameter { .. })
        ));
    }

    #[test]
    fn test_classical_decompose_insufficient_data() {
        let values: Vec<f64> = (0..10).map(|i| i as f64).collect();
        let result = classical_decompose(&values, 12, ClassicalKind::Additive);
        assert!(matches!(
            result,
            Err(ForecastError::InsufficientData { .. })
        ));
    }

    #[test]
    fn test_classical_decompose_non_finite_value_is_invalid_input() {
        let mut values: Vec<f64> = (0..48).map(|i| i as f64).collect();
        values[10] = f64::NAN;
        let result = classical_decompose(&values, 12, ClassicalKind::Additive);
        assert!(matches!(result, Err(ForecastError::InvalidInput(_))));
    }

    #[test]
    fn test_classical_decompose_multiplicative_nonpositive_is_invalid_parameter() {
        let mut values: Vec<f64> = (1..=48).map(|i| i as f64).collect();
        values[5] = 0.0;
        let result = classical_decompose(&values, 12, ClassicalKind::Multiplicative);
        assert!(matches!(
            result,
            Err(ForecastError::InvalidParameter { .. })
        ));
    }

    #[test]
    fn test_classical_decompose_additive_reconstructs_input() {
        let n = 84;
        let values: Vec<f64> = (0..n)
            .map(|i| 10.0 + 0.1 * i as f64 + 3.0 * (2.0 * PI * i as f64 / 7.0).sin())
            .collect();
        let result = classical_decompose(&values, 7, ClassicalKind::Additive).unwrap();
        for (i, (((&t, &s), &r), &want)) in result
            .trend
            .iter()
            .zip(result.seasonal.iter())
            .zip(result.remainder.iter())
            .zip(values.iter())
            .enumerate()
        {
            if t.is_nan() {
                assert!(r.is_nan());
                continue;
            }
            let recon = t + s + r;
            assert!(
                (recon - want).abs() < 1e-9,
                "index {i}: recon={recon}, want={want}"
            );
        }
    }

    #[test]
    fn test_classical_decompose_multiplicative_reconstructs_input() {
        let n = 84;
        let values: Vec<f64> = (0..n)
            .map(|i| {
                let trend = 10.0 + 0.1 * i as f64;
                let seasonal = 1.0 + 0.3 * (2.0 * PI * i as f64 / 7.0).sin();
                trend * seasonal
            })
            .collect();
        let result = classical_decompose(&values, 7, ClassicalKind::Multiplicative).unwrap();
        for (i, (((&t, &s), &r), &want)) in result
            .trend
            .iter()
            .zip(result.seasonal.iter())
            .zip(result.remainder.iter())
            .zip(values.iter())
            .enumerate()
        {
            if t.is_nan() {
                assert!(r.is_nan());
                continue;
            }
            let recon = t * s * r;
            assert!(
                (recon - want).abs() < 1e-6,
                "index {i}: recon={recon}, want={want}"
            );
        }
    }
}
