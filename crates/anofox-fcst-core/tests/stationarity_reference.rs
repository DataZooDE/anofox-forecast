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

use anofox_fcst_core::validation::{adf, kpss, stationarity, StationarityRegression};
use common::{assert_close, f64_vec, load_fixture, num, tolerance};

/// R-backed series names in the fixture (also present in
/// `r_diagnostics/stationarity_portmanteau.json`, D-05a cross-check).
const R_SERIES_NAMES: [&str; 5] = ["air", "log_air", "rw", "diff_rw", "ar1"];

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

#[test]
fn kpss_matches_statsmodels_for_every_series_and_regression() {
    let fx = load_fixture("statsmodels_stationarity/adf_kpss_cases.json");
    let kpss_statistic_abs = tolerance(&fx, "kpss_statistic_abs");
    let kpss_p_value_abs = tolerance(&fx, "kpss_p_value_abs");
    let cv_abs = tolerance(&fx, "cv_abs");

    let cases = fx["cases"]
        .as_array()
        .expect("adf_kpss_cases.json: 'cases' is not an array");

    for case in cases {
        let name = case["name"].as_str().unwrap();
        let values = f64_vec(&case["values"]);

        for reg_str in ["c", "ct"] {
            let regression = parse_regression(reg_str);
            let block = &case["regressions"][reg_str]["kpss"];
            let want_statistic = block["statistic"].as_f64().unwrap();
            let want_p_value = block["p_value"].as_f64().unwrap();
            let want_lags = block["lags"].as_u64().unwrap() as usize;
            let want_cv1 = block["cv_1pct"].as_f64().unwrap();
            let want_cv5 = block["cv_5pct"].as_f64().unwrap();
            let want_cv10 = block["cv_10pct"].as_f64().unwrap();
            // KPSS "is_stationary" is NOT reject_5pct (fails to reject the
            // stationarity null): statistic < cv_5pct.
            let want_is_stationary = !block["reject_5pct"].as_bool().unwrap();

            let got = kpss(&values, None, regression);
            let ctx = format!("{name} regression={reg_str}");

            assert_close(
                got.statistic,
                want_statistic,
                kpss_statistic_abs,
                0.0,
                &format!("{ctx} statistic"),
            );
            assert_close(
                got.p_value,
                want_p_value,
                kpss_p_value_abs,
                0.0,
                &format!("{ctx} p_value"),
            );
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

/// D-05a: the five R-backed series' KPSS 'c'/'ct' match R
/// `tseries::kpss.test` (kpss_level / kpss_trend blocks of
/// `r_diagnostics/stationarity_portmanteau.json`) within that fixture's own
/// tolerance (lag exact).
#[test]
fn kpss_matches_r_tseries_for_r_backed_series() {
    let r_fx = load_fixture("r_diagnostics/stationarity_portmanteau.json");
    let statistic_abs = tolerance(&r_fx, "statistic_abs");
    let p_value_abs = tolerance(&r_fx, "p_value_abs");

    let sm_fx = load_fixture("statsmodels_stationarity/adf_kpss_cases.json");
    let sm_cases = sm_fx["cases"].as_array().unwrap();

    for name in R_SERIES_NAMES {
        let case = sm_cases
            .iter()
            .find(|c| c["name"].as_str() == Some(name))
            .unwrap_or_else(|| panic!("{name}: not found in adf_kpss_cases.json"));
        let values = f64_vec(&case["values"]);

        for (reg_str, r_key) in [("c", "kpss_level"), ("ct", "kpss_trend")] {
            let regression = parse_regression(reg_str);
            let r_block = &r_fx["series"][name]["tests"][r_key];
            let want_statistic = num(&r_block["statistic"]);
            let want_lag = r_block["lag"].as_u64().unwrap() as usize;
            let want_p_value = num(&r_block["p_value"]);

            let got = kpss(&values, None, regression);
            let ctx = format!("{name} regression={reg_str} vs R {r_key}");

            assert_close(
                got.statistic,
                want_statistic,
                statistic_abs,
                0.0,
                &format!("{ctx} statistic"),
            );
            assert_eq!(got.lags, want_lag, "{ctx} lags");
            assert_close(
                got.p_value,
                want_p_value,
                p_value_abs,
                0.0,
                &format!("{ctx} p_value"),
            );
        }
    }
}

/// D-04: the D-04 four-way verdict matches the fixture's own verdict
/// (computed there from each test's 5%-level rejection flag) for every
/// series x regression except `diag_b` (ill-conditioned ADF, see
/// `gen_stationarity_reference.py`). A guard confirms the asserted fixture
/// verdicts cover all four D-04 labels.
#[test]
fn stationarity_verdict_matches_fixture_for_every_comparable_series() {
    let fx = load_fixture("statsmodels_stationarity/adf_kpss_cases.json");
    let cases = fx["cases"].as_array().unwrap();

    let mut seen_labels = std::collections::BTreeSet::new();

    for case in cases {
        let name = case["name"].as_str().unwrap();
        if name == "diag_b" {
            continue;
        }
        let values = f64_vec(&case["values"]);

        for reg_str in ["c", "ct"] {
            let regression = parse_regression(reg_str);
            let want_verdict = case["regressions"][reg_str]["verdict"]
                .as_str()
                .unwrap()
                .to_string();
            seen_labels.insert(want_verdict.clone());

            let got = stationarity(&values, regression);
            assert_eq!(
                got.verdict,
                Some(want_verdict.as_str()),
                "{name} regression={reg_str} verdict"
            );
        }
    }

    for label in [
        "stationary",
        "non_stationary",
        "trend_stationary",
        "difference_stationary",
    ] {
        assert!(
            seen_labels.contains(label),
            "fixture verdicts (excluding diag_b) do not cover D-04 label {label:?}: {seen_labels:?}"
        );
    }
}
