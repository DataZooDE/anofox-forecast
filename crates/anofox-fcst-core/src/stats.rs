//! Time series statistics computation.
//!
//! Provides ts_stats functionality that computes 24 metrics per series.

use crate::calendar::CalendarStep;
use crate::error::Result;
use crate::gaps::{gap_counts, resolve_gap_step};
use crate::missing::trim_edges_reject_interior;
use crate::seasonality::detect_seasonality;
use crate::stl::{stl, StlParams};
use crate::supsmu::supsmu;

/// Frequency type for calendar vs fixed frequencies.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum FrequencyType {
    /// Fixed duration frequencies (daily, hourly, weekly, etc.)
    #[default]
    Fixed,
    /// Monthly frequency - variable duration (28-31 days)
    Monthly,
    /// Quarterly frequency - variable duration (3 months)
    Quarterly,
    /// Yearly frequency - variable duration (365 or 366 days)
    Yearly,
}

/// Time series statistics result containing 34 metrics.
#[derive(Debug, Clone, Default)]
pub struct TsStats {
    /// Total number of observations
    pub length: usize,
    /// Number of NULL values
    pub n_nulls: usize,
    /// Number of NaN values (distinct from NULL)
    pub n_nan: usize,
    /// Number of zero values
    pub n_zeros: usize,
    /// Number of positive values
    pub n_positive: usize,
    /// Number of negative values
    pub n_negative: usize,
    /// Count of distinct values
    pub n_unique_values: usize,
    /// Whether series has only one unique value
    pub is_constant: bool,
    /// Count of leading zeros
    pub n_zeros_start: usize,
    /// Count of trailing zeros
    pub n_zeros_end: usize,
    /// Longest run of constant values
    pub plateau_size: usize,
    /// Longest run of constant non-zero values
    pub plateau_size_nonzero: usize,
    /// Arithmetic mean
    pub mean: f64,
    /// Median (50th percentile)
    pub median: f64,
    /// Standard deviation
    pub std_dev: f64,
    /// Variance
    pub variance: f64,
    /// Minimum value
    pub min: f64,
    /// Maximum value
    pub max: f64,
    /// Range (max - min)
    pub range: f64,
    /// Sum of all values
    pub sum: f64,
    /// Skewness: Fisher's bias-corrected G1, equal to `scipy.stats.skew(x, bias=False)`.
    /// Computed from the population 2nd/3rd central moments (both divided by n, not n-1),
    /// then bias-corrected: `g1 = m3/m2^1.5`, `G1 = g1 * sqrt(n(n-1)) / (n-2)` (n > 2).
    pub skewness: f64,
    /// Excess kurtosis: Fisher's bias-corrected G2, equal to
    /// `scipy.stats.kurtosis(x, fisher=True, bias=False)`. Computed from the population
    /// 2nd/4th central moments: `g2 = m4/m2^2 - 3`, `G2 = ((n+1)*g2 + 6)*(n-1) / ((n-2)(n-3))`
    /// (n > 3).
    pub kurtosis: f64,
    /// Tail index (Hill estimator, α > 0 indicates heavy tail)
    pub tail_index: f64,
    /// Bimodality coefficient, SAS small-sample formula from the G1/G2 above:
    /// `BC = (G1^2 + 1) / (G2 + 3*(n-1)^2 / ((n-2)(n-3)))` (n > 3). `BC > 0.555` (the
    /// uniform-distribution value) suggests bimodality or multimodality.
    pub bimodality_coef: f64,
    /// Trimmed mean (10% trimmed from each tail)
    pub trimmed_mean: f64,
    /// Coefficient of variation (std_dev / mean)
    pub coef_variation: f64,
    /// First quartile (25th percentile)
    pub q1: f64,
    /// Third quartile (75th percentile)
    pub q3: f64,
    /// Interquartile range (q3 - q1)
    pub iqr: f64,
    /// Autocorrelation at lag 1
    pub autocorr_lag1: f64,
    /// Trend strength (D-02, STAT-02): `F_T = max(0, 1 - Var(R)/Var(T+R))`
    /// from an R-faithful STL decomposition (`stl::stl` for a single usable
    /// period, the `forecast::mstl` iteration for several, `supsmu::supsmu`
    /// trend when no period is usable). `None` when the series is
    /// interior-missing (entropy/strength share the trim rule) or all
    /// values are missing.
    pub trend_strength: Option<f64>,
    /// Seasonality strength (D-02, STAT-02): `F_S = max(0, 1 - Var(R)/Var(S+R))`,
    /// the maximum over every usable period when several are resolved.
    /// `None` when no period is usable (none detected, `seasonal_period =
    /// 1`, or `n <= 2*period`), when the per-period variance ratio is `0/0`
    /// (e.g. a constant series), or when the series is interior-missing.
    pub seasonality_strength: Option<f64>,
    /// Pincus (1991) approximate entropy, m=2, r=0.2*sample std dev, Chebyshev distance,
    /// self-match included (D-03). `None` when fewer than 3 present values remain after
    /// trimming leading/trailing missing values, or when an interior value is missing
    /// (NaN counts as missing). A constant series gives `Some(0.0)`.
    pub entropy: Option<f64>,
    /// Stability measure
    pub stability: f64,
    /// Expected number of observations based on date range and frequency (None if no dates provided)
    pub expected_length: Option<usize>,
    /// Number of missing periods (expected timestamps absent under the declared or
    /// detected frequency) (D-04). `None` if no dates provided. See also
    /// [`TsStats::n_gap_runs`] for the count of gap intervals (the pre-v0.10.0 meaning).
    pub n_gaps: Option<usize>,
    /// Number of gap runs/intervals (consecutive-timestamp transitions whose step count
    /// is `>= 2`) — the pre-v0.10.0 meaning of `n_gaps` (D-04). `None` if no dates
    /// provided.
    pub n_gap_runs: Option<usize>,
}

/// Compute time series statistics for a series with potential NULL values.
///
/// # Arguments
/// * `series` - A slice of optional f64 values (None represents NULL)
/// * `seasonal_period` - Period resolution for `trend_strength` /
///   `seasonality_strength` (D-02): `0` auto-detects via
///   [`crate::seasonality::detect_seasonality`] (no usable period if it
///   errors or returns empty); `1` means "non-seasonal" (no period at all);
///   `>= 2` uses that period explicitly.
///
/// # Returns
/// * `Result<TsStats>` - Statistics for the series
pub fn compute_ts_stats(series: &[Option<f64>], seasonal_period: usize) -> Result<TsStats> {
    let length = series.len();

    if length == 0 {
        return Ok(TsStats::default());
    }

    // Count NULLs, NaNs and extract valid (non-NULL, non-NaN) values
    let mut n_nulls = 0;
    let mut n_nan = 0;
    let mut values: Vec<f64> = Vec::with_capacity(length);

    for val in series {
        match val {
            Some(v) if v.is_nan() => n_nan += 1,
            Some(v) => values.push(*v),
            None => n_nulls += 1,
        }
    }

    let n_valid = values.len();

    if n_valid == 0 {
        return Ok(TsStats {
            length,
            n_nulls,
            n_nan,
            ..Default::default()
        });
    }

    // Count zeros, positives, negatives
    let n_zeros = values.iter().filter(|&&v| v == 0.0).count();
    let n_positive = values.iter().filter(|&&v| v > 0.0).count();
    let n_negative = values.iter().filter(|&&v| v < 0.0).count();

    // Count unique values (using bit representation for exact f64 comparison)
    let mut unique_bits: std::collections::HashSet<u64> = std::collections::HashSet::new();
    for &v in &values {
        unique_bits.insert(v.to_bits());
    }
    let n_unique_values = unique_bits.len();
    let is_constant = n_unique_values == 1;

    // Count leading zeros (from original series, including NULLs as breaks)
    let n_zeros_start = count_leading_zeros(series);
    let n_zeros_end = count_trailing_zeros(series);

    // Compute plateau sizes
    let plateau_size = compute_plateau_size(&values);
    let plateau_size_nonzero = compute_plateau_size_nonzero(&values);

    // Basic statistics
    let sum: f64 = values.iter().sum();
    let mean = sum / n_valid as f64;

    let min = values.iter().cloned().fold(f64::INFINITY, f64::min);
    let max = values.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
    let range = max - min;

    // Variance and standard deviation
    let variance = if n_valid > 1 {
        values.iter().map(|v| (v - mean).powi(2)).sum::<f64>() / (n_valid - 1) as f64
    } else {
        0.0
    };
    let std_dev = variance.sqrt();

    // Coefficient of variation
    let coef_variation = if mean.abs() > f64::EPSILON {
        std_dev / mean.abs()
    } else {
        f64::NAN
    };

    // Median and quartiles
    let mut sorted = values.clone();
    sorted.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));

    let median = percentile(&sorted, 0.5);
    let q1 = percentile(&sorted, 0.25);
    let q3 = percentile(&sorted, 0.75);
    let iqr = q3 - q1;

    // Moments (D-01, STAT-01): population central moments m2/m3/m4 (divided by n, NOT
    // n-1), matching scipy's internal computation before its own bias correction.
    let n = n_valid as f64;
    let m2 = values.iter().map(|v| (v - mean).powi(2)).sum::<f64>() / n;

    // Skewness: scipy.stats.skew(x, bias=False) — Fisher's bias-corrected G1.
    // g1 = m3 / m2^1.5; G1 = g1 * sqrt(n(n-1)) / (n-2)
    let skewness = if n_valid > 2 && m2 > f64::EPSILON {
        let m3 = values.iter().map(|v| (v - mean).powi(3)).sum::<f64>() / n;
        let g1 = m3 / m2.powf(1.5);
        g1 * (n * (n - 1.0)).sqrt() / (n - 2.0)
    } else {
        f64::NAN
    };

    // Excess kurtosis: scipy.stats.kurtosis(x, fisher=True, bias=False) — Fisher's
    // bias-corrected G2. g2 = m4/m2^2 - 3; G2 = ((n+1)*g2 + 6)*(n-1) / ((n-2)(n-3))
    let kurtosis = if n_valid > 3 && m2 > f64::EPSILON {
        let m4 = values.iter().map(|v| (v - mean).powi(4)).sum::<f64>() / n;
        let g2 = m4 / m2.powi(2) - 3.0;
        ((n + 1.0) * g2 + 6.0) * (n - 1.0) / ((n - 2.0) * (n - 3.0))
    } else {
        f64::NAN
    };

    // Tail index (Hill estimator on absolute values)
    let tail_index = compute_hill_estimator(&values);

    // Bimodality coefficient: SAS small-sample formula using the G1/G2 above.
    // BC = (G1^2 + 1) / (G2 + 3*(n-1)^2 / ((n-2)(n-3)))
    // BC > 0.555 suggests bimodality (threshold for uniform distribution)
    let bimodality_coef = if n_valid > 3 && kurtosis.is_finite() && skewness.is_finite() {
        let small_sample_term = 3.0 * (n - 1.0).powi(2) / ((n - 2.0) * (n - 3.0));
        (skewness.powi(2) + 1.0) / (kurtosis + small_sample_term)
    } else {
        f64::NAN
    };

    // Trimmed mean (10% trimmed from each tail)
    let trimmed_mean = compute_trimmed_mean(&sorted, 0.1);

    // Autocorrelation at lag 1
    let autocorr_lag1 = compute_autocorrelation(&values, 1);

    // Trend and seasonality strength (D-02, STAT-02): STL-based F_T/F_S.
    // `None`/`None` when the series is interior-missing (same trim rule as
    // entropy) or entirely missing; otherwise resolved via `seasonal_period`
    // and delegated to `stl_strengths`.
    let (trend_strength, seasonality_strength) = match trim_edges_reject_interior(series) {
        Ok(Some(range)) => {
            let trimmed: Vec<f64> = series[range]
                .iter()
                .map(|v| v.expect("trim_edges_reject_interior guarantees Some in range"))
                .collect();
            let periods: Vec<usize> = match seasonal_period {
                0 => detect_seasonality(&trimmed, None)
                    .ok()
                    .map(|p| {
                        p.into_iter()
                            .filter(|&x| x >= 2)
                            .map(|x| x as usize)
                            .collect()
                    })
                    .unwrap_or_default(),
                1 => vec![],
                p => vec![p],
            };
            match stl_strengths(&trimmed, &periods) {
                Ok(s) => (s.trend_strength, s.seasonality_strength),
                Err(_) => (None, None),
            }
        }
        _ => (None, None),
    };

    // Entropy (Pincus approximate entropy, D-03) — computed on the original Option
    // series (NaN counts as missing), trimming leading/trailing missing first.
    let entropy = compute_entropy_pincus(series);

    // Stability (inverse of coefficient of variation of rolling means)
    let stability = compute_stability(&values);

    Ok(TsStats {
        length,
        n_nulls,
        n_nan,
        n_zeros,
        n_positive,
        n_negative,
        n_unique_values,
        is_constant,
        n_zeros_start,
        n_zeros_end,
        plateau_size,
        plateau_size_nonzero,
        mean,
        median,
        std_dev,
        variance,
        min,
        max,
        range,
        sum,
        skewness,
        kurtosis,
        tail_index,
        bimodality_coef,
        trimmed_mean,
        coef_variation,
        q1,
        q3,
        iqr,
        autocorr_lag1,
        trend_strength,
        seasonality_strength,
        entropy,
        stability,
        expected_length: None,
        n_gaps: None,
        n_gap_runs: None,
    })
}

/// Compute time series statistics with date information and frequency type for gap detection.
///
/// This version properly handles calendar frequencies (monthly, quarterly, yearly) which
/// have variable durations that cannot be accurately represented in microseconds.
///
/// # Arguments
/// * `series` - A slice of optional f64 values (None represents NULL)
/// * `dates` - A slice of timestamps in microseconds (must be same length as series)
/// * `frequency_micros` - Expected frequency between observations in microseconds (used for Fixed type)
/// * `frequency_type` - The type of frequency (Fixed, Monthly, Quarterly, Yearly)
/// * `seasonal_period` - See [`compute_ts_stats`]'s `seasonal_period` argument (D-02).
///
/// # Returns
/// * `Result<TsStats>` - Statistics for the series including expected_length and n_gaps
pub fn compute_ts_stats_with_dates_and_type(
    series: &[Option<f64>],
    dates: &[i64],
    frequency_micros: i64,
    frequency_type: FrequencyType,
    seasonal_period: usize,
) -> Result<TsStats> {
    // First compute base stats
    let mut stats = compute_ts_stats(series, seasonal_period)?;

    // Then compute date-based metrics if we have valid dates (D-04, STAT-03): n_gaps
    // becomes the number of missing periods, n_gap_runs keeps the pre-v0.10.0 meaning
    // (number of gap intervals), via the shared gaps::gap_counts helper.
    if !dates.is_empty() {
        let mut distinct_dates = dates.to_vec();
        distinct_dates.sort_unstable();
        distinct_dates.dedup();

        if distinct_dates.len() < 2 {
            // A single distinct date (or none) needs no step: gap_counts short-circuits
            // before the step is ever consulted. The placeholder step is unused.
            let counts = gap_counts(dates, CalendarStep::Fixed(1));
            stats.expected_length = Some(counts.expected_length);
            stats.n_gaps = Some(counts.n_missing_periods);
            stats.n_gap_runs = Some(counts.n_gap_runs);
        } else if let Some(step) = resolve_gap_step(dates, frequency_micros, frequency_type)? {
            let counts = gap_counts(dates, step);
            stats.expected_length = Some(counts.expected_length);
            stats.n_gaps = Some(counts.n_missing_periods);
            stats.n_gap_runs = Some(counts.n_gap_runs);
        }
    }

    Ok(stats)
}

/// Compute percentile using linear interpolation.
fn percentile(sorted: &[f64], p: f64) -> f64 {
    if sorted.is_empty() {
        return f64::NAN;
    }
    if sorted.len() == 1 {
        return sorted[0];
    }

    let n = sorted.len() as f64;
    let idx = p * (n - 1.0);
    let lower = idx.floor() as usize;
    let upper = idx.ceil() as usize;
    let frac = idx - lower as f64;

    if upper >= sorted.len() {
        sorted[sorted.len() - 1]
    } else {
        sorted[lower] * (1.0 - frac) + sorted[upper] * frac
    }
}

/// Compute autocorrelation at a given lag.
fn compute_autocorrelation(values: &[f64], lag: usize) -> f64 {
    if values.len() <= lag {
        return f64::NAN;
    }

    let n = values.len();
    let mean: f64 = values.iter().sum::<f64>() / n as f64;

    let mut numerator = 0.0;
    let mut denominator = 0.0;

    for (i, &v) in values.iter().enumerate() {
        denominator += (v - mean).powi(2);
        if i >= lag {
            numerator += (v - mean) * (values[i - lag] - mean);
        }
    }

    if denominator.abs() < f64::EPSILON {
        0.0
    } else {
        numerator / denominator
    }
}

/// Trend/seasonality strength result (D-02, STAT-02); see
/// [`TsStats::trend_strength`] / [`TsStats::seasonality_strength`] for the
/// full contract.
#[derive(Debug, Clone, Copy, Default)]
pub struct StlStrengths {
    pub trend_strength: Option<f64>,
    pub seasonality_strength: Option<f64>,
}

/// Sample variance (`ddof = 1`), matching R's `var()`. `NaN` for `n < 2`
/// (never reached by the callers below, which always pass `n == values.len()
/// >= 1` complete series, but kept total for safety).
fn sample_var(x: &[f64]) -> f64 {
    let n = x.len();
    if n < 2 {
        return f64::NAN;
    }
    let mean = x.iter().sum::<f64>() / n as f64;
    x.iter().map(|v| (v - mean).powi(2)).sum::<f64>() / (n - 1) as f64
}

/// `tsfeatures::stl_features`'s trend-strength rule: `varx < eps -> 0`;
/// `vardeseason/varx < 1e-10 -> 0`; else `max(0, min(1, 1 -
/// vare/vardeseason))`. `deseason = trend + remainder` (the series with
/// every seasonal component removed; an exact decomposition identity).
fn trend_strength_rule(x: &[f64], deseason: &[f64], remainder: &[f64]) -> f64 {
    let varx = sample_var(x);
    if varx < f64::EPSILON {
        return 0.0;
    }
    let vardeseason = sample_var(deseason);
    if vardeseason / varx < 1e-10 {
        return 0.0;
    }
    let vare = sample_var(remainder);
    (1.0 - vare / vardeseason).clamp(0.0, 1.0)
}

/// `tsfeatures::stl_features`'s per-period seasonal-strength rule: `max(0,
/// min(1, 1 - vare/var(remainder + seasonal_i)))`. `None` for R's `NaN`
/// (both variances exactly zero, e.g. a constant series or constant
/// seasonal component).
fn seasonal_strength_rule(remainder: &[f64], seasonal: &[f64]) -> Option<f64> {
    let n = remainder.len();
    let remainder_plus: Vec<f64> = (0..n).map(|i| remainder[i] + seasonal[i]).collect();
    let var_rp = sample_var(&remainder_plus);
    let vare = sample_var(remainder);
    // R's `vare/var_rp` is exactly `NaN` only when both are exactly 0, but a
    // genuinely constant (or near-constant) input series decomposes into
    // trend/seasonal/remainder components that carry pure floating-point
    // rounding noise at the ~1e-30 scale (confirmed against R's own
    // `stats::stl`/`tsfeatures::stl_features` on `rep(5, 48)`, which itself
    // divides two ~1e-30 noise variances and happens to land on 0.056 — not
    // reproducible bit-for-bit across STL ports or platforms). Treat both
    // variances as "exactly zero" below `f64::EPSILON` so a degenerate input
    // deterministically reports `None` rather than chasing non-portable
    // rounding noise (D-02).
    if var_rp.abs() < f64::EPSILON && vare.abs() < f64::EPSILON {
        return None;
    }
    Some((1.0 - vare / var_rp).clamp(0.0, 1.0))
}

/// STL-based trend/seasonal strength (D-02, STAT-02), matching
/// `tsfeatures::stl_features` (built on `forecast::mstl` -> `stats::stl`):
/// `F_T = max(0, 1 - Var(R)/Var(T+R))`, `F_S = max(0, 1 - Var(R)/Var(S+R))`.
///
/// `periods` is filtered to `p >= 2` with `n > 2*p`, sorted ascending, and
/// de-duplicated. No usable period left -> the trend comes from
/// [`crate::supsmu::supsmu`] (R's `stats::supsmu(seq_len(n), x)`, the same
/// fallback `forecast::mstl` uses) and `seasonality_strength` is `None`.
/// Exactly one usable period -> a single [`crate::stl::stl`] call with
/// `s.window = 11` (R's `stats::stl` default). Two or more -> the
/// `forecast::mstl` iteration (2 passes, `s.window = 7 + 4*(position+1)` by
/// ascending period position, trend from the last `stl` call); seasonality
/// is the maximum `F_S_i` over every period (not exposed as separate
/// columns — "strongest seasonal component").
pub fn stl_strengths(values: &[f64], periods: &[usize]) -> Result<StlStrengths> {
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
        // deseason = trend + remainder = values exactly (no seasonal component to
        // remove); pass `values` directly rather than re-deriving it.
        let trend_strength = trend_strength_rule(values, values, &remainder);
        return Ok(StlStrengths {
            trend_strength: Some(trend_strength),
            seasonality_strength: None,
        });
    }

    if usable.len() == 1 {
        let period = usable[0];
        let params = StlParams::r_default(period, 11);
        let fit = stl(values, &params)?;
        let deseason: Vec<f64> = (0..n).map(|i| fit.trend[i] + fit.remainder[i]).collect();
        let trend_strength = trend_strength_rule(values, &deseason, &fit.remainder);
        let seasonality_strength = seasonal_strength_rule(&fit.remainder, &fit.seasonal);
        return Ok(StlStrengths {
            trend_strength: Some(trend_strength),
            seasonality_strength,
        });
    }

    // forecast::mstl iteration: 2 passes over the ascending periods, each
    // adding back its own previous seasonal estimate before re-fitting.
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
    let deseason_total: Vec<f64> = (0..n).map(|i| last_trend[i] + remainder[i]).collect();
    let trend_strength = trend_strength_rule(values, &deseason_total, &remainder);

    let mut best_fs: Option<f64> = None;
    for seasonal in &seas {
        if let Some(fs) = seasonal_strength_rule(&remainder, seasonal) {
            best_fs = Some(best_fs.map_or(fs, |b| b.max(fs)));
        }
    }

    Ok(StlStrengths {
        trend_strength: Some(trend_strength),
        seasonality_strength: best_fs,
    })
}

/// Compute Pincus (1991) approximate entropy for the original (possibly-missing)
/// series (D-03): m=2, r=0.2*sample std dev of the trimmed values, Chebyshev
/// distance, self-match included (matching R `pracma::approx_entropy(x, edim = 2, r =
/// 0.2*sd(x), elag = 1)`).
///
/// Leading/trailing missing values (`None` or `Some(NaN)`) are trimmed first; an
/// interior missing value, or fewer than 3 present values after trimming, gives
/// `None`. A constant series gives `Some(0.0)`.
fn compute_entropy_pincus(series: &[Option<f64>]) -> Option<f64> {
    let range = match trim_edges_reject_interior(series) {
        Ok(Some(r)) => r,
        Ok(None) => return None,
        Err(_) => return None, // interior missing value
    };

    let x: Vec<f64> = series[range]
        .iter()
        .map(|v| v.expect("trim_edges_reject_interior guarantees Some in range"))
        .collect();

    if x.len() < 3 {
        return None;
    }

    Some(approximate_entropy(&x, 2))
}

/// Pincus approximate entropy: `ApEn = Phi(m) - Phi(m+1)`, `r = 0.2 * sample std dev`.
///
/// Uses a sorted-band search (T-13-01): template start indices are sorted by their
/// first coordinate, so each template's match count is found by binary-searching the
/// `[x_i - r, x_i + r]` window on that coordinate before checking the remaining `m-1`
/// (or `m`) coordinates via Chebyshev distance — avoiding the naive all-pairs O(n^2)
/// loop for long series while producing identical counts.
fn approximate_entropy(x: &[f64], m: usize) -> f64 {
    let n = x.len() as f64;
    let mean = x.iter().sum::<f64>() / n;
    let sample_var = x.iter().map(|v| (v - mean).powi(2)).sum::<f64>() / (n - 1.0);
    let r = 0.2 * sample_var.sqrt();

    phi(x, m, r) - phi(x, m + 1, r)
}

/// `Phi(m) = mean_i(ln(C_i / N_m))` where `C_i` counts templates `j` (self included)
/// with Chebyshev distance `<= r` from template `i`, over the `N_m = n - m + 1`
/// length-`m` templates starting at each index.
fn phi(x: &[f64], m: usize, r: f64) -> f64 {
    let n_m = x.len() - m + 1;

    // Sort template-start indices by their first coordinate for the band search.
    let mut order: Vec<usize> = (0..n_m).collect();
    order.sort_by(|&a, &b| x[a].partial_cmp(&x[b]).unwrap());
    let sorted_first: Vec<f64> = order.iter().map(|&i| x[i]).collect();

    let mut sum_log = 0.0;
    for i in 0..n_m {
        let xi = x[i];
        let lo = sorted_first.partition_point(|&v| v < xi - r);
        let hi = sorted_first.partition_point(|&v| v <= xi + r);

        let mut count = 0usize;
        for &j in &order[lo..hi] {
            let mut max_diff = 0.0f64;
            for d in 0..m {
                let diff = (x[i + d] - x[j + d]).abs();
                if diff > max_diff {
                    max_diff = diff;
                }
            }
            if max_diff <= r {
                count += 1;
            }
        }

        let c_i = count as f64 / n_m as f64;
        sum_log += c_i.ln();
    }

    sum_log / n_m as f64
}

/// Compute stability as inverse of coefficient of variation of rolling means.
fn compute_stability(values: &[f64]) -> f64 {
    if values.len() < 10 {
        return f64::NAN;
    }

    let window = (values.len() / 5).max(3);
    let mut rolling_means = Vec::new();

    for i in 0..=(values.len() - window) {
        let mean: f64 = values[i..i + window].iter().sum::<f64>() / window as f64;
        rolling_means.push(mean);
    }

    if rolling_means.is_empty() {
        return f64::NAN;
    }

    let rm_mean: f64 = rolling_means.iter().sum::<f64>() / rolling_means.len() as f64;
    let rm_std = (rolling_means
        .iter()
        .map(|v| (v - rm_mean).powi(2))
        .sum::<f64>()
        / rolling_means.len() as f64)
        .sqrt();

    if rm_mean.abs() > f64::EPSILON {
        1.0 / (rm_std / rm_mean.abs() + 0.01) // Add small constant to avoid division by zero
    } else {
        f64::NAN
    }
}

/// Count leading zeros in a series (stops at first non-zero or NULL).
fn count_leading_zeros(series: &[Option<f64>]) -> usize {
    let mut count = 0;
    for val in series {
        match val {
            Some(v) if *v == 0.0 => count += 1,
            _ => break,
        }
    }
    count
}

/// Count trailing zeros in a series (stops at first non-zero or NULL from end).
fn count_trailing_zeros(series: &[Option<f64>]) -> usize {
    let mut count = 0;
    for val in series.iter().rev() {
        match val {
            Some(v) if *v == 0.0 => count += 1,
            _ => break,
        }
    }
    count
}

/// Compute the longest run of constant values.
fn compute_plateau_size(values: &[f64]) -> usize {
    if values.is_empty() {
        return 0;
    }

    let mut max_run = 1;
    let mut current_run = 1;

    for i in 1..values.len() {
        if values[i].to_bits() == values[i - 1].to_bits() {
            current_run += 1;
            max_run = max_run.max(current_run);
        } else {
            current_run = 1;
        }
    }

    max_run
}

/// Compute the longest run of constant non-zero values.
fn compute_plateau_size_nonzero(values: &[f64]) -> usize {
    if values.is_empty() {
        return 0;
    }

    let mut max_run = 0;
    let mut current_run = 0;
    let mut prev_nonzero: Option<u64> = None;

    for &v in values {
        if v == 0.0 {
            // Zero breaks the run
            max_run = max_run.max(current_run);
            current_run = 0;
            prev_nonzero = None;
        } else {
            let bits = v.to_bits();
            match prev_nonzero {
                Some(prev) if prev == bits => {
                    current_run += 1;
                }
                _ => {
                    max_run = max_run.max(current_run);
                    current_run = 1;
                }
            }
            prev_nonzero = Some(bits);
        }
    }

    max_run.max(current_run)
}

/// Compute the Hill estimator for tail index.
///
/// The Hill estimator estimates the tail index α of a heavy-tailed distribution.
/// For a Pareto-like tail P(X > x) ~ x^(-α), smaller α indicates heavier tails.
///
/// Uses the top k = sqrt(n) order statistics by default, which is a common choice.
/// Applied to absolute values to capture both tails symmetrically.
fn compute_hill_estimator(values: &[f64]) -> f64 {
    let n = values.len();
    if n < 10 {
        return f64::NAN;
    }

    // Use absolute values to capture both tails
    let mut abs_values: Vec<f64> = values.iter().map(|v| v.abs()).collect();

    // Filter out zeros (can't take log of zero)
    abs_values.retain(|&v| v > f64::EPSILON);

    if abs_values.len() < 10 {
        return f64::NAN;
    }

    // Sort in descending order
    abs_values.sort_by(|a, b| b.partial_cmp(a).unwrap_or(std::cmp::Ordering::Equal));

    // Use k = sqrt(n) order statistics (common heuristic)
    let k = (abs_values.len() as f64).sqrt().floor() as usize;
    let k = k.max(2).min(abs_values.len() - 1);

    // Hill estimator: H = (1/k) * sum_{i=1}^{k} log(X_{(i)} / X_{(k+1)})
    // where X_{(1)} >= X_{(2)} >= ... are order statistics
    let threshold = abs_values[k];

    if threshold <= f64::EPSILON {
        return f64::NAN;
    }

    let mut sum_log = 0.0;
    for val in abs_values.iter().take(k) {
        sum_log += (val / threshold).ln();
    }

    let hill_h = sum_log / k as f64;

    if hill_h <= f64::EPSILON {
        f64::NAN
    } else {
        // Return the tail index α = 1/H
        1.0 / hill_h
    }
}

/// Compute trimmed mean by removing a proportion from each tail.
///
/// # Arguments
/// * `sorted` - Already sorted slice of values
/// * `trim_proportion` - Proportion to trim from each tail (e.g., 0.1 for 10%)
fn compute_trimmed_mean(sorted: &[f64], trim_proportion: f64) -> f64 {
    let n = sorted.len();
    if n == 0 {
        return f64::NAN;
    }

    // Number of values to trim from each end
    let trim_count = (n as f64 * trim_proportion).floor() as usize;

    // Ensure we have at least one value left
    if 2 * trim_count >= n {
        // Not enough values to trim, return regular mean
        return sorted.iter().sum::<f64>() / n as f64;
    }

    let trimmed_slice = &sorted[trim_count..n - trim_count];
    trimmed_slice.iter().sum::<f64>() / trimmed_slice.len() as f64
}

#[cfg(test)]
mod tests {
    use super::*;
    use approx::assert_relative_eq;

    #[test]
    fn test_basic_stats() {
        let series: Vec<Option<f64>> = vec![Some(1.0), Some(2.0), Some(3.0), Some(4.0), Some(5.0)];
        let stats = compute_ts_stats(&series, 0).unwrap();

        assert_eq!(stats.length, 5);
        assert_eq!(stats.n_nulls, 0);
        assert_relative_eq!(stats.mean, 3.0, epsilon = 1e-10);
        assert_relative_eq!(stats.median, 3.0, epsilon = 1e-10);
        assert_relative_eq!(stats.min, 1.0, epsilon = 1e-10);
        assert_relative_eq!(stats.max, 5.0, epsilon = 1e-10);
        assert_relative_eq!(stats.sum, 15.0, epsilon = 1e-10);
    }

    #[test]
    fn test_with_nulls() {
        let series: Vec<Option<f64>> = vec![Some(1.0), None, Some(3.0), None, Some(5.0)];
        let stats = compute_ts_stats(&series, 0).unwrap();

        assert_eq!(stats.length, 5);
        assert_eq!(stats.n_nulls, 2);
        assert_relative_eq!(stats.mean, 3.0, epsilon = 1e-10);
    }

    #[test]
    fn test_empty_series() {
        let series: Vec<Option<f64>> = vec![];
        let stats = compute_ts_stats(&series, 0).unwrap();
        assert_eq!(stats.length, 0);
    }

    #[test]
    fn test_zeros_and_signs() {
        let series: Vec<Option<f64>> =
            vec![Some(-2.0), Some(-1.0), Some(0.0), Some(1.0), Some(2.0)];
        let stats = compute_ts_stats(&series, 0).unwrap();

        assert_eq!(stats.n_zeros, 1);
        assert_eq!(stats.n_positive, 2);
        assert_eq!(stats.n_negative, 2);
    }

    #[test]
    fn test_nan_values() {
        let series: Vec<Option<f64>> = vec![Some(1.0), Some(f64::NAN), Some(3.0), Some(f64::NAN)];
        let stats = compute_ts_stats(&series, 0).unwrap();

        assert_eq!(stats.length, 4);
        assert_eq!(stats.n_nan, 2);
        assert_eq!(stats.n_nulls, 0);
        // Mean should be computed only from valid, non-NaN values
        assert_relative_eq!(stats.mean, 2.0, epsilon = 1e-10);
    }

    #[test]
    fn test_unique_values_and_constant() {
        // Constant series
        let series: Vec<Option<f64>> = vec![Some(5.0), Some(5.0), Some(5.0)];
        let stats = compute_ts_stats(&series, 0).unwrap();
        assert_eq!(stats.n_unique_values, 1);
        assert!(stats.is_constant);

        // Non-constant series
        let series: Vec<Option<f64>> = vec![Some(1.0), Some(2.0), Some(3.0), Some(1.0)];
        let stats = compute_ts_stats(&series, 0).unwrap();
        assert_eq!(stats.n_unique_values, 3);
        assert!(!stats.is_constant);
    }

    #[test]
    fn test_leading_trailing_zeros() {
        let series: Vec<Option<f64>> = vec![
            Some(0.0),
            Some(0.0),
            Some(1.0),
            Some(2.0),
            Some(0.0),
            Some(0.0),
            Some(0.0),
        ];
        let stats = compute_ts_stats(&series, 0).unwrap();

        assert_eq!(stats.n_zeros_start, 2);
        assert_eq!(stats.n_zeros_end, 3);
    }

    #[test]
    fn test_leading_zeros_with_null() {
        // NULL breaks the leading zero count
        let series: Vec<Option<f64>> = vec![Some(0.0), None, Some(0.0), Some(1.0)];
        let stats = compute_ts_stats(&series, 0).unwrap();
        assert_eq!(stats.n_zeros_start, 1);
    }

    #[test]
    fn test_plateau_size() {
        let series: Vec<Option<f64>> = vec![
            Some(1.0),
            Some(2.0),
            Some(2.0),
            Some(2.0),
            Some(3.0),
            Some(3.0),
        ];
        let stats = compute_ts_stats(&series, 0).unwrap();
        assert_eq!(stats.plateau_size, 3); // Three 2.0s
    }

    #[test]
    fn test_plateau_size_nonzero() {
        let series: Vec<Option<f64>> = vec![
            Some(0.0),
            Some(5.0),
            Some(5.0),
            Some(5.0),
            Some(5.0),
            Some(0.0),
            Some(3.0),
            Some(3.0),
        ];
        let stats = compute_ts_stats(&series, 0).unwrap();
        assert_eq!(stats.plateau_size_nonzero, 4); // Four 5.0s
    }

    #[test]
    fn test_skewness_kurtosis_bias_corrected() {
        // Symmetric distribution: skewness should be 0
        let series: Vec<Option<f64>> = vec![Some(1.0), Some(2.0), Some(3.0), Some(4.0), Some(5.0)];
        let stats = compute_ts_stats(&series, 0).unwrap();
        assert_relative_eq!(stats.skewness, 0.0, epsilon = 1e-10);

        // Skewed distribution: [1, 1, 1, 1, 5] should have positive skewness
        let skewed: Vec<Option<f64>> = vec![Some(1.0), Some(1.0), Some(1.0), Some(1.0), Some(5.0)];
        let stats_skewed = compute_ts_stats(&skewed, 0).unwrap();
        assert!(stats_skewed.skewness > 0.0, "Expected positive skewness");

        // Verify kurtosis is computed (uniform-like has negative excess kurtosis)
        assert!(stats.kurtosis.is_finite());
    }
}
