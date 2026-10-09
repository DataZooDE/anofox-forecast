//! R-fixture parity and validation tests for the real crate ARIMA/SARIMA fit
//! (D-08/D-09, plan 10-02 Task 2). Calls `anofox_fcst_core::forecast::forecast`
//! directly (no FFI layer) — the FFI-level airline-through-`anofox_ts_forecast`
//! assertion lives in `crates/anofox-fcst-ffi/tests/core_ffi_parity.rs`
//! (`arima_airline_matches_r_through_ffi`).

mod common;

use anofox_fcst_core::error::ForecastError;
use anofox_fcst_core::forecast::{forecast, ForecastOptions, ModelType};
use anofox_fcst_core::{parse_order_triplet, parse_seasonal_periods};

// ── R fixture parity ────────────────────────────────────────────────────

/// FCST-04: ARIMA(0,1,1)(0,1,1)[12] on AirPassengers must match R
/// `Arima(order=c(0,1,1), seasonal=c(0,1,1))` within the fixture's
/// `model_point_rel` tolerance; model_name must be the exact seasonal
/// name (d+D=2, so the crate never fits a constant — no " with drift"/
/// " with non-zero mean" suffix); bounds must bracket every point.
#[test]
fn arima_airline_matches_r_fixture() {
    let fx = common::load_fixture("r_forecast/air_passengers.json");
    let values: Vec<Option<f64>> = common::f64_vec(&fx["inputs"]["values"])
        .into_iter()
        .map(Some)
        .collect();
    let r_points = common::f64_vec(&fx["models"]["arima_airline"]["point"]);
    let rel_tol = common::tolerance(&fx, "model_point_rel");

    let options = ForecastOptions {
        model: ModelType::ARIMA,
        horizon: 12,
        seasonal_period: 12,
        auto_detect_seasonality: false,
        arima_order: Some([0, 1, 1]),
        seasonal_order: Some([0, 1, 1]),
        ..Default::default()
    };

    let result = forecast(&values, &options).expect("ARIMA airline fit should succeed");
    assert_eq!(result.model_name, "ARIMA(0,1,1)(0,1,1)[12]");
    common::assert_vec_close(
        &result.point,
        &r_points,
        0.0,
        rel_tol,
        "arima_airline.point",
    );

    assert_eq!(result.lower.len(), result.point.len());
    assert_eq!(result.upper.len(), result.point.len());
    for i in 0..result.point.len() {
        assert!(
            result.lower[i] < result.point[i] && result.point[i] < result.upper[i],
            "step {i}: expected lower < point < upper, got {} < {} < {}",
            result.lower[i],
            result.point[i],
            result.upper[i]
        );
    }
}

/// FCST-04: non-seasonal ARIMA(1,0,0) and ARIMA(1,0,1) on the seeded
/// level-10 AR(1) series (`simulated_series.json#series.ar1_l10`) must
/// match R `Arima()` at the same order within `model_point_rel`.
/// model_name is asserted with `starts_with` because d=0 commonly carries
/// a non-zero-mean suffix (the crate's own intercept, which R always
/// shows via `include.mean`).
#[test]
fn arima_100_and_101_match_r_fixture() {
    let fx = common::load_fixture("r_forecast/simulated_series.json");
    let series = &fx["series"]["ar1_l10"];
    let values: Vec<Option<f64>> = common::f64_vec(&series["inputs"]["values"])
        .into_iter()
        .map(Some)
        .collect();
    let rel_tol = common::tolerance(&fx, "model_point_rel");

    let cases: [(&str, [usize; 3]); 2] = [("arima_100", [1, 0, 0]), ("arima_101", [1, 0, 1])];

    for (model_key, order) in cases {
        let r_points = common::f64_vec(&series["models"][model_key]["point"]);
        let options = ForecastOptions {
            model: ModelType::ARIMA,
            horizon: 12,
            arima_order: Some(order),
            ..Default::default()
        };
        let result = forecast(&values, &options)
            .unwrap_or_else(|e| panic!("ARIMA{order:?} fit should succeed, got {e}"));
        let expected_prefix = format!("ARIMA({},{},{})", order[0], order[1], order[2]);
        assert!(
            result.model_name.starts_with(&expected_prefix),
            "expected model_name to start with '{expected_prefix}', got '{}'",
            result.model_name
        );
        common::assert_vec_close(
            &result.point,
            &r_points,
            0.0,
            rel_tol,
            &format!("{model_key}.point"),
        );
    }
}

// ── Error behavior (D-09) ────────────────────────────────────────────────

fn trending_values() -> Vec<Option<f64>> {
    (0..30)
        .map(|i| Some(100.0 + i as f64 * 2.0 + (i % 3) as f64))
        .collect()
}

#[test]
fn arima_without_order_errors_naming_auto_arima() {
    let options = ForecastOptions {
        model: ModelType::ARIMA,
        horizon: 5,
        ..Default::default()
    };
    let err = forecast(&trending_values(), &options).unwrap_err();
    assert!(matches!(err, ForecastError::InvalidInput(_)));
    let msg = err.to_string();
    assert!(
        msg.contains("arima_order") && msg.contains("AutoARIMA"),
        "expected message to name arima_order and AutoARIMA, got: {msg}"
    );
}

#[test]
fn seasonal_order_without_arima_order_errors() {
    let options = ForecastOptions {
        model: ModelType::ARIMA,
        horizon: 5,
        seasonal_order: Some([0, 1, 1]),
        seasonal_period: 12,
        auto_detect_seasonality: false,
        ..Default::default()
    };
    let err = forecast(&trending_values(), &options).unwrap_err();
    assert!(matches!(err, ForecastError::InvalidInput(_)));
}

#[test]
fn seasonal_order_nonzero_without_seasonal_period_errors() {
    let options = ForecastOptions {
        model: ModelType::ARIMA,
        horizon: 5,
        arima_order: Some([1, 1, 1]),
        seasonal_order: Some([0, 1, 1]),
        seasonal_period: 0,
        ..Default::default()
    };
    let err = forecast(&trending_values(), &options).unwrap_err();
    assert!(matches!(err, ForecastError::InvalidInput(_)));
    assert!(err.to_string().contains("seasonal_period > 1"));
}

#[test]
fn seasonal_period_without_seasonal_order_errors() {
    let options = ForecastOptions {
        model: ModelType::ARIMA,
        horizon: 5,
        arima_order: Some([1, 1, 1]),
        seasonal_period: 12,
        auto_detect_seasonality: false,
        ..Default::default()
    };
    let err = forecast(&trending_values(), &options).unwrap_err();
    assert!(matches!(err, ForecastError::InvalidInput(_)));
    assert!(err.to_string().contains("seasonal_order"));
}

#[test]
fn arima_order_on_non_arima_model_is_invalid_parameter() {
    let options = ForecastOptions {
        model: ModelType::AutoARIMA,
        horizon: 5,
        arima_order: Some([1, 1, 1]),
        ..Default::default()
    };
    let err = forecast(&trending_values(), &options).unwrap_err();
    match err {
        ForecastError::InvalidParameter { param, reason, .. } => {
            assert_eq!(param, "arima_order");
            assert!(reason.contains("ARIMA"));
        }
        other => panic!("expected InvalidParameter, got {other:?}"),
    }
}

// ── parse_order_triplet (D-08) ───────────────────────────────────────────

#[test]
fn parse_order_triplet_valid_inputs() {
    assert_eq!(
        parse_order_triplet("arima_order", "[0,1,1]").unwrap(),
        Some([0, 1, 1])
    );
    assert_eq!(
        parse_order_triplet("arima_order", " 0, 1 ,1 ").unwrap(),
        Some([0, 1, 1])
    );
    assert_eq!(parse_order_triplet("arima_order", "").unwrap(), None);
    assert_eq!(parse_order_triplet("arima_order", "   ").unwrap(), None);
}

#[test]
fn parse_order_triplet_invalid_inputs() {
    for bad in [
        "[1,2]",
        "[1,-1,0]",
        "[11,0,0]",
        "[0,3,0]",
        "[a,b,c]",
        "[1.5,0,0]",
    ] {
        let result = parse_order_triplet("arima_order", bad);
        assert!(
            matches!(result, Err(ForecastError::InvalidParameter { .. })),
            "expected InvalidParameter for input {bad:?}, got {result:?}"
        );
    }
}

// ── parse_seasonal_periods (D-07 strict replacement) ─────────────────────

#[test]
fn parse_seasonal_periods_valid_inputs() {
    assert_eq!(parse_seasonal_periods("[7, 365]").unwrap(), vec![7, 365]);
    assert_eq!(parse_seasonal_periods("").unwrap(), Vec::<usize>::new());
    assert_eq!(parse_seasonal_periods("   ").unwrap(), Vec::<usize>::new());
}

#[test]
fn parse_seasonal_periods_invalid_inputs() {
    for bad in ["[7, abc]", "[1]", "[0]"] {
        let result = parse_seasonal_periods(bad);
        assert!(
            matches!(result, Err(ForecastError::InvalidParameter { .. })),
            "expected InvalidParameter for input {bad:?}, got {result:?}"
        );
    }
}
