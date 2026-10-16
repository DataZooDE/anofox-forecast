//! Reference-fixture assertions for `decomposition::{mstl_decompose,
//! classical_decompose}` (Phase 15 plan 15-04, DET-04: D-09, D-10).
//!
//! Fixture: `r_decomposition/cases.json`
//! (`validation/reference/r/gen_decomposition_reference.R`).
//!
//! - `mstl_decompose_matches_r_forecast_mstl`: asserts `mstl_decompose`'s
//!   trend/per-period seasonal/remainder and ascending output `periods`
//!   against R `forecast::mstl()` for every `mstl_cases` entry, including
//!   `msts_7_28` requested in the REVERSE order `[28, 7]` (must still come
//!   back ascending `[7, 28]` with identical components) and the G11
//!   constant-series anchor (not from the fixture -- `supsmu` on a constant
//!   series trivially returns the constant).
//! - `classical_decompose_matches_r_decompose`: asserts `classical_decompose`
//!   against R `stats::decompose()` for every `decompose_cases` entry
//!   (trend, including its `NaN` edges; seasonal; remainder==R's `random`;
//!   figure).

mod common;

use anofox_fcst_core::{classical_decompose, mstl_decompose, ClassicalKind, InsufficientDataMode};
use common::{assert_close, assert_vec_close, f64_vec, load_fixture, num, tolerance};

#[test]
fn mstl_decompose_matches_r_forecast_mstl() {
    let fx = load_fixture("r_decomposition/cases.json");
    let component_abs = tolerance(&fx, "component_abs");

    let cases = fx
        .get("mstl_cases")
        .and_then(|c| c.as_object())
        .expect("r_decomposition/cases.json: 'mstl_cases' is not an object");

    for (name, case) in cases {
        let values = f64_vec(&case["values"]);
        let kept_periods: Vec<i32> = case["kept_periods"]
            .as_array()
            .unwrap_or_else(|| panic!("{name}: kept_periods is not an array"))
            .iter()
            .map(|v| v.as_i64().unwrap() as i32)
            .collect();

        let requested_periods: Vec<i32> = case["requested_periods"]
            .as_array()
            .unwrap_or_else(|| panic!("{name}: requested_periods is not an array"))
            .iter()
            .map(|v| v.as_i64().unwrap() as i32)
            .collect();

        let result = mstl_decompose(&values, &requested_periods, InsufficientDataMode::Fail)
            .unwrap_or_else(|e| panic!("{name}: mstl_decompose() failed: {e:?}"));

        assert_eq!(
            result.periods, kept_periods,
            "{name}: periods (ascending, dropped-filtered)"
        );

        assert_vec_close(
            &result.trend.unwrap(),
            &f64_vec(&case["mstl"]["trend"]),
            component_abs,
            0.0,
            &format!("{name}: trend"),
        );
        assert_vec_close(
            &result.remainder.unwrap(),
            &f64_vec(&case["mstl"]["remainder"]),
            component_abs,
            0.0,
            &format!("{name}: remainder"),
        );

        let seasonal_fx = &case["mstl"]["seasonal"];
        for (i, &period) in kept_periods.iter().enumerate() {
            let key = format!("p{period}");
            let want = f64_vec(
                seasonal_fx
                    .get(&key)
                    .unwrap_or_else(|| panic!("{name}: mstl.seasonal.{key} missing")),
            );
            assert_vec_close(
                &result.seasonal[i],
                &want,
                component_abs,
                0.0,
                &format!("{name}: seasonal[{period}]"),
            );
        }
    }
}

#[test]
fn mstl_decompose_msts_7_28_reverse_order_still_ascends() {
    let fx = load_fixture("r_decomposition/cases.json");
    let component_abs = tolerance(&fx, "component_abs");
    let case = &fx["mstl_cases"]["msts_7_28"];
    let values = f64_vec(&case["values"]);

    let forward = mstl_decompose(&values, &[7, 28], InsufficientDataMode::Fail).unwrap();
    let reverse = mstl_decompose(&values, &[28, 7], InsufficientDataMode::Fail).unwrap();

    assert_eq!(forward.periods, vec![7, 28]);
    assert_eq!(reverse.periods, vec![7, 28]);

    assert_vec_close(
        &reverse.trend.clone().unwrap(),
        &forward.trend.clone().unwrap(),
        1e-12,
        0.0,
        "msts_7_28 reverse-order trend vs forward",
    );
    for i in 0..2 {
        assert_vec_close(
            &reverse.seasonal[i],
            &forward.seasonal[i],
            1e-12,
            0.0,
            &format!("msts_7_28 reverse-order seasonal[{i}] vs forward"),
        );
    }

    // And both match the R fixture directly.
    assert_vec_close(
        &reverse.trend.unwrap(),
        &f64_vec(&case["mstl"]["trend"]),
        component_abs,
        0.0,
        "msts_7_28 reverse-order trend vs R",
    );
}

#[test]
fn mstl_decompose_nonseasonal_constant_n100_trend_is_exactly_one() {
    // G11 anchor: not derived from the R fixture -- a constant series'
    // `supsmu` trend is trivially the constant itself, independent of R.
    let values = vec![1.0; 100];
    let result = mstl_decompose(&values, &[], InsufficientDataMode::Fail).unwrap();
    for (i, &t) in result.trend.unwrap().iter().enumerate() {
        assert!((t - 1.0).abs() < 1e-12, "trend[{i}] = {t}");
    }
}

#[test]
fn classical_decompose_matches_r_decompose() {
    let fx = load_fixture("r_decomposition/cases.json");
    let classical_abs = tolerance(&fx, "classical_abs");

    let cases = fx
        .get("decompose_cases")
        .and_then(|c| c.as_object())
        .expect("r_decomposition/cases.json: 'decompose_cases' is not an object");

    for (name, case) in cases {
        let values = f64_vec(&case["values"]);
        let period = case["period"].as_u64().unwrap() as usize;
        let type_str = case["type"].as_str().unwrap();
        let kind = match type_str {
            "additive" => ClassicalKind::Additive,
            "multiplicative" => ClassicalKind::Multiplicative,
            other => panic!("{name}: unknown decompose type {other:?}"),
        };

        let result = classical_decompose(&values, period, kind)
            .unwrap_or_else(|e| panic!("{name}: classical_decompose() failed: {e:?}"));

        let dfx = &case["decompose"];
        assert_vec_close(
            &result.trend,
            &f64_vec(&dfx["trend"]),
            classical_abs,
            0.0,
            &format!("{name}: trend"),
        );
        assert_vec_close(
            &result.seasonal,
            &f64_vec(&dfx["seasonal"]),
            classical_abs,
            0.0,
            &format!("{name}: seasonal"),
        );
        assert_vec_close(
            &result.remainder,
            &f64_vec(&dfx["random"]),
            classical_abs,
            0.0,
            &format!("{name}: remainder (R 'random')"),
        );
        assert_vec_close(
            &result.figure,
            &f64_vec(&dfx["figure"]),
            classical_abs,
            0.0,
            &format!("{name}: figure"),
        );
    }
}

/// Sanity check that `assert_close`'s NaN-handling is actually exercised by
/// the decompose fixture (the trend/remainder vectors genuinely contain
/// `NaN` at the edges) -- guards against a silently-vacuous assertion if a
/// future refactor stops emitting edge `NaN`s.
#[test]
fn classical_decompose_air_additive_has_nan_edges() {
    let fx = load_fixture("r_decomposition/cases.json");
    let case = &fx["decompose_cases"]["air_additive"];
    let trend_fx = f64_vec(&case["decompose"]["trend"]);
    assert!(
        trend_fx.iter().any(|v| v.is_nan()),
        "fixture air_additive trend has no NaN edges -- test would be vacuous"
    );

    let values = f64_vec(&case["values"]);
    let result = classical_decompose(&values, 12, ClassicalKind::Additive).unwrap();
    assert!(result.trend[0].is_nan());
    assert_close(
        num(&case["decompose"]["figure"][0]),
        result.figure[0],
        1e-10,
        0.0,
        "figure[0]",
    );
}
