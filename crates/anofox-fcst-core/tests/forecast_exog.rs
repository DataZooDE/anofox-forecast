//! D-10/FCST-05/FCST-08 tests: the exogenous forecasting path (`forecast_with_exog`)
//! routes exog-capable models through the crate's OWN exog support
//! (`CalendarAnnotations` + `predict_with_exog_intervals`) instead of the
//! wrapper OLS-on-residuals hack, gives a regressor constant in history a 0
//! effect, and reports the real model identity (e.g. the AutoARIMA selected
//! order) with a `" with regressors"` suffix — never a fixed stand-in name
//! like "ARIMAX".
//!
//! Task 1 (tracer) scope: AutoARIMA only — `autoarima_exog_runs_order_search`
//! proves the crate's own order search runs WITH the regressor attached and
//! is bit-identical to the crate fitted/predicted directly the same way.
//! Task 2 generalises this to every exog-capable model and adds the
//! constant-regressor / NULL-policy / non-exog-model-error coverage.

mod common;

use anofox_fcst_core::forecast::{
    forecast, forecast_with_exog, ExogenousData, ForecastOptions, ForecastOptionsExog, ModelType,
};
use anofox_forecast::core::{CalendarAnnotations, TimeSeries};
use anofox_forecast::models::arima::{AutoARIMA, AutoARIMAConfig};
use anofox_forecast::prelude::Forecaster;
use chrono::{Duration, TimeZone, Utc};
use std::collections::HashMap;

/// Matches `^AutoARIMA\(\d+,\d+,\d+\)(\(\d+,\d+,\d+\)\[\d+\])? with regressors$`
/// without pulling in a `regex` dev-dependency.
fn is_digit_triplet(s: &str) -> bool {
    let parts: Vec<&str> = s.split(',').collect();
    parts.len() == 3
        && parts
            .iter()
            .all(|p| !p.is_empty() && p.chars().all(|c| c.is_ascii_digit()))
}

fn matches_autoarima_with_regressors_name(name: &str) -> bool {
    let Some(before) = name.strip_suffix(" with regressors") else {
        return false;
    };
    let Some(rest) = before.strip_prefix("AutoARIMA(") else {
        return false;
    };
    let Some(close) = rest.find(')') else {
        return false;
    };
    let (order, rest) = (&rest[..close], &rest[close + 1..]);
    if !is_digit_triplet(order) {
        return false;
    }
    if rest.is_empty() {
        return true;
    }
    let Some(rest) = rest.strip_prefix('(') else {
        return false;
    };
    let Some(close2) = rest.find(')') else {
        return false;
    };
    let (seasonal_order, rest) = (&rest[..close2], &rest[close2 + 1..]);
    if !is_digit_triplet(seasonal_order) {
        return false;
    }
    let Some(rest) = rest.strip_prefix('[') else {
        return false;
    };
    let Some(close3) = rest.find(']') else {
        return false;
    };
    let (s, rest) = (&rest[..close3], &rest[close3 + 1..]);
    rest.is_empty() && !s.is_empty() && s.chars().all(|c| c.is_ascii_digit())
}

fn make_timestamps(n: usize) -> Vec<chrono::DateTime<Utc>> {
    let base = Utc.with_ymd_and_hms(2024, 1, 1, 0, 0, 0).unwrap();
    (0..n).map(|i| base + Duration::hours(i as i64)).collect()
}

/// Deterministic `y = 50 + 3*x1 + AR(1)-style noise` over `n` points, plus a
/// deterministic non-constant regressor `x1` extended `extra` points beyond
/// `n` (used as the future regressor window in these tests).
fn generate_exog_series(n: usize, extra: usize) -> (Vec<f64>, Vec<f64>) {
    let total = n + extra;
    let mut x1 = Vec::with_capacity(total);
    for i in 0..total {
        x1.push((i as f64 * 0.37).sin() + i as f64 * 0.01);
    }

    let mut y = Vec::with_capacity(n);
    let mut ar = 0.0_f64;
    let ar_coef = 0.5_f64;
    for (i, &x1_val) in x1.iter().take(n).enumerate() {
        let noise = (((i as u64 + 1).wrapping_mul(97) % 1000) as f64 - 500.0) / 500.0;
        ar = ar_coef * ar + noise;
        y.push(50.0 + 3.0 * x1_val + ar);
    }

    (y, x1)
}

/// AutoARIMA with regressors runs the crate's OWN exog-aware order search
/// (not a fixed stand-in): `model_name` carries the real selected order
/// followed by `" with regressors"`, and point/lower/upper are bit-identical
/// to the crate `AutoARIMA` fitted directly on a `TimeSeries` carrying the
/// same regressor via `CalendarAnnotations` and predicted with
/// `predict_with_exog_intervals` — proving `forecast_with_exog` does not
/// recompute anything of its own.
#[test]
fn autoarima_exog_runs_order_search() {
    let n = 120;
    let horizon = 6;
    let (y, x1) = generate_exog_series(n, horizon);
    let (x1_hist, x1_future) = (x1[..n].to_vec(), x1[n..].to_vec());

    // ── Through forecast_with_exog ──────────────────────────────────────
    let values: Vec<Option<f64>> = y.iter().copied().map(Some).collect();
    let exog = ExogenousData::new(vec![x1_hist.clone()], vec![x1_future.clone()]);
    let options = ForecastOptionsExog {
        model: ModelType::AutoARIMA,
        horizon,
        auto_detect_seasonality: false,
        seasonal_period: 0,
        exog: Some(exog),
        ..Default::default()
    };
    let result = forecast_with_exog(&values, &options)
        .expect("AutoARIMA with one informative regressor should fit and predict");

    assert!(
        matches_autoarima_with_regressors_name(&result.model_name),
        "model_name '{}' does not match AutoARIMA(...) with regressors",
        result.model_name
    );
    assert_eq!(result.point.len(), horizon);
    for v in &result.point {
        assert!(
            v.is_finite(),
            "AutoARIMA-with-regressors point must be finite"
        );
    }

    // ── Direct crate fit/predict with the SAME regressor attached ───────
    let ts_timestamps = make_timestamps(n);
    let calendar = CalendarAnnotations::new().with_regressor("x000".to_string(), x1_hist.clone());
    let mut ts = TimeSeries::univariate(ts_timestamps, y.clone()).expect("valid TimeSeries");
    ts.set_calendar(calendar);

    let mut direct_model = AutoARIMA::with_config(AutoARIMAConfig::default());
    direct_model
        .fit(&ts)
        .expect("direct AutoARIMA fit with regressor should succeed");

    let mut future_map = HashMap::new();
    future_map.insert("x000".to_string(), x1_future.clone());
    let direct_forecast = direct_model
        .predict_with_exog_intervals(horizon, &future_map, 0.95)
        .expect("direct AutoARIMA predict_with_exog_intervals should succeed");

    let direct_point = direct_forecast.primary().to_vec();
    let direct_lower = direct_forecast
        .lower()
        .and_then(|i| i.first())
        .cloned()
        .unwrap_or_default();
    let direct_upper = direct_forecast
        .upper()
        .and_then(|i| i.first())
        .cloned()
        .unwrap_or_default();

    assert_eq!(
        result.point, direct_point,
        "forecast_with_exog point must be bit-identical to the direct crate fit/predict"
    );
    if direct_lower.len() == direct_point.len() {
        assert_eq!(
            result.lower, direct_lower,
            "forecast_with_exog lower bound must be bit-identical to the direct crate fit/predict"
        );
    }
    if direct_upper.len() == direct_point.len() {
        assert_eq!(
            result.upper, direct_upper,
            "forecast_with_exog upper bound must be bit-identical to the direct crate fit/predict"
        );
    }
}

/// OptimizedTheta/DynamicTheta/DynamicOptimizedTheta/AutoTheta with
/// regressors stay THEMSELVES (never silently substituted with plain
/// Theta): model_name is `"<Model> with regressors"` (AutoTheta reports the
/// selected variant: `"AutoTheta(<selected>) with regressors"`), and point
/// forecasts are bit-identical to the crate model fitted directly with the
/// same regressor via `CalendarAnnotations`.
#[test]
fn optimized_theta_stays_itself() {
    use anofox_forecast::models::theta::{AutoTheta, DynamicTheta, OptimizedTheta};

    let n = 80;
    let horizon = 4;
    let (y, x1) = generate_exog_series(n, horizon);
    let (x1_hist, x1_future) = (x1[..n].to_vec(), x1[n..].to_vec());
    let values: Vec<Option<f64>> = y.iter().copied().map(Some).collect();

    let ts_timestamps = make_timestamps(n);
    let mut future_map = HashMap::new();
    future_map.insert("x000".to_string(), x1_future.clone());

    let make_ts = || {
        let calendar =
            CalendarAnnotations::new().with_regressor("x000".to_string(), x1_hist.clone());
        let mut ts =
            TimeSeries::univariate(ts_timestamps.clone(), y.clone()).expect("valid TimeSeries");
        ts.set_calendar(calendar);
        ts
    };

    let run_exog = |model: ModelType| {
        let exog = ExogenousData::new(vec![x1_hist.clone()], vec![x1_future.clone()]);
        let options = ForecastOptionsExog {
            model,
            horizon,
            auto_detect_seasonality: false,
            seasonal_period: 0,
            exog: Some(exog),
            ..Default::default()
        };
        forecast_with_exog(&values, &options)
            .unwrap_or_else(|e| panic!("{:?} with regressor should succeed: {e}", model))
    };

    // OptimizedTheta
    let result = run_exog(ModelType::OptimizedTheta);
    assert_eq!(result.model_name, "OptimizedTheta with regressors");
    let mut direct = OptimizedTheta::new();
    direct.fit(&make_ts()).expect("direct OptimizedTheta fit");
    let direct_forecast = direct
        .predict_with_exog_intervals(horizon, &future_map, 0.95)
        .expect("direct OptimizedTheta predict_with_exog_intervals");
    assert_eq!(result.point, direct_forecast.primary().to_vec());

    // DynamicTheta
    let result = run_exog(ModelType::DynamicTheta);
    assert_eq!(result.model_name, "DynamicTheta with regressors");
    let mut direct = DynamicTheta::new(0.1);
    direct.fit(&make_ts()).expect("direct DynamicTheta fit");
    let direct_forecast = direct
        .predict_with_exog_intervals(horizon, &future_map, 0.95)
        .expect("direct DynamicTheta predict_with_exog_intervals");
    assert_eq!(result.point, direct_forecast.primary().to_vec());

    // DynamicOptimizedTheta
    let result = run_exog(ModelType::DynamicOptimizedTheta);
    assert_eq!(result.model_name, "DynamicOptimizedTheta with regressors");
    let mut direct = DynamicTheta::optimized();
    direct
        .fit(&make_ts())
        .expect("direct DynamicOptimizedTheta fit");
    let direct_forecast = direct
        .predict_with_exog_intervals(horizon, &future_map, 0.95)
        .expect("direct DynamicOptimizedTheta predict_with_exog_intervals");
    assert_eq!(result.point, direct_forecast.primary().to_vec());

    // AutoTheta: model_name reports the selected variant, never plain "Theta".
    let result = run_exog(ModelType::AutoTheta);
    assert!(
        result.model_name.starts_with("AutoTheta(")
            && result.model_name.ends_with(" with regressors"),
        "AutoTheta model_name '{}' should be 'AutoTheta(<selected>) with regressors'",
        result.model_name
    );
    let mut direct = AutoTheta::new();
    direct.fit(&make_ts()).expect("direct AutoTheta fit");
    let direct_forecast = direct
        .predict_with_exog_intervals(horizon, &future_map, 0.95)
        .expect("direct AutoTheta predict_with_exog_intervals");
    assert_eq!(result.point, direct_forecast.primary().to_vec());
}

/// ETS with an explicit `ets_spec` + a regressor reports `"ETS(A,A,N) with
/// regressors"`; ARIMA with `arima_order` + a regressor reports a name
/// starting `"ARIMA(1,0,0)"` and ending `" with regressors"`; MFLES and MSTL
/// with regressors run (MSTL: point-only, no native exog interval override —
/// D-02 empty bounds).
#[test]
fn ets_spec_with_regressors() {
    let n = 80;
    let horizon = 4;
    let (y, x1) = generate_exog_series(n, horizon);
    let (x1_hist, x1_future) = (x1[..n].to_vec(), x1[n..].to_vec());
    let values: Vec<Option<f64>> = y.iter().copied().map(Some).collect();

    let run_exog = |model: ModelType,
                    ets_spec: Option<&str>,
                    arima_order: Option<[usize; 3]>,
                    seasonal_period: usize| {
        let exog = ExogenousData::new(vec![x1_hist.clone()], vec![x1_future.clone()]);
        let options = ForecastOptionsExog {
            model,
            horizon,
            auto_detect_seasonality: false,
            seasonal_period,
            ets_spec: ets_spec.map(str::to_string),
            arima_order,
            exog: Some(exog),
            ..Default::default()
        };
        forecast_with_exog(&values, &options)
            .unwrap_or_else(|e| panic!("{:?} with regressor should succeed: {e}", model))
    };

    let result = run_exog(ModelType::ETS, Some("AAN"), None, 0);
    assert_eq!(result.model_name, "ETS(A,A,N) with regressors");
    for v in &result.point {
        assert!(v.is_finite());
    }

    let result = run_exog(ModelType::ARIMA, None, Some([1, 0, 0]), 0);
    assert!(
        result.model_name.starts_with("ARIMA(1,0,0)")
            && result.model_name.ends_with(" with regressors"),
        "ARIMA model_name '{}' should start with ARIMA(1,0,0) and end with ' with regressors'",
        result.model_name
    );

    let result = run_exog(ModelType::MFLES, None, None, 0);
    assert_eq!(result.model_name, "MFLES with regressors");
    assert_eq!(result.point.len(), horizon);

    // MSTL requires an explicit seasonal period >= 2 (forecast_with_exog's
    // exog path does not auto-detect for MSTL — D-07's auto-detection is
    // forecast()'s non-exog scope); a small period is enough to exercise
    // the dispatch, not to produce a meaningful decomposition.
    let result = run_exog(ModelType::MSTL, None, None, 4);
    assert_eq!(result.model_name, "MSTL with regressors");
    assert_eq!(result.point.len(), horizon);
    assert!(
        result.lower.is_empty() && result.upper.is_empty(),
        "MSTL with regressors has no native exog interval override (D-02): expected empty bounds"
    );
}

/// A regressor constant in history (0 effect by construction, D-10): adding
/// `x2` (0 in history, 1 in the future) to a forecast that already has `x1`
/// leaves every point identical (and finite) to the `[x1]`-only run. When
/// EVERY regressor is constant in history, the result is identical to
/// `forecast()` without regressors at all (no `" with regressors"` suffix).
#[test]
fn constant_regressor_has_zero_effect() {
    let n = 80;
    let horizon = 4;
    let (y, x1) = generate_exog_series(n, horizon);
    let (x1_hist, x1_future) = (x1[..n].to_vec(), x1[n..].to_vec());
    let values: Vec<Option<f64>> = y.iter().copied().map(Some).collect();

    // [x1] only
    let exog_x1 = ExogenousData::new(vec![x1_hist.clone()], vec![x1_future.clone()]);
    let options_x1 = ForecastOptionsExog {
        model: ModelType::OptimizedTheta,
        horizon,
        auto_detect_seasonality: false,
        seasonal_period: 0,
        exog: Some(exog_x1),
        ..Default::default()
    };
    let result_x1 =
        forecast_with_exog(&values, &options_x1).expect("[x1]-only forecast should succeed");

    // [x1, x2] where x2 = 0 in history, 1 in the future.
    let x2_hist = vec![0.0; n];
    let x2_future = vec![1.0; horizon];
    let exog_both = ExogenousData::new(
        vec![x1_hist.clone(), x2_hist],
        vec![x1_future.clone(), x2_future],
    );
    let options_both = ForecastOptionsExog {
        exog: Some(exog_both),
        ..options_x1.clone()
    };
    let result_both =
        forecast_with_exog(&values, &options_both).expect("[x1, x2] forecast should succeed");

    assert_eq!(
        result_x1.model_name, result_both.model_name,
        "dropping the constant x2 regressor must not change the reported model identity"
    );
    for (i, (a, b)) in result_x1
        .point
        .iter()
        .zip(result_both.point.iter())
        .enumerate()
    {
        assert!(
            a.is_finite() && b.is_finite(),
            "point[{i}] must be finite in both runs"
        );
        assert_eq!(
            a, b,
            "point[{i}]: constant-in-history x2 must have exactly 0 effect"
        );
    }

    // Every regressor constant in history → identical to forecast() without
    // regressors, no suffix.
    let x3_hist = vec![7.0; n];
    let x3_future = vec![7.0; horizon];
    let exog_all_constant = ExogenousData::new(vec![x3_hist], vec![x3_future]);
    let options_all_constant = ForecastOptionsExog {
        exog: Some(exog_all_constant),
        ..options_x1.clone()
    };
    let result_all_constant = forecast_with_exog(&values, &options_all_constant)
        .expect("all-constant-regressor forecast should succeed");

    let plain_options = ForecastOptions {
        model: ModelType::OptimizedTheta,
        horizon,
        auto_detect_seasonality: false,
        seasonal_period: 0,
        ..Default::default()
    };
    let plain_result = forecast(&values, &plain_options).expect("plain forecast() should succeed");

    assert_eq!(result_all_constant.model_name, plain_result.model_name);
    assert!(
        !result_all_constant.model_name.contains("with regressors"),
        "all-constant-regressor result should carry no ' with regressors' suffix, got '{}'",
        result_all_constant.model_name
    );
    assert_eq!(result_all_constant.point, plain_result.point);
}

/// A non-exog-capable model (HoltWinters, SES, GARCH, Kalman, Croston) given
/// at least one non-constant regressor is a hard `InvalidInput` naming the
/// problem — never a silent ignore of the regressors (D-04 spirit).
#[test]
fn non_exog_model_with_regressors_errors() {
    let n = 80;
    let horizon = 4;
    let (y, x1) = generate_exog_series(n, horizon);
    let (x1_hist, x1_future) = (x1[..n].to_vec(), x1[n..].to_vec());
    let values: Vec<Option<f64>> = y.iter().copied().map(Some).collect();

    for model in [
        ModelType::HoltWinters,
        ModelType::SES,
        ModelType::GARCH,
        ModelType::Kalman,
        ModelType::CrostonClassic,
    ] {
        let exog = ExogenousData::new(vec![x1_hist.clone()], vec![x1_future.clone()]);
        let options = ForecastOptionsExog {
            model,
            horizon,
            auto_detect_seasonality: false,
            seasonal_period: if model == ModelType::HoltWinters {
                12
            } else {
                0
            },
            exog: Some(exog),
            ..Default::default()
        };
        let err = forecast_with_exog(&values, &options)
            .expect_err(&format!("{:?} with a regressor should error", model));
        assert!(
            err.to_string()
                .contains("does not support exogenous regressors"),
            "{:?} error message '{}' should mention 'does not support exogenous regressors'",
            model,
            err
        );
    }
}

/// Regressor NULL policy (D-12, FCST-08): an interior historical NaN is
/// interpolated and the forecast succeeds; an all-NaN historical regressor
/// is a hard error naming the regressor; a future NaN at a specific step is
/// a hard error naming the regressor and step; ±Inf anywhere in a regressor
/// is a hard error.
#[test]
fn regressor_nan_policy() {
    let n = 80;
    let horizon = 4;
    let (y, x1) = generate_exog_series(n, horizon);
    let (x1_hist, x1_future) = (x1[..n].to_vec(), x1[n..].to_vec());
    let values: Vec<Option<f64>> = y.iter().copied().map(Some).collect();

    let base_options = |exog: ExogenousData| ForecastOptionsExog {
        model: ModelType::OptimizedTheta,
        horizon,
        auto_detect_seasonality: false,
        seasonal_period: 0,
        exog: Some(exog),
        ..Default::default()
    };

    // Interior historical NaN: interpolated, forecast succeeds.
    let mut hist_with_interior_nan = x1_hist.clone();
    hist_with_interior_nan[n / 2] = f64::NAN;
    let exog = ExogenousData::new(vec![hist_with_interior_nan], vec![x1_future.clone()]);
    forecast_with_exog(&values, &base_options(exog))
        .expect("interior historical NaN should be interpolated, not an error");

    // All-NaN historical regressor: hard error naming the regressor.
    let all_nan_hist = vec![f64::NAN; n];
    let exog = ExogenousData::new(vec![all_nan_hist], vec![x1_future.clone()]);
    let err = forecast_with_exog(&values, &base_options(exog))
        .expect_err("all-NaN historical regressor should error");
    assert!(
        err.to_string().contains("regressor 0") && err.to_string().contains("historical"),
        "error '{err}' should name the regressor and mention 'historical'"
    );

    // Future NaN at step 2: hard error naming the regressor and step.
    let mut future_with_nan = x1_future.clone();
    future_with_nan[2] = f64::NAN;
    let exog = ExogenousData::new(vec![x1_hist.clone()], vec![future_with_nan]);
    let err =
        forecast_with_exog(&values, &base_options(exog)).expect_err("future NaN should error");
    let msg = err.to_string();
    assert!(
        msg.contains("regressor 0") && msg.contains("step 2"),
        "error '{msg}' should name the regressor and the future step"
    );

    // ±Inf anywhere: hard error.
    let mut hist_with_inf = x1_hist.clone();
    hist_with_inf[3] = f64::INFINITY;
    let exog = ExogenousData::new(vec![hist_with_inf], vec![x1_future.clone()]);
    let err = forecast_with_exog(&values, &base_options(exog))
        .expect_err("±Inf in a regressor should error");
    assert!(
        err.to_string().contains("infinite"),
        "error '{}' should mention 'infinite'",
        err
    );
}

/// y with 2 trailing NULL rows (D-12): the crate is asked for
/// `horizon + trail_trim` steps, with the future regressor map built from
/// the 2 trimmed historical regressor rows followed by the user's future
/// rows — the returned point still has length `horizon` (trimmed/realigned
/// by `finalize_output`).
#[test]
fn trailing_y_null_uses_trailing_regressors() {
    let n = 80;
    let horizon = 4;
    let trailing_nulls = 2;
    let (mut y, x1) = generate_exog_series(n, horizon);
    let (x1_hist, x1_future) = (x1[..n].to_vec(), x1[n..].to_vec());

    // Trim the last `trailing_nulls` y values to NULL (x1_hist stays full
    // length n; align_exog is responsible for re-slicing/extending it).
    let mut values: Vec<Option<f64>> = y.drain(..).map(Some).collect();
    for v in values.iter_mut().rev().take(trailing_nulls) {
        *v = None;
    }

    let exog = ExogenousData::new(vec![x1_hist], vec![x1_future]);
    let options = ForecastOptionsExog {
        model: ModelType::OptimizedTheta,
        horizon,
        auto_detect_seasonality: false,
        seasonal_period: 0,
        exog: Some(exog),
        ..Default::default()
    };

    let result = forecast_with_exog(&values, &options)
        .expect("trailing-NULL y with regressors should succeed");
    assert_eq!(
        result.point.len(),
        horizon,
        "returned point must have length horizon regardless of trailing NULLs"
    );
    for v in &result.point {
        assert!(v.is_finite());
    }
}

/// DEP-01 regression-effect proof (replaces the deleted
/// `fit_ols_regression_matches_numpy_lstsq_fixture`): OptimizedTheta on the
/// `multiscale_ols` `exog_typical` fixture (365 history rows, 4 regressors,
/// 5 future rows) produces points within `tolerance.sql_point_abs` of
/// `expected_regression` (or, absent that key, the fixture's `[1, x] . coef`
/// projection — the same before/after evidence the deleted test pinned).
#[test]
fn exog_typical_regression_effect() {
    let fx = common::load_fixture("multiscale_ols/cases.json");
    let case = &fx["cases"]["exog_typical"];
    let sql_point_abs = common::tolerance(&fx, "sql_point_abs");

    let x_cols: Vec<Vec<f64>> = case["X"]
        .as_array()
        .expect("exog_typical: 'X' is not an array")
        .iter()
        .map(common::f64_vec)
        .collect();
    let y = common::f64_vec(&case["y"]);
    let coef = common::f64_vec(&case["coef"]);

    let horizon = 5;
    let historical = x_cols.clone();
    let future: Vec<Vec<f64>> = x_cols.iter().map(|col| col[0..horizon].to_vec()).collect();

    let values: Vec<Option<f64>> = y.iter().copied().map(Some).collect();
    let exog = ExogenousData::new(historical, future.clone());
    let options = ForecastOptionsExog {
        model: ModelType::OptimizedTheta,
        horizon,
        auto_detect_seasonality: false,
        seasonal_period: 0,
        exog: Some(exog),
        ..Default::default()
    };

    let result = forecast_with_exog(&values, &options)
        .expect("OptimizedTheta on exog_typical should succeed");
    assert_eq!(result.point.len(), horizon);

    let want_projection: Vec<f64> = case
        .get("expected_regression")
        .and_then(|v| v.as_array())
        .map(|arr| arr.iter().map(|v| v.as_f64().unwrap()).collect())
        .unwrap_or_else(|| {
            (0..horizon)
                .map(|h| {
                    let mut v = coef[0];
                    for (j, col) in future.iter().enumerate() {
                        v += coef[j + 1] * col[h];
                    }
                    v
                })
                .collect()
        });

    for (i, (p, want)) in result.point.iter().zip(want_projection.iter()).enumerate() {
        assert!(p.is_finite(), "point[{i}] is not finite: {p}");
        common::assert_close(*p, *want, sql_point_abs, 0.0, &format!("point[{i}]"));
    }
}

/// WR-01 regression: `forecast_crate_exog`'s regressors-present branch must
/// reject `arima_order`/`seasonal_order` set on any model other than ARIMA —
/// the same guard `forecast()` applies (D-04) — instead of silently
/// ignoring the parameter. Uses ETS (exog-capable, not ARIMA) with a real
/// regressor so execution reaches `forecast_crate_exog` directly rather than
/// delegating to `forecast()` via the no-regressor `.into()` branch.
#[test]
fn arima_order_on_non_arima_model_errors_on_exog_path() {
    let n = 40;
    let horizon = 4;
    let (y, x1) = generate_exog_series(n, horizon);
    let (x1_hist, x1_future) = (x1[..n].to_vec(), x1[n..].to_vec());
    let values: Vec<Option<f64>> = y.iter().copied().map(Some).collect();
    let exog = ExogenousData::new(vec![x1_hist], vec![x1_future]);

    let options = ForecastOptionsExog {
        model: ModelType::ETS,
        horizon,
        ets_spec: Some("AAN".to_string()),
        arima_order: Some([1, 1, 1]),
        exog: Some(exog),
        ..Default::default()
    };

    let err = forecast_with_exog(&values, &options).unwrap_err();
    let msg = err.to_string();
    assert!(
        msg.contains("arima_order") && msg.to_lowercase().contains("ets"),
        "expected error naming arima_order and ETS, got: {msg}"
    );
}
