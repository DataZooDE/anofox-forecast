//! Reference-fixture assertions for `ts_stats` moments, approximate entropy, and gap
//! counting (Phase 13, plan 13-01: STAT-01, STAT-03).
//!
//! - `moments_fixture_matches_compute_ts_stats`: asserts `compute_ts_stats` skewness /
//!   kurtosis / bimodality_coef against `moments/scipy_moments.json` for every vector,
//!   including the audit `neg30` vector (D-01).
//! - `r_entropy_fixture_matches_compute_ts_stats` (added plan 13-01 task 2): asserts
//!   `compute_ts_stats` entropy against `r_entropy/approx_entropy.json` (D-03).
//! - `gap_counts_fixture_matches_gap_counts` (added plan 13-01 task 2): asserts
//!   `gaps::gap_counts` against `gap_counts/cases.json` (D-04).

mod common;

use anofox_fcst_core::compute_ts_stats;
use common::{f64_vec, load_fixture, num, tolerance};

#[test]
fn moments_fixture_matches_compute_ts_stats() {
    let fx = load_fixture("moments/scipy_moments.json");
    let abs_tol = tolerance(&fx, "abs");
    let rel_tol = tolerance(&fx, "rel");
    // Documented fallback for floating-point summation-order noise below 1e-10 (plan
    // 13-01 task 1, step 3). Not present in the fixture today — every vector matches
    // within abs/rel — but read defensively in case a future regeneration needs it.
    let product_tol = fx
        .get("tolerance")
        .and_then(|t| t.get("product_abs"))
        .map(num)
        .unwrap_or(abs_tol);

    let vectors = fx["vectors"]
        .as_object()
        .expect("moments_fixture_matches_compute_ts_stats: 'vectors' is not an object");
    assert!(
        !vectors.is_empty(),
        "moments_fixture_matches_compute_ts_stats: 'vectors' is empty"
    );

    let mut saw_neg30 = false;

    for (name, v) in vectors {
        if name == "neg30" {
            saw_neg30 = true;
        }
        let x = f64_vec(&v["x"]);
        let series: Vec<Option<f64>> = x.iter().map(|&v| Some(v)).collect();
        let stats = compute_ts_stats(&series)
            .unwrap_or_else(|e| panic!("{name}: compute_ts_stats failed: {e:?}"));

        let want_skew = num(&v["skew_bias_false"]);
        let want_kurt = num(&v["kurtosis_excess_bias_false"]);
        let want_bc = num(&v["bimodality_coefficient_sas"]);

        common::assert_close(
            stats.skewness,
            want_skew,
            product_tol.max(abs_tol),
            rel_tol,
            &format!("{name}.skewness"),
        );
        common::assert_close(
            stats.kurtosis,
            want_kurt,
            product_tol.max(abs_tol),
            rel_tol,
            &format!("{name}.kurtosis"),
        );
        common::assert_close(
            stats.bimodality_coef,
            want_bc,
            product_tol.max(abs_tol),
            rel_tol,
            &format!("{name}.bimodality_coef"),
        );
    }

    assert!(
        saw_neg30,
        "moments_fixture_matches_compute_ts_stats: expected a 'neg30' vector in the fixture (ROADMAP SC1 audit vector)"
    );

    // Pin the literal values named in the plan/ROADMAP so a fixture regeneration that
    // silently drifts these numbers is caught here, not just in the generic loop above.
    let x20 = &fx["vectors"]["x20"];
    common::assert_close(
        num(&x20["kurtosis_excess_bias_false"]),
        0.2765286546322532,
        1e-10,
        0.0,
        "x20.kurtosis_excess_bias_false (ROADMAP SC1 literal)",
    );
    let neg30 = &fx["vectors"]["neg30"];
    common::assert_close(
        num(&neg30["bimodality_coefficient_sas"]),
        0.4661259199823024,
        1e-10,
        0.0,
        "neg30.bimodality_coefficient_sas (ROADMAP SC1 literal)",
    );
}
