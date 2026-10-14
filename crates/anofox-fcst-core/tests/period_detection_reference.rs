//! Reference-fixture assertions for `periods::{detrended_periodogram,
//! estimate_period_fft_ts, acf, estimate_period_acf_ts,
//! regression_period_rss, estimate_period_regression_ts, lomb_scargle_power,
//! lomb_scargle}` and `seasonality`'s detrend-first strength functions
//! (Phase 15 plan 15-05, DET-05/DET-06: D-11, D-12).
//!
//! Fixture: `period_detection/cases.json`
//! (`validation/reference/python/gen_period_detection_reference.py`).
//!
//! - `periodogram_matches_scipy`: `detrended_periodogram`'s one-sided power
//!   vector against `scipy.signal.periodogram(detrend='linear',
//!   scaling='spectrum')`, plus `estimate_period_fft_ts`'s selected
//!   period/confidence against the fixture's own selection-rule output
//!   (G18).
//! - `acf_matches_statsmodels`: `acf()` against `statsmodels.tsa.stattools
//!   .acf(fft=False, adjusted=False)`, plus `estimate_period_acf_ts`'s
//!   selected period (G17).
//! - `regression_rss_matches_numpy_lstsq`: `regression_period_rss` against
//!   `numpy.linalg.lstsq` RSS per candidate, plus
//!   `estimate_period_regression_ts`'s selected period (G20).
//! - `lomb_scargle_power_matches_scipy`: `lomb_scargle_power` against
//!   `scipy.signal.lombscargle(..., normalize=True) * n/2` over the exact
//!   frequency grid `lomb_scargle` itself builds, plus the resulting best
//!   period (G21).
//! - `strength_detrend_matches_scipy`: the `detrend_linear` residual fed to
//!   `seasonal_strength_variance`/`_spectral` against
//!   `scipy.signal.detrend(type='linear')` (G19).

mod common;

use anofox_fcst_core::{
    acf, detrend_linear, detrended_periodogram, estimate_period_acf_ts, estimate_period_fft_ts,
    estimate_period_regression_ts, lomb_scargle, lomb_scargle_power, regression_period_rss,
};
use common::{assert_close, assert_vec_close, f64_vec, load_fixture, num, tolerance};
use serde_json::Value;

fn opt_f64(v: &Value) -> Option<f64> {
    match v {
        Value::Null => None,
        other => Some(num(other)),
    }
}

#[test]
fn periodogram_matches_scipy() {
    let fx = load_fixture("period_detection/cases.json");
    let power_rel = tolerance(&fx, "power_rel");
    let power_abs = tolerance(&fx, "power_abs");

    let cases = fx
        .get("periodogram_cases")
        .and_then(|c| c.as_object())
        .expect("period_detection/cases.json: 'periodogram_cases' is not an object");

    for (name, case) in cases {
        let x = f64_vec(&case["x"]);
        let want_power = f64_vec(&case["expected"]["power"]);
        let want_period = opt_f64(&case["expected"]["period"]);
        let want_confidence = num(&case["expected"]["confidence"]);

        let got_power = detrended_periodogram(&x)
            .unwrap_or_else(|e| panic!("{name}: detrended_periodogram() failed: {e:?}"));
        assert_vec_close(
            &got_power,
            &want_power,
            power_abs,
            power_rel,
            &format!("{name}: periodogram power"),
        );

        let got = estimate_period_fft_ts(&x)
            .unwrap_or_else(|e| panic!("{name}: estimate_period_fft_ts() failed: {e:?}"));
        match want_period {
            Some(p) => {
                assert!(
                    (got.period - p).abs() <= 1e-9,
                    "{name}: period got {} want {}",
                    got.period,
                    p
                );
                assert_close(
                    got.confidence,
                    want_confidence,
                    1e-6,
                    1e-9,
                    &format!("{name}: confidence"),
                );
            }
            None => {
                assert!(
                    got.period.is_nan(),
                    "{name}: expected NaN period, got {}",
                    got.period
                );
            }
        }
    }
}

#[test]
fn acf_matches_statsmodels() {
    let fx = load_fixture("period_detection/cases.json");
    let acf_abs = tolerance(&fx, "acf_abs");

    let cases = fx
        .get("acf_cases")
        .and_then(|c| c.as_object())
        .expect("period_detection/cases.json: 'acf_cases' is not an object");

    for (name, case) in cases {
        let x = f64_vec(&case["x"]);
        let max_lag = case["max_lag"].as_u64().unwrap() as usize;
        let want_acf = f64_vec(&case["expected"]["acf"]);
        let want_period = opt_f64(&case["expected"]["period"]);

        let got_acf = acf(&x, max_lag);
        assert_vec_close(&got_acf, &want_acf, acf_abs, 0.0, &format!("{name}: acf"));

        let got = estimate_period_acf_ts(&x, Some(max_lag))
            .unwrap_or_else(|e| panic!("{name}: estimate_period_acf_ts() failed: {e:?}"));
        match want_period {
            Some(p) => {
                assert_eq!(got.period, p, "{name}: period");
            }
            None => {
                assert!(
                    got.period.is_nan(),
                    "{name}: expected NaN period, got {}",
                    got.period
                );
            }
        }
    }
}

#[test]
fn regression_rss_matches_numpy_lstsq() {
    let fx = load_fixture("period_detection/cases.json");
    let rss_rel = tolerance(&fx, "rss_rel");
    let rss_abs = tolerance(&fx, "rss_abs");

    let cases = fx
        .get("regression_cases")
        .and_then(|c| c.as_object())
        .expect("period_detection/cases.json: 'regression_cases' is not an object");

    for (name, case) in cases {
        let x = f64_vec(&case["x"]);
        let harmonics = case["harmonics"].as_u64().unwrap() as usize;
        let candidates = f64_vec(&case["candidates"]);
        let want_rss = f64_vec(&case["expected"]["rss"]);
        let want_period = num(&case["expected"]["period"]);

        let got_rss = regression_period_rss(&x, &candidates, harmonics);
        assert_vec_close(
            &got_rss,
            &want_rss,
            rss_abs,
            rss_rel,
            &format!("{name}: rss"),
        );

        let got = estimate_period_regression_ts(&x, None, None, None, Some(harmonics))
            .unwrap_or_else(|e| panic!("{name}: estimate_period_regression_ts() failed: {e:?}"));
        assert_eq!(got.period, want_period, "{name}: period");
    }
}

#[test]
fn lomb_scargle_power_matches_scipy() {
    let fx = load_fixture("period_detection/cases.json");
    let ls_rel = tolerance(&fx, "ls_rel");

    let cases = fx
        .get("lombscargle_cases")
        .and_then(|c| c.as_object())
        .expect("period_detection/cases.json: 'lombscargle_cases' is not an object");

    for (name, case) in cases {
        let x = f64_vec(&case["x"]);
        let n = x.len();
        let times: Vec<f64> = (0..n).map(|i| i as f64).collect();
        let min_period = num(&case["min_period"]);
        let max_period = num(&case["max_period"]);
        let n_frequencies = case["n_frequencies"].as_u64().unwrap() as usize;
        let want_freqs = f64_vec(&case["expected"]["freqs"]);
        let want_power = f64_vec(&case["expected"]["power"]);
        let want_period = opt_f64(&case["expected"]["period"]);

        let got_power = lomb_scargle_power(&x, &times, &want_freqs);
        assert_vec_close(
            &got_power,
            &want_power,
            0.0,
            ls_rel,
            &format!("{name}: power"),
        );

        let got = lomb_scargle(
            &x,
            None,
            Some(min_period),
            Some(max_period),
            Some(n_frequencies),
        )
        .unwrap_or_else(|e| panic!("{name}: lomb_scargle() failed: {e:?}"));
        match want_period {
            Some(p) => {
                assert_close(got.period, p, 0.0, ls_rel, &format!("{name}: period"));
            }
            None => {
                assert!(got.period.is_nan(), "{name}: expected NaN period");
            }
        }
    }
}

#[test]
fn strength_detrend_matches_scipy() {
    let fx = load_fixture("period_detection/cases.json");
    let detrend_abs = tolerance(&fx, "detrend_abs");

    let cases = fx
        .get("strength_cases")
        .and_then(|c| c.as_object())
        .expect("period_detection/cases.json: 'strength_cases' is not an object");

    for (name, case) in cases {
        let x = f64_vec(&case["x"]);
        let want_detrended = f64_vec(&case["expected"]["detrended"]);

        let got =
            detrend_linear(&x).unwrap_or_else(|e| panic!("{name}: detrend_linear() failed: {e:?}"));
        assert_vec_close(
            &got.detrended,
            &want_detrended,
            detrend_abs,
            0.0,
            &format!("{name}: detrended"),
        );
    }
}

/// Behavior anchors (D-11, D-12) not directly encoded in the fixture's own
/// expected values -- pinned here per the plan's `<behavior>`/`must_haves`
/// blocks.
mod behavior_anchors {
    use super::*;
    use anofox_fcst_core::{seasonal_strength_spectral, seasonal_strength_variance};

    #[test]
    fn fft_trend_season_n120_period_is_12() {
        let x: Vec<f64> = (0..120)
            .map(|i| {
                let t = i as f64;
                0.5 * t + 2.0 * (2.0 * std::f64::consts::PI * t / 12.0).sin()
            })
            .collect();
        let r = estimate_period_fft_ts(&x).unwrap();
        assert!((r.period - 12.0).abs() < 1e-9, "period = {}", r.period);
        assert!(r.confidence > 5.0, "confidence = {}", r.confidence);
    }

    #[test]
    fn acf_alternating_period_is_2() {
        let x: Vec<f64> = (0..40)
            .map(|i| if i % 2 == 0 { 1.0 } else { -1.0 })
            .collect();
        let r = estimate_period_acf_ts(&x, None).unwrap();
        assert_eq!(r.period, 2.0);
    }

    #[test]
    fn seasonal_strength_variance_and_spectral_detrend_first() {
        let x: Vec<f64> = (0..120)
            .map(|i| {
                let t = i as f64;
                0.5 * t + 2.0 * (2.0 * std::f64::consts::PI * t / 12.0).sin()
            })
            .collect();
        let variance = seasonal_strength_variance(&x, 12.0, None).unwrap();
        assert!(variance >= 0.95, "variance strength = {variance}");
        let spectral = seasonal_strength_spectral(&x, 12.0).unwrap();
        assert!(spectral >= 0.9, "spectral strength = {spectral}");

        let noise: Vec<f64> = (0..120)
            .map(|i| {
                let mut state = 15800u64.wrapping_add(i as u64);
                state = state
                    .wrapping_mul(6364136223846793005)
                    .wrapping_add(1442695040888963407);
                ((state >> 11) as f64 / (1u64 << 53) as f64) * 2.0 - 1.0
            })
            .collect();
        let noise_strength = seasonal_strength_variance(&noise, 12.0, None).unwrap();
        assert!(noise_strength < 0.2, "noise strength = {noise_strength}");
    }
}
