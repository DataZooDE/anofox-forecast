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
    conformal_apply, conformal_calibrate, conformal_from_pairs, conformal_learn,
    conformal_predict_asymmetric, conformal_quantile, ConformalMethod, ConformalStrategy,
};
use approx::assert_relative_eq;
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

// ============================================================================
// Phase 16 plan 16-02 Task 2: D-07 signed-residual asymmetric conformal,
// seeded Monte Carlo coverage, conformal_learn/conformal_apply consistency.
// ============================================================================

/// `asym_n20`/`asym_n10`/`asym_all_positive`: `conformal_predict_asymmetric`
/// (forecast 0.0, so `lower`/`upper` ARE the signed margins) reproduces
/// numpy's lower/upper margin, coverage, conformity_score, and insufficiency
/// flag exactly.
#[test]
fn asymmetric_hand_checked_cases_match_numpy() {
    let fx = load_fixture("conformal/asymmetric_cases.json");
    let score_abs = tolerance(&fx, "score_abs");
    let coverage_abs = tolerance(&fx, "coverage_abs");

    for key in ["asym_n20", "asym_n10", "asym_all_positive"] {
        let case = &fx[key];
        let residuals = f64_vec(&case["residuals"]);
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

            let result = conformal_predict_asymmetric(&residuals, &[0.0], alpha)
                .unwrap_or_else(|e| panic!("{key} alpha {alpha}: {e}"));

            let ctx = format!("{key} alpha {alpha}");
            assert_eq!(
                result.calibration_insufficient, insufficient,
                "{ctx} calibration_insufficient"
            );
            assert_eq!(
                result.n_calibration,
                common::num(&expected["n_calibration"]) as usize,
                "{ctx} n_calibration"
            );

            if insufficient {
                assert_eq!(result.lower[0], f64::NEG_INFINITY, "{ctx} lower");
                assert_eq!(result.upper[0], f64::INFINITY, "{ctx} upper");
                assert!(result.coverage.is_nan(), "{ctx} coverage");
            } else {
                // forecast = 0.0, so lower[0]/upper[0] ARE the signed margins.
                let lower_margin = -result.lower[0];
                let upper_margin = result.upper[0];
                common::assert_close(
                    lower_margin,
                    common::num(&expected["lower_margin"]),
                    score_abs,
                    0.0,
                    &format!("{ctx} lower_margin"),
                );
                common::assert_close(
                    upper_margin,
                    common::num(&expected["upper_margin"]),
                    score_abs,
                    0.0,
                    &format!("{ctx} upper_margin"),
                );
                common::assert_close(
                    result.coverage,
                    common::num(&expected["coverage"]),
                    coverage_abs,
                    0.0,
                    &format!("{ctx} coverage"),
                );
                common::assert_close(
                    result.conformity_score,
                    common::num(&expected["conformity_score"]),
                    score_abs,
                    0.0,
                    &format!("{ctx} conformity_score"),
                );
            }
        }
    }
}

/// `seeded.{seeded_n40_normal,seeded_n99_lognormal,seeded_n199_normal}`: the
/// stored residual arrays (drawn by numpy's RNG, fixed in the fixture) are
/// reproduced exactly by `conformal_predict_asymmetric` -- no RNG re-run on
/// the Rust side.
#[test]
fn asymmetric_seeded_cases_match_numpy_margins() {
    let fx = load_fixture("conformal/asymmetric_cases.json");
    let score_abs = tolerance(&fx, "score_abs");
    let coverage_abs = tolerance(&fx, "coverage_abs");

    let seeded = fx["seeded"]
        .as_object()
        .expect("asymmetric_seeded_cases_match_numpy_margins: 'seeded' is not an object");
    assert_eq!(seeded.len(), 3);

    for (name, case) in seeded {
        let residuals = f64_vec(&case["residuals"]);
        let alpha = common::num(&case["alpha"]);

        let result = conformal_predict_asymmetric(&residuals, &[0.0], alpha)
            .unwrap_or_else(|e| panic!("{name}: {e}"));
        assert!(
            !result.calibration_insufficient,
            "{name}: unexpectedly insufficient"
        );

        let lower_margin = -result.lower[0];
        let upper_margin = result.upper[0];
        common::assert_close(
            lower_margin,
            common::num(&case["lower_margin"]),
            score_abs,
            0.0,
            &format!("{name} lower_margin"),
        );
        common::assert_close(
            upper_margin,
            common::num(&case["upper_margin"]),
            score_abs,
            0.0,
            &format!("{name} upper_margin"),
        );
        common::assert_close(
            result.coverage,
            common::num(&case["coverage"]),
            coverage_abs,
            0.0,
            &format!("{name} coverage"),
        );
    }
}

/// `conformal_learn`'s Symmetric branch stores the same score
/// `conformal_quantile` computes directly for the same residuals.
#[test]
fn conformal_learn_symmetric_matches_conformal_quantile() {
    let fx = load_fixture("conformal/split_cases.json");
    let case = &fx["two_group"]["groups"]["A"];
    let actual = f64_vec(&case["actual"]);
    let forecast = f64_vec(&case["forecast"]);
    let residuals: Vec<f64> = actual
        .iter()
        .zip(forecast.iter())
        .map(|(a, f)| a - f)
        .collect();

    let q = conformal_quantile(&residuals, 0.1).unwrap();
    let profile = conformal_learn(
        &residuals,
        &[0.1],
        ConformalMethod::Symmetric,
        ConformalStrategy::Split,
        None,
    )
    .unwrap();
    assert_relative_eq!(profile.scores_lower[0], q, epsilon = 1e-9);
    assert_relative_eq!(profile.scores_upper[0], q, epsilon = 1e-9);
}

/// `conformal_learn`'s Asymmetric branch stores the same margins
/// `conformal_predict_asymmetric` computes directly, and `conformal_apply`
/// reproduces the same bounds.
#[test]
fn conformal_learn_asymmetric_matches_predict_asymmetric_margins() {
    let fx = load_fixture("conformal/asymmetric_cases.json");
    let residuals = f64_vec(&fx["asym_n20"]["residuals"]);
    let alpha = 0.2;

    let direct = conformal_predict_asymmetric(&residuals, &[0.0], alpha).unwrap();
    let profile = conformal_learn(
        &residuals,
        &[alpha],
        ConformalMethod::Asymmetric,
        ConformalStrategy::Split,
        None,
    )
    .unwrap();

    assert_relative_eq!(profile.scores_lower[0], -direct.lower[0], epsilon = 1e-9);
    assert_relative_eq!(profile.scores_upper[0], direct.upper[0], epsilon = 1e-9);

    let intervals = conformal_apply(&[0.0], &profile, None).unwrap();
    assert_relative_eq!(intervals.lower[0][0], direct.lower[0], epsilon = 1e-9);
    assert_relative_eq!(intervals.upper[0][0], direct.upper[0], epsilon = 1e-9);
}

/// An alpha level with an insufficient score in a multi-level profile only
/// produces unbounded bounds for THAT level; other, sufficient levels stay
/// finite (Task 2 action item 2).
#[test]
fn conformal_apply_insufficient_level_is_unbounded_for_that_level_only() {
    // n=5: alpha 0.1 insufficient (ceil(6*0.9)=6>5), alpha 0.2 sufficient
    // (ceil(6*0.8)=5<=5).
    let residuals = vec![1.0, 2.0, 3.0, 4.0, 5.0];
    let profile = conformal_learn(
        &residuals,
        &[0.1, 0.2],
        ConformalMethod::Symmetric,
        ConformalStrategy::Split,
        None,
    )
    .unwrap();
    assert!(profile.scores_lower[0].is_infinite());
    assert!(profile.scores_upper[0].is_infinite());
    assert!(profile.scores_lower[1].is_finite());
    assert!(profile.scores_upper[1].is_finite());

    let intervals = conformal_apply(&[10.0], &profile, None).unwrap();
    assert_eq!(intervals.lower[0][0], f64::NEG_INFINITY);
    assert_eq!(intervals.upper[0][0], f64::INFINITY);
    assert!(intervals.lower[1][0].is_finite());
    assert!(intervals.upper[1][0].is_finite());
}

// ----------------------------------------------------------------------------
// Seeded Monte Carlo coverage (G13): an inline SplitMix64 generator with
// Box-Muller normals, independent of numpy's PCG64 -- compared against
// monte_carlo.json via a coverage/width TOLERANCE band, not RNG-exact parity.
// ----------------------------------------------------------------------------

/// Minimal, dependency-free SplitMix64 PRNG (this plan forbids adding a
/// `rand` crate dependency).
struct SplitMix64 {
    state: u64,
}

impl SplitMix64 {
    fn new(seed: u64) -> Self {
        Self { state: seed }
    }

    fn next_u64(&mut self) -> u64 {
        self.state = self.state.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.state;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    }

    /// Uniform in (0, 1) -- excludes 0.0 so Box-Muller's `ln()` never sees it.
    fn next_open01(&mut self) -> f64 {
        let x = (self.next_u64() >> 11) as f64 * (1.0 / (1u64 << 53) as f64);
        if x <= 0.0 {
            f64::MIN_POSITIVE
        } else {
            x
        }
    }

    /// One standard normal draw via Box-Muller (discards the paired value;
    /// simplicity over throughput in a test-only generator).
    fn next_standard_normal(&mut self) -> f64 {
        let u1 = self.next_open01();
        let u2 = self.next_open01();
        let r = (-2.0 * u1.ln()).sqrt();
        let theta = 2.0 * std::f64::consts::PI * u2;
        r * theta.cos()
    }
}

fn draw_normal(rng: &mut SplitMix64, n: usize) -> Vec<f64> {
    (0..n).map(|_| rng.next_standard_normal()).collect()
}

/// Centred log-normal: `exp(N(0, sigma)) - exp(sigma^2 / 2)` (right-skewed,
/// mean 0) -- matches the fixture generator's construction exactly.
fn draw_lognormal(rng: &mut SplitMix64, n: usize) -> Vec<f64> {
    let sigma: f64 = 0.75;
    let shift = (sigma * sigma / 2.0).exp();
    (0..n)
        .map(|_| (sigma * rng.next_standard_normal()).exp() - shift)
        .collect()
}

/// Runs `trials` Monte Carlo trials ONCE per (method, distribution),
/// evaluating every alpha in `alphas` from the SAME draws per trial.
/// Returns `alpha -> (coverage, mean_width)`.
fn monte_carlo_run(
    method: ConformalMethod,
    dist: &str,
    seed: u64,
    alphas: &[f64],
    trials: usize,
    n_cal: usize,
    m_test: usize,
) -> std::collections::HashMap<String, (f64, f64)> {
    let mut rng = SplitMix64::new(seed);
    let mut covered: std::collections::HashMap<String, usize> = std::collections::HashMap::new();
    let mut total: std::collections::HashMap<String, usize> = std::collections::HashMap::new();
    let mut width_sum: std::collections::HashMap<String, f64> = std::collections::HashMap::new();
    for &a in alphas {
        covered.insert(format!("{a}"), 0);
        total.insert(format!("{a}"), 0);
        width_sum.insert(format!("{a}"), 0.0);
    }

    for _ in 0..trials {
        let cal = match dist {
            "normal" => draw_normal(&mut rng, n_cal),
            "lognormal" => draw_lognormal(&mut rng, n_cal),
            _ => panic!("unknown distribution: {dist}"),
        };
        let test = match dist {
            "normal" => draw_normal(&mut rng, m_test),
            "lognormal" => draw_lognormal(&mut rng, m_test),
            _ => unreachable!(),
        };

        for &alpha in alphas {
            let (lower0, upper0) = match method {
                ConformalMethod::Symmetric => {
                    let q = conformal_quantile(&cal, alpha).unwrap();
                    (-q, q)
                }
                ConformalMethod::Asymmetric => {
                    let r = conformal_predict_asymmetric(&cal, &[0.0], alpha).unwrap();
                    (r.lower[0], r.upper[0])
                }
                ConformalMethod::Adaptive => unreachable!("not exercised by this test"),
            };

            let key = format!("{alpha}");
            let n_covered = test.iter().filter(|&&t| t >= lower0 && t <= upper0).count();
            *covered.get_mut(&key).unwrap() += n_covered;
            *total.get_mut(&key).unwrap() += test.len();
            *width_sum.get_mut(&key).unwrap() += upper0 - lower0;
        }
    }

    alphas
        .iter()
        .map(|&a| {
            let key = format!("{a}");
            let cov = covered[&key] as f64 / total[&key] as f64;
            let width = width_sum[&key] / trials as f64;
            (key, (cov, width))
        })
        .collect()
}

/// Symmetric and asymmetric coverage/mean-width, normal and log-normal
/// distributions, alpha 0.1 and 0.2 (8 coverage assertions): Rust's
/// independent SplitMix64 Monte Carlo agrees with numpy's PCG64 Monte Carlo
/// within the fixture's tolerance (G13, CONF-03).
#[test]
fn monte_carlo_symmetric_and_asymmetric_coverage_matches_numpy() {
    let fx = load_fixture("conformal/monte_carlo.json");
    let coverage_abs = tolerance(&fx, "coverage_abs");
    let mean_width_rel = tolerance(&fx, "mean_width_rel");

    let trials = common::num(&fx["params"]["trials"]) as usize;
    let n_cal = common::num(&fx["params"]["n_cal"]) as usize;
    let m_test = common::num(&fx["params"]["m_test"]) as usize;
    let alphas: Vec<f64> = fx["params"]["alphas"]
        .as_array()
        .unwrap()
        .iter()
        .map(common::num)
        .collect();

    // Seeds 16101 (normal) / 16102 (lognormal) shared by both methods, per
    // the plan interfaces block.
    let combos: [(ConformalMethod, &str, &str, u64); 4] = [
        (ConformalMethod::Symmetric, "symmetric", "normal", 16101),
        (ConformalMethod::Symmetric, "symmetric", "lognormal", 16102),
        (ConformalMethod::Asymmetric, "asymmetric", "normal", 16101),
        (
            ConformalMethod::Asymmetric,
            "asymmetric",
            "lognormal",
            16102,
        ),
    ];

    for (method, method_key, dist, seed) in combos {
        let got = monte_carlo_run(method, dist, seed, &alphas, trials, n_cal, m_test);

        for &alpha in &alphas {
            let key = format!("{alpha}");
            let expected = &fx["results"][method_key][dist][&key];
            let want_cov = common::num(&expected["coverage"]);
            let want_width = common::num(&expected["mean_width"]);
            let (got_cov, got_width) = got[&key];

            let ctx = format!("{method_key} {dist} alpha {alpha}");
            assert!(
                (got_cov - want_cov).abs() <= coverage_abs,
                "{ctx} coverage: got {got_cov}, want {want_cov}"
            );
            let rel_diff = (got_width - want_width).abs() / want_width.abs();
            assert!(
                rel_diff <= mean_width_rel,
                "{ctx} mean_width: got {got_width}, want {want_width}, rel_diff {rel_diff}"
            );
        }
    }
}
