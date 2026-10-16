//! Pinned `anofox-forecast` 0.15.10 behaviour (now superseded, see below):
//! on a non-seasonal, no-zero series, `Theta` (STM, fixed alpha=0.1) and
//! `CrostonClassic` (fixed alpha=0.1, unit intervals when every point is a
//! demand) computed IN-SAMPLE fitted values via the exact same
//! SES(alpha=0.1) one-step-level recursion, even though their out-of-sample
//! forecasts diverged (Theta adds a drift term `extract_forecast`/
//! `predict_with_intervals` applies; Croston does not). This was NOT a
//! wrapper dispatch bug — `forecast.rs::extract_forecast` reads
//! `model.fitted_values()`/`model.residuals()` straight from the crate
//! `Forecaster` trait object each model type fits, with no shared helper
//! and no aliasing (`wrapper_fitted_is_the_crate_models_own_fitted_values`
//! below proves this independently, and still holds).
//!
//! Gap-closure plan 10-10, Task 2 (FCST-03 / D-11 / 10-VERIFICATION.md
//! coincidental_reliance item). 10-VERIFICATION.md flagged this fitted-array
//! identity as something "a future verifier/phase should confirm ... is
//! intentional crate behavior, not a latent aliasing bug, before relying on
//! Theta's fitted/residuals output for anything calibration-sensitive" — this
//! file is that confirmation.
//!
//! **Phase 17 plan 17-01 (anofox-forecast 0.16.0+ bump):** Theta's alpha is
//! now optimised (no longer fixed at 0.1), so the SES(0.1) coincidence
//! above broke for Theta specifically.
//! `theta_optimised_alpha_diverges_from_croston_classic_on_positive_series`
//! (renamed from
//! `theta_and_croston_classic_fitted_are_both_ses_alpha_0_1_on_positive_series`)
//! now documents the OPPOSITE: CrostonClassic's fitted values still equal
//! the independent SES(0.1) recursion exactly (its alpha is still fixed),
//! but Theta's fitted values now diverge from both that recursion and from
//! Croston's fitted array (max abs diff on the AirPassengers prefix:
//! ~38.44, far above 1e-6) because Theta's alpha is optimised per-series.
//! The other two tests are unchanged — they document real, load-bearing
//! crate behaviour (Theta's drift-free in-sample path, and Croston's
//! intermittent-series divergence) that the dependency bump did not touch.

use anofox_fcst_core::forecast::{forecast, ForecastOptions, ModelType};
use anofox_forecast::core::TimeSeries;
use anofox_forecast::models::intermittent::Croston;
use anofox_forecast::models::theta::Theta;
use anofox_forecast::models::Forecaster;
use chrono::{Duration, TimeZone, Utc};

/// First 24 values of the R `AirPassengers` fixture (112..140), the same
/// 24-point prefix `test/sql/ts_forecast_crate_delegation.test` and
/// `10-REVIEW.md`'s coincidental_reliance finding use. Non-seasonal (period
/// resolves to 1 with `auto_detect_seasonality: false`) and has no zeros, so
/// Croston treats every point as a demand (interval_level stays exactly 1.0).
fn air_passengers_prefix_24() -> Vec<f64> {
    vec![
        112.0, 118.0, 132.0, 129.0, 121.0, 135.0, 148.0, 148.0, 136.0, 119.0, 104.0, 118.0, 115.0,
        126.0, 141.0, 135.0, 125.0, 149.0, 170.0, 170.0, 158.0, 133.0, 114.0, 140.0,
    ]
}

/// An intermittent-demand 24-value series with zero periods interspersed
/// (same style of fixture the crate's own Croston tests use), so Croston's
/// demand/interval decomposition actually diverges from Theta's plain SES.
fn intermittent_24() -> Vec<f64> {
    vec![
        0.0, 3.0, 0.0, 0.0, 5.0, 0.0, 2.0, 0.0, 0.0, 4.0, 0.0, 0.0, 6.0, 0.0, 1.0, 0.0, 0.0, 3.0,
        0.0, 5.0, 0.0, 0.0, 2.0, 0.0,
    ]
}

/// Builds a `TimeSeries` the same way `forecast.rs::make_timeseries` does
/// (hourly timestamps from 2024-01-01), so a model fit directly against this
/// series is fitting the identical data the wrapper's `forecast()` fits.
fn make_timeseries_like_wrapper(values: &[f64]) -> TimeSeries {
    let base = Utc.with_ymd_and_hms(2024, 1, 1, 0, 0, 0).unwrap();
    let timestamps: Vec<_> = (0..values.len())
        .map(|i| base + Duration::hours(i as i64))
        .collect();
    TimeSeries::univariate(timestamps, values.to_vec())
        .expect("TimeSeries::univariate should succeed for a complete, non-empty series")
}

/// Independent re-implementation of the SES(alpha=0.1) one-step-level
/// recursion both Theta's non-seasonal in-sample path and Croston-classic's
/// no-zero-demand in-sample path reduce to:
/// `f[0] = y[0]`, `level = y[0]`; for `i >= 1`: `f[i] = level`, then
/// `level = 0.1 * y[i] + 0.9 * level`.
fn independent_ses_alpha_0_1(values: &[f64]) -> Vec<f64> {
    assert!(!values.is_empty());
    let alpha = 0.1_f64;
    let mut fitted = Vec::with_capacity(values.len());
    let mut level = values[0];
    fitted.push(values[0]);
    for &y in &values[1..] {
        fitted.push(level);
        level = alpha * y + (1.0 - alpha) * level;
    }
    fitted
}

fn theta_options(seasonal_period: usize, horizon: usize) -> ForecastOptions {
    ForecastOptions {
        model: ModelType::Theta,
        horizon,
        seasonal_period,
        auto_detect_seasonality: false,
        include_fitted: true,
        include_residuals: true,
        ..Default::default()
    }
}

fn croston_options(horizon: usize) -> ForecastOptions {
    ForecastOptions {
        model: ModelType::CrostonClassic,
        horizon,
        seasonal_period: 0,
        auto_detect_seasonality: false,
        include_fitted: true,
        include_residuals: true,
        ..Default::default()
    }
}

#[test]
fn theta_optimised_alpha_diverges_from_croston_classic_on_positive_series() {
    let raw = air_passengers_prefix_24();
    let values: Vec<Option<f64>> = raw.iter().map(|&v| Some(v)).collect();

    let theta_result =
        forecast(&values, &theta_options(0, 3)).expect("Theta forecast should succeed");
    let croston_result =
        forecast(&values, &croston_options(3)).expect("CrostonClassic forecast should succeed");

    assert_eq!(theta_result.model_name, "Theta");
    assert_eq!(croston_result.model_name, "CrostonClassic");

    // Dispatch is genuinely distinct — the two models' forecasts diverge
    // even though, pre-0.16, their fitted arrays coincided.
    let max_point_diff = theta_result
        .point
        .iter()
        .zip(croston_result.point.iter())
        .map(|(a, b)| (a - b).abs())
        .fold(0.0_f64, f64::max);
    assert!(
        max_point_diff > 1.0,
        "Theta and CrostonClassic point forecasts should differ (dispatch is distinct); max diff = {}",
        max_point_diff
    );

    let theta_fitted = theta_result
        .fitted
        .expect("Theta fitted values should be present when include_fitted=true");
    let croston_fitted = croston_result
        .fitted
        .expect("CrostonClassic fitted values should be present when include_fitted=true");

    assert_eq!(theta_fitted.len(), raw.len());
    assert_eq!(croston_fitted.len(), raw.len());

    let expected = independent_ses_alpha_0_1(&raw);

    // CrostonClassic's alpha is still fixed at 0.1 — its fitted values
    // still match the independent SES(0.1) recursion exactly.
    for (i, (&actual, &exp)) in croston_fitted.iter().zip(expected.iter()).enumerate() {
        assert!(
            (actual - exp).abs() < 1e-9,
            "CrostonClassic fitted[{}] = {} does not match independent SES(0.1) recursion {}",
            i,
            actual,
            exp
        );
    }

    // Theta's alpha is now optimised (anofox-forecast 0.16.0+), so its
    // fitted values diverge from the fixed-alpha=0.1 recursion...
    let theta_vs_expected_max_diff = theta_fitted
        .iter()
        .zip(expected.iter())
        .map(|(a, b)| (a - b).abs())
        .fold(0.0_f64, f64::max);
    assert!(
        theta_vs_expected_max_diff > 1e-6,
        "Theta's fitted values should now diverge from the fixed SES(0.1) recursion (alpha is \
         optimised since anofox-forecast 0.16.0); max diff = {}",
        theta_vs_expected_max_diff
    );

    // ...and therefore from CrostonClassic's fitted array too (the
    // pre-0.16 coincidence this file documented no longer holds).
    let theta_vs_croston_max_diff = theta_fitted
        .iter()
        .zip(croston_fitted.iter())
        .map(|(a, b)| (a - b).abs())
        .fold(0.0_f64, f64::max);
    assert!(
        theta_vs_croston_max_diff > 1e-6,
        "Theta and CrostonClassic fitted arrays should now diverge (Theta's alpha is optimised, \
         Croston's stays fixed at 0.1); max diff = {}",
        theta_vs_croston_max_diff
    );
}

#[test]
fn wrapper_fitted_is_the_crate_models_own_fitted_values() {
    let raw = air_passengers_prefix_24();
    let values: Vec<Option<f64>> = raw.iter().map(|&v| Some(v)).collect();

    // --- Theta ---
    let wrapper_theta =
        forecast(&values, &theta_options(0, 3)).expect("Theta forecast should succeed");
    let wrapper_theta_fitted = wrapper_theta
        .fitted
        .expect("wrapper Theta fitted values should be present");

    let ts = make_timeseries_like_wrapper(&raw);
    let mut direct_theta = Theta::new();
    direct_theta
        .fit(&ts)
        .expect("direct Theta::new().fit() should succeed on a complete series");
    let direct_theta_fitted = direct_theta
        .fitted_values()
        .expect("direct Theta fitted_values() should be Some after fit()");

    assert_eq!(
        wrapper_theta_fitted.as_slice(),
        direct_theta_fitted,
        "forecast.rs's Theta fitted output must be exactly the crate Theta model's own \
         fitted_values() — no shared helper, no aliasing"
    );

    // --- CrostonClassic ---
    let wrapper_croston =
        forecast(&values, &croston_options(3)).expect("CrostonClassic forecast should succeed");
    let wrapper_croston_fitted = wrapper_croston
        .fitted
        .expect("wrapper CrostonClassic fitted values should be present");

    let mut direct_croston = Croston::new();
    direct_croston
        .fit(&ts)
        .expect("direct Croston::new().fit() should succeed on a complete series");
    let direct_croston_fitted = direct_croston
        .fitted_values()
        .expect("direct Croston fitted_values() should be Some after fit()");

    assert_eq!(
        wrapper_croston_fitted.as_slice(),
        direct_croston_fitted,
        "forecast.rs's CrostonClassic fitted output must be exactly the crate Croston model's \
         own fitted_values() — no shared helper, no aliasing"
    );
}

#[test]
fn croston_fitted_diverges_from_theta_on_intermittent_series() {
    let raw = intermittent_24();
    let values: Vec<Option<f64>> = raw.iter().map(|&v| Some(v)).collect();

    let theta_result =
        forecast(&values, &theta_options(0, 3)).expect("Theta forecast should succeed");
    let croston_result =
        forecast(&values, &croston_options(3)).expect("CrostonClassic forecast should succeed");

    let theta_fitted = theta_result
        .fitted
        .expect("Theta fitted values should be present");
    let croston_fitted = croston_result
        .fitted
        .expect("CrostonClassic fitted values should be present");

    assert_eq!(theta_fitted.len(), raw.len());
    assert_eq!(croston_fitted.len(), raw.len());

    let max_diff = theta_fitted
        .iter()
        .zip(croston_fitted.iter())
        .map(|(a, b)| (a - b).abs())
        .fold(0.0_f64, f64::max);
    assert!(
        max_diff > 1e-6,
        "Theta and CrostonClassic fitted arrays should diverge on an intermittent-demand series \
         (Croston's demand/interval decomposition skips zero periods); max diff = {}",
        max_diff
    );
}
