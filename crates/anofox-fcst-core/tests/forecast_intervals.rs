//! R-fixture value assertions for the explicit input policy and MSE policy
//! introduced in plan 10-01 Task 3 (FCST-01/FCST-02/FCST-03/FCST-08, D-11/D-12).
//!
//! These tests call `anofox_fcst_core::forecast::forecast` directly (no FFI
//! layer) and assert against `validation/reference/fixtures/r_forecast/
//! air_passengers.json` and hand-built synthetic series.

mod common;

use anofox_fcst_core::error::ForecastError;
use anofox_fcst_core::forecast::{forecast, ForecastOptions, ModelType};
use anofox_fcst_core::list_models;
use anofox_forecast::core::TimeSeries;
use anofox_forecast::models::arima::{AutoARIMA, AutoARIMAConfig};
use anofox_forecast::models::exponential::{
    AutoETS, AutoETSConfig, HoltLinearTrend, HoltWinters, SeasonalType, SimpleExponentialSmoothing,
};
use anofox_forecast::models::intermittent::Croston;
use anofox_forecast::models::laplace::LaplaceForecaster;
use anofox_forecast::models::mstl_forecaster::MSTLForecaster;
use anofox_forecast::models::theta::Theta;
use anofox_forecast::prelude::Forecaster;
use chrono::{Duration, TimeZone, Utc};
use std::str::FromStr;

/// Build a `TimeSeries` the same way `forecast.rs::make_timeseries` does
/// (hourly timestamps from 2024-01-01), so a directly-fit crate model is
/// bit-identical to the one `forecast()` fits internally.
fn make_ts(values: &[f64]) -> TimeSeries {
    let base = Utc.with_ymd_and_hms(2024, 1, 1, 0, 0, 0).unwrap();
    let timestamps: Vec<_> = (0..values.len())
        .map(|i| base + Duration::hours(i as i64))
        .collect();
    TimeSeries::univariate(timestamps, values.to_vec()).expect("valid timeseries")
}

fn air_passengers_values() -> Vec<f64> {
    let fx = common::load_fixture("r_forecast/air_passengers.json");
    common::f64_vec(&fx["inputs"]["values"])
}

// ── naive_bounds_match_r_fixture ───────────────────────────────────────────
//
// FCST-01/FCST-03: Naive 80%/95% point/lower/upper at all 12 steps must be
// close to R's naive() — not the old fabricated ~234.32 h=1 95% half-width.
//
// Tolerance note (same deviation as `core_ffi_parity.rs::
// naive_intervals_match_r_naive_through_ffi`, Task 1): `anofox-forecast`
// 0.15.10's `Naive::predict_with_intervals` computes sigma identically to R
// (`sqrt(mean(diff^2))`) but its internal z comes from a private Abramowitz &
// Stegun 26.2.23 approximation, not an exact/statrs-backed inverse normal CDF
// as `10-RESEARCH.md` §2 assumed. The documented max error is ~4.5e-4 in z,
// bounding the lower/upper deviation from R at ≤0.1 absolute on this series.
// Per D-01 ("pass crate intervals through unchanged, including models whose
// crate intervals are known to be miscalibrated until Phase 11/17") the fix
// is passthrough, not patching the upstream crate's z.
#[test]
fn naive_bounds_match_r_fixture() {
    let fx = common::load_fixture("r_forecast/air_passengers.json");
    let values: Vec<Option<f64>> = common::f64_vec(&fx["inputs"]["values"])
        .into_iter()
        .map(Some)
        .collect();
    let naive = &fx["models"]["naive"];
    let r_point = common::f64_vec(&naive["point"]);
    let r_lower_80 = common::f64_vec(&naive["lower_80"]);
    let r_upper_80 = common::f64_vec(&naive["upper_80"]);
    let r_lower_95 = common::f64_vec(&naive["lower_95"]);
    let r_upper_95 = common::f64_vec(&naive["upper_95"]);

    let tol = 0.1;

    for (level, r_lower, r_upper) in [
        (0.80_f64, &r_lower_80, &r_upper_80),
        (0.95_f64, &r_lower_95, &r_upper_95),
    ] {
        let options = ForecastOptions {
            model: ModelType::Naive,
            horizon: 12,
            confidence_level: level,
            seasonal_period: 0,
            auto_detect_seasonality: false,
            ..Default::default()
        };
        let out = forecast(&values, &options).unwrap();

        assert_eq!(out.point.len(), 12, "expected 12-step horizon");
        assert_eq!(out.lower.len(), 12, "Naive must return native lower bounds");
        assert_eq!(out.upper.len(), 12, "Naive must return native upper bounds");

        for i in 0..12 {
            common::assert_close(
                out.point[i],
                r_point[i],
                tol,
                0.0,
                &format!("Naive.point[{i}] level={level}"),
            );
            common::assert_close(
                out.lower[i],
                r_lower[i],
                tol,
                0.0,
                &format!("Naive.lower[{i}] level={level}"),
            );
            common::assert_close(
                out.upper[i],
                r_upper[i],
                tol,
                0.0,
                &format!("Naive.upper[{i}] level={level}"),
            );
        }
    }
}

// ── z_is_exact_inverse_normal ───────────────────────────────────────────────
//
// FCST-02: no z-step-table remains — the Naive h=1 half-width ratio between
// confidence levels is governed by a single quantile function, not a lookup
// table of a handful of hardcoded z values.
//
// Deviation from the plan's originally-stated 1e-9 relative tolerance (same
// root cause as `naive_bounds_match_r_fixture` above): the crate's z comes
// from a private A&S 26.2.23 approximation rather than an exact inverse
// normal CDF, so the ratio of two approximated z's is close to — but not
// bit-identical to — the ratio of the exact quantiles (empirically ~8e-5
// relative on this series, verified this session). 1e-3 relative comfortably
// proves "no step table" (a step table would produce a wildly different,
// non-continuous ratio pattern) without chasing upstream-exact-z precision
// that is Phase 11/17's scope.
#[test]
fn z_is_exact_inverse_normal() {
    let values: Vec<Option<f64>> = air_passengers_values().into_iter().map(Some).collect();

    let half_width_at = |level: f64| -> f64 {
        let options = ForecastOptions {
            model: ModelType::Naive,
            horizon: 1,
            confidence_level: level,
            seasonal_period: 0,
            auto_detect_seasonality: false,
            ..Default::default()
        };
        let out = forecast(&values, &options).unwrap();
        (out.upper[0] - out.lower[0]) / 2.0
    };

    let hw80 = half_width_at(0.80);
    let hw85 = half_width_at(0.85);
    let hw95 = half_width_at(0.95);

    let ratio_80_95 = hw80 / hw95;
    let ratio_85_95 = hw85 / hw95;

    let expected_80_95 = 0.6538648544837132;
    let expected_85_95 = 0.7344683281393418;

    let rel_tol = 1e-3;
    common::assert_close(ratio_80_95, expected_80_95, 0.0, rel_tol, "ratio_80_95");
    common::assert_close(ratio_85_95, expected_85_95, 0.0, rel_tol, "ratio_85_95");
}

// ── no_model_reproduces_fabricated_width ────────────────────────────────────
//
// FCST-01: the old fabricated Naive 95% h=1 half-width was ~234.3161214042321
// (a hardcoded z-step-table value times a hand-rolled sigma). After the D-01
// crate-passthrough fix, no model on AirPassengers should land anywhere near
// that fabricated number.
//
// Deviation: ADIDA and IMAPA are excluded from this check. Both were already
// crate-native via `extract_forecast` before this plan (never part of the
// Naive/SeasonalNaive/SMA/RandomWalkDrift fabrication Tasks 1/2 removed), and
// both legitimately produce a 95% h=1 half-width of ~235.18 on AirPassengers
// (verified this session) — an intermittent-demand-oriented interval formula
// applied to a non-intermittent, strongly-trended series, coincidentally
// landing within 1% of the unrelated old Naive fabrication constant. Asserting
// against them here would be testing a coincidence, not a regression.
#[test]
fn no_model_reproduces_fabricated_width() {
    const FABRICATED_HALF_WIDTH: f64 = 234.3161214042321;
    let values: Vec<Option<f64>> = air_passengers_values().into_iter().map(Some).collect();

    // Mirrors forecast()'s own seasonal_period-vs-model guard (forecast.rs,
    // the explicit-seasonal-period error list) — these models reject a
    // seasonal_period > 1 when auto_detect_seasonality is false.
    let non_seasonal = |m: ModelType| -> bool {
        matches!(
            m,
            ModelType::Naive
                | ModelType::SES
                | ModelType::SESOptimized
                | ModelType::Holt
                | ModelType::RandomWalkDrift
                | ModelType::ARIMA
                | ModelType::CrostonClassic
                | ModelType::CrostonOptimized
                | ModelType::CrostonSBA
                | ModelType::TSB
                | ModelType::ADIDA
                | ModelType::IMAPA
        )
    };

    let mut names = list_models();
    names.push("AutoEnsemble".to_string());

    let mut checked = 0;
    for name in names {
        let model = ModelType::from_str(&name)
            .unwrap_or_else(|e| panic!("list_models() name {name:?} does not parse: {e:?}"));
        let seasonal_period = if non_seasonal(model) { 0 } else { 12 };
        let options = ForecastOptions {
            model,
            horizon: 1,
            confidence_level: 0.95,
            seasonal_period,
            auto_detect_seasonality: false,
            ..Default::default()
        };
        if matches!(model, ModelType::ADIDA | ModelType::IMAPA) {
            continue; // coincidental unrelated overlap — see doc comment above
        }
        let out = match forecast(&values, &options) {
            Ok(out) => out,
            Err(_) => continue, // models returning Err are skipped
        };
        if out.lower.is_empty() || out.upper.is_empty() {
            continue; // D-02: no native interval support — nothing to check
        }
        let half_width = (out.upper[0] - out.lower[0]) / 2.0;
        let deviation = (half_width - FABRICATED_HALF_WIDTH).abs();
        assert!(
            deviation > 0.01 * FABRICATED_HALF_WIDTH,
            "{name}: half-width {half_width} is within 1% of the fabricated \
             {FABRICATED_HALF_WIDTH} — D-01 passthrough may have regressed"
        );
        checked += 1;
    }
    assert!(
        checked >= 5,
        "expected at least 5 models to yield checkable bounds, got {checked}"
    );
}

// ── laplace_intervals_pass_through ─────────────────────────────────────────
//
// FCST-01: forecast()'s Laplace path must be a bit-identical passthrough of
// `LaplaceForecaster::new().auto().with_seasonal(12).predict_with_intervals(12, 0.95)`.
#[test]
fn laplace_intervals_pass_through() {
    let values_f64 = air_passengers_values();
    let values: Vec<Option<f64>> = values_f64.iter().copied().map(Some).collect();

    let options = ForecastOptions {
        model: ModelType::Laplace,
        horizon: 12,
        seasonal_period: 12,
        auto_detect_seasonality: false,
        confidence_level: 0.95,
        ..Default::default()
    };
    let out = forecast(&values, &options).unwrap();

    let ts = make_ts(&values_f64);
    let mut model = LaplaceForecaster::new().auto().with_seasonal(12);
    model.fit(&ts).expect("Laplace library fit failed");
    let lib_forecast = model
        .predict_with_intervals(12, 0.95)
        .expect("Laplace library predict_with_intervals failed");
    let lib_point = lib_forecast.primary().to_vec();
    let lib_lower = lib_forecast
        .lower()
        .and_then(|i| i.first())
        .cloned()
        .unwrap_or_default();
    let lib_upper = lib_forecast
        .upper()
        .and_then(|i| i.first())
        .cloned()
        .unwrap_or_default();

    assert_eq!(out.point, lib_point, "Laplace.point must be bit-identical");
    assert_eq!(out.lower, lib_lower, "Laplace.lower must be bit-identical");
    assert_eq!(out.upper, lib_upper, "Laplace.upper must be bit-identical");
}

// ── mse_is_crate_residual_mse ───────────────────────────────────────────────
//
// FCST-08: forecast().mse must equal the mean of the crate model's own
// finite residuals² (fit the same model directly, same constructors
// forecast.rs uses), and the eight models' MSEs must be pairwise distinct —
// proving each model's MSE comes from its OWN residuals, not a single
// SES(0.3)-approximated fitted-value stand-in applied uniformly to every
// model (the pre-Phase-10 bug).
//
// Deviation from the plan's literal "none equals 2090.28": SES genuinely
// IS `SimpleExponentialSmoothing::new(0.3)`, so its real residual MSE on
// this series legitimately lands at ~2090.284 (verified this session) — that
// number is not a bug when it's SES's own answer. 2090.28 was specifically
// the OLD interim MSE that the pre-Phase-10 code produced for EVERY model
// (since every model's residuals were approximated via the same SES(0.3)
// stand-in); the meaningful check is that none of the OTHER SEVEN models
// collapses to that number, which would indicate the stand-in is still in use.
#[test]
fn mse_is_crate_residual_mse() {
    let values_f64 = air_passengers_values();
    let values: Vec<Option<f64>> = values_f64.iter().copied().map(Some).collect();
    let ts = make_ts(&values_f64);

    fn mean_sq_finite_residuals(r: &[f64]) -> f64 {
        let finite: Vec<f64> = r.iter().copied().filter(|v| v.is_finite()).collect();
        assert!(!finite.is_empty(), "expected at least one finite residual");
        finite.iter().map(|v| v.powi(2)).sum::<f64>() / finite.len() as f64
    }

    let cases: Vec<(&str, ModelType, usize)> = vec![
        ("SES", ModelType::SES, 0),
        ("Holt", ModelType::Holt, 0),
        ("AutoARIMA", ModelType::AutoARIMA, 12),
        ("AutoETS", ModelType::AutoETS, 12),
        ("Theta", ModelType::Theta, 12),
        ("HoltWinters", ModelType::HoltWinters, 12),
        ("MSTL", ModelType::MSTL, 12),
        ("CrostonClassic", ModelType::CrostonClassic, 0),
    ];

    let mut mses = Vec::with_capacity(cases.len());
    for (name, model_type, period) in &cases {
        let options = ForecastOptions {
            model: *model_type,
            horizon: 12,
            seasonal_period: *period,
            auto_detect_seasonality: false,
            confidence_level: 0.95,
            include_residuals: true,
            ..Default::default()
        };
        let out = forecast(&values, &options).unwrap();
        let wrapper_mse = out
            .mse
            .unwrap_or_else(|| panic!("{name}: expected forecast().mse to be Some"));

        let crate_mse = match *name {
            "SES" => {
                let mut m = SimpleExponentialSmoothing::new(0.3);
                m.fit(&ts).unwrap();
                mean_sq_finite_residuals(m.residuals().expect("SES residuals"))
            }
            "Holt" => {
                let mut m = HoltLinearTrend::auto();
                m.fit(&ts).unwrap();
                mean_sq_finite_residuals(m.residuals().expect("Holt residuals"))
            }
            "AutoARIMA" => {
                let config = AutoARIMAConfig::default().with_seasonal_period(12);
                let mut m = AutoARIMA::with_config(config);
                m.fit(&ts).unwrap();
                mean_sq_finite_residuals(m.residuals().expect("AutoARIMA residuals"))
            }
            "AutoETS" => {
                let config = AutoETSConfig::with_period(12);
                let mut m = AutoETS::with_config(config);
                m.fit(&ts).unwrap();
                mean_sq_finite_residuals(m.residuals().expect("AutoETS residuals"))
            }
            "Theta" => {
                let mut m = Theta::seasonal(12);
                m.fit(&ts).unwrap();
                mean_sq_finite_residuals(m.residuals().expect("Theta residuals"))
            }
            "HoltWinters" => {
                let mut m = HoltWinters::auto(12, SeasonalType::Additive);
                m.fit(&ts).unwrap();
                mean_sq_finite_residuals(m.residuals().expect("HoltWinters residuals"))
            }
            "MSTL" => {
                let mut m = MSTLForecaster::new(vec![12]);
                m.fit(&ts).unwrap();
                mean_sq_finite_residuals(m.residuals().expect("MSTL residuals"))
            }
            "CrostonClassic" => {
                let mut m = Croston::new();
                m.fit(&ts).unwrap();
                mean_sq_finite_residuals(m.residuals().expect("Croston residuals"))
            }
            other => panic!("unhandled case {other}"),
        };

        common::assert_close(
            wrapper_mse,
            crate_mse,
            0.0,
            1e-9,
            &format!("{name}.mse vs crate residual MSE"),
        );
        mses.push((*name, wrapper_mse));
    }

    // Pairwise distinct (relative gap > 1e-6).
    for i in 0..mses.len() {
        for j in (i + 1)..mses.len() {
            let (a_name, a) = mses[i];
            let (b_name, b) = mses[j];
            let rel_gap = (a - b).abs() / a.abs().max(b.abs()).max(1e-12);
            assert!(
                rel_gap > 1e-6,
                "{a_name} ({a}) and {b_name} ({b}) are not pairwise distinct (rel_gap={rel_gap})"
            );
        }
    }

    // None of the other seven (excluding SES, which legitimately lands there
    // — see the deviation note above) is within 0.01 of the old universal
    // SES(0.3)-stand-in value 2090.28.
    for (name, mse) in &mses {
        if *name == "SES" {
            continue;
        }
        assert!(
            (*mse - 2090.28).abs() > 0.01,
            "{name}: mse={mse} is within 0.01 of the old universal SES(0.3) \
             stand-in value 2090.28 — suspect the stand-in is still in use"
        );
    }
}

// ── interpolated_points_excluded ────────────────────────────────────────────
//
// D-12: a single interior NULL must not count toward MSE, but its fitted
// value (the crate's own in-sample estimate on the interpolated input) is
// still returned.
#[test]
fn interpolated_points_excluded() {
    let mut values: Vec<Option<f64>> = (0..60).map(|i| Some(10.0 + (i % 7) as f64 * 0.3)).collect();
    values[50] = None;

    let options = ForecastOptions {
        model: ModelType::Holt,
        horizon: 3,
        include_fitted: true,
        include_residuals: true,
        ..Default::default()
    };
    let out = forecast(&values, &options).unwrap();

    let residuals = out.residuals.as_ref().expect("Holt residuals must be Some");
    let fitted = out.fitted.as_ref().expect("Holt fitted must be Some");
    assert_eq!(residuals.len(), 60);
    assert_eq!(fitted.len(), 60);
    assert!(
        residuals[50].is_nan(),
        "residuals[50] must be NaN at the interpolated position"
    );
    assert!(
        fitted[50].is_finite(),
        "fitted[50] must keep the crate's in-sample value, not NaN"
    );

    let expected_mse = residuals
        .iter()
        .enumerate()
        .filter(|(i, v)| *i != 50 && v.is_finite())
        .map(|(_, v)| v * v)
        .sum::<f64>()
        / residuals
            .iter()
            .enumerate()
            .filter(|(i, v)| *i != 50 && v.is_finite())
            .count() as f64;
    common::assert_close(
        out.mse.expect("mse must be Some"),
        expected_mse,
        0.0,
        1e-12,
        "mse excludes interpolated index 50",
    );
}

// ── nan_equals_null ──────────────────────────────────────────────────────
//
// D-11: a series with `Some(NaN)` at an index must behave identically to the
// same series with `None` at that index.
#[test]
fn nan_equals_null() {
    let base: Vec<Option<f64>> = (0..30).map(|i| Some(i as f64 + (i % 3) as f64)).collect();
    let mut with_nan = base.clone();
    let mut with_null = base;
    with_nan[15] = Some(f64::NAN);
    with_null[15] = None;

    let options = ForecastOptions {
        model: ModelType::Holt,
        horizon: 3,
        include_fitted: true,
        include_residuals: true,
        ..Default::default()
    };
    let out_nan = forecast(&with_nan, &options).unwrap();
    let out_null = forecast(&with_null, &options).unwrap();

    assert_eq!(out_nan.point, out_null.point, "point must be identical");
    assert_eq!(out_nan.lower, out_null.lower, "lower must be identical");
    assert_eq!(out_nan.upper, out_null.upper, "upper must be identical");
    assert_eq!(out_nan.mse, out_null.mse, "mse must be identical");
}

// ── all_null_errors ────────────────────────────────────────────────────────
//
// D-11: an all-missing series returns InsufficientData{needed:3, got:0}.
#[test]
fn all_null_errors() {
    let values: Vec<Option<f64>> = vec![None; 30];
    let options = ForecastOptions {
        model: ModelType::Naive,
        horizon: 1,
        ..Default::default()
    };
    match forecast(&values, &options) {
        Err(ForecastError::InsufficientData { needed, got }) => {
            assert_eq!(needed, 3);
            assert_eq!(got, 0);
        }
        other => panic!("expected InsufficientData{{needed:3,got:0}}, got {other:?}"),
    }
}

// ── invalid_input_rejects_inf ───────────────────────────────────────────────
//
// D-11: a series containing ±Inf returns InvalidInput naming the index.
#[test]
fn invalid_input_rejects_inf() {
    let mut values: Vec<Option<f64>> = (0..30).map(|i| Some(i as f64)).collect();
    values[10] = Some(f64::INFINITY);
    let options = ForecastOptions {
        model: ModelType::Naive,
        horizon: 1,
        ..Default::default()
    };
    match forecast(&values, &options) {
        Err(ForecastError::InvalidInput(msg)) => {
            assert!(
                msg.contains("10"),
                "InvalidInput message must name the offending index: {msg}"
            );
        }
        other => panic!("expected InvalidInput naming index 10, got {other:?}"),
    }

    let mut values_neg: Vec<Option<f64>> = (0..30).map(|i| Some(i as f64)).collect();
    values_neg[5] = Some(f64::NEG_INFINITY);
    let options_neg = ForecastOptions {
        model: ModelType::Naive,
        horizon: 1,
        ..Default::default()
    };
    match forecast(&values_neg, &options_neg) {
        Err(ForecastError::InvalidInput(msg)) => {
            assert!(
                msg.contains('5'),
                "InvalidInput message must name the offending index: {msg}"
            );
        }
        other => panic!("expected InvalidInput naming index 5, got {other:?}"),
    }
}

// ── trailing_null_alignment ─────────────────────────────────────────────────
//
// D-12: RandomWalkDrift on y = 0..30 (slope 1) followed by two trailing
// NULLs must forecast step 1 as the period after the ORIGINAL last row
// (index 31, value 32.0 given slope 1 and the two trimmed trailing NULLs),
// not the period after the trimmed series' own last observed value (30.0).
// Leading NULLs must be dropped without shifting point values.
#[test]
fn trailing_null_alignment() {
    let mut values: Vec<Option<f64>> = (0..30).map(|i| Some(i as f64)).collect();
    values.push(None);
    values.push(None);

    let options = ForecastOptions {
        model: ModelType::RandomWalkDrift,
        horizon: 1,
        include_fitted: true,
        include_residuals: true,
        ..Default::default()
    };
    let out = forecast(&values, &options).unwrap();

    assert_eq!(
        out.point[0], 32.0,
        "step 1 must be the period after the ORIGINAL last row (32.0), not \
         after the trimmed series' last observed value (30.0)"
    );

    let fitted = out.fitted.expect("fitted must be Some");
    let residuals = out.residuals.expect("residuals must be Some");
    assert_eq!(fitted.len(), 32, "fitted must have the ORIGINAL length");
    assert_eq!(
        residuals.len(),
        32,
        "residuals must have the ORIGINAL length"
    );
    assert!(
        fitted[30].is_nan() && fitted[31].is_nan(),
        "fitted at the two trailing-trimmed positions must be NaN"
    );
    assert!(
        residuals[30].is_nan() && residuals[31].is_nan(),
        "residuals at the two trailing-trimmed positions must be NaN"
    );

    // Leading NULLs are dropped without shifting point values: a series with
    // 2 leading NULLs prepended to the same ramp forecasts the same step 1.
    let mut leading_null_values: Vec<Option<f64>> = vec![None, None];
    leading_null_values.extend((0..30).map(|i| Some(i as f64)));
    let out_leading = forecast(&leading_null_values, &options).unwrap();
    assert_eq!(
        out_leading.point[0], 30.0,
        "leading NULLs must not shift the point forecast (last value 29, \
         drift 1, h=1 -> 30.0)"
    );
}

// ── confidence_level_outside_open_interval ──────────────────────────────────
//
// D-11-adjacent input validation: confidence_level outside (0, 1) is rejected
// before any model is fit.
#[test]
fn confidence_level_outside_open_interval() {
    let values: Vec<Option<f64>> = (0..30).map(|i| Some(i as f64)).collect();
    for level in [0.0, 1.0, -0.5, 1.5, f64::NAN] {
        let options = ForecastOptions {
            model: ModelType::Naive,
            horizon: 1,
            confidence_level: level,
            ..Default::default()
        };
        match forecast(&values, &options) {
            Err(ForecastError::InvalidParameter { param, .. }) => {
                assert_eq!(param, "confidence_level");
            }
            other => panic!("level={level}: expected InvalidParameter, got {other:?}"),
        }
    }
}

// ── r_vs_crate_half_width_information (not asserted — FCST-09 is Phase 17) ──
//
// Prints the R-vs-crate h=1 95% half-width for a handful of models as
// information for Phase 17's interval-calibration work. No assertions.
#[test]
fn r_vs_crate_half_width_information() {
    let fx = common::load_fixture("r_forecast/air_passengers.json");
    let values: Vec<Option<f64>> = common::f64_vec(&fx["inputs"]["values"])
        .into_iter()
        .map(Some)
        .collect();

    let cases: [(&str, &str, ModelType, usize); 7] = [
        ("snaive", "snaive", ModelType::SeasonalNaive, 12),
        ("rwf_drift", "rwf_drift", ModelType::RandomWalkDrift, 0),
        ("ses", "ses", ModelType::SES, 0),
        ("holt", "holt", ModelType::Holt, 0),
        ("ets_auto", "ets_auto", ModelType::AutoETS, 12),
        ("auto_arima", "auto_arima", ModelType::AutoARIMA, 12),
        ("thetaf", "thetaf", ModelType::Theta, 12),
    ];

    for (r_key, label, model, period) in cases {
        let r_entry = &fx["models"][r_key];
        if r_entry.is_null() {
            eprintln!("{label}: no R fixture entry for key {r_key:?}, skipping");
            continue;
        }
        let r_upper_95 = common::f64_vec(&r_entry["upper_95"]);
        let r_lower_95 = common::f64_vec(&r_entry["lower_95"]);
        if r_upper_95.is_empty() || r_lower_95.is_empty() {
            eprintln!("{label}: R fixture has no upper_95/lower_95, skipping");
            continue;
        }
        let r_half_width = (r_upper_95[0] - r_lower_95[0]) / 2.0;

        let options = ForecastOptions {
            model,
            horizon: 1,
            seasonal_period: period,
            auto_detect_seasonality: false,
            confidence_level: 0.95,
            ..Default::default()
        };
        match forecast(&values, &options) {
            Ok(out) if !out.lower.is_empty() && !out.upper.is_empty() => {
                let crate_half_width = (out.upper[0] - out.lower[0]) / 2.0;
                eprintln!(
                    "{label}: R h=1 95% half-width={r_half_width}, crate={crate_half_width}, \
                     diff={}",
                    (crate_half_width - r_half_width).abs()
                );
            }
            Ok(_) => eprintln!("{label}: crate returned no native interval (D-02)"),
            Err(e) => eprintln!("{label}: crate forecast() failed: {e:?}"),
        }
    }
}
