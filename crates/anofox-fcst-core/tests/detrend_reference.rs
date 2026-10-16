//! Reference-fixture assertions pinning `anofox_fcst_core::detrending` against
//! numpy `polyfit` (Phase 15 plan 15-01, DEP-03/WRAP-01/D-01).
//!
//! Fixture: `numpy_detrend/polyfit_cases.json`
//! (`validation/reference/python/gen_detrend_reference.py`).
//!
//! Before the fdars-core 0.47 bump (B1), every one of these assertions would
//! have failed: `detrend_linear`/`detrend_polynomial` passed `(values, n, 1,
//! ...)` to fdars-core (backwards for a single series), which silently
//! returned `trend = 0` / `detrended = input` for every method.
//!
//! For every fixture case and degree, asserts BOTH entry points:
//! - `detrend_linear`/`detrend_polynomial(.., degree)` directly, and
//! - `detrend(values, "linear"|"quadratic"|"cubic".parse()?)` through the
//!   named `DetrendMethod`, so the SQL-facing method names are pinned too.

mod common;

use anofox_fcst_core::detrending::{detrend, detrend_linear, detrend_polynomial, DetrendMethod};
use common::{assert_close, assert_vec_close, f64_vec, load_fixture, tolerance};

#[test]
fn polyfit_cases_match_numpy_for_every_degree() {
    let fx = load_fixture("numpy_detrend/polyfit_cases.json");
    let trend_abs = tolerance(&fx, "trend_abs");
    let coef_rel = tolerance(&fx, "coef_rel");
    let coef_abs = tolerance(&fx, "coef_abs");
    let rss_rel = tolerance(&fx, "rss_rel");

    let cases = fx
        .get("cases")
        .and_then(|c| c.as_array())
        .expect("numpy_detrend/polyfit_cases.json: 'cases' is not an array");

    for case in cases {
        let name = case["name"].as_str().unwrap();
        let values = f64_vec(&case["values"]);
        let degrees = case["degrees"]
            .as_object()
            .unwrap_or_else(|| panic!("{name}: 'degrees' is not an object"));

        for (degree_str, degree_fx) in degrees {
            let degree: usize = degree_str.parse().unwrap();
            let want_fitted = f64_vec(&degree_fx["fitted"]);
            let want_coefficients = f64_vec(&degree_fx["coefficients"]);
            let want_detrended = f64_vec(&degree_fx["detrended"]);
            let want_rss = degree_fx["rss"].as_f64().unwrap();
            let want_n_params = degree + 1;

            // --- 1. direct entry points ---
            let got = if degree == 1 {
                detrend_linear(&values).unwrap_or_else(|e| {
                    panic!("{name} degree {degree}: detrend_linear failed: {e:?}")
                })
            } else {
                detrend_polynomial(&values, degree).unwrap_or_else(|e| {
                    panic!("{name} degree {degree}: detrend_polynomial failed: {e:?}")
                })
            };

            assert_vec_close(
                &got.trend,
                &want_fitted,
                trend_abs,
                0.0,
                &format!("{name} degree {degree}: trend (direct)"),
            );
            assert_vec_close(
                &got.detrended,
                &want_detrended,
                trend_abs,
                0.0,
                &format!("{name} degree {degree}: detrended (direct)"),
            );
            let got_coefficients = got
                .coefficients
                .clone()
                .unwrap_or_else(|| panic!("{name} degree {degree}: no coefficients returned"));
            assert_vec_close(
                &got_coefficients,
                &want_coefficients,
                coef_abs,
                coef_rel,
                &format!("{name} degree {degree}: coefficients (direct)"),
            );
            // `rss_rel` alone is too tight for a near-zero RSS (e.g. a
            // perfect/constant fit, where numpy's RSS is ~1e-29 floating
            // noise rather than exactly 0) -- fold in `trend_abs` as the
            // absolute floor, consistent with the trend/detrended checks
            // above which already bound this family's noise at 1e-8.
            assert_close(
                got.rss,
                want_rss,
                trend_abs,
                rss_rel,
                &format!("{name} degree {degree}: rss (direct)"),
            );
            assert_eq!(
                got.n_params, want_n_params,
                "{name} degree {degree}: n_params (direct)"
            );

            // --- 2. named DetrendMethod entry point (SQL-facing method names) ---
            let method_name = match degree {
                1 => "linear",
                2 => "quadratic",
                3 => "cubic",
                other => panic!(
                    "fixture has unexpected degree {other}; only 1..=3 are wired to a named method"
                ),
            };
            let method: DetrendMethod = method_name
                .parse()
                .unwrap_or_else(|e| panic!("{method_name}: failed to parse DetrendMethod: {e:?}"));
            let got_named = detrend(&values, method)
                .unwrap_or_else(|e| panic!("{name} method {method_name}: detrend failed: {e:?}"));

            assert_vec_close(
                &got_named.trend,
                &want_fitted,
                trend_abs,
                0.0,
                &format!("{name} method {method_name}: trend (named)"),
            );
            let expected_method_str = if degree == 1 {
                "linear".to_string()
            } else {
                format!("polynomial({degree})")
            };
            assert_eq!(
                got_named.method, expected_method_str,
                "{name} method {method_name}: method string"
            );
        }
    }
}
