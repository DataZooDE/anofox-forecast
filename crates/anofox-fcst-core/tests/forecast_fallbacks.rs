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

use anofox_fcst_core::detect_seasonality;
use anofox_fcst_core::forecast::{
    forecast, forecast_explain, forecast_explicit_ensemble, forecast_inspect, forecast_with_exog,
    parse_kalman_spec, ForecastOptions, ForecastOptionsExog, KalmanSpec, ModelType,
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
    let ensemble = forecast_explicit_ensemble(&values, 12, &members, Some("mean"), 12, 0.90)
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

// ============================================================================
// Task 2: resolve_period (D-07) — HoltWinters/SeasonalNaive
// ============================================================================

/// HoltWinters on a 24-point linear ramp (no detectable seasonality) with
/// seasonal_period=0 and auto_detect_seasonality=false must still run
/// detection (HoltWinters requires a period, D-07) — detecting nothing, this
/// is a hard InvalidInput naming "seasonal period" and "HoltWinters", never
/// a silent default of 12.
#[test]
fn holt_winters_without_period_errors_naming_model() {
    let values: Vec<Option<f64>> = (0..24).map(|i| Some(100.0 + i as f64 * 2.0)).collect();
    let options = ForecastOptions {
        model: ModelType::HoltWinters,
        horizon: 5,
        seasonal_period: 0,
        auto_detect_seasonality: false,
        ..Default::default()
    };
    let err = forecast(&values, &options).unwrap_err();
    let msg = err.to_string();
    assert!(
        msg.contains("seasonal period") && msg.contains("HoltWinters"),
        "expected message naming 'seasonal period' and 'HoltWinters', got: {msg}"
    );
}

/// HoltWinters on a 96-point series with embedded period-12 seasonality,
/// seasonal_period=0 and auto_detect_seasonality=false: HoltWinters requires
/// a period (D-07), so detection runs regardless of auto_detect_seasonality
/// — the result must be bit-identical to HoltWinters with the explicit
/// detected period (`detect_seasonality(values, None)[0]` == 12 for this
/// series — asserts resolve_period uses the existing detector, not its
/// quality; detector accuracy is Phase 15).
#[test]
fn holt_winters_auto_detected_period_matches_explicit() {
    let values: Vec<Option<f64>> = (0..96)
        .map(|i| Some(10.0 + 5.0 * (2.0 * std::f64::consts::PI * i as f64 / 12.0).sin()))
        .collect();

    let f64_values: Vec<f64> = values.iter().map(|v| v.unwrap()).collect();
    let detected = detect_seasonality(&f64_values, None)
        .expect("detection should succeed on a clean period-12 sine series");
    let detected_period = detected
        .first()
        .copied()
        .expect("a period must be detected") as usize;

    let implicit_options = ForecastOptions {
        model: ModelType::HoltWinters,
        horizon: 6,
        seasonal_period: 0,
        auto_detect_seasonality: false,
        ..Default::default()
    };
    let explicit_options = ForecastOptions {
        seasonal_period: detected_period,
        ..implicit_options.clone()
    };

    let implicit = forecast(&values, &implicit_options).expect("implicit-detect should succeed");
    let explicit = forecast(&values, &explicit_options).expect("explicit period should succeed");

    assert_eq!(
        implicit.point, explicit.point,
        "resolve_period's auto-detected period must produce the same point forecast as \
         passing that same period explicitly"
    );
}

/// SeasonalNaive with an explicit seasonal_period=1 is a hard InvalidInput
/// (the crate's own SeasonalNaive requires period >= 2) — never a silent
/// delegation to plain Naive (the pre-10-03 behaviour this plan removes).
#[test]
fn seasonal_naive_explicit_period_1_errors() {
    let values: Vec<Option<f64>> = (0..24).map(|i| Some(i as f64)).collect();
    let options = ForecastOptions {
        model: ModelType::SeasonalNaive,
        horizon: 5,
        seasonal_period: 1,
        auto_detect_seasonality: false,
        ..Default::default()
    };
    let err = forecast(&values, &options).unwrap_err();
    assert!(
        matches!(err, anofox_fcst_core::error::ForecastError::InvalidInput(_)),
        "expected InvalidInput, got: {err:?}"
    );
}

// ============================================================================
// Task 2: AutoETS crate failure is a hard error (D-07) — never a silent
// fallback to HoltWinters/Holt/SES.
// ============================================================================

/// AutoETS on a constant series either succeeds (reporting a selected spec,
/// bit-identical to fitting the crate AutoETS directly) or fails with a hard
/// InvalidInput naming AutoETS — never succeeds with a HoltWinters/Holt/SES
/// point silently substituted under the "AutoETS" name.
#[test]
fn auto_ets_constant_series_never_silently_substitutes() {
    let values: Vec<Option<f64>> = vec![Some(5.0); 30];
    let options = ForecastOptions {
        model: ModelType::AutoETS,
        horizon: 5,
        ..Default::default()
    };
    match forecast(&values, &options) {
        Ok(result) => {
            assert!(
                result.model_name.starts_with("AutoETS("),
                "expected model_name starting with 'AutoETS(', got '{}'",
                result.model_name
            );
        }
        Err(e) => {
            let msg = e.to_string();
            assert!(
                msg.contains("AutoETS"),
                "error must name AutoETS, got: {msg}"
            );
        }
    }
}

// ============================================================================
// Task 2: AutoMSTL/AutoMFLES report the non-Auto family name exactly (D-06)
// ============================================================================

/// AutoMSTL on AirPassengers reports model_name "MSTL" exactly and is
/// bit-identical to standalone 'MSTL' with the same seasonal_period.
#[test]
fn auto_mstl_reports_mstl_name_and_matches_standalone() {
    let values = air_passengers_values();
    let auto_options = ForecastOptions {
        model: ModelType::AutoMSTL,
        horizon: 12,
        seasonal_period: 12,
        auto_detect_seasonality: false,
        ..Default::default()
    };
    let standalone_options = ForecastOptions {
        model: ModelType::MSTL,
        ..auto_options.clone()
    };

    let auto_result = forecast(&values, &auto_options).expect("AutoMSTL should fit");
    let standalone_result = forecast(&values, &standalone_options).expect("MSTL should fit");

    assert_eq!(auto_result.model_name, "MSTL");
    assert_eq!(auto_result.point, standalone_result.point);
}

/// AutoMFLES on AirPassengers reports model_name "MFLES" exactly and is
/// bit-identical to standalone 'MFLES' with the same seasonal_period.
#[test]
fn auto_mfles_reports_mfles_name_and_matches_standalone() {
    let values = air_passengers_values();
    let auto_options = ForecastOptions {
        model: ModelType::AutoMFLES,
        horizon: 12,
        seasonal_period: 12,
        auto_detect_seasonality: false,
        ..Default::default()
    };
    let standalone_options = ForecastOptions {
        model: ModelType::MFLES,
        ..auto_options.clone()
    };

    let auto_result = forecast(&values, &auto_options).expect("AutoMFLES should fit");
    let standalone_result = forecast(&values, &standalone_options).expect("MFLES should fit");

    assert_eq!(auto_result.model_name, "MFLES");
    assert_eq!(auto_result.point, standalone_result.point);
}

// ============================================================================
// Task 2: Kalman strict spec parsing (D-07)
// ============================================================================

#[test]
fn kalman_model_name_reports_spec() {
    let values: Vec<Option<f64>> = (0..30)
        .map(|i| Some(10.0 + i as f64 * 0.5 + (i % 3) as f64))
        .collect();

    let local_level_options = ForecastOptions {
        model: ModelType::Kalman,
        horizon: 5,
        kalman_model: None,
        ..Default::default()
    };
    let local_level = forecast(&values, &local_level_options).expect("Kalman local_level");
    assert_eq!(local_level.model_name, "Kalman(local_level)");

    let local_linear_trend_options = ForecastOptions {
        model: ModelType::Kalman,
        horizon: 5,
        kalman_model: Some("local_linear_trend".to_string()),
        ..Default::default()
    };
    let local_linear_trend =
        forecast(&values, &local_linear_trend_options).expect("Kalman local_linear_trend");
    assert_eq!(local_linear_trend.model_name, "Kalman(local_linear_trend)");
}

#[test]
fn kalman_model_misspelled_errors_naming_param() {
    let values: Vec<Option<f64>> = (0..30)
        .map(|i| Some(10.0 + i as f64 * 0.5 + (i % 3) as f64))
        .collect();
    let options = ForecastOptions {
        model: ModelType::Kalman,
        horizon: 5,
        kalman_model: Some("local_lvl".to_string()),
        ..Default::default()
    };
    let err = forecast(&values, &options).unwrap_err();
    match err {
        anofox_fcst_core::error::ForecastError::InvalidParameter { param, .. } => {
            assert_eq!(param, "kalman_model");
        }
        other => panic!("expected InvalidParameter, got {other:?}"),
    }
}

#[test]
fn parse_kalman_spec_table() {
    assert_eq!(parse_kalman_spec(None).unwrap(), KalmanSpec::LocalLevel);
    assert_eq!(parse_kalman_spec(Some("")).unwrap(), KalmanSpec::LocalLevel);
    assert_eq!(
        parse_kalman_spec(Some("local_level")).unwrap(),
        KalmanSpec::LocalLevel
    );
    assert_eq!(
        parse_kalman_spec(Some("Local_Linear_Trend")).unwrap(),
        KalmanSpec::LocalLinearTrend
    );
    assert!(parse_kalman_spec(Some("local_lvl")).is_err());
}

// ============================================================================
// Task 2: forecast_inspect/forecast_explain propagate parse_model_pool
// errors (D-07) — never a silent `if let Ok` swallow.
// ============================================================================

#[test]
fn forecast_inspect_invalid_model_pool_errors() {
    let values = air_passengers_values();
    let options = ForecastOptions {
        model: ModelType::AutoETS,
        horizon: 12,
        seasonal_period: 12,
        auto_detect_seasonality: false,
        model_pool: Some("nonsense".to_string()),
        ..Default::default()
    };
    let err = forecast_inspect(&values, &options).unwrap_err();
    let msg = err.to_string();
    assert!(
        msg.contains("model_pool") || msg.to_lowercase().contains("nonsense"),
        "expected error naming the bad model_pool, got: {msg}"
    );
}

#[test]
fn forecast_explain_mstl_without_period_errors() {
    // forecast_explain's MSTL/AutoMSTL arm also runs resolve_period (D-07) —
    // a 24-point linear ramp has no detectable seasonality, so this must
    // error rather than silently default to a period of 12.
    let values: Vec<Option<f64>> = (0..24).map(|i| Some(100.0 + i as f64 * 2.0)).collect();
    let options = ForecastOptions {
        model: ModelType::MSTL,
        horizon: 5,
        seasonal_period: 0,
        auto_detect_seasonality: false,
        ..Default::default()
    };
    let err = forecast_explain(&values, 5, &options).unwrap_err();
    let msg = err.to_string();
    assert!(
        msg.contains("seasonal period"),
        "expected message naming 'seasonal period', got: {msg}"
    );
}

// ============================================================================
// Task 2: forecast_with_exog's no-regressor branch honours every option
// (D-10/FCST-05 "it runs forecast() itself") — never the deleted
// forecast_with_model's hardcoded GARCH(1,1)/local_level/no-spec-ETS/top_k=3
// defaults.
// ============================================================================

#[test]
fn forecast_with_exog_honours_garch_order_with_no_regressors() {
    let values: Vec<Option<f64>> = (0..30)
        .map(|i| Some(0.01 * ((i as f64 * 1.3).sin() + 2.0)))
        .collect();
    let options = ForecastOptionsExog {
        model: ModelType::GARCH,
        horizon: 5,
        garch_p: 2,
        garch_q: 1,
        exog: None,
        ..Default::default()
    };
    let result = forecast_with_exog(&values, &options).expect("GARCH(2,1) should fit");
    assert_eq!(result.model_name, "GARCH(2,1)");
}

#[test]
fn forecast_with_exog_honours_kalman_model_with_no_regressors() {
    let values: Vec<Option<f64>> = (0..30).map(|i| Some(5.0 + i as f64 * 1.2)).collect();
    let options = ForecastOptionsExog {
        model: ModelType::Kalman,
        horizon: 5,
        kalman_model: Some("local_linear_trend".to_string()),
        exog: None,
        ..Default::default()
    };
    let result = forecast_with_exog(&values, &options).expect("Kalman should fit");
    assert_eq!(result.model_name, "Kalman(local_linear_trend)");
}

#[test]
fn forecast_with_exog_honours_ets_spec_with_no_regressors() {
    let values: Vec<Option<f64>> = (0..30).map(|i| Some(100.0 + i as f64 * 1.5)).collect();
    let options = ForecastOptionsExog {
        model: ModelType::ETS,
        horizon: 5,
        ets_spec: Some("AAN".to_string()),
        exog: None,
        ..Default::default()
    };
    let result = forecast_with_exog(&values, &options).expect("ETS(AAN) should fit");
    assert_eq!(result.model_name, "ETS(A,A,N)");
}
