//! Reference-fixture assertions pinning `anofox_fcst_core::changepoint`
//! against independent numpy references (Phase 15 plan 15-03, DET-02,
//! DET-03, D-06, D-07).
//!
//! Fixtures: `changepoints/bocpd_cases.json` (Task 1, this file's
//! `bocpd_*` tests), `changepoints/pelt_cases.json` (Task 2, this file's
//! `pelt_*` tests).
//! (`validation/reference/python/gen_changepoint_reference.py`).
//!
//! Before this plan, BOCPD used an unnormalised Student-t kernel, a
//! posterior-mean-based (not sample-mean) update, and dropped probability
//! mass on run-length truncation without renormalising -- all three
//! verified live against the pre-fix source in 15-RESEARCH.md (G3-G6).
//! PELT's cost had no sigma-scale term (G7), L1 used the mean instead of
//! the median (G8), and its pruning set was computed but never consulted
//! (G8) -- giving 32 spurious changepoints on a `values * 100` series.

mod common;

use anofox_fcst_core::changepoint::{
    bocpd_trace, deseasonalize_for_changepoints, detect_changepoints,
    detect_changepoints_bocpd_with_options, robust_noise_scale, ChangepointOptions, CostFunction,
};
use anofox_fcst_core::detrend_linear;
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

/// Phase 20 plan 20-06 Task 3 (#287, BUG-06, D2-ii): `issue287_A/B/C`
/// (the #287 panel's exact SQL formula, hazard_lambda 250) pin
/// `detect_changepoints_bocpd_with_options`'s D2-ii threshold rule against
/// the independent numpy reference's `expected_threshold_0_5` -- the
/// indices `t >= 1` where the reference `prob[t] >= 0.5`.
#[test]
fn bocpd_with_options_threshold_matches_numpy_expected_threshold_0_5() {
    let fx = load_fixture("changepoints/bocpd_cases.json");
    let cases = fx["cases"].as_array().unwrap();
    let case = |name: &str| {
        cases
            .iter()
            .find(|c| c["name"] == name)
            .unwrap_or_else(|| panic!("bocpd_cases.json: no case named {name:?}"))
    };

    let mut checked_at_least_one = false;
    for name in ["issue287_A", "issue287_B", "issue287_C"] {
        let c = case(name);
        let expected = &c["expected"];
        let want_threshold_flags: Vec<usize> = expected["expected_threshold_0_5"]
            .as_array()
            .unwrap_or_else(|| panic!("{name}: fixture case has no 'expected_threshold_0_5' field"))
            .iter()
            .map(|v| v.as_u64().unwrap() as usize)
            .collect();

        let x = f64_vec(&c["x"]);
        let hazard_lambda = num(&c["hazard_lambda"]);
        let opts = ChangepointOptions {
            hazard_lambda,
            threshold: Some(0.5),
            ..Default::default()
        };
        let result = detect_changepoints_bocpd_with_options(&x, &opts).unwrap();
        assert_eq!(
            result.changepoints, want_threshold_flags,
            "{name}: threshold=0.5 flags"
        );
        assert!(
            !result.is_changepoint[0],
            "{name}: index 0 must never be flagged"
        );
        checked_at_least_one = true;
    }
    assert!(
        checked_at_least_one,
        "no issue287_* case carried expected_threshold_0_5 -- regenerate bocpd_cases.json"
    );
}

/// Phase 20 plan 20-07 Task 2 (#287, BUG-05, D1-b): pins
/// `deseasonalize_for_changepoints`'s seasonal removal against R
/// `forecast::mstl`'s own seasonal component at period 7 -- an independent
/// reference for the OTHER half of the transform (`bocpd_with_options_
/// threshold_matches_numpy_expected_threshold_0_5` above pins the BOCPD
/// decision rule; this pins the deseasonalization step that feeds it).
#[test]
fn deseasonalize_for_changepoints_matches_r_mstl_seasonal_for_issue287() {
    let fx = load_fixture("r_decomposition/cases.json");
    let component_abs = tolerance(&fx, "component_abs");

    let mut checked_at_least_one = false;
    for name in ["issue287_A", "issue287_B", "issue287_C"] {
        let case = fx["mstl_cases"]
            .get(name)
            .unwrap_or_else(|| panic!("r_decomposition/cases.json: no mstl_cases.{name}"));
        let values = f64_vec(&case["values"]);
        let r_seasonal = f64_vec(
            case["mstl"]["seasonal"]
                .get("p7")
                .unwrap_or_else(|| panic!("{name}: mstl.seasonal.p7 missing")),
        );

        // What deseasonalize_for_changepoints is supposed to compute:
        // values minus R's own seasonal component, then linearly detrended
        // (D1-b) -- the independent reference for the seasonal-removal
        // half of the transform.
        let x_minus_r_seasonal: Vec<f64> = values
            .iter()
            .zip(r_seasonal.iter())
            .map(|(&v, &s)| v - s)
            .collect();
        let want = detrend_linear(&x_minus_r_seasonal)
            .unwrap_or_else(|e| panic!("{name}: detrend_linear(values - R seasonal) failed: {e:?}"))
            .detrended;

        let got = deseasonalize_for_changepoints(&values, Some(7))
            .unwrap_or_else(|e| panic!("{name}: deseasonalize_for_changepoints failed: {e:?}"));

        assert_vec_close(
            &got,
            &want,
            component_abs,
            0.0,
            &format!(
                "{name}: deseasonalize_for_changepoints vs R mstl seasonal (p7) + detrend_linear"
            ),
        );
        checked_at_least_one = true;
    }
    assert!(
        checked_at_least_one,
        "no issue287_* case found in r_decomposition/cases.json -- regenerate the fixture"
    );
}

fn cost_fn_from_str(s: &str) -> CostFunction {
    match s {
        "L1" => CostFunction::L1,
        "L2" => CostFunction::L2,
        "Normal" => CostFunction::Normal,
        other => panic!("pelt_cases.json: unknown cost {other:?}"),
    }
}

#[test]
fn pelt_matches_numpy_exact_dp() {
    let fx = load_fixture("changepoints/pelt_cases.json");
    let cost_rel = tolerance(&fx, "cost_rel");
    let sigma_rel = tolerance(&fx, "sigma_rel");

    let cases = fx["cases"]
        .as_array()
        .expect("pelt_cases.json: 'cases' is not an array");

    for case in cases {
        let name = case["name"].as_str().unwrap();
        let x = f64_vec(&case["x"]);
        let cost_fn = cost_fn_from_str(case["cost"].as_str().unwrap());
        let min_size = case["min_size"].as_u64().unwrap() as usize;
        let penalty = if case["penalty"].is_null() {
            None
        } else {
            Some(num(&case["penalty"]))
        };
        let expected = &case["expected"];

        let want_sigma = num(&expected["sigma_hat"]);
        let want_changepoints: Vec<usize> = expected["changepoints"]
            .as_array()
            .unwrap()
            .iter()
            .map(|v| v.as_u64().unwrap() as usize)
            .collect();
        let want_cost = num(&expected["cost"]);

        let got_sigma = robust_noise_scale(&x);
        assert_close(
            got_sigma,
            want_sigma,
            0.0,
            sigma_rel,
            &format!("{name}: sigma_hat"),
        );

        let result = detect_changepoints(&x, min_size, penalty, cost_fn)
            .unwrap_or_else(|e| panic!("{name}: detect_changepoints failed: {e:?}"));

        assert_eq!(
            result.changepoints, want_changepoints,
            "{name}: changepoints"
        );
        assert_close(
            result.cost,
            want_cost,
            0.0,
            cost_rel,
            &format!("{name}: cost"),
        );
    }
}

#[test]
fn pelt_is_scale_and_shift_invariant() {
    let fx = load_fixture("changepoints/pelt_cases.json");
    let cases = fx["cases"].as_array().unwrap();
    let case = |name: &str| {
        cases
            .iter()
            .find(|c| c["name"] == name)
            .unwrap_or_else(|| panic!("pelt_cases.json: no case named {name:?}"))
    };

    let base = case("mean3_n150_L2_m5");
    let base_x = f64_vec(&base["x"]);
    let base_result = detect_changepoints(&base_x, 5, None, CostFunction::L2).unwrap();

    let scaled = case("mean3_x100");
    let scaled_x = f64_vec(&scaled["x"]);
    let scaled_result = detect_changepoints(&scaled_x, 5, None, CostFunction::L2).unwrap();
    assert_eq!(
        scaled_result.changepoints, base_result.changepoints,
        "mean3_x100 should equal the base mean3_n150 changepoints"
    );

    let shifted = case("mean3_shift");
    let shifted_x = f64_vec(&shifted["x"]);
    let shifted_result = detect_changepoints(&shifted_x, 5, None, CostFunction::L2).unwrap();
    assert_eq!(
        shifted_result.changepoints, base_result.changepoints,
        "mean3_shift should equal the base mean3_n150 changepoints"
    );

    // Also check against every live x100/x computation directly, not just
    // the two fixture cases, for every cost function.
    for cost_fn in [CostFunction::L1, CostFunction::L2, CostFunction::Normal] {
        let base_live = detect_changepoints(&base_x, 5, None, cost_fn).unwrap();
        let scaled_live: Vec<f64> = base_x.iter().map(|v| v * 100.0).collect();
        let scaled_live_result = detect_changepoints(&scaled_live, 5, None, cost_fn).unwrap();
        assert_eq!(
            scaled_live_result.changepoints, base_live.changepoints,
            "{cost_fn:?}: x*100 changepoints should equal base"
        );
    }
}
