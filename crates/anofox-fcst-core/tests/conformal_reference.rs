//! Reference-fixture assertions for `anofox_fcst_core::conformal::{conformal_from_pairs,
//! conformal_calibrate}` (Phase 16 plan 16-01: RUST-04, D-01, D-02, D-03).
//!
//! `validation/reference/fixtures/conformal/split_cases.json` (independent numpy
//! split-conformal reference, `python/gen_conformal_reference.py`) is extended by
//! plans 16-02/16-03 -- this file only asserts the `two_group` split-conformal case
//! this plan owns.
//!
//! - `conformal_from_pairs_matches_numpy_per_group`: for each group ("A", "B") and
//!   alpha (0.1, 0.2), reproduces `conformity_score`/empirical `coverage`/
//!   `nominal_level`/`n_calibration`/`lower`/`upper` against the fixture, and
//!   confirms the output `point` order is NOT value-sorted (stays 30, 10, 20).
//! - `conformal_calibrate_pooled_matches_numpy`: pools both groups' 40 pairs and
//!   reproduces the pooled `conformity_score`/`coverage`/`nominal_level`/
//!   `n_calibration` for each alpha, via `conformal_calibrate` directly (not
//!   `conformal_from_pairs`).

mod common;

use anofox_fcst_core::conformal::{
    conformal_calibrate, conformal_from_pairs, conformal_quantile, ConformalMethod,
};
use common::{f64_vec, load_fixture, tolerance};
use serde_json::Value;

fn option_vec(values: &[f64]) -> Vec<Option<f64>> {
    values.iter().map(|&v| Some(v)).collect()
}

#[test]
fn conformal_from_pairs_matches_numpy_per_group() {
    let fx = load_fixture("conformal/split_cases.json");
    let score_abs = tolerance(&fx, "score_abs");
    let coverage_abs = tolerance(&fx, "coverage_abs");

    let groups = fx["two_group"]["groups"].as_object().expect(
        "conformal_from_pairs_matches_numpy_per_group: 'two_group.groups' is not an object",
    );
    assert!(
        !groups.is_empty(),
        "conformal_from_pairs_matches_numpy_per_group: 'two_group.groups' is empty"
    );

    for (group_name, group) in groups {
        let actual = f64_vec(&group["actual"]);
        let forecast = f64_vec(&group["forecast"]);
        let future_point = f64_vec(&group["future_point"]);

        let actual_opt = option_vec(&actual);
        let forecast_opt = option_vec(&forecast);
        let point_opt = option_vec(&future_point);

        let alphas = group["alphas"]
            .as_object()
            .unwrap_or_else(|| panic!("group {group_name}: 'alphas' is not an object"));

        for (alpha_str, expected) in alphas {
            let alpha: f64 = alpha_str
                .parse()
                .unwrap_or_else(|e| panic!("group {group_name} alpha {alpha_str:?}: {e}"));

            let result = conformal_from_pairs(
                &actual_opt,
                &forecast_opt,
                &point_opt,
                alpha,
                ConformalMethod::Symmetric,
            )
            .unwrap_or_else(|e| {
                panic!("group {group_name} alpha {alpha}: conformal_from_pairs failed: {e}")
            });

            let ctx = format!("group {group_name} alpha {alpha}");

            common::assert_close(
                result.conformity_score,
                common::num(&expected["conformity_score"]),
                score_abs,
                0.0,
                &format!("{ctx} conformity_score"),
            );
            common::assert_close(
                result.coverage,
                common::num(&expected["coverage"]),
                coverage_abs,
                0.0,
                &format!("{ctx} coverage"),
            );
            common::assert_close(
                result.nominal_level,
                common::num(&expected["nominal_level"]),
                1e-12,
                0.0,
                &format!("{ctx} nominal_level"),
            );
            assert_eq!(
                result.n_calibration,
                common::num(&expected["n_calibration"]) as usize,
                "{ctx} n_calibration"
            );

            // Output order equals input point order (D-01: never value-sorted) --
            // this plan's fixture deliberately feeds a non-monotonic point list
            // (30, 10, 20), so a value-sort bug would be caught here.
            assert_eq!(
                result.point, future_point,
                "{ctx} point order must equal input order"
            );

            common::assert_vec_close(
                &result.lower,
                &f64_vec(&expected["lower"]),
                score_abs,
                0.0,
                &format!("{ctx} lower"),
            );
            common::assert_vec_close(
                &result.upper,
                &f64_vec(&expected["upper"]),
                score_abs,
                0.0,
                &format!("{ctx} upper"),
            );
        }
    }
}

#[test]
fn conformal_calibrate_pooled_matches_numpy() {
    let fx = load_fixture("conformal/split_cases.json");
    let score_abs = tolerance(&fx, "score_abs");
    let coverage_abs = tolerance(&fx, "coverage_abs");

    let groups = fx["two_group"]["groups"]
        .as_object()
        .expect("conformal_calibrate_pooled_matches_numpy: 'two_group.groups' is not an object");

    // Pool actual/forecast pairs across both groups, in fixture iteration order
    // (quantile math is order-invariant -- same pooled 40 pairs the generator
    // concatenated in `all_residuals`).
    let mut pooled_actual: Vec<Option<f64>> = Vec::new();
    let mut pooled_forecast: Vec<Option<f64>> = Vec::new();
    for group in groups.values() {
        pooled_actual.extend(option_vec(&f64_vec(&group["actual"])));
        pooled_forecast.extend(option_vec(&f64_vec(&group["forecast"])));
    }

    let pooled_alphas = fx["two_group"]["pooled"]["alphas"].as_object().expect(
        "conformal_calibrate_pooled_matches_numpy: 'two_group.pooled.alphas' is not an object",
    );
    assert!(
        !pooled_alphas.is_empty(),
        "conformal_calibrate_pooled_matches_numpy: 'two_group.pooled.alphas' is empty"
    );

    for (alpha_str, expected) in pooled_alphas {
        let alpha: f64 = alpha_str
            .parse()
            .unwrap_or_else(|e| panic!("pooled alpha {alpha_str:?}: {e}"));

        let cal = conformal_calibrate(
            &pooled_actual,
            &pooled_forecast,
            alpha,
            ConformalMethod::Symmetric,
        )
        .unwrap_or_else(|e| panic!("pooled alpha {alpha}: conformal_calibrate failed: {e}"));

        let ctx = format!("pooled alpha {alpha}");

        common::assert_close(
            cal.conformity_score,
            common::num(&expected["conformity_score"]),
            score_abs,
            0.0,
            &format!("{ctx} conformity_score"),
        );
        common::assert_close(
            cal.coverage,
            common::num(&expected["coverage"]),
            coverage_abs,
            0.0,
            &format!("{ctx} coverage"),
        );
        common::assert_close(
            cal.nominal_level,
            common::num(&expected["nominal_level"]),
            1e-12,
            0.0,
            &format!("{ctx} nominal_level"),
        );
        assert_eq!(
            cal.n_calibration,
            common::num(&expected["n_calibration"]) as usize,
            "{ctx} n_calibration"
        );
        assert_eq!(cal.method, ConformalMethod::Symmetric, "{ctx} method");
    }
}

/// Guard against a stale/empty fixture silently passing both tests above with
/// zero real assertions (e.g. a future edit that renames `two_group` away).
#[test]
fn conformal_fixture_has_expected_shape() {
    let fx = load_fixture("conformal/split_cases.json");
    let groups = fx["two_group"]["groups"].as_object().unwrap();
    assert_eq!(groups.len(), 2, "expected exactly groups A and B");
    assert!(groups.contains_key("A"));
    assert!(groups.contains_key("B"));
    let _: &Value = &fx["two_group"]["pooled"]["alphas"];
}

// ============================================================================
// Phase 16 plan 16-02: D-05 finite-sample insufficiency, D-06 alpha
// validation and non-finite filtering (Task 1 tracer).
// ============================================================================

/// `insufficient_n5`, `n1`, `all_nan`, `nan_filtered`: for each alpha,
/// `conformal_calibrate` must agree with numpy's `calibration_insufficient`
/// flag, `n_calibration` (the FINITE residual count), and -- when
/// sufficient -- `conformity_score`/`coverage`. When insufficient, the core
/// reports an infinite score and NaN coverage rather than a NULL-equivalent
/// clamped value.
#[test]
fn split_insufficiency_cases_match_numpy() {
    let fx = load_fixture("conformal/split_cases.json");
    let score_abs = tolerance(&fx, "score_abs");
    let coverage_abs = tolerance(&fx, "coverage_abs");

    for key in ["insufficient_n5", "n1", "all_nan", "nan_filtered"] {
        let case = &fx[key];
        let actual_opt = option_vec(&f64_vec(&case["actual"]));
        let forecast_opt = option_vec(&f64_vec(&case["forecast"]));

        let alphas = case["alphas"]
            .as_object()
            .unwrap_or_else(|| panic!("{key}: 'alphas' is not an object"));
        assert!(!alphas.is_empty(), "{key}: 'alphas' is empty");

        for (alpha_str, expected) in alphas {
            let alpha: f64 = alpha_str
                .parse()
                .unwrap_or_else(|e| panic!("{key} alpha {alpha_str:?}: {e}"));
            let insufficient = expected["calibration_insufficient"]
                .as_bool()
                .unwrap_or_else(|| panic!("{key} alpha {alpha}: missing calibration_insufficient"));
            let expected_n = common::num(&expected["n_calibration"]) as usize;

            let cal = conformal_calibrate(
                &actual_opt,
                &forecast_opt,
                alpha,
                ConformalMethod::Symmetric,
            )
            .unwrap_or_else(|e| panic!("{key} alpha {alpha}: conformal_calibrate failed: {e}"));

            let ctx = format!("{key} alpha {alpha}");
            assert_eq!(
                cal.calibration_insufficient, insufficient,
                "{ctx} calibration_insufficient"
            );
            assert_eq!(cal.n_calibration, expected_n, "{ctx} n_calibration");

            if insufficient {
                assert!(
                    cal.conformity_score.is_infinite(),
                    "{ctx} conformity_score should be infinite, got {}",
                    cal.conformity_score
                );
                assert!(
                    cal.coverage.is_nan(),
                    "{ctx} coverage should be NaN, got {}",
                    cal.coverage
                );
            } else {
                common::assert_close(
                    cal.conformity_score,
                    common::num(&expected["conformity_score"]),
                    score_abs,
                    0.0,
                    &format!("{ctx} conformity_score"),
                );
                common::assert_close(
                    cal.coverage,
                    common::num(&expected["coverage"]),
                    coverage_abs,
                    0.0,
                    &format!("{ctx} coverage"),
                );
            }
        }
    }
}

/// `n0`: zero calibration pairs is always insufficient, regardless of alpha,
/// and is no longer an error from either `conformal_quantile` or
/// `conformal_calibrate`.
#[test]
fn n0_is_always_insufficient_not_an_error() {
    let fx = load_fixture("conformal/split_cases.json");
    let empty_actual: Vec<Option<f64>> = Vec::new();
    let empty_forecast: Vec<Option<f64>> = Vec::new();

    let alphas = fx["n0"]["alphas"]
        .as_object()
        .expect("n0_is_always_insufficient_not_an_error: 'n0.alphas' is not an object");
    assert!(!alphas.is_empty());

    for (alpha_str, expected) in alphas {
        let alpha: f64 = alpha_str
            .parse()
            .unwrap_or_else(|e| panic!("{alpha_str:?}: {e}"));

        let q = conformal_quantile(&[], alpha).unwrap();
        assert!(
            q.is_infinite(),
            "conformal_quantile([], {alpha}) should be infinite"
        );

        let cal = conformal_calibrate(
            &empty_actual,
            &empty_forecast,
            alpha,
            ConformalMethod::Symmetric,
        )
        .unwrap_or_else(|e| panic!("alpha {alpha}: conformal_calibrate failed: {e}"));
        assert!(
            cal.calibration_insufficient,
            "alpha {alpha}: expected insufficient"
        );
        assert_eq!(
            cal.n_calibration,
            common::num(&expected["n_calibration"]) as usize,
            "alpha {alpha} n_calibration"
        );
    }
}

/// `insufficient_n5` end-to-end through `conformal_from_pairs`: the flag,
/// `n_calibration`, and (when insufficient) the unbounded -inf/+inf bounds
/// with NaN coverage; (when sufficient) the exact numpy score/coverage.
#[test]
fn insufficient_n5_from_pairs_end_to_end() {
    let fx = load_fixture("conformal/split_cases.json");
    let score_abs = tolerance(&fx, "score_abs");
    let coverage_abs = tolerance(&fx, "coverage_abs");
    let case = &fx["insufficient_n5"];

    let actual_opt = option_vec(&f64_vec(&case["actual"]));
    let forecast_opt = option_vec(&f64_vec(&case["forecast"]));
    let point_opt = option_vec(&f64_vec(&case["future_point"]));

    let alphas = case["alphas"].as_object().unwrap();
    for (alpha_str, expected) in alphas {
        let alpha: f64 = alpha_str.parse().unwrap();
        let insufficient = expected["calibration_insufficient"].as_bool().unwrap();

        let result = conformal_from_pairs(
            &actual_opt,
            &forecast_opt,
            &point_opt,
            alpha,
            ConformalMethod::Symmetric,
        )
        .unwrap_or_else(|e| panic!("alpha {alpha}: conformal_from_pairs failed: {e}"));

        let ctx = format!("insufficient_n5 alpha {alpha}");
        assert_eq!(result.calibration_insufficient, insufficient, "{ctx} flag");
        assert_eq!(
            result.n_calibration,
            common::num(&expected["n_calibration"]) as usize,
            "{ctx} n_calibration"
        );

        if insufficient {
            assert!(result.conformity_score.is_infinite(), "{ctx} score");
            assert!(result.coverage.is_nan(), "{ctx} coverage");
            assert_eq!(result.lower[0], f64::NEG_INFINITY, "{ctx} lower");
            assert_eq!(result.upper[0], f64::INFINITY, "{ctx} upper");
        } else {
            common::assert_close(
                result.conformity_score,
                common::num(&expected["conformity_score"]),
                score_abs,
                0.0,
                &format!("{ctx} score"),
            );
            common::assert_close(
                result.coverage,
                common::num(&expected["coverage"]),
                coverage_abs,
                0.0,
                &format!("{ctx} coverage"),
            );
        }
    }
}

/// Every `(n, alpha)` combination in `threshold_table` (n = 0..60, alpha in
/// {0.01, 0.05, 0.1, 0.2, 0.25, 0.5, 0.9}): Rust's `conformal_quantile`
/// insufficiency (infinite score) agrees with numpy's
/// `ceil((n + 1) * (1 - alpha)) > n` flag.
#[test]
fn threshold_table_matches_numpy_for_every_n_and_alpha() {
    let fx = load_fixture("conformal/split_cases.json");
    let rows = fx["threshold_table"]
        .as_array()
        .expect("threshold_table_matches_numpy_for_every_n_and_alpha: not an array");
    assert!(!rows.is_empty());

    for row in rows {
        let n = common::num(&row["n"]) as usize;
        let alpha = common::num(&row["alpha"]);
        let expected_insufficient = row["insufficient"]
            .as_bool()
            .expect("threshold_table row missing 'insufficient'");

        let residuals: Vec<f64> = (1..=n).map(|i| i as f64).collect();
        let q = conformal_quantile(&residuals, alpha).unwrap();
        assert_eq!(
            q.is_infinite(),
            expected_insufficient,
            "n={n} alpha={alpha}: got q={q}"
        );
    }
}

/// `invalid_alphas` (0.0, 1.0, -0.1, 1.5): every one is rejected by
/// `conformal_quantile` with an `InvalidInput` error naming the value via
/// "got" (D-06).
#[test]
fn invalid_alphas_are_rejected_naming_the_value() {
    let fx = load_fixture("conformal/split_cases.json");
    let alphas = fx["invalid_alphas"]
        .as_array()
        .expect("invalid_alphas_are_rejected_naming_the_value: not an array");
    assert!(!alphas.is_empty());

    let residuals = vec![1.0, 2.0, 3.0, 4.0, 5.0];
    for a in alphas {
        let alpha = common::num(a);
        let err = conformal_quantile(&residuals, alpha).unwrap_err();
        let msg = err.to_string();
        assert!(msg.contains("got"), "alpha {alpha}: {msg}");
    }
}
