//! Reference-fixture assertions for `anofox_fcst_core::metrics::mase` (Phase 13
//! plan 13-04: STAT-06, D-08).
//!
//! - `r_mase_fixture_accuracy_and_manual_agree`: for every `r_mase/accuracy_mase.json`
//!   case, asserts R's two independently-computed expected values
//!   (`forecast::accuracy()`'s own MASE and the hand-written formula) agree with
//!   each other -- a disagreement would mean the case/fixture itself is wrong,
//!   independent of this crate's `mase()`.
//! - `r_mase_fixture_matches_mase`: asserts `anofox_fcst_core::metrics::mase`
//!   reproduces the fixture's expected MASE for every finite case, and that the
//!   one `Inf`-expected case (`constant_insample`, a zero in-sample scale) maps
//!   to this product's documented `Ok(None)` policy, not a propagated infinity.

mod common;

use anofox_fcst_core::metrics::mase;
use common::{f64_vec, load_fixture, tolerance};
use serde_json::Value;

/// Reads a case's `insample` array as `Vec<Option<f64>>` (NA -> None), unlike
/// `common::f64_vec` which maps NA to `f64::NAN` -- `mase()`'s in-sample
/// argument takes `Option<f64>` to preserve missing-value positions exactly.
fn insample_option_vec(v: &Value) -> Vec<Option<f64>> {
    v.as_array()
        .unwrap_or_else(|| panic!("insample_option_vec(): expected a JSON array, got {v:?}"))
        .iter()
        .map(|elem| match elem {
            Value::String(s) if s == "NA" || s == "NaN" => None,
            other => {
                let x = common::num(other);
                if x.is_nan() {
                    None
                } else {
                    Some(x)
                }
            }
        })
        .collect()
}

#[test]
fn r_mase_fixture_accuracy_and_manual_agree() {
    let fx = load_fixture("r_mase/accuracy_mase.json");
    let abs_tol = tolerance(&fx, "mase_abs");

    let cases = fx["cases"]
        .as_object()
        .expect("r_mase_fixture_accuracy_and_manual_agree: 'cases' is not an object");
    assert!(
        !cases.is_empty(),
        "r_mase_fixture_accuracy_and_manual_agree: 'cases' is empty"
    );

    for (name, c) in cases {
        let acc = common::num(&c["mase_accuracy"]);
        let manual = common::num(&c["mase_manual"]);
        common::assert_close(acc, manual, abs_tol, 0.0, &format!("{name}: mase_accuracy vs mase_manual"));
    }
}

#[test]
fn r_mase_fixture_matches_mase() {
    let fx = load_fixture("r_mase/accuracy_mase.json");
    let abs_tol = tolerance(&fx, "mase_abs");

    let cases = fx["cases"]
        .as_object()
        .expect("r_mase_fixture_matches_mase: 'cases' is not an object");

    let mut saw_constant_insample = false;

    for (name, c) in cases {
        let insample = insample_option_vec(&c["insample"]);
        let actual = f64_vec(&c["actual"]);
        let forecast = f64_vec(&c["forecast"]);
        let seasonal_period = c["seasonal_period"]
            .as_u64()
            .unwrap_or_else(|| panic!("{name}: seasonal_period is not a non-negative integer")) as usize;
        let want = common::num(&c["mase_manual"]);

        // Mirror the FFI/SQL contract: `actual`/`forecast` arrive at `mase()`
        // already pairwise-complete (test_na_m4 has an NA actual, dropped here
        // exactly as `complete_cases` would drop it before the FFI call).
        let (actual_complete, forecast_complete): (Vec<f64>, Vec<f64>) = actual
            .iter()
            .zip(forecast.iter())
            .filter(|(a, f)| !a.is_nan() && !f.is_nan())
            .map(|(a, f)| (*a, *f))
            .collect();

        let got = mase(&actual_complete, &forecast_complete, &insample, seasonal_period)
            .unwrap_or_else(|e| panic!("{name}: mase() failed: {e:?}"));

        if name == "constant_insample" {
            saw_constant_insample = true;
            assert!(
                want.is_infinite(),
                "{name}: fixture's mase_manual should be Inf (zero in-sample scale), got {want}"
            );
            assert_eq!(
                got, None,
                "{name}: product policy (D-08) maps a zero in-sample scale to None, not Some(Inf)"
            );
            continue;
        }

        let got = got.unwrap_or_else(|| panic!("{name}: mase() returned None, expected Some({want})"));
        common::assert_close(got, want, abs_tol, 0.0, name);
    }

    assert!(
        saw_constant_insample,
        "r_mase_fixture_matches_mase: fixture is missing the 'constant_insample' case"
    );
}
