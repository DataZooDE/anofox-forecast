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
    forecast_with_exog, ExogenousData, ForecastOptionsExog, ModelType,
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
