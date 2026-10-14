//! statsmodels ADF/KPSS reference-fixture parity (D-05/D-05a/D-04, Phase 17
//! plans 17-01/17-02).
//!
//! Fixture: `statsmodels_stationarity/adf_kpss_cases.json`
//! (`validation/reference/python/gen_stationarity_reference.py`).
//!
//! 17-01 (this file's first test) pins ADF for 8 series x regression in
//! {"c", "ct"} against `statsmodels.tsa.stattools.adfuller(autolag="AIC")`.
//! 17-02 appends KPSS-vs-statsmodels, KPSS-vs-R (D-05a) and D-04 verdict
//! parity tests.

mod common;

use anofox_fcst_core::validation::{adf, StationarityRegression};
use common::{assert_close, f64_vec, load_fixture, tolerance};

fn parse_regression(s: &str) -> StationarityRegression {
    s.parse()
        .unwrap_or_else(|e| panic!("bad regression {s:?}: {e:?}"))
}

#[test]
fn adf_matches_statsmodels_for_every_series_and_regression() {
    let fx = load_fixture("statsmodels_stationarity/adf_kpss_cases.json");
    let adf_statistic_abs = tolerance(&fx, "adf_statistic_abs");
    let adf_p_value_abs = tolerance(&fx, "adf_p_value_abs");
    let cv_abs = tolerance(&fx, "cv_abs");

    let cases = fx["cases"]
        .as_array()
        .expect("adf_kpss_cases.json: 'cases' is not an array");
    assert_eq!(cases.len(), 8, "expected 8 series in the fixture");

    for case in cases {
        let name = case["name"].as_str().unwrap();
        let values = f64_vec(&case["values"]);
        // diag_b's ADF statistic is ill-conditioned (two sinusoids fitted
        // almost exactly by a 3-lag regression over 40 points) -- the
        // generator flags it, and we skip statistic/p_value for it only.
        let comparable = case
            .get("adf_statistic_comparable")
            .and_then(|v| v.as_bool())
            .unwrap_or(true);

        for reg_str in ["c", "ct"] {
            let regression = parse_regression(reg_str);
            let block = &case["regressions"][reg_str]["adf"];
            let want_statistic = block["statistic"].as_f64().unwrap();
            let want_p_value = block["p_value"].as_f64().unwrap();
            let want_lags = block["usedlag"].as_u64().unwrap() as usize;
            let want_cv1 = block["cv_1pct"].as_f64().unwrap();
            let want_cv5 = block["cv_5pct"].as_f64().unwrap();
            let want_cv10 = block["cv_10pct"].as_f64().unwrap();
            let want_is_stationary = block["reject_5pct"].as_bool().unwrap();

            let got = adf(&values, None, regression);
            let ctx = format!("{name} regression={reg_str}");

            if comparable {
                assert_close(
                    got.statistic,
                    want_statistic,
                    adf_statistic_abs,
                    0.0,
                    &format!("{ctx} statistic"),
                );
                assert_close(
                    got.p_value,
                    want_p_value,
                    adf_p_value_abs,
                    0.0,
                    &format!("{ctx} p_value"),
                );
            }
            assert_eq!(got.lags, want_lags, "{ctx} lags");
            assert_close(
                got.cv_1pct,
                want_cv1,
                cv_abs,
                0.0,
                &format!("{ctx} cv_1pct"),
            );
            assert_close(
                got.cv_5pct,
                want_cv5,
                cv_abs,
                0.0,
                &format!("{ctx} cv_5pct"),
            );
            assert_close(
                got.cv_10pct,
                want_cv10,
                cv_abs,
                0.0,
                &format!("{ctx} cv_10pct"),
            );
            assert_eq!(got.is_stationary, want_is_stationary, "{ctx} is_stationary");
        }
    }
}
