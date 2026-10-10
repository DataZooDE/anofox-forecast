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

use anofox_fcst_core::calendar::CalendarStep;
use anofox_fcst_core::gaps::gap_counts;
use anofox_fcst_core::{compute_ts_stats, compute_ts_stats_with_dates_and_type, FrequencyType};
use chrono::NaiveDateTime;
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
        let stats = compute_ts_stats(&series, 0)
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

/// Parses an ISO-8601 naive timestamp (as emitted by `pandas.Timestamp.isoformat()`,
/// e.g. `"2024-01-31T00:00:00"`) into microseconds since the Unix epoch.
fn iso_to_micros(s: &str) -> i64 {
    let dt = NaiveDateTime::parse_from_str(s, "%Y-%m-%dT%H:%M:%S")
        .unwrap_or_else(|e| panic!("iso_to_micros({s:?}): parse failed: {e}"));
    dt.and_utc().timestamp_micros()
}

#[test]
fn r_entropy_fixture_matches_compute_ts_stats() {
    let fx = load_fixture("r_entropy/approx_entropy.json");
    let abs_tol = tolerance(&fx, "abs");

    let cases = fx["cases"]
        .as_object()
        .expect("r_entropy_fixture_matches_compute_ts_stats: 'cases' is not an object");
    assert!(
        !cases.is_empty(),
        "r_entropy_fixture_matches_compute_ts_stats: 'cases' is empty"
    );

    for (name, case) in cases {
        let x = f64_vec(&case["values"]);
        let series: Vec<Option<f64>> = x.iter().map(|&v| Some(v)).collect();
        let stats = compute_ts_stats(&series, 0)
            .unwrap_or_else(|e| panic!("{name}: compute_ts_stats failed: {e:?}"));

        let want = num(&case["apen"]);
        let got = stats
            .entropy
            .unwrap_or_else(|| panic!("{name}: expected Some(entropy), got None"));
        common::assert_close(got, want, abs_tol, 0.0, &format!("{name}.entropy"));
    }

    // Interior NULL -> None.
    let interior_null: Vec<Option<f64>> = vec![Some(1.0), Some(2.0), None, Some(4.0), Some(5.0)];
    assert_eq!(
        compute_ts_stats(&interior_null, 0).unwrap().entropy,
        None,
        "interior NULL series: expected entropy None"
    );

    // Fewer than 3 present values -> None.
    let two_values: Vec<Option<f64>> = vec![Some(1.0), Some(2.0)];
    assert_eq!(
        compute_ts_stats(&two_values, 0).unwrap().entropy,
        None,
        "2-value series: expected entropy None"
    );
}

/// Builds the [`CalendarStep`] a `gap_counts` fixture case's `step` object describes.
fn step_from_fixture(step: &serde_json::Value) -> CalendarStep {
    match step["kind"].as_str().expect("step.kind is not a string") {
        "fixed" => CalendarStep::Fixed(num(&step["micros"]) as i64),
        "months" => CalendarStep::Months(num(&step["months"]) as u32),
        other => panic!("step_from_fixture: unknown kind {other:?}"),
    }
}

#[test]
fn gap_counts_fixture_matches_gap_counts() {
    let fx = load_fixture("gap_counts/cases.json");

    let cases = fx["cases"]
        .as_object()
        .expect("gap_counts_fixture_matches_gap_counts: 'cases' is not an object");
    assert!(
        !cases.is_empty(),
        "gap_counts_fixture_matches_gap_counts: 'cases' is empty"
    );

    for (name, case) in cases {
        let observed: Vec<i64> = case["observed"]
            .as_array()
            .unwrap_or_else(|| panic!("{name}: 'observed' is not an array"))
            .iter()
            .map(|v| iso_to_micros(v.as_str().expect("observed entry is not a string")))
            .collect();
        let step = step_from_fixture(&case["step"]);

        let counts = gap_counts(&observed, step);

        assert_eq!(
            counts.expected_length,
            num(&case["expected_length"]) as usize,
            "{name}: expected_length"
        );
        assert_eq!(
            counts.n_missing_periods,
            num(&case["n_missing_periods"]) as usize,
            "{name}: n_missing_periods"
        );
        assert_eq!(
            counts.n_gap_runs,
            num(&case["n_gap_runs"]) as usize,
            "{name}: n_gap_runs"
        );
    }

    // compute_ts_stats_with_dates_and_type on daily_hole20 (Fixed) and month_end (Monthly).
    let daily_hole20 = &fx["cases"]["daily_hole20"];
    let dates: Vec<i64> = daily_hole20["observed"]
        .as_array()
        .unwrap()
        .iter()
        .map(|v| iso_to_micros(v.as_str().unwrap()))
        .collect();
    let values: Vec<Option<f64>> = dates.iter().map(|_| Some(1.0)).collect();
    let micros = num(&daily_hole20["step"]["micros"]) as i64;
    let stats =
        compute_ts_stats_with_dates_and_type(&values, &dates, micros, FrequencyType::Fixed, 0, 1)
            .unwrap();
    assert_eq!(stats.n_gaps, Some(20), "daily_hole20: n_gaps");
    assert_eq!(stats.n_gap_runs, Some(1), "daily_hole20: n_gap_runs");
    assert_eq!(
        stats.expected_length,
        Some(60),
        "daily_hole20: expected_length"
    );

    let month_end = &fx["cases"]["month_end"];
    let dates: Vec<i64> = month_end["observed"]
        .as_array()
        .unwrap()
        .iter()
        .map(|v| iso_to_micros(v.as_str().unwrap()))
        .collect();
    let values: Vec<Option<f64>> = dates.iter().map(|_| Some(1.0)).collect();
    let stats =
        compute_ts_stats_with_dates_and_type(&values, &dates, 0, FrequencyType::Monthly, 0, 1)
            .unwrap();
    assert_eq!(stats.n_gaps, Some(2), "month_end: n_gaps");
    assert_eq!(stats.n_gap_runs, Some(1), "month_end: n_gap_runs");
    assert_eq!(stats.expected_length, Some(5), "month_end: expected_length");
}

/// CR-01 regression (13-03 deferred item): `compute_ts_stats_with_dates_and_type`
/// must honor the declared calendar multiplier (`period_count`), not hard-code
/// Monthly/Quarterly/Yearly to a single-unit step. A correctly-spaced bi-monthly
/// series declared with `period_count=2` (i.e. `'2mo'`) must report `n_gaps = 0`.
#[test]
fn compute_ts_stats_honors_calendar_period_count() {
    // 2023-01-01, 03-01, 05-01, 07-01 UTC -- 2 months apart, every time.
    let dates: Vec<i64> = vec![
        1672531200_i64 * 1_000_000,
        1677628800_i64 * 1_000_000,
        1682899200_i64 * 1_000_000,
        1688169600_i64 * 1_000_000,
    ];
    let values: Vec<Option<f64>> = dates.iter().map(|_| Some(1.0)).collect();

    let stats =
        compute_ts_stats_with_dates_and_type(&values, &dates, 0, FrequencyType::Monthly, 0, 2)
            .unwrap();
    assert_eq!(stats.n_gaps, Some(0), "period_count=2 ('2mo'): n_gaps");
    assert_eq!(
        stats.n_gap_runs,
        Some(0),
        "period_count=2 ('2mo'): n_gap_runs"
    );
    assert_eq!(
        stats.expected_length,
        Some(4),
        "period_count=2 ('2mo'): expected_length"
    );

    // Without the multiplier (period_count=1, the pre-fix hard-coded behavior), the
    // same series is misclassified as 3 missing single months.
    let stats_unmultiplied =
        compute_ts_stats_with_dates_and_type(&values, &dates, 0, FrequencyType::Monthly, 0, 1)
            .unwrap();
    assert_eq!(
        stats_unmultiplied.n_gaps,
        Some(3),
        "period_count=1: n_gaps (sanity check for the pre-fix behavior)"
    );
}
