//! tsfresh/numpy 117-feature parity harness (Phase 14, D-01/D-12).
//!
//! Driven entirely by `validation/reference/fixtures/tsfresh_features/mapping.json`
//! (the parity contract) and the nine per-series fixtures it maps onto. One
//! `#[test]` per fixture series asserts every mapping row's `status: "match"`
//! feature against `extract_features()`'s output within the mapping's
//! tolerance (or the row's own `tolerance` override, each carrying a reason
//! per D-10). `status: "pending:<owner>"` rows are recorded but not asserted
//! -- plan 14-01 adds only the five `partial_autocorrelation_lag{1..5}` rows,
//! all `status: "match"`; plans 14-02..14-04 add the remaining 112 rows and
//! their own ratchet coverage.
//!
//! NaN <-> NaN and equal infinities count as a match (D-11); NaN against a
//! number (either direction) fails -- enforced by `common::assert_close`.

mod common;

use anofox_fcst_core::extract_features;
use common::{f64_vec, load_fixture, num, tolerance};
use serde_json::Value;

const SERIES_NAMES: [&str; 9] = [
    "white_noise_n200",
    "random_walk_n200",
    "seasonal_ar12_n144",
    "trend_season_n120",
    "intermittent_n100",
    "short_n20",
    "constant_n30",
    "all_zero_n50",
    "tiny_n8",
];

/// Row-level tolerance: the row's own `tolerance` override (abs/rel) if
/// present, else the fixture's own `tolerance.abs`/`tolerance.rel` (D-10 --
/// never hard-code a tolerance in Rust).
fn row_tolerance(fixture: &Value, row: &Value) -> (f64, f64) {
    if let Some(t) = row.get("tolerance") {
        let abs = t.get("abs").map(num).unwrap_or_else(|| tolerance(fixture, "abs"));
        let rel = t.get("rel").map(num).unwrap_or_else(|| tolerance(fixture, "rel"));
        (abs, rel)
    } else {
        (tolerance(fixture, "abs"), tolerance(fixture, "rel"))
    }
}

/// Resolves the value a `status: "match"` row should be asserted against for
/// a given series: the row's `deviation.expected_ours` when this series is
/// named in `deviation.series`, else the fixture's own tsfresh/numpy
/// `expected` value.
fn expected_value_for(row: &Value, series_name: &str, fixture_expected: f64) -> f64 {
    if let Some(deviation) = row.get("deviation") {
        let series_list = deviation["series"]
            .as_array()
            .map(|a| a.iter().filter_map(|v| v.as_str()).collect::<Vec<_>>())
            .unwrap_or_default();
        if series_list.contains(&series_name) {
            let expected_ours = &deviation["expected_ours"];
            return match expected_ours.as_str() {
                Some("NaN") => f64::NAN,
                _ => num(expected_ours),
            };
        }
    }
    fixture_expected
}

fn run_series_parity(series_name: &str) {
    let mapping = load_fixture("tsfresh_features/mapping.json");
    let rows = mapping["features"]
        .as_array()
        .expect("tsfresh_features/mapping.json: 'features' is not an array");
    assert!(
        !rows.is_empty(),
        "tsfresh_features/mapping.json: 'features' is empty"
    );

    let fixture = load_fixture(&format!("tsfresh_features/{series_name}.json"));
    let expected = fixture["expected"]
        .as_object()
        .unwrap_or_else(|| panic!("{series_name}: fixture has no 'expected' object"));

    let values = f64_vec(&fixture["series"]["values"]);
    let got = extract_features(&values)
        .unwrap_or_else(|e| panic!("{series_name}: extract_features failed: {e:?}"));

    let mut asserted = 0usize;
    for row in rows {
        let name = row["name"]
            .as_str()
            .unwrap_or_else(|| panic!("{series_name}: mapping row missing string 'name'"));
        let status = row["status"]
            .as_str()
            .unwrap_or_else(|| panic!("{series_name}.{name}: mapping row missing string 'status'"));

        let fixture_expected = expected
            .get(name)
            .map(num)
            .unwrap_or_else(|| panic!("{series_name}: fixture 'expected' missing key {name:?}"));
        let got_value = got.get(name).copied().unwrap_or(f64::NAN);

        if status == "match" {
            let want = expected_value_for(row, series_name, fixture_expected);
            let (abs_tol, rel_tol) = row_tolerance(&fixture, row);
            common::assert_close(got_value, want, abs_tol, rel_tol, &format!("{series_name}.{name}"));
            asserted += 1;
        }
        // status "pending:<owner>" rows are not asserted here -- owned by a
        // later Phase 14 plan; `pending_features_still_mismatch` (added in
        // plan 14-02) enforces they still genuinely mismatch somewhere.
    }

    assert!(
        asserted > 0,
        "{series_name}: no 'match' rows were asserted (mapping.json has no match rows yet?)"
    );
}

#[test]
fn tsfresh_parity_white_noise_n200() {
    run_series_parity("white_noise_n200");
}

#[test]
fn tsfresh_parity_random_walk_n200() {
    run_series_parity("random_walk_n200");
}

#[test]
fn tsfresh_parity_seasonal_ar12_n144() {
    run_series_parity("seasonal_ar12_n144");
}

#[test]
fn tsfresh_parity_trend_season_n120() {
    run_series_parity("trend_season_n120");
}

#[test]
fn tsfresh_parity_intermittent_n100() {
    run_series_parity("intermittent_n100");
}

#[test]
fn tsfresh_parity_short_n20() {
    run_series_parity("short_n20");
}

#[test]
fn tsfresh_parity_constant_n30() {
    run_series_parity("constant_n30");
}

#[test]
fn tsfresh_parity_all_zero_n50() {
    run_series_parity("all_zero_n50");
}

#[test]
fn tsfresh_parity_tiny_n8() {
    run_series_parity("tiny_n8");
}

/// Sanity check that every series name this harness loads actually has a
/// fixture file on disk (keeps the SERIES_NAMES list and the fixtures
/// directory from silently drifting apart).
#[test]
fn all_nine_series_fixtures_present() {
    for name in SERIES_NAMES {
        let _ = load_fixture(&format!("tsfresh_features/{name}.json"));
    }
}
