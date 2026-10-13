//! Reference-fixture assertions pinning `anofox_fcst_core::peaks` against
//! `scipy.signal.find_peaks`/`peak_prominences` (Phase 15 plan 15-02,
//! DET-01, D-05).
//!
//! Fixture: `scipy_peaks/cases.json`
//! (`validation/reference/python/gen_scipy_peaks_reference.py`).
//!
//! Before this plan, `detect_peaks` called fdars-core's own derivative
//! zero-crossing peak detector: the Phase 15 audit found it missed peaks,
//! reported local minima, and gave every plateau prominence 0 (51 found vs
//! scipy's 66 on an audited random walk, only 31 matching).
//!
//! Cases carrying a `height`/`distance`/`prominence` filter are asserted
//! here too (Task 15-02/2, TDD) -- every case in the fixture is covered.

mod common;

use anofox_fcst_core::peaks::{detect_peaks, find_peaks, FindPeaksOptions};
use common::{assert_vec_close, f64_vec, load_fixture, tolerance};
use serde_json::Value;

/// Build `FindPeaksOptions` from a fixture case's `params` object.
fn opts_from_params(params: &Value) -> FindPeaksOptions {
    let get = |key: &str| -> Option<f64> {
        params
            .get(key)
            .and_then(|v| if v.is_null() { None } else { v.as_f64() })
    };
    FindPeaksOptions {
        height: get("height"),
        distance: get("distance"),
        prominence: get("prominence"),
    }
}

#[test]
fn scipy_find_peaks_parity() {
    let fx = load_fixture("scipy_peaks/cases.json");
    let prominence_abs = tolerance(&fx, "prominence_abs");
    let height_abs = tolerance(&fx, "height_abs");

    let cases = fx
        .get("cases")
        .and_then(|c| c.as_array())
        .expect("scipy_peaks/cases.json: 'cases' is not an array");

    for case in cases {
        let name = case["name"].as_str().unwrap();
        let x = f64_vec(&case["x"]);
        let params = &case["params"];

        let want_peaks: Vec<usize> = case["expected"]["peaks"]
            .as_array()
            .unwrap()
            .iter()
            .map(|v| v.as_u64().unwrap() as usize)
            .collect();
        let want_heights = f64_vec(&case["expected"]["peak_heights"]);
        let want_prominences = f64_vec(&case["expected"]["prominences"]);

        let opts = opts_from_params(params);
        let found = find_peaks(&x, &opts)
            .unwrap_or_else(|e| panic!("{name}: find_peaks failed: {e:?}"));

        let got_peaks: Vec<usize> = found.iter().map(|p| p.index).collect();
        assert_eq!(got_peaks, want_peaks, "{name}: peak indices");

        let got_heights: Vec<f64> = found.iter().map(|p| p.height).collect();
        assert_vec_close(
            &got_heights,
            &want_heights,
            height_abs,
            0.0,
            &format!("{name}: peak heights"),
        );

        let got_prominences: Vec<f64> = found.iter().map(|p| p.prominence).collect();
        assert_vec_close(
            &got_prominences,
            &want_prominences,
            prominence_abs,
            0.0,
            &format!("{name}: prominences"),
        );
    }
}

#[test]
fn detect_peaks_matches_scipy_on_rw150() {
    let fx = load_fixture("scipy_peaks/cases.json");
    let cases = fx["cases"].as_array().unwrap();
    let rw150 = cases
        .iter()
        .find(|c| c["name"] == "rw150")
        .expect("scipy_peaks/cases.json: no 'rw150' case");

    let x = f64_vec(&rw150["x"]);
    let want_peaks: Vec<usize> = rw150["expected"]["peaks"]
        .as_array()
        .unwrap()
        .iter()
        .map(|v| v.as_u64().unwrap() as usize)
        .collect();
    assert_eq!(want_peaks.len(), 38, "fixture sanity: rw150 has 38 peaks");

    let result = detect_peaks(&x, None, None, false, None).unwrap();
    assert_eq!(result.n_peaks, 38, "detect_peaks(rw150) should find 38 peaks");

    let got_peaks: Vec<usize> = result.peaks.iter().map(|p| p.index).collect();
    assert_eq!(got_peaks, want_peaks, "detect_peaks(rw150) peak indices");

    // No reported index is a local minimum.
    for &idx in &got_peaks {
        if idx > 0 && idx + 1 < x.len() {
            assert!(
                !(x[idx] < x[idx - 1] && x[idx] < x[idx + 1]),
                "detect_peaks(rw150) reported a local minimum at index {idx}"
            );
        }
    }
}
