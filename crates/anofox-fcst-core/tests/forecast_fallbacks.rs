//! D-05/D-06/D-07/FCST-06/FCST-07 tests: every former silent fallback in
//! `forecast.rs` becomes an error or an explicitly reported identity, and
//! `build_forecaster` ensemble members match their standalone model
//! bit-for-bit.
//!
//! Task 1 (tracer) scope: 'ETS' without a spec runs the crate AutoETS
//! selection through the shared `new_auto_ets` constructor, the
//! "ETS(ETS(...))" double-wrap bug is fixed, and an `['ETS','ETS']` explicit
//! ensemble matches standalone 'ETS'. Task 2 adds the remaining
//! resolve_period/Kalman/panel-pool/AutoMSTL-AutoMFLES coverage.

mod common;

use anofox_fcst_core::forecast::{
    forecast, forecast_explicit_ensemble, ForecastOptions, ModelType,
};

fn air_passengers_values() -> Vec<Option<f64>> {
    let fx = common::load_fixture("r_forecast/air_passengers.json");
    common::f64_vec(&fx["inputs"]["values"])
        .into_iter()
        .map(Some)
        .collect()
}

/// 'ETS' without an `ets_spec` runs the same crate AutoETS selection as
/// standalone 'AutoETS' (D-05/FCST-07): point/lower/upper are bit-identical,
/// and the model_name differs only by the "Auto" prefix (e.g. "ETS(M,A,M)"
/// vs "AutoETS(M,A,M)").
#[test]
fn ets_without_spec_runs_autoets() {
    let values = air_passengers_values();

    let ets_options = ForecastOptions {
        model: ModelType::ETS,
        horizon: 12,
        seasonal_period: 12,
        auto_detect_seasonality: false,
        ..Default::default()
    };
    let auto_ets_options = ForecastOptions {
        model: ModelType::AutoETS,
        ..ets_options.clone()
    };

    let ets_result = forecast(&values, &ets_options).expect("'ETS' without spec should succeed");
    let auto_ets_result = forecast(&values, &auto_ets_options).expect("'AutoETS' should succeed");

    assert_eq!(
        ets_result.point, auto_ets_result.point,
        "'ETS' without spec and 'AutoETS' must produce bit-identical point forecasts"
    );
    assert_eq!(
        ets_result.lower, auto_ets_result.lower,
        "'ETS' without spec and 'AutoETS' must produce bit-identical lower bounds"
    );
    assert_eq!(
        ets_result.upper, auto_ets_result.upper,
        "'ETS' without spec and 'AutoETS' must produce bit-identical upper bounds"
    );

    assert!(
        ets_result.model_name.starts_with("ETS("),
        "'ETS' model_name should start with 'ETS(', got '{}'",
        ets_result.model_name
    );
    assert!(
        auto_ets_result.model_name.starts_with("AutoETS("),
        "'AutoETS' model_name should start with 'AutoETS(', got '{}'",
        auto_ets_result.model_name
    );
    // Names differ only by the "Auto" prefix.
    let auto_stripped = auto_ets_result
        .model_name
        .strip_prefix("Auto")
        .expect("AutoETS model_name must start with 'Auto'");
    assert_eq!(
        ets_result.model_name, auto_stripped,
        "'ETS' model_name '{}' should equal 'AutoETS' model_name '{}' with the \
         'Auto' prefix stripped",
        ets_result.model_name, auto_ets_result.model_name
    );
}

/// An explicit ETS spec never double-wraps: `ETSSpec::short_name()` already
/// returns the fully-formed "ETS(E,T,S)" text, so `forecast_with_ets_spec`'s
/// model_name must be exactly that text, never "ETS(ETS(M,A,M))".
#[test]
fn ets_spec_name_has_no_double_wrap() {
    let values: Vec<Option<f64>> = (0..60)
        .map(|i| {
            Some(100.0 + i as f64 * 0.5 + (i as f64 * std::f64::consts::PI / 6.0).sin() * 10.0)
        })
        .collect();

    let options = ForecastOptions {
        model: ModelType::ETS,
        ets_spec: Some("MAM".to_string()),
        seasonal_period: 12,
        auto_detect_seasonality: false,
        horizon: 5,
        ..Default::default()
    };

    let result = forecast(&values, &options).expect("ETS('MAM') should fit");
    assert_eq!(
        result.model_name, "ETS(M,A,M)",
        "expected no double-wrap, got '{}'",
        result.model_name
    );
    assert!(
        !result.model_name.contains("ETS(ETS"),
        "model_name must never double-wrap, got '{}'",
        result.model_name
    );
}

/// An explicit `['ETS', 'ETS']` ensemble must match standalone 'ETS' exactly
/// (FCST-07): `build_forecaster`'s `ModelType::ETS` arm uses the same
/// `new_auto_ets` shared constructor as the standalone path, so both members
/// equal — and their Mean-combined point forecast therefore equals —
/// standalone 'ETS's own point forecast.
#[test]
fn ensemble_of_two_ets_equals_standalone_ets() {
    let values = air_passengers_values();

    let standalone_options = ForecastOptions {
        model: ModelType::ETS,
        horizon: 12,
        seasonal_period: 12,
        auto_detect_seasonality: false,
        ..Default::default()
    };
    let standalone = forecast(&values, &standalone_options).expect("standalone 'ETS' should fit");

    let members = vec!["ETS".to_string(), "ETS".to_string()];
    let ensemble = forecast_explicit_ensemble(&values, 12, &members, Some("mean"), 12)
        .expect("['ETS', 'ETS'] ensemble should fit");

    assert_eq!(
        ensemble.point.len(),
        standalone.point.len(),
        "ensemble and standalone forecasts must have the same horizon length"
    );
    for (i, (e, s)) in ensemble
        .point
        .iter()
        .zip(standalone.point.iter())
        .enumerate()
    {
        assert!(
            (e - s).abs() < 1e-9,
            "step {i}: ensemble point {e} != standalone point {s} (diff {})",
            (e - s).abs()
        );
    }
}
