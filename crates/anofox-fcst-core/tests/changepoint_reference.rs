//! Reference-fixture assertions pinning `anofox_fcst_core::changepoint`
//! against independent numpy references (Phase 15 plan 15-03, DET-02,
//! DET-03, D-06, D-07).
//!
//! Fixtures: `changepoints/bocpd_cases.json` (Task 1, this file's
//! `bocpd_*` tests), `changepoints/pelt_cases.json` (Task 2, added in a
//! later commit).
//! (`validation/reference/python/gen_changepoint_reference.py`).
//!
//! Before this plan, BOCPD used an unnormalised Student-t kernel, a
//! posterior-mean-based (not sample-mean) update, and dropped probability
//! mass on run-length truncation without renormalising -- all three
//! verified live against the pre-fix source in 15-RESEARCH.md (G3-G6).

mod common;

use anofox_fcst_core::changepoint::{bocpd_trace, robust_noise_scale};
use common::{assert_close, assert_vec_close, f64_vec, load_fixture, num, tolerance};

#[test]
fn bocpd_matches_numpy_reference() {
    let fx = load_fixture("changepoints/bocpd_cases.json");
    let prob_abs = tolerance(&fx, "prob_abs");
    let sigma_rel = tolerance(&fx, "sigma_rel");

    let cases = fx["cases"]
        .as_array()
        .expect("bocpd_cases.json: 'cases' is not an array");

    for case in cases {
        let name = case["name"].as_str().unwrap();
        let x = f64_vec(&case["x"]);
        let hazard_lambda = num(&case["hazard_lambda"]);
        let expected = &case["expected"];

        let want_sigma = num(&expected["sigma_hat"]);
        let want_loc = num(&expected["loc"]);
        let want_prob = f64_vec(&expected["prob"]);
        let want_map_run_length: Vec<usize> = expected["map_run_length"]
            .as_array()
            .unwrap()
            .iter()
            .map(|v| v.as_u64().unwrap() as usize)
            .collect();
        let want_is_changepoint: Vec<bool> = expected["is_changepoint"]
            .as_array()
            .unwrap()
            .iter()
            .map(|v| v.as_bool().unwrap())
            .collect();
        let want_changepoints: Vec<usize> = expected["changepoints"]
            .as_array()
            .unwrap()
            .iter()
            .map(|v| v.as_u64().unwrap() as usize)
            .collect();

        let got_sigma = robust_noise_scale(&x);
        assert_close(
            got_sigma,
            want_sigma,
            0.0,
            sigma_rel,
            &format!("{name}: sigma_hat"),
        );

        let trace = bocpd_trace(&x, hazard_lambda)
            .unwrap_or_else(|e| panic!("{name}: bocpd_trace failed: {e:?}"));

        assert_close(trace.loc, want_loc, 1e-12, 0.0, &format!("{name}: loc"));
        assert_vec_close(
            &trace.prob,
            &want_prob,
            prob_abs,
            0.0,
            &format!("{name}: prob"),
        );
        assert_eq!(
            trace.map_run_length, want_map_run_length,
            "{name}: map_run_length"
        );
        assert_eq!(
            trace.is_changepoint, want_is_changepoint,
            "{name}: is_changepoint"
        );
        assert_eq!(
            trace.changepoints, want_changepoints,
            "{name}: changepoints"
        );
    }
}

#[test]
fn bocpd_behaviour_anchors() {
    let fx = load_fixture("changepoints/bocpd_cases.json");
    let cases = fx["cases"].as_array().unwrap();
    let case = |name: &str| {
        cases
            .iter()
            .find(|c| c["name"] == name)
            .unwrap_or_else(|| panic!("bocpd_cases.json: no case named {name:?}"))
    };

    let step3 = case("step3_n200_h250");
    let step3_x = f64_vec(&step3["x"]);
    let step3_trace = bocpd_trace(&step3_x, num(&step3["hazard_lambda"])).unwrap();
    assert_eq!(step3_trace.changepoints, vec![100], "step3 -> [100]");

    let noise = case("noise_n200");
    let noise_x = f64_vec(&noise["x"]);
    let noise_trace = bocpd_trace(&noise_x, num(&noise["hazard_lambda"])).unwrap();
    assert_eq!(noise_trace.changepoints, Vec::<usize>::new(), "noise -> []");

    let gross = case("gross_step_n24");
    let gross_x = f64_vec(&gross["x"]);
    let gross_trace = bocpd_trace(&gross_x, num(&gross["hazard_lambda"])).unwrap();
    assert_eq!(gross_trace.changepoints, vec![12], "issue-71 -> [12]");

    let scaled = case("step3_scaled");
    let scaled_x = f64_vec(&scaled["x"]);
    let scaled_trace = bocpd_trace(&scaled_x, num(&scaled["hazard_lambda"])).unwrap();
    assert_eq!(
        scaled_trace.changepoints, step3_trace.changepoints,
        "scaled == base (scale/shift invariance)"
    );
}
